/*
 * ThermoConsole Editor — SoundEditor panel implementation
 *
 * Layout
 *   ┌── Toolbar ────────────────────────────────────────────┐
 *   │ Sfx 00 [name] Speed:[..] Loop:[..] [Play][Stop][New]  │
 *   ├── Sfx list  ┬── Step grid (32 cols, click to select)──┤
 *   │  00 jump    │  ▮  ▮  ▮          ▮▮▮     ▮             │
 *   │  01 hit     │                                          │
 *   │  ...        │                                          │
 *   │             │  ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━   │
 *   │             │  W W W W ... waveform row                │
 *   │             │  V V V V ... volume bars                 │
 *   │             │  E E E E ... effect indicators           │
 *   ├─────────────┴─── Step inspector ────────────────────────┤
 *   │ Pitch: [C4 ▼]  Wave:[Square]  Vol:[5]  Effect:[None]   │
 *   └────────────────────────────────────────────────────────┘
 */

#include "SoundEditor.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <random>
#include <sstream>
#include <vector>

namespace {

// ─── Pitch / waveform helpers ───────────────────────────────────────────────

constexpr double kTwoPi = 6.28318530717958647692;

// PICO-8-ish: pitch 33 = A4 = 440 Hz. Range [0, 63] spans roughly A1..D7.
double pitchToFreq(double pitch) {
    return 440.0 * std::pow(2.0, (pitch - 33.0) / 12.0);
}

// Sample one period of the requested waveform at phase ∈ [0, 1).
double waveformSample(SoundEditor::Wave w, double phase, uint32_t& noiseState) {
    switch (w) {
        case SoundEditor::WaveTriangle: return 4.0 * std::fabs(phase - 0.5) - 1.0;
        case SoundEditor::WaveSquare:   return phase < 0.5 ? 1.0 : -1.0;
        case SoundEditor::WaveSaw:      return 2.0 * phase - 1.0;
        case SoundEditor::WavePulse:    return phase < 0.25 ? 1.0 : -1.0;
        case SoundEditor::WaveNoise: {
            // xorshift32 — deterministic from the captured noiseState so we
            // get reproducible per-step grain.
            uint32_t x = noiseState;
            x ^= x << 13; x ^= x >> 17; x ^= x << 5;
            noiseState = x;
            return (double)(x & 0xFFFF) / 32768.0 - 1.0;
        }
        default: return 0.0;
    }
}

// Pretty note name for inspector display. The synth uses PICO-8 numbering
// where pitch 33 = A4 = 440 Hz, so pitch 0 = C2 (~65.4 Hz). Pitch 63 ≈ D#7.
const char* noteNames[] = {"C", "C#", "D", "D#", "E", "F",
                           "F#", "G", "G#", "A", "A#", "B"};
std::string pitchLabel(int p) {
    if (p < 0 || p > 63) return "?";
    int oct = p / 12 + 2;
    int n   = p % 12;
    char buf[8];
    std::snprintf(buf, sizeof(buf), "%s%d", noteNames[n], oct);
    return buf;
}

// Piano-keyboard helper: which pitches are black (sharp/flat) keys.
bool isBlackKey(int p) {
    int n = p % 12;
    return n == 1 || n == 3 || n == 6 || n == 8 || n == 10;
}

const char* waveLabel(uint8_t w) {
    static const char* names[] = {"Triangle", "Square", "Saw", "Pulse", "Noise"};
    return (w < 5) ? names[w] : "?";
}

const char* effectLabel(uint8_t e) {
    static const char* names[] = {"None", "Slide", "Vibrato", "Drop",
                                  "Fade-in", "Fade-out"};
    return (e < 6) ? names[e] : "?";
}

ImU32 waveColor(uint8_t w) {
    switch (w) {
        case SoundEditor::WaveTriangle: return IM_COL32(120, 200, 100, 255);
        case SoundEditor::WaveSquare:   return IM_COL32(100, 180, 230, 255);
        case SoundEditor::WaveSaw:      return IM_COL32(230, 180,  80, 255);
        case SoundEditor::WavePulse:    return IM_COL32(220, 110, 200, 255);
        case SoundEditor::WaveNoise:    return IM_COL32(180, 180, 180, 255);
        default:                        return IM_COL32( 80,  80,  80, 255);
    }
}

// ─── Tiny JSON tokenizer (recursive descent, sized to sounds.json) ──────────
//
// We need objects, arrays, numbers, strings. No true/false/null support.
// Throws std::runtime_error on malformed input — caller catches & logs.

struct JsonReader {
    const std::string& s;
    size_t             p = 0;

    void skipWs() {
        while (p < s.size() && std::isspace((unsigned char)s[p])) ++p;
    }
    void expect(char c) {
        skipWs();
        if (p >= s.size() || s[p] != c)
            throw std::runtime_error(std::string("expected '") + c + "'");
        ++p;
    }
    bool peek(char c) {
        skipWs();
        return p < s.size() && s[p] == c;
    }
    std::string readString() {
        skipWs();
        if (p >= s.size() || s[p] != '"') throw std::runtime_error("expected string");
        ++p;
        std::string out;
        while (p < s.size() && s[p] != '"') {
            if (s[p] == '\\' && p + 1 < s.size()) {
                char e = s[p + 1];
                switch (e) {
                    case '"':  out += '"';  break;
                    case '\\': out += '\\'; break;
                    case '/':  out += '/';  break;
                    case 'n':  out += '\n'; break;
                    case 't':  out += '\t'; break;
                    case 'r':  out += '\r'; break;
                    default:   out += e;    break;
                }
                p += 2;
            } else {
                out += s[p++];
            }
        }
        if (p >= s.size()) throw std::runtime_error("unterminated string");
        ++p; // consume closing quote
        return out;
    }
    double readNumber() {
        skipWs();
        size_t start = p;
        if (p < s.size() && (s[p] == '-' || s[p] == '+')) ++p;
        while (p < s.size() && (std::isdigit((unsigned char)s[p]) ||
                                s[p] == '.' || s[p] == 'e' || s[p] == 'E' ||
                                s[p] == '-' || s[p] == '+')) ++p;
        if (start == p) throw std::runtime_error("expected number");
        return std::stod(s.substr(start, p - start));
    }
    // Skip an arbitrary JSON value (used to skip unknown fields).
    void skipValue() {
        skipWs();
        if (p >= s.size()) throw std::runtime_error("unexpected eof");
        char c = s[p];
        if (c == '"') { readString(); return; }
        if (c == '{' || c == '[') {
            char open = c, close = (c == '{' ? '}' : ']');
            int depth = 0;
            while (p < s.size()) {
                if (s[p] == '"') { readString(); continue; }
                if (s[p] == open)  { ++depth; ++p; continue; }
                if (s[p] == close) { --depth; ++p; if (depth == 0) return; continue; }
                ++p;
            }
            throw std::runtime_error("unbalanced brackets");
        }
        // Number, true, false, null — read until terminator.
        while (p < s.size() && s[p] != ',' && s[p] != '}' && s[p] != ']' &&
               !std::isspace((unsigned char)s[p])) ++p;
    }
};

} // namespace

