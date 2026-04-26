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

// Pretty note name for inspector display. PICO-8 pitch 0 → C0.
const char* noteNames[] = {"C", "C#", "D", "D#", "E", "F",
                           "F#", "G", "G#", "A", "A#", "B"};
std::string pitchLabel(int p) {
    if (p < 0 || p > 63) return "?";
    int oct = p / 12;
    int n   = p % 12;
    char buf[8];
    std::snprintf(buf, sizeof(buf), "%s%d", noteNames[n], oct);
    return buf;
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
    load();   // no-op + log if file missing — that's fine, blank slate
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

        // 0.6 amplitude headroom keeps two layered waves from clipping.
        int v = (int)(sample * 0.6 * 32767.0);
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
    o << "{\n  \"version\": 1,\n  \"sfx\": [\n";
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
            if (key == "sfx") {
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
    ImGui::SetNextWindowSize({900, 520}, ImGuiCond_FirstUseEver);
    ImGui::Begin("Sound Editor", &m_visible);

    drawToolbar();
    ImGui::Separator();

    // Two-column body: sfx list on the left, grid + inspector on the right.
    const float listW = 140.f;
    if (ImGui::BeginChild("##sfxlist", {listW, 0}, true)) {
        drawSfxList();
    }
    ImGui::EndChild();
    ImGui::SameLine();
    if (ImGui::BeginChild("##gridarea", {0, 0}, false)) {
        drawGrid();
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

// ─── Step grid ─────────────────────────────────────────────────────────────

void SoundEditor::drawGrid() {
    Sfx& s = m_sfx[m_selectedSfx];

    const ImVec2 avail   = ImGui::GetContentRegionAvail();
    const float  colW    = std::max(14.f, std::min(28.f, (avail.x - 12.f) / kSteps));
    const float  pitchH  = 200.f;
    const float  rowH    = 16.f;
    const float  totalH  = pitchH + rowH * 3 + 4.f;

    ImVec2 origin = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    // Background
    dl->AddRectFilled(origin,
                      {origin.x + colW * kSteps, origin.y + totalH},
                      IM_COL32(20, 20, 28, 255));

    // ── Pitch bars + waveform / volume / effect rows ─────────────────────
    int playingStep = -1;
    {
        std::lock_guard<std::mutex> lock(m_playMutex);
        if (m_playing && m_playSfxId == m_selectedSfx)
            playingStep = m_playStepIdx;
    }

    for (int i = 0; i < kSteps; ++i) {
        const Step& st = s.steps[i];
        float x0 = origin.x + i * colW;
        float x1 = x0 + colW - 1.f;

        // Selection / playhead highlights
        if (i == m_selectedStep)
            dl->AddRectFilled({x0, origin.y}, {x1, origin.y + totalH},
                              IM_COL32(60, 80, 130, 80));
        if (i == playingStep)
            dl->AddRectFilled({x0, origin.y}, {x1, origin.y + totalH},
                              IM_COL32(80, 200, 100, 60));

        // Loop range shading (uses end as exclusive: [start, end))
        if (s.loop_end > s.loop_start && i >= s.loop_start && i < s.loop_end)
            dl->AddRectFilled({x0, origin.y + pitchH - 2.f},
                              {x1, origin.y + pitchH      },
                              IM_COL32(200, 180, 60, 140));

        // Pitch bar (silent steps render as a faint dash so the user sees them).
        float pitchY = origin.y + (1.f - st.pitch / 63.f) * (pitchH - 4.f) + 2.f;
        if (st.volume > 0) {
            ImU32 col = waveColor(st.wave);
            dl->AddRectFilled({x0 + 2, pitchY},
                              {x1 - 2, origin.y + pitchH - 2}, col);
        } else {
            dl->AddLine({x0 + 4, origin.y + pitchH - 6},
                        {x1 - 4, origin.y + pitchH - 6},
                        IM_COL32(80, 80, 90, 255), 1.f);
        }

        // Row 1 — waveform color square
        float r1y = origin.y + pitchH + 1.f;
        dl->AddRectFilled({x0 + 2, r1y}, {x1 - 2, r1y + rowH - 2.f},
                          waveColor(st.wave));

        // Row 2 — volume bar (height-coded 0..7)
        float r2y = r1y + rowH;
        float vh  = (st.volume / 7.f) * (rowH - 4.f);
        dl->AddRectFilled({x0 + 2, r2y + (rowH - 2.f) - vh},
                          {x1 - 2, r2y + (rowH - 2.f)},
                          IM_COL32(220, 220, 220, 255));

        // Row 3 — effect glyph (single letter)
        float r3y = r2y + rowH;
        const char* glyph = "";
        switch (st.effect) {
            case EffectSlide:   glyph = "S"; break;
            case EffectVibrato: glyph = "V"; break;
            case EffectDrop:    glyph = "D"; break;
            case EffectFadeIn:  glyph = "<"; break;
            case EffectFadeOut: glyph = ">"; break;
            default: break;
        }
        if (*glyph)
            dl->AddText({x0 + colW * 0.5f - 4, r3y + 1},
                        IM_COL32(220, 200, 100, 255), glyph);
    }

    // Click-to-select (pitch area also sets pitch by vertical position).
    ImGui::InvisibleButton("##gridhit", {colW * kSteps, totalH});
    if (ImGui::IsItemActive() && ImGui::IsMouseDown(0)) {
        ImVec2 mp = ImGui::GetIO().MousePos;
        int col = (int)((mp.x - origin.x) / colW);
        if (col >= 0 && col < kSteps) {
            m_selectedStep = col;
            // If the click is in the pitch area, set the pitch too — quick
            // sketching workflow.
            if (mp.y >= origin.y && mp.y <= origin.y + pitchH) {
                float t = 1.f - (mp.y - origin.y) / pitchH;
                int p = (int)std::round(std::clamp(t, 0.f, 1.f) * 63.f);
                Step& st = s.steps[col];
                if (st.pitch != (uint8_t)p) {
                    st.pitch = (uint8_t)p;
                    if (st.volume == 0) st.volume = 5;   // make it audible
                    markDirty();
                }
            }
        }
    }
}

// ─── Step inspector ────────────────────────────────────────────────────────

void SoundEditor::drawStepInspector() {
    if (m_selectedStep < 0) {
        ImGui::TextDisabled("Click a step in the grid to edit. Drag inside the "
                            "pitch area to sketch a melody.");
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
