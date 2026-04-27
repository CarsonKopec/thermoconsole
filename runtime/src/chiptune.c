/*
 * ThermoConsole Runtime — chiptune.c
 *
 * sounds.json → 32 pre-rendered Mix_Chunk SFX. Synth math is the C port
 * of editor/panels/SoundEditor.cpp's fillAudioBuffer() — keep them in
 * sync if you tweak waveforms or effects.
 *
 * Loop semantics for the runtime MVP:
 *   - One-shot SFX (loop_end <= loop_start): we render only up to the
 *     last non-silent step, Mix_PlayChannel(loops = 0).
 *   - Looping SFX (loop_end >  loop_start): we render *only* the loop
 *     region (steps loop_start..loop_end-1) and Mix_PlayChannel(loops
 *     = -1). Any "intro" before loop_start is skipped — the editor's
 *     preview honors it, the runtime doesn't yet.
 */

#include "chiptune.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL.h>
#include <SDL_mixer.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define CT_TICKS_PER_SEC  120.0    /* PICO-8 tempo unit */

/* ─── Pitch / waveform synth ────────────────────────────────────────────── */

static double pitch_to_freq(double pitch) {
    return 440.0 * pow(2.0, (pitch - 33.0) / 12.0);
}

static double waveform_sample(int wave, double phase, uint32_t* noise_state) {
    switch (wave) {
        case CT_WAVE_TRIANGLE: return 4.0 * fabs(phase - 0.5) - 1.0;
        case CT_WAVE_SQUARE:   return phase < 0.5 ? 1.0 : -1.0;
        case CT_WAVE_SAW:      return 2.0 * phase - 1.0;
        case CT_WAVE_PULSE:    return phase < 0.25 ? 1.0 : -1.0;
        case CT_WAVE_NOISE: {
            uint32_t x = *noise_state;
            x ^= x << 13; x ^= x >> 17; x ^= x << 5;
            *noise_state = x;
            return (double)(x & 0xFFFF) / 32768.0 - 1.0;
        }
        default: return 0.0;
    }
}

/* ─── Tiny JSON tokenizer (sized to sounds.json) ────────────────────────── */

typedef struct {
    const char* s;
    size_t      p;
    size_t      n;
    int         err;
} JReader;

static void j_skipws(JReader* r) {
    while (r->p < r->n && isspace((unsigned char)r->s[r->p])) r->p++;
}

static int j_peek(JReader* r, char c) {
    j_skipws(r);
    return r->p < r->n && r->s[r->p] == c;
}

static int j_expect(JReader* r, char c) {
    j_skipws(r);
    if (r->p >= r->n || r->s[r->p] != c) { r->err = 1; return 0; }
    r->p++;
    return 1;
}

/* Read a string into out (max out_cap including terminator). */
static int j_read_string(JReader* r, char* out, size_t out_cap) {
    j_skipws(r);
    if (r->p >= r->n || r->s[r->p] != '"') { r->err = 1; return 0; }
    r->p++;
    size_t w = 0;
    while (r->p < r->n && r->s[r->p] != '"') {
        char c = r->s[r->p];
        if (c == '\\' && r->p + 1 < r->n) {
            char e = r->s[r->p + 1];
            switch (e) {
                case '"':  c = '"';  break;
                case '\\': c = '\\'; break;
                case '/':  c = '/';  break;
                case 'n':  c = '\n'; break;
                case 't':  c = '\t'; break;
                case 'r':  c = '\r'; break;
                default:   c = e;    break;
            }
            r->p += 2;
        } else {
            r->p++;
        }
        if (out && w + 1 < out_cap) out[w++] = c;
    }
    if (out && w < out_cap) out[w] = '\0';
    if (r->p >= r->n) { r->err = 1; return 0; }
    r->p++;
    return 1;
}

static double j_read_number(JReader* r) {
    j_skipws(r);
    char buf[64];
    size_t w = 0;
    while (r->p < r->n && w + 1 < sizeof(buf)) {
        char c = r->s[r->p];
        if (!(isdigit((unsigned char)c) || c == '-' || c == '+' ||
              c == '.' || c == 'e' || c == 'E')) break;
        buf[w++] = c;
        r->p++;
    }
    if (w == 0) { r->err = 1; return 0.0; }
    buf[w] = '\0';
    return strtod(buf, NULL);
}