// ─── Construction / project lifecycle ───────────────────────────────────────

SoundEditor::SoundEditor(ThermoEditor* editor) : m_editor(editor) {
    initAudio();
    clearAllSfx();
    seedDefaultPresets();
}

SoundEditor::~SoundEditor() {
    shutdownAudio();
}

void SoundEditor::onProjectOpened(const fs::path& projectPath) {
    stop();
    m_projectPath  = projectPath;
    m_selectedSfx  = 0;
    m_selectedStep = -1;
    clearAllSfx();
    m_presets.clear();
    load();                  // populates m_sfx and (if present) m_presets
    seedDefaultPresets();    // only fills if load found none
}

void SoundEditor::onProjectClosed() {
    stop();
    m_projectPath.clear();
    clearAllSfx();
    m_dirty = false;
}

void SoundEditor::drawMenuItem() {
    ImGui::MenuItem("Sound Editor", nullptr, &m_visible);
}

// ─── Audio init / shutdown ──────────────────────────────────────────────────

void SoundEditor::initAudio() {
    SDL_AudioSpec want{}, got{};
    want.freq     = m_sampleRate;
    want.format   = AUDIO_S16SYS;
    want.channels = 1;          // mono is plenty for chiptune SFX
    want.samples  = 1024;       // ~46 ms at 22050
    want.callback = &SoundEditor::audioCallback;
    want.userdata = this;

    m_audioDev = SDL_OpenAudioDevice(nullptr, 0, &want, &got, 0);
    if (m_audioDev == 0) {
        m_editor->log(std::string("SoundEditor: SDL_OpenAudioDevice failed: ")
                      + SDL_GetError(), true);
        return;
    }
    m_sampleRate = got.freq;
    SDL_PauseAudioDevice(m_audioDev, 0);   // start streaming silence
}

void SoundEditor::shutdownAudio() {
    if (m_audioDev != 0) {
        SDL_CloseAudioDevice(m_audioDev);
        m_audioDev = 0;
    }
}

// ─── Synth callback ─────────────────────────────────────────────────────────
//
// Runs on SDL's audio thread. Holds m_playMutex for the whole fill so the UI
// thread can't mutate playback state mid-block. SFX *data* is read live from
// m_sfx[m_playSfxId] under the same lock — UI edits to that slot will land
// on the next callback (audible glitch is acceptable for a preview).

void SDLCALL SoundEditor::audioCallback(void* user, Uint8* stream, int len) {
    auto* self = static_cast<SoundEditor*>(user);
    self->fillAudioBuffer(reinterpret_cast<int16_t*>(stream),
                          len / (int)sizeof(int16_t));
}

void SoundEditor::fillAudioBuffer(int16_t* out, int sampleCount) {
    std::lock_guard<std::mutex> lock(m_playMutex);

    if (!m_playing) {
        std::memset(out, 0, sampleCount * sizeof(int16_t));
        return;
    }

    // Capture the SFX by reference (we hold the mutex; UI writes also need it
    // ... but we deliberately *don't* require UI to lock for editing — see
    // the comment block above. Worst case a tweak appears mid-step.)
    const Sfx&  sfx       = m_sfx[m_playSfxId];
    const double tickSec  = 1.0 / 120.0;                      // PICO-8 tempo unit
    const double stepSec  = std::max(1, (int)sfx.speed) * tickSec;
    const double stepDur  = stepSec * m_sampleRate;           // samples / step

    // 1 ms attack + release on every step — matches runtime/src/chiptune.c.
    // Without it, hard volume edges between steps click; stacked, that reads
    // as harshness.
    const double edgeSamples = 0.001 * m_sampleRate;

    for (int i = 0; i < sampleCount; ++i) {
        // End of pattern? Either loop or stop.
        if (m_playStepIdx >= kSteps) {
            if (sfx.loop_end > sfx.loop_start && sfx.loop_end <= kSteps) {
                m_playStepIdx  = sfx.loop_start;
                m_playStepFrac = 0.0;
            } else {
                m_playing = false;
                std::memset(out + i, 0, (sampleCount - i) * sizeof(int16_t));
                return;
            }
        }

        const Step& step = sfx.steps[m_playStepIdx];

        // ── Pitch (with slide / vibrato / drop) ─────────────────────────────
        double pitch = (double)step.pitch;
        switch (step.effect) {
            case EffectSlide: {
                int prevIdx = (m_playStepIdx == 0) ? 0 : (m_playStepIdx - 1);
                double prev = (double)sfx.steps[prevIdx].pitch;
                pitch = prev + (pitch - prev) * m_playStepFrac;
                break;
            }
            case EffectVibrato: {
                double t = (m_playStepIdx + m_playStepFrac) * stepSec;
                pitch += 0.5 * std::sin(kTwoPi * 6.0 * t);
                break;
            }
            case EffectDrop:
                pitch -= 12.0 * m_playStepFrac;   // drop one octave across step
                break;
            default: break;
        }
        const double freq = pitchToFreq(pitch);

        // ── Volume (with fade-in / fade-out) ────────────────────────────────
        double vol = (double)step.volume / 7.0;
        if (step.effect == EffectFadeIn)  vol *= m_playStepFrac;
        if (step.effect == EffectFadeOut) vol *= (1.0 - m_playStepFrac);

        double sample = (step.volume == 0)
            ? 0.0
            : waveformSample((Wave)step.wave, m_oscPhase, m_noiseState) * vol;

        // Step-edge envelope (anti-click) — matches the runtime synth.
        double local     = m_playStepFrac * stepDur;
        double remaining = stepDur - local;
        if (local     < edgeSamples) sample *= local     / edgeSamples;
        if (remaining < edgeSamples) sample *= remaining / edgeSamples;

        // 0.30 headroom keeps multiple stacked channels from clipping.
        int v = (int)(sample * 0.30 * 32767.0);
        out[i] = (int16_t)std::clamp(v, -32768, 32767);

        m_oscPhase += freq / m_sampleRate;
        if (m_oscPhase >= 1.0) m_oscPhase -= std::floor(m_oscPhase);

        m_playStepFrac += 1.0 / stepDur;
        while (m_playStepFrac >= 1.0) {
            m_playStepFrac -= 1.0;
            ++m_playStepIdx;
        }
    }
}

