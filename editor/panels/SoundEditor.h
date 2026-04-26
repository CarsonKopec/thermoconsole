#pragma once

/*
 * ThermoConsole Editor
 * SoundEditor panel — PICO-8-style chiptune SFX editor.
 *
 * Composes 32 SFX slots, each a 32-step pattern of (pitch, waveform,
 * volume, effect). Synthesizes preview audio via SDL_OpenAudioDevice
 * (callback runs on a separate thread — m_playMutex guards shared state).
 *
 * Persists to <project>/<manifest.sounds_file>  (default sounds.json).
 *
 * Runtime integration is intentionally *not* in this panel — the runtime
 * still loads .wav/.ogg via SDL_mixer; the next round wires sounds.json
 * into the runtime so Lua's play_sfx() can synthesize at playback time.
 */

#include "../ThermoEditor.h"

#include <SDL_audio.h>

#include <array>
#include <cstdint>
#include <mutex>
#include <string>

class SoundEditor {
public:
    explicit SoundEditor(ThermoEditor* editor);
    ~SoundEditor();

    SoundEditor(const SoundEditor&) = delete;
    SoundEditor& operator=(const SoundEditor&) = delete;

    void onProjectOpened(const fs::path& projectPath);
    void onProjectClosed();
    void draw();
    void drawMenuItem();

    static constexpr int kSfxCount = 32;
    static constexpr int kSteps    = 32;

    // Waveform indices match what we serialize to JSON. Only the first 5 are
    // implemented in the synthesizer for MVP; the others are reserved.
    enum Wave : uint8_t {
        WaveTriangle = 0,
        WaveSquare   = 1,
        WaveSaw      = 2,
        WavePulse    = 3,   // 25 % duty cycle
        WaveNoise    = 4,
        WaveCount
    };

    // Effect indices, also serialized. Skipping arpeggios for MVP.
    enum Effect : uint8_t {
        EffectNone    = 0,
        EffectSlide   = 1,   // pitch interp from previous step
        EffectVibrato = 2,   // ~6 Hz pitch modulation
        EffectDrop    = 3,   // pitch drops linearly across the step
        EffectFadeIn  = 4,   // volume ramps 0 → set
        EffectFadeOut = 5,   // volume ramps set → 0
        EffectCount
    };

    struct Step {
        uint8_t pitch  = 24;          // 0..63 (PICO-8 numbering, 33 = A4)
        uint8_t wave   = WaveTriangle;
        uint8_t volume = 0;           // 0..7;  0 = silent step
        uint8_t effect = EffectNone;
    };

    struct Sfx {
        std::string                  name;
        uint8_t                      speed      = 4;   // 1..31, ticks per step
        uint8_t                      loop_start = 0;
        uint8_t                      loop_end   = 0;   // 0 ⇒ no loop
        std::array<Step, kSteps>     steps      {};
    };

private:
    ThermoEditor*              m_editor;
    fs::path                   m_projectPath;
    bool                       m_visible = true;
    bool                       m_dirty   = false;

    std::array<Sfx, kSfxCount> m_sfx {};
    int                        m_selectedSfx  = 0;
    int                        m_selectedStep = -1;   // -1 = no inspector

    // ── Audio (SDL_AudioDeviceID; callback runs on its own thread) ─────────
    SDL_AudioDeviceID          m_audioDev   = 0;
    int                        m_sampleRate = 22050;

    // Shared with the audio callback; m_playMutex guards every field below.
    std::mutex                 m_playMutex;
    bool                       m_playing      = false;
    int                        m_playSfxId    = 0;
    int                        m_playStepIdx  = 0;
    double                     m_playStepFrac = 0.0;   // [0, 1) within step
    double                     m_oscPhase     = 0.0;   // [0, 1) carrier phase
    uint32_t                   m_noiseState   = 0xCAFEBABEu;

    // ── Audio plumbing ─────────────────────────────────────────────────────
    void initAudio();
    void shutdownAudio();
    static void SDLCALL audioCallback(void* user, Uint8* stream, int len);
    void fillAudioBuffer(int16_t* out, int sampleCount);

    // ── Playback control ───────────────────────────────────────────────────
    void play(int sfxId);
    void stop();

    // ── Edit operations ────────────────────────────────────────────────────
    void clearSfx(int slot);
    void clearAllSfx();
    void markDirty();   // also pings the manifest's notify-source path

    // ── IO ─────────────────────────────────────────────────────────────────
    fs::path soundsPath() const;
    void load();
    void save();
    void serialize(std::string& out) const;
    bool deserialize(const std::string& text);

    // ── UI sub-sections ────────────────────────────────────────────────────
    void drawToolbar();
    void drawSfxList();
    void drawGrid();
    void drawStepInspector();
};