/* Skip whatever JSON value is next (string, number, object, array, literal). */
static void j_skip_value(JReader* r) {
    j_skipws(r);
    if (r->p >= r->n) { r->err = 1; return; }
    char c = r->s[r->p];
    if (c == '"') { j_read_string(r, NULL, 0); return; }
    if (c == '{' || c == '[') {
        char open = c, close = (c == '{' ? '}' : ']');
        int depth = 0;
        while (r->p < r->n) {
            if (r->s[r->p] == '"') { j_read_string(r, NULL, 0); continue; }
            if (r->s[r->p] == open)  { depth++; r->p++; continue; }
            if (r->s[r->p] == close) {
                depth--; r->p++;
                if (depth == 0) return;
                continue;
            }
            r->p++;
        }
        r->err = 1;
        return;
    }
    /* Number / true / false / null — skip until terminator. */
    while (r->p < r->n) {
        char c2 = r->s[r->p];
        if (c2 == ',' || c2 == '}' || c2 == ']' || isspace((unsigned char)c2)) break;
        r->p++;
    }
}

static int clampi(int v, int lo, int hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

/* ─── sounds.json parser ────────────────────────────────────────────────── */

static int parse_steps_array(JReader* r, ChiptuneSfx* sfx) {
    if (!j_expect(r, '[')) return 0;
    int si = 0;
    while (!j_peek(r, ']') && !r->err) {
        if (si >= CHIPTUNE_STEPS) { j_skip_value(r); }
        else {
            ChiptuneStep* st = &sfx->steps[si];
            if (!j_expect(r, '{')) return 0;
            while (!j_peek(r, '}') && !r->err) {
                char fk[16] = {0};
                if (!j_read_string(r, fk, sizeof(fk))) return 0;
                if (!j_expect(r, ':'))                 return 0;
                int v = (int)j_read_number(r);
                if      (strcmp(fk, "pitch")  == 0) st->pitch  = (uint8_t)clampi(v, 0, 63);
                else if (strcmp(fk, "wave")   == 0) st->wave   = (uint8_t)clampi(v, 0, CT_WAVE_COUNT   - 1);
                else if (strcmp(fk, "volume") == 0) st->volume = (uint8_t)clampi(v, 0, 7);
                else if (strcmp(fk, "effect") == 0) st->effect = (uint8_t)clampi(v, 0, CT_FX_COUNT - 1);
                if (j_peek(r, ',')) j_expect(r, ',');
            }
            if (!j_expect(r, '}')) return 0;
            si++;
        }
        if (j_peek(r, ',')) j_expect(r, ',');
    }
    return j_expect(r, ']');
}

static int parse_sfx_object(JReader* r, ChiptuneSfx* sfx) {
    if (!j_expect(r, '{')) return 0;
    while (!j_peek(r, '}') && !r->err) {
        char fk[16] = {0};
        if (!j_read_string(r, fk, sizeof(fk))) return 0;
        if (!j_expect(r, ':'))                 return 0;
        if      (strcmp(fk, "id")          == 0) (void)j_read_number(r);
        else if (strcmp(fk, "name")        == 0) j_read_string(r, sfx->name, sizeof(sfx->name));
        else if (strcmp(fk, "speed")       == 0) sfx->speed      = (uint8_t)clampi((int)j_read_number(r), 1, 31);
        else if (strcmp(fk, "loop_start")  == 0) sfx->loop_start = (uint8_t)clampi((int)j_read_number(r), 0, CHIPTUNE_STEPS - 1);
        else if (strcmp(fk, "loop_end")    == 0) sfx->loop_end   = (uint8_t)clampi((int)j_read_number(r), 0, CHIPTUNE_STEPS);
        else if (strcmp(fk, "steps")       == 0) { if (!parse_steps_array(r, sfx)) return 0; }
        else                                       j_skip_value(r);
        if (j_peek(r, ',')) j_expect(r, ',');
    }
    return j_expect(r, '}');
}

static int parse_sounds_json(Chiptune* ct, const char* text, size_t len) {
    JReader r = { text, 0, len, 0 };
    if (!j_expect(&r, '{')) return -1;
    int saw_sfx = 0;
    while (!j_peek(&r, '}') && !r.err) {
        char key[16] = {0};
        if (!j_read_string(&r, key, sizeof(key))) return -1;
        if (!j_expect(&r, ':'))                   return -1;
        if (strcmp(key, "sfx") == 0) {
            if (!j_expect(&r, '[')) return -1;
            int slot = 0;
            while (!j_peek(&r, ']') && !r.err) {
                if (slot >= CHIPTUNE_SFX_COUNT) { j_skip_value(&r); }
                else {
                    /* Reset the slot to defaults, then fill from the object. */
                    ChiptuneSfx* sfx = &ct->sfx[slot];
                    memset(sfx, 0, sizeof(*sfx));
                    sfx->speed = 4;
                    for (int i = 0; i < CHIPTUNE_STEPS; ++i) sfx->steps[i].pitch = 24;
                    if (!parse_sfx_object(&r, sfx)) return -1;
                    slot++;
                }
                if (j_peek(&r, ',')) j_expect(&r, ',');
            }
            if (!j_expect(&r, ']')) return -1;
            saw_sfx = 1;
        } else {
            j_skip_value(&r);   /* presets / version / unknowns */
        }
        if (j_peek(&r, ',')) j_expect(&r, ',');
    }
    if (!j_expect(&r, '}')) return -1;
    return saw_sfx ? 0 : -1;
}

/* ─── Synthesise one SFX into a stereo S16 PCM buffer ───────────────────── */
/*
 * SDL_mixer was opened with MIX_DEFAULT_FORMAT (signed 16-bit) and 2 channels
 * — see runtime/src/main.c. Mix_QuickLoad_RAW expects PCM in exactly that
 * format, so we render to interleaved stereo S16 (L = R = mono synth).
 */

static int render_sfx(const ChiptuneSfx* sfx, int sample_rate,
                      int from_step, int to_step,
                      uint8_t** out_buf, uint32_t* out_bytes)
{
    if (to_step <= from_step) {
        *out_buf = NULL;
        *out_bytes = 0;
        return -1;
    }

    const double tick_sec = 1.0 / CT_TICKS_PER_SEC;
    const int    spd      = sfx->speed < 1 ? 4 : sfx->speed;
    const double step_sec = spd * tick_sec;
    const double samples_per_step = step_sec * sample_rate;

    const int    total_steps   = to_step - from_step;
    const size_t mono_samples  = (size_t)(samples_per_step * total_steps + 0.5);
    if (mono_samples == 0) { *out_buf = NULL; *out_bytes = 0; return -1; }

    const size_t bytes = mono_samples * 2 /* stereo */ * sizeof(int16_t);
    int16_t* out = (int16_t*)malloc(bytes);
    if (!out) return -1;

    double   osc_phase   = 0.0;
    uint32_t noise_state = 0xCAFEBABEu;

    for (size_t i = 0; i < mono_samples; ++i) {
        double whole_step = (double)i / samples_per_step;
        int    si         = (int)whole_step;
        if (si >= total_steps) si = total_steps - 1;
        double frac       = whole_step - (double)si;
        int    abs_step   = from_step + si;
        const ChiptuneStep* st = &sfx->steps[abs_step];

        /* Pitch with effects */
        double pitch = (double)st->pitch;
        switch (st->effect) {
            case CT_FX_SLIDE: {
                int prev_idx = (abs_step == 0) ? 0 : (abs_step - 1);
                double prev = (double)sfx->steps[prev_idx].pitch;
                pitch = prev + (pitch - prev) * frac;
                break;
            }
            case CT_FX_VIBRATO: {
                double t = (abs_step + frac) * step_sec;
                pitch += 0.5 * sin(2.0 * M_PI * 6.0 * t);
                break;
            }
            case CT_FX_DROP:
                pitch -= 12.0 * frac;
                break;
            default: break;
        }
        const double freq = pitch_to_freq(pitch);

        /* Volume with fades */
        double vol = (double)st->volume / 7.0;
        if (st->effect == CT_FX_FADE_IN)  vol *= frac;
        if (st->effect == CT_FX_FADE_OUT) vol *= (1.0 - frac);

        double sample = (st->volume == 0)
            ? 0.0
            : waveform_sample(st->wave, osc_phase, &noise_state) * vol;

        int v = (int)(sample * 0.6 * 32767.0);
        if (v < -32768) v = -32768;
        if (v >  32767) v =  32767;
        out[i * 2 + 0] = (int16_t)v;
        out[i * 2 + 1] = (int16_t)v;

        osc_phase += freq / sample_rate;
        if (osc_phase >= 1.0) osc_phase -= floor(osc_phase);
    }

    *out_buf = (uint8_t*)out;
    *out_bytes = (uint32_t)bytes;
    return 0;
}

/* Pre-render every loaded slot into its Mix_Chunk. */
static void render_all_chunks(Chiptune* ct) {
    for (int i = 0; i < CHIPTUNE_SFX_COUNT; ++i) {
        ChiptuneSfx* sfx = &ct->sfx[i];

        int from = 0, to = 0;
        const bool looping = (sfx->loop_end > sfx->loop_start);
        if (looping) {
            from = sfx->loop_start;
            to   = sfx->loop_end;
        } else {
            /* Walk back from the end to find the last audible step. */
            int last = -1;
            for (int s = CHIPTUNE_STEPS - 1; s >= 0; --s) {
                if (sfx->steps[s].volume > 0) { last = s; break; }
            }
            if (last < 0) continue;   /* fully silent slot — no chunk */
            from = 0;
            to   = last + 1;
        }

        uint8_t* buf = NULL;
        uint32_t bytes = 0;
        if (render_sfx(sfx, ct->sample_rate, from, to, &buf, &bytes) != 0)
            continue;

        Mix_Chunk* chunk = Mix_QuickLoad_RAW(buf, bytes);
        if (!chunk) {
            free(buf);
            continue;
        }
        sfx->chunk      = chunk;
        sfx->pcm_buffer = buf;
    }
}

/* ─── Public API ───────────────────────────────────────────────────────── */

int chiptune_load(Chiptune* ct, const char* rom_base_path, int sample_rate) {
    memset(ct, 0, sizeof(*ct));
    ct->sample_rate = sample_rate > 0 ? sample_rate : 44100;
    /* Pre-fill defaults so unfilled slots are valid (silent, speed 4, etc.). */
    for (int i = 0; i < CHIPTUNE_SFX_COUNT; ++i) {
        ct->sfx[i].speed = 4;
        for (int s = 0; s < CHIPTUNE_STEPS; ++s) ct->sfx[i].steps[s].pitch = 24;
    }

    if (!rom_base_path) return 0;
    char path[512];
    snprintf(path, sizeof(path), "%s/sounds.json", rom_base_path);

    FILE* f = fopen(path, "rb");
    if (!f) return 0;   /* missing is fine */

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0 || sz > 4 * 1024 * 1024) { fclose(f); return 0; }

    char* buf = (char*)malloc((size_t)sz + 1);
    if (!buf) { fclose(f); return 0; }
    size_t got = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    buf[got] = '\0';

    int rc = parse_sounds_json(ct, buf, got);
    free(buf);

    if (rc != 0) {
        fprintf(stderr, "WARN: sounds.json parse failed; SFX disabled.\n");
        return 0;
    }

    render_all_chunks(ct);

    int rendered = 0;
    for (int i = 0; i < CHIPTUNE_SFX_COUNT; ++i) if (ct->sfx[i].chunk) rendered++;
    printf("[OK] Chiptune loaded: %d / %d sfx rendered\n",
           rendered, CHIPTUNE_SFX_COUNT);
    ct->loaded = 1;
    return 0;
}

void chiptune_free(Chiptune* ct) {
    if (!ct) return;
    for (int i = 0; i < CHIPTUNE_SFX_COUNT; ++i) {
        ChiptuneSfx* sfx = &ct->sfx[i];
        if (sfx->chunk) {
            Mix_FreeChunk(sfx->chunk);
            sfx->chunk = NULL;
        }
        if (sfx->pcm_buffer) {
            free(sfx->pcm_buffer);
            sfx->pcm_buffer = NULL;
        }
    }
    ct->loaded = 0;
}

void chiptune_play(Chiptune* ct, int sfx_id, int channel, bool loop_override) {
    if (!ct || !ct->loaded) return;
    if (sfx_id < 0 || sfx_id >= CHIPTUNE_SFX_COUNT) return;
    ChiptuneSfx* sfx = &ct->sfx[sfx_id];
    if (!sfx->chunk) return;
    int loops = loop_override ? -1
              : ((sfx->loop_end > sfx->loop_start) ? -1 : 0);
    Mix_PlayChannel(channel, sfx->chunk, loops);
}