// ─── Playback control ───────────────────────────────────────────────────────

void SoundEditor::play(int sfxId) {
    std::lock_guard<std::mutex> lock(m_playMutex);
    m_playSfxId    = std::clamp(sfxId, 0, kSfxCount - 1);
    m_playStepIdx  = 0;
    m_playStepFrac = 0.0;
    m_oscPhase     = 0.0;
    m_playing      = true;
}

void SoundEditor::stop() {
    std::lock_guard<std::mutex> lock(m_playMutex);
    m_playing = false;
}

// ─── Edit ops ──────────────────────────────────────────────────────────────

void SoundEditor::clearSfx(int slot) {
    if (slot < 0 || slot >= kSfxCount) return;
    m_sfx[slot] = Sfx{};
    char buf[16];
    std::snprintf(buf, sizeof(buf), "sfx_%02d", slot);
    m_sfx[slot].name = buf;
}

void SoundEditor::clearAllSfx() {
    for (int i = 0; i < kSfxCount; ++i) clearSfx(i);
}

void SoundEditor::markDirty() {
    m_dirty = true;
}

void SoundEditor::seedDefaultPresets() {
    if (!m_presets.empty()) return;
    // Hand-picked starters that show off each waveform / effect.
    m_presets.push_back({"Lead",  WaveSquare,   5, EffectNone});
    m_presets.push_back({"Bass",  WaveTriangle, 6, EffectNone});
    m_presets.push_back({"Pluck", WaveSaw,      4, EffectFadeOut});
    m_presets.push_back({"Pad",   WavePulse,    4, EffectFadeIn});
    m_presets.push_back({"Kick",  WaveNoise,    7, EffectDrop});
    m_presets.push_back({"Snare", WaveNoise,    5, EffectFadeOut});
    m_presets.push_back({"Hat",   WaveNoise,    3, EffectFadeOut});
}

void SoundEditor::applyPresetToStep(const Preset& p, int stepIdx) {
    if (stepIdx < 0 || stepIdx >= kSteps) return;
    Step& st = m_sfx[m_selectedSfx].steps[stepIdx];
    st.wave   = p.wave;
    st.volume = p.volume;
    st.effect = p.effect;
    markDirty();
}

// ─── IO ────────────────────────────────────────────────────────────────────

fs::path SoundEditor::soundsPath() const {
    if (m_projectPath.empty()) return {};
    const std::string& f = m_editor->manifest().sounds_file;
    return m_projectPath / (f.empty() ? std::string("sounds.json") : f);
}

void SoundEditor::load() {
    fs::path path = soundsPath();
    if (path.empty()) return;
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        // Missing file is normal for a fresh project.
        return;
    }
    std::string text((std::istreambuf_iterator<char>(f)),
                      std::istreambuf_iterator<char>());
    if (deserialize(text)) {
        m_editor->log("Loaded " + path.filename().string()
                      + " (" + std::to_string(kSfxCount) + " sfx slots).");
        m_dirty = false;
    } else {
        m_editor->log("Failed to parse " + path.string()
                      + " — kept current sfx in memory.", true);
    }
}

void SoundEditor::save() {
    fs::path path = soundsPath();
    if (path.empty()) {
        m_editor->log("No project open — nothing to save.", true);
        return;
    }
    std::string out;
    serialize(out);
    std::ofstream f(path, std::ios::binary);
    if (!f) {
        m_editor->log("Failed to open " + path.string() + " for write.", true);
        return;
    }
    f << out;
    if (!f) {
        m_editor->log("Write failed: " + path.string(), true);
        return;
    }
    m_editor->log("Saved " + path.filename().string());
    m_dirty = false;
    // Run live-reload so a future runtime that synthesises sounds.json sees
    // the change without a restart.
    m_editor->notifySourceSaved();
}

