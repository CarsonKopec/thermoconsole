#pragma once

/*
 * ThermoConsole Editor
 * GamePreview panel — spawns the thermoconsole runtime as a subprocess,
 * captures stdout/stderr on a reader thread, streams it into the Console.
 *
 * The reader thread is joinable and owned by this object — not detached.
 * Shutdown order: set m_shutdown, close pipe/terminate process, join thread,
 * then release OS handles. This fixes the UAF/race seen in the original.
 */

#include "../ThermoEditor.h"

#include <atomic>
#include <deque>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#ifdef _WIN32
    #define WIN32_LEAN_AND_MEAN
    #define NOMINMAX
    #include <windows.h>
#else
    #include <sys/types.h>
#endif

class GamePreview {
public:
    explicit GamePreview(ThermoEditor* editor);
    ~GamePreview();

    GamePreview(const GamePreview&) = delete;
    GamePreview& operator=(const GamePreview&) = delete;

    void onProjectOpened(const fs::path& projectPath);
    void draw();
    void drawMenuItem();

    // Public API
    void launchGame();
    void stopGame();
    void packRom();   // async; polled in draw()
    void reload();    // stop + relaunch if currently running (no-op otherwise)

    // Called by other panels after a save event — relaunches iff auto-reload
    // is enabled and the game is currently running.
    void onSourceSaved();

    bool isRunning() const { return m_running.load(); }

private:
    ThermoEditor*  m_editor;
    fs::path       m_projectPath;
    bool           m_visible         = true;
    bool           m_autoReloadOnSave = true;   // live-reload on Ctrl+S

    // Process + reader thread lifecycle
    std::atomic<bool>  m_running{false};
    std::atomic<bool>  m_shutdown{false};  // tells reader thread to bail
    std::thread        m_readerThread;
    std::mutex         m_procMutex;        // protects pid/handles

#ifdef _WIN32
    HANDLE m_hProcess = nullptr;
    HANDLE m_hStdout  = nullptr;
#else
    pid_t  m_pid  = -1;
    int    m_pipe = -1;
#endif

    // Output, shared between reader thread and UI thread
    std::mutex                m_outputMutex;
    std::deque<std::string>   m_pendingOutput;   // drained each frame -> console
    std::deque<std::string>   m_recentOutput;    // shown in preview box (last N)

    // Last time the game emitted any output, in SDL ticks (ms since boot).
    // Atomic so the UI thread can read without taking m_outputMutex every frame.
    // 0 = never; reset on each launch.
    std::atomic<uint64_t>     m_lastOutputTick{0};

    // Async ROM packing
    std::future<int>   m_packFuture;

    // Helpers
    void drawIdleDisplay();
    void drawRunningDisplay();
    void drainOutput();
    void pollPack();

    void startProcess(const std::string& binary, const std::string& arg);
    void joinAndReset();        // synchronizes state after shutdown
    void appendOutput(const char* text, size_t n);

#ifdef _WIN32
    void readerLoopWin();
    void killProcessLocked();   // caller holds m_procMutex
#else
    void readerLoopPosix();
    void killProcessLocked();
#endif
};
