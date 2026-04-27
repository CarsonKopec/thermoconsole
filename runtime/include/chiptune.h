/*
 * ThermoConsole Runtime — chiptune.h
 *
 * PICO-8-style synthesised SFX. Load once from <rom>/sounds.json (the
 * format the editor's SoundEditor writes), pre-render every slot into
 * an SDL_mixer chunk, then play by integer slot id.
 *
 * The synth math here is the C port of editor/panels/SoundEditor.cpp's
 * fillAudioBuffer() — keep them in sync if you tweak the waveforms or
 * effects. Pitch 33 = A4 = 440 Hz; tick = 1/120 s; speed = ticks per step.
 */

#ifndef THERMO_CHIPTUNE_H
#define THERMO_CHIPTUNE_H

#include <stdbool.h>
#include <stdint.h>
#include <SDL_mixer.h>

#define CHIPTUNE_SFX_COUNT  32
#define CHIPTUNE_STEPS      32

typedef enum {
    CT_WAVE_TRIANGLE = 0,
    CT_WAVE_SQUARE   = 1,
    CT_WAVE_SAW      = 2,
    CT_WAVE_PULSE    = 3,
    CT_WAVE_NOISE    = 4,
    CT_WAVE_COUNT
} ChiptuneWave;

typedef enum {
    CT_FX_NONE     = 0,
    CT_FX_SLIDE    = 1,
    CT_FX_VIBRATO  = 2,
    CT_FX_DROP     = 3,
    CT_FX_FADE_IN  = 4,
    CT_FX_FADE_OUT = 5,
    CT_FX_COUNT
} ChiptuneEffect;

typedef struct {
    uint8_t pitch;       /* 0..63 */
    uint8_t wave;        /* ChiptuneWave */
    uint8_t volume;      /* 0..7; 0 = silent step */
    uint8_t effect;      /* ChiptuneEffect */
} ChiptuneStep;

typedef struct {
    char         name[32];
    uint8_t      speed;        /* 1..31 ticks per step */
    uint8_t      loop_start;
    uint8_t      loop_end;     /* > loop_start ⇒ loops infinitely */
    ChiptuneStep steps[CHIPTUNE_STEPS];

    /* Pre-rendered, owned by us. NULL until chiptune_render_all(). */
    Mix_Chunk*   chunk;
    uint8_t*     pcm_buffer;   /* backing memory for chunk; we free it */
} ChiptuneSfx;

typedef struct {
    int          loaded;       /* 0 = no sounds.json, just defaults */
    int          sample_rate;  /* Mix_OpenAudio's negotiated rate */
    ChiptuneSfx  sfx[CHIPTUNE_SFX_COUNT];
} Chiptune;

/* Load + parse <rom_base_path>/sounds.json and pre-render every slot.
 * Safe to call with a missing file — leaves the chiptune in an
 * unloaded state (loaded == 0) and returns 0. Returns non-zero only on
 * a hard error like a malformed file we can't recover from. */
int  chiptune_load(Chiptune* ct, const char* rom_base_path, int sample_rate);

/* Free every Mix_Chunk + backing buffer. Safe to call repeatedly. */
void chiptune_free(Chiptune* ct);

/* Play a slot on the given mixer channel. channel < 0 ⇒ auto-pick.
 * loop_override: if true, force infinite loop regardless of slot's
 *                loop_end / loop_start. Pass false to honour the slot. */
void chiptune_play(Chiptune* ct, int sfx_id, int channel, bool loop_override);

#endif /* THERMO_CHIPTUNE_H */