void SoundEditor::serialize(std::string& out) const {
    std::ostringstream o;
    o << "{\n  \"version\": 1,\n";

    // Presets first so the file reads top-down (declarations before uses).
    o << "  \"presets\": [";
    for (size_t i = 0; i < m_presets.size(); ++i) {
        const Preset& p = m_presets[i];
        if (i > 0) o << ",";
        o << "\n    {\"name\": \"" << p.name << "\""
          << ", \"wave\": "   << (int)p.wave
          << ", \"volume\": " << (int)p.volume
          << ", \"effect\": " << (int)p.effect
          << "}";
    }
    o << (m_presets.empty() ? "" : "\n  ") << "],\n";

    o << "  \"sfx\": [\n";
    for (int i = 0; i < kSfxCount; ++i) {
        const Sfx& s = m_sfx[i];
        o << "    {\n"
          << "      \"id\": "         << i                  << ",\n"
          << "      \"name\": \""     << s.name             << "\",\n"
          << "      \"speed\": "      << (int)s.speed       << ",\n"
          << "      \"loop_start\": " << (int)s.loop_start  << ",\n"
          << "      \"loop_end\": "   << (int)s.loop_end    << ",\n"
          << "      \"steps\": [";
        for (int j = 0; j < kSteps; ++j) {
            const Step& st = s.steps[j];
            if (j > 0) o << ",";
            o << "\n        {\"pitch\": " << (int)st.pitch
              << ", \"wave\": "   << (int)st.wave
              << ", \"volume\": " << (int)st.volume
              << ", \"effect\": " << (int)st.effect
              << "}";
        }
        o << "\n      ]\n    }";
        if (i + 1 < kSfxCount) o << ",";
        o << "\n";
    }
    o << "  ]\n}\n";
    out = o.str();
}

bool SoundEditor::deserialize(const std::string& text) {
    try {
        JsonReader r{text};
        r.expect('{');
        // Walk top-level keys until we hit the "sfx" array.
        bool sawSfx = false;
        while (!r.peek('}')) {
            std::string key = r.readString();
            r.expect(':');
            if (key == "presets") {
                m_presets.clear();
                r.expect('[');
                while (!r.peek(']')) {
                    Preset p;
                    r.expect('{');
                    while (!r.peek('}')) {
                        std::string fk = r.readString();
                        r.expect(':');
                        if      (fk == "name")   p.name   = r.readString();
                        else if (fk == "wave")   p.wave   = (uint8_t)std::clamp((int)r.readNumber(), 0, (int)WaveCount   - 1);
                        else if (fk == "volume") p.volume = (uint8_t)std::clamp((int)r.readNumber(), 0, 7);
                        else if (fk == "effect") p.effect = (uint8_t)std::clamp((int)r.readNumber(), 0, (int)EffectCount - 1);
                        else                     r.skipValue();
                        if (r.peek(',')) r.expect(',');
                    }
                    r.expect('}');
                    m_presets.push_back(std::move(p));
                    if (r.peek(',')) r.expect(',');
                }
                r.expect(']');
            } else if (key == "sfx") {
                r.expect('[');
                int slot = 0;
                while (!r.peek(']')) {
                    if (slot >= kSfxCount) { r.skipValue(); }
                    else                   {
                        Sfx& s = m_sfx[slot];
                        s = Sfx{};
                        r.expect('{');
                        while (!r.peek('}')) {
                            std::string fk = r.readString();
                            r.expect(':');
                            if      (fk == "id")          (void)r.readNumber();   // honored by position, ignored
                            else if (fk == "name")        s.name       = r.readString();
                            else if (fk == "speed")       s.speed      = (uint8_t)std::clamp((int)r.readNumber(), 1, 31);
                            else if (fk == "loop_start")  s.loop_start = (uint8_t)std::clamp((int)r.readNumber(), 0, kSteps - 1);
                            else if (fk == "loop_end")    s.loop_end   = (uint8_t)std::clamp((int)r.readNumber(), 0, kSteps);
                            else if (fk == "steps") {
                                r.expect('[');
                                int si = 0;
                                while (!r.peek(']')) {
                                    if (si >= kSteps) { r.skipValue(); }
                                    else {
                                        Step& st = s.steps[si];
                                        r.expect('{');
                                        while (!r.peek('}')) {
                                            std::string sk = r.readString();
                                            r.expect(':');
                                            int v = (int)r.readNumber();
                                            if      (sk == "pitch")  st.pitch  = (uint8_t)std::clamp(v, 0, 63);
                                            else if (sk == "wave")   st.wave   = (uint8_t)std::clamp(v, 0, (int)WaveCount   - 1);
                                            else if (sk == "volume") st.volume = (uint8_t)std::clamp(v, 0, 7);
                                            else if (sk == "effect") st.effect = (uint8_t)std::clamp(v, 0, (int)EffectCount - 1);
                                            if (r.peek(',')) r.expect(',');
                                        }
                                        r.expect('}');
                                        ++si;
                                    }
                                    if (r.peek(',')) r.expect(',');
                                }
                                r.expect(']');
                            } else {
                                r.skipValue();
                            }
                            if (r.peek(',')) r.expect(',');
                        }
                        r.expect('}');
                        ++slot;
                    }
                    if (r.peek(',')) r.expect(',');
                }
                r.expect(']');
                sawSfx = true;
            } else {
                r.skipValue();
            }
            if (r.peek(',')) r.expect(',');
        }
        r.expect('}');
        return sawSfx;
    } catch (const std::exception& e) {
        m_editor->log(std::string("sounds.json parse error: ") + e.what(), true);
        return false;
    }
}

// ─── Top-level draw ────────────────────────────────────────────────────────

void SoundEditor::draw() {
    if (!m_visible) return;
    ImGui::SetNextWindowSize({1080, 720}, ImGuiCond_FirstUseEver);
    ImGui::Begin("Sound Editor", &m_visible);

    drawToolbar();
    ImGui::Separator();

    // Two-column body: sfx list on the left, palette + grid + inspector on
    // the right.
    const float listW = 140.f;
    if (ImGui::BeginChild("##sfxlist", {listW, 0}, true)) {
        drawSfxList();
    }
    ImGui::EndChild();
    ImGui::SameLine();
    if (ImGui::BeginChild("##gridarea", {0, 0}, false)) {
        drawPalette();
        ImGui::Separator();
        drawPianoRoll();
        drawVelocityStrip();
        ImGui::Separator();
        drawStepInspector();
    }
    ImGui::EndChild();

    ImGui::End();
}

// ─── Toolbar ───────────────────────────────────────────────────────────────

void SoundEditor::drawToolbar() {
    Sfx& s = m_sfx[m_selectedSfx];

    ImGui::Text("Sfx %02d", m_selectedSfx);
    ImGui::SameLine();

    // Name
    char nameBuf[64];
    std::snprintf(nameBuf, sizeof(nameBuf), "%s", s.name.c_str());
    ImGui::SetNextItemWidth(140);
    if (ImGui::InputText("##name", nameBuf, sizeof(nameBuf))) {
        s.name = nameBuf;
        markDirty();
    }
    ImGui::SameLine();

    // Speed slider
    int spd = s.speed;
    ImGui::SetNextItemWidth(100);
    if (ImGui::SliderInt("Speed", &spd, 1, 31)) {
        s.speed = (uint8_t)spd;
        markDirty();
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Ticks per step (1/120 s each).\nLower = faster.");
    ImGui::SameLine();

    // Loop start / end
    int ls = s.loop_start, le = s.loop_end;
    ImGui::SetNextItemWidth(60);
    if (ImGui::InputInt("##ls", &ls, 0)) { s.loop_start = (uint8_t)std::clamp(ls, 0, kSteps - 1); markDirty(); }
    ImGui::SameLine();
    ImGui::TextDisabled("→");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(60);
    if (ImGui::InputInt("Loop", &le, 0)) { s.loop_end = (uint8_t)std::clamp(le, 0, kSteps); markDirty(); }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Loop range. End ≤ Start ⇒ play once.");

    // Right-aligned playback + IO buttons
    ImGui::SameLine();
    float right = ImGui::GetWindowWidth() - 10.f;
    float wPlay = 60, wStop = 60, wNew = 70, wSave = 70, gap = 6;
    float total = wPlay + wStop + wNew + wSave + gap * 3;
    ImGui::SameLine(right - total);

    ImGui::PushStyleColor(ImGuiCol_Button,        {0.20f, 0.65f, 0.30f, 1.f});
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, {0.30f, 0.85f, 0.40f, 1.f});
    if (ImGui::Button("\xE2\x96\xB6 Play", {wPlay, 0})) play(m_selectedSfx);
    ImGui::PopStyleColor(2);
    ImGui::SameLine(0, gap);
    if (ImGui::Button("\xE2\x96\xA0 Stop", {wStop, 0})) stop();
    ImGui::SameLine(0, gap);
    if (ImGui::Button("Clear", {wNew, 0})) {
        clearSfx(m_selectedSfx);
        markDirty();
    }
    ImGui::SameLine(0, gap);
    if (ImGui::Button(m_dirty ? "Save *" : "Save", {wSave, 0})) save();
}

// ─── SFX list (left column) ────────────────────────────────────────────────

void SoundEditor::drawSfxList() {
    for (int i = 0; i < kSfxCount; ++i) {
        char label[64];
        std::snprintf(label, sizeof(label), "%02d  %s", i,
                      m_sfx[i].name.empty() ? "—" : m_sfx[i].name.c_str());
        bool sel = (m_selectedSfx == i);
        if (ImGui::Selectable(label, sel)) {
            m_selectedSfx  = i;
            m_selectedStep = -1;
        }
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
            play(i);
        }
    }
}

// ─── Palette (drag sources) ────────────────────────────────────────────────
//
// Three tabs of draggable cards. The grid columns (drawGrid) accept the
// payloads as drop targets. Payload IDs:
//   "SFX_WAVE"   → uint8_t   waveform index
//   "SFX_EFFECT" → uint8_t   effect index
//   "SFX_PRESET" → int       index into m_presets

void SoundEditor::drawPalette() {
    ImGui::Text("Palette");
    ImGui::SameLine();
    ImGui::TextDisabled(" \xE2\x80\xA2 drag onto a step in the grid below");

    if (ImGui::BeginTabBar("##palette_tabs")) {

        // ── Waves ────────────────────────────────────────────────────────
        if (ImGui::BeginTabItem("Waves")) {
            m_paletteTab = PaletteTab::Waves;
            for (uint8_t w = 0; w < (uint8_t)WaveCount; ++w) {
                ImGui::PushID(w);
                ImGui::PushStyleColor(ImGuiCol_Button, waveColor(w));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, waveColor(w));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive,  waveColor(w));
                ImGui::Button(waveLabel(w), {78, 30});
                ImGui::PopStyleColor(3);
                if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
                    ImGui::SetDragDropPayload("SFX_WAVE", &w, sizeof(w));
                    ImGui::Text("Wave: %s", waveLabel(w));
                    ImGui::EndDragDropSource();
                }
                ImGui::PopID();
                ImGui::SameLine();
            }
            ImGui::NewLine();
            ImGui::EndTabItem();
        }

        // ── Effects ──────────────────────────────────────────────────────
        if (ImGui::BeginTabItem("Effects")) {
            m_paletteTab = PaletteTab::Effects;
            for (uint8_t e = 0; e < (uint8_t)EffectCount; ++e) {
                ImGui::PushID(100 + e);
                if (ImGui::Button(effectLabel(e), {78, 30})) { /* drag-only */ }
                if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
                    ImGui::SetDragDropPayload("SFX_EFFECT", &e, sizeof(e));
                    ImGui::Text("Effect: %s", effectLabel(e));
                    ImGui::EndDragDropSource();
                }
                ImGui::PopID();
                ImGui::SameLine();
            }
            ImGui::NewLine();
            ImGui::EndTabItem();
        }

        // ── Presets ──────────────────────────────────────────────────────
        if (ImGui::BeginTabItem("Presets")) {
            m_paletteTab = PaletteTab::Presets;

            // Save-current-step-as-preset button. Only enabled when a step is
            // selected — that's where we read the wave / volume / effect from.
            const bool canSave = (m_selectedStep >= 0);
            ImGui::BeginDisabled(!canSave);
            if (ImGui::Button("+ Save selected step", {180, 26})) {
                const Step& src = m_sfx[m_selectedSfx].steps[m_selectedStep];
                Preset p;
                char nameBuf[32];
                std::snprintf(nameBuf, sizeof(nameBuf),
                              "preset_%d", (int)m_presets.size());
                p.name   = nameBuf;
                p.wave   = src.wave;
                p.volume = (src.volume == 0) ? 5 : src.volume;
                p.effect = src.effect;
                m_presets.push_back(p);
                markDirty();
            }
            ImGui::EndDisabled();
            if (!canSave && ImGui::IsItemHovered())
                ImGui::SetTooltip("Click a step in the grid first.");
            ImGui::SameLine();
            ImGui::TextDisabled(" \xE2\x80\xA2 right-click a preset to delete");

            // Card row.
            int toDelete = -1;
            for (int i = 0; i < (int)m_presets.size(); ++i) {
                ImGui::PushID(200 + i);
                const Preset& p = m_presets[i];
                ImGui::PushStyleColor(ImGuiCol_Button, waveColor(p.wave));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, waveColor(p.wave));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive,  waveColor(p.wave));
                ImGui::Button(p.name.c_str(), {96, 30});
                ImGui::PopStyleColor(3);
                if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
                    ImGui::SetDragDropPayload("SFX_PRESET", &i, sizeof(i));
                    ImGui::Text("Preset: %s\n%s, vol %d, %s",
                                p.name.c_str(),
                                waveLabel(p.wave), p.volume,
                                effectLabel(p.effect));
                    ImGui::EndDragDropSource();
                }
                if (ImGui::BeginPopupContextItem("##preset_ctx")) {
                    if (ImGui::MenuItem("Delete preset")) toDelete = i;
                    ImGui::EndPopup();
                }
                ImGui::PopID();
                ImGui::SameLine();
            }
            ImGui::NewLine();
            if (toDelete >= 0 && toDelete < (int)m_presets.size()) {
                m_presets.erase(m_presets.begin() + toDelete);
                markDirty();
            }
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }
}

// ─── Piano roll (FL-Studio-style) ──────────────────────────────────────────
//
// Layout:
//   [keyboard sidebar | roll grid (32 steps × 64 pitches)]
//
// The keyboard on the left shows alternating white/black key rows with C
// labels every octave (C2..C7+). Each non-silent step in the SFX is a
// colored rectangle in the roll (color = waveColor(step.wave)). Click
// anywhere in the roll to select that step *and* set its pitch from the
// row under the cursor — same one-handed sketching as the old step grid,
// just rotated. Right-click silences a step. Drag-drop targets per column
// accept SFX_WAVE / SFX_EFFECT / SFX_PRESET payloads from the palette.
//
// Pitch range: 0..63 (PICO-8 numbering). Row 0 is the highest pitch
// visually (top of the grid), so we flip the index when drawing.

void SoundEditor::drawPianoRoll() {
    Sfx& s = m_sfx[m_selectedSfx];

    constexpr int   kPitches  = 64;
    constexpr float kRowH     = 7.f;
    constexpr float kKeysW    = 38.f;
    const float     gridH     = kPitches * kRowH;

    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float  rollW = std::max(200.f, avail.x - kKeysW);
    const float  colW  = rollW / kSteps;

    ImVec2 origin = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    const ImVec2 keysOrigin = origin;
    const ImVec2 rollOrigin = {origin.x + kKeysW, origin.y};

    // ── Keyboard sidebar ────────────────────────────────────────────────
    for (int p = 0; p < kPitches; ++p) {
        // Row 0 of the visual grid is the *highest* pitch (63).
        const int   pitch = (kPitches - 1) - p;
        const float y0    = keysOrigin.y + p * kRowH;
        const float y1    = y0 + kRowH;
        const bool  black = isBlackKey(pitch);
        ImU32 keyCol = black ? IM_COL32(40, 40, 50, 255)
                             : IM_COL32(220, 220, 225, 255);
        dl->AddRectFilled({keysOrigin.x, y0},
                          {keysOrigin.x + kKeysW, y1}, keyCol);
        // Hairline separator between keys.
        dl->AddLine({keysOrigin.x, y1},
                    {keysOrigin.x + kKeysW, y1},
                    IM_COL32(60, 60, 70, 255), 1.f);
        // Octave label sits on every C row.
        if (pitch % 12 == 0) {
            char lbl[8];
            std::snprintf(lbl, sizeof(lbl), "C%d", pitch / 12 + 2);
            dl->AddText({keysOrigin.x + 4, y0 - 2},
                        IM_COL32(20, 20, 30, 255), lbl);
        }
    }

    // ── Roll background + grid lines ────────────────────────────────────
    dl->AddRectFilled(rollOrigin,
                      {rollOrigin.x + rollW, rollOrigin.y + gridH},
                      IM_COL32(18, 18, 24, 255));

    // Subtle row striping aligned with black keys, so the roll mirrors
    // the keyboard sidebar visually.
    for (int p = 0; p < kPitches; ++p) {
        const int   pitch = (kPitches - 1) - p;
        if (!isBlackKey(pitch)) continue;
        const float y0 = rollOrigin.y + p * kRowH;
        dl->AddRectFilled({rollOrigin.x, y0},
                          {rollOrigin.x + rollW, y0 + kRowH},
                          IM_COL32(28, 28, 36, 255));
    }

    // Vertical grid lines: thicker every 4 steps (beat lines), thinner
    // between. The 16-step (half-pattern) line is brighter.
    for (int i = 0; i <= kSteps; ++i) {
        const float x = rollOrigin.x + i * colW;
        ImU32 col = (i % 4 == 0) ? IM_COL32(70, 70, 85, 255)
                                 : IM_COL32(40, 40, 50, 255);
        if (i == kSteps / 2) col = IM_COL32(100, 100, 120, 255);
        dl->AddLine({x, rollOrigin.y}, {x, rollOrigin.y + gridH}, col,
                    (i % 4 == 0) ? 1.5f : 1.f);
    }

    // Loop range shading on top of the grid (between loop_start..loop_end).
    if (s.loop_end > s.loop_start) {
        const float lx0 = rollOrigin.x + s.loop_start * colW;
        const float lx1 = rollOrigin.x + s.loop_end   * colW;
        dl->AddRectFilled({lx0, rollOrigin.y},
                          {lx1, rollOrigin.y + gridH},
                          IM_COL32(220, 200, 80, 25));
    }

    // ── Playhead ────────────────────────────────────────────────────────
    int playingStep = -1;
    {
        std::lock_guard<std::mutex> lock(m_playMutex);
        if (m_playing && m_playSfxId == m_selectedSfx)
            playingStep = m_playStepIdx;
    }
    if (playingStep >= 0 && playingStep < kSteps) {
        const float x = rollOrigin.x + playingStep * colW;
        dl->AddRectFilled({x, rollOrigin.y},
                          {x + colW, rollOrigin.y + gridH},
                          IM_COL32(80, 200, 100, 50));
    }

    // ── Notes (one rectangle per non-silent step) ───────────────────────
    for (int i = 0; i < kSteps; ++i) {
        const Step& st = s.steps[i];
        if (st.volume == 0) continue;
        const int   visualRow = (kPitches - 1) - st.pitch;
        const float x0 = rollOrigin.x + i * colW + 1.f;
        const float x1 = rollOrigin.x + (i + 1) * colW - 1.f;
        const float y0 = rollOrigin.y + visualRow * kRowH + 0.5f;
        const float y1 = y0 + kRowH - 1.f;
        const ImU32 col = waveColor(st.wave);
        dl->AddRectFilled({x0, y0}, {x1, y1}, col);
        dl->AddRect({x0, y0}, {x1, y1}, IM_COL32(0, 0, 0, 120), 0.f, 0, 1.f);

        // Effect glyph overlaid on the note (small).
        const char* glyph = "";
        switch (st.effect) {
            case EffectSlide:   glyph = "S"; break;
            case EffectVibrato: glyph = "V"; break;
            case EffectDrop:    glyph = "D"; break;
            case EffectFadeIn:  glyph = "<"; break;
            case EffectFadeOut: glyph = ">"; break;
            default: break;
        }
        if (*glyph) {
            dl->AddText({x0 + 2, y0 - 1},
                        IM_COL32(255, 255, 255, 220), glyph);
        }

        // Selection outline.
        if (i == m_selectedStep) {
            dl->AddRect({x0 - 1, y0 - 1}, {x1 + 1, y1 + 1},
                        IM_COL32(255, 220, 60, 255), 0.f, 0, 2.f);
        }
    }

    // ── Hit areas ───────────────────────────────────────────────────────
    // One InvisibleButton per column so each can carry its own drag-drop
    // target. Within a column, click+drag sketches pitch by reading
    // mouse Y; right-click silences the step.
    for (int i = 0; i < kSteps; ++i) {
        const float x0 = rollOrigin.x + i * colW;
        ImGui::SetCursorScreenPos({x0, rollOrigin.y});
        char id[16];
        std::snprintf(id, sizeof(id), "##col%d", i);
        ImGui::InvisibleButton(id, {colW, gridH},
                               ImGuiButtonFlags_MouseButtonLeft |
                               ImGuiButtonFlags_MouseButtonRight);

        // Left-click / drag: select + set pitch from cursor row.
        if (ImGui::IsItemActive() && ImGui::IsMouseDown(0)) {
            m_selectedStep = i;
            ImVec2 mp = ImGui::GetIO().MousePos;
            float t = (mp.y - rollOrigin.y) / kRowH;
            int   visualRow = (int)std::clamp(t, 0.f, (float)(kPitches - 1));
            int   pitch = (kPitches - 1) - visualRow;
            Step& st = s.steps[i];
            if (st.pitch != (uint8_t)pitch) {
                st.pitch = (uint8_t)pitch;
                if (st.volume == 0) st.volume = 5;
                markDirty();
            } else if (st.volume == 0) {
                st.volume = 5;
                markDirty();
            }
        }

        // Right-click: silence the step.
        if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(1)) {
            Step& st = s.steps[i];
            if (st.volume != 0) { st.volume = 0; markDirty(); }
            m_selectedStep = i;
        }

        // Drag-drop drop target — palette cards land here.
        if (ImGui::BeginDragDropTarget()) {
            if (auto* pl = ImGui::AcceptDragDropPayload("SFX_WAVE")) {
                uint8_t w = *(const uint8_t*)pl->Data;
                Step& st = s.steps[i];
                st.wave = w;
                if (st.volume == 0) st.volume = 5;
                m_selectedStep = i;
                markDirty();
            }
            if (auto* pl = ImGui::AcceptDragDropPayload("SFX_EFFECT")) {
                uint8_t e = *(const uint8_t*)pl->Data;
                s.steps[i].effect = e;
                m_selectedStep = i;
                markDirty();
            }
            if (auto* pl = ImGui::AcceptDragDropPayload("SFX_PRESET")) {
                int idx = *(const int*)pl->Data;
                if (idx >= 0 && idx < (int)m_presets.size()) {
                    applyPresetToStep(m_presets[idx], i);
                    m_selectedStep = i;
                }
            }
            ImGui::EndDragDropTarget();
        }
    }

    // Reserve the layout space we drew into so subsequent widgets stack
    // below the roll instead of on top of it.
    ImGui::SetCursorScreenPos({origin.x, origin.y + gridH + 4.f});
    ImGui::Dummy({kKeysW + rollW, 0.f});
}

// ─── Velocity strip (per-step volume bars) ─────────────────────────────────

void SoundEditor::drawVelocityStrip() {
    Sfx& s = m_sfx[m_selectedSfx];

    constexpr int   kPitches = 64;
    constexpr float kRowH    = 7.f;
    constexpr float kKeysW   = 38.f;
    constexpr float kStripH  = 70.f;
    (void)kPitches; (void)kRowH;   // (kept for layout symmetry)

    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float  rollW = std::max(200.f, avail.x - kKeysW);
    const float  colW  = rollW / kSteps;

    ImVec2 origin = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    // Sidebar label that lines up with the keyboard column above.
    dl->AddRectFilled({origin.x, origin.y},
                      {origin.x + kKeysW, origin.y + kStripH},
                      IM_COL32(28, 28, 36, 255));
    dl->AddText({origin.x + 4, origin.y + kStripH * 0.5f - 7},
                IM_COL32(180, 180, 190, 255), "VEL");

    const ImVec2 stripOrigin = {origin.x + kKeysW, origin.y};
    dl->AddRectFilled(stripOrigin,
                      {stripOrigin.x + rollW, stripOrigin.y + kStripH},
                      IM_COL32(18, 18, 24, 255));

    // Vertical step dividers, matching the roll above (every-4 thicker).
    for (int i = 0; i <= kSteps; ++i) {
        float x = stripOrigin.x + i * colW;
        ImU32 col = (i % 4 == 0) ? IM_COL32(70, 70, 85, 255)
                                 : IM_COL32(40, 40, 50, 255);
        dl->AddLine({x, stripOrigin.y},
                    {x, stripOrigin.y + kStripH}, col,
                    (i % 4 == 0) ? 1.5f : 1.f);
    }

    // Bars: height = volume / 7 of the strip.
    for (int i = 0; i < kSteps; ++i) {
        const Step& st = s.steps[i];
        if (st.volume == 0) continue;
        const float h  = (st.volume / 7.f) * (kStripH - 6.f);
        const float x0 = stripOrigin.x + i * colW + 2.f;
        const float x1 = stripOrigin.x + (i + 1) * colW - 2.f;
        const float y1 = stripOrigin.y + kStripH - 2.f;
        const float y0 = y1 - h;
        ImU32 col = waveColor(st.wave);
        dl->AddRectFilled({x0, y0}, {x1, y1}, col);
        if (i == m_selectedStep)
            dl->AddRect({x0 - 1, y0 - 1}, {x1 + 1, y1 + 1},
                        IM_COL32(255, 220, 60, 255), 0.f, 0, 2.f);
    }

    // Per-column hit areas: drag vertically to set this step's volume.
    for (int i = 0; i < kSteps; ++i) {
        const float x0 = stripOrigin.x + i * colW;
        ImGui::SetCursorScreenPos({x0, stripOrigin.y});
        char id[16];
        std::snprintf(id, sizeof(id), "##vel%d", i);
        ImGui::InvisibleButton(id, {colW, kStripH});

        if (ImGui::IsItemActive() && ImGui::IsMouseDown(0)) {
            m_selectedStep = i;
            ImVec2 mp = ImGui::GetIO().MousePos;
            float t  = 1.f - (mp.y - stripOrigin.y) / kStripH;
            int   v  = (int)std::round(std::clamp(t, 0.f, 1.f) * 7.f);
            Step& st = s.steps[i];
            if ((int)st.volume != v) { st.volume = (uint8_t)v; markDirty(); }
        }
    }

    ImGui::SetCursorScreenPos({origin.x, origin.y + kStripH + 4.f});
    ImGui::Dummy({kKeysW + rollW, 0.f});
}

// ─── Step inspector ────────────────────────────────────────────────────────

void SoundEditor::drawStepInspector() {
    if (m_selectedStep < 0) {
        ImGui::TextDisabled(
            "Click in the roll to place / move a note. Right-click to silence. "
            "Drag bars in the velocity strip to set per-step volume.");
        return;
    }
    Sfx&  s  = m_sfx[m_selectedSfx];
    Step& st = s.steps[m_selectedStep];

    ImGui::Text("Step %d", m_selectedStep);
    ImGui::SameLine();
    ImGui::TextDisabled("(%s, %s, vol %d, %s)",
                        pitchLabel(st.pitch).c_str(),
                        waveLabel(st.wave),
                        st.volume,
                        effectLabel(st.effect));

    int p = st.pitch;
    ImGui::SetNextItemWidth(140);
    if (ImGui::SliderInt("Pitch", &p, 0, 63, pitchLabel(p).c_str())) {
        st.pitch = (uint8_t)p; markDirty();
    }
    ImGui::SameLine();

    int w = st.wave;
    ImGui::SetNextItemWidth(110);
    if (ImGui::Combo("Wave", &w,
                     "Triangle\0Square\0Saw\0Pulse\0Noise\0\0")) {
        st.wave = (uint8_t)std::clamp(w, 0, (int)WaveCount - 1); markDirty();
    }
    ImGui::SameLine();

    int v = st.volume;
    ImGui::SetNextItemWidth(110);
    if (ImGui::SliderInt("Vol", &v, 0, 7)) { st.volume = (uint8_t)v; markDirty(); }
    ImGui::SameLine();

    int e = st.effect;
    ImGui::SetNextItemWidth(110);
    if (ImGui::Combo("Effect", &e,
                     "None\0Slide\0Vibrato\0Drop\0Fade-in\0Fade-out\0\0")) {
        st.effect = (uint8_t)std::clamp(e, 0, (int)EffectCount - 1); markDirty();
    }
}
