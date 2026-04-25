/*
 * ThermoConsole Editor — GamePreview panel implementation
 */

#include "GamePreview.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#ifndef _WIN32
    #include <unistd.h>
    #include <sys/wait.h>
    #include <signal.h>
    #include <fcntl.h>
    #include <errno.h>
    #include <poll.h>
#endif

GamePreview::GamePreview(ThermoEditor* editor) : m_editor(editor) {}

GamePreview::~GamePreview() {
    stopGame();       // issues shutdown signal, terminates the process
    joinAndReset();   // blocks on the reader thread
}

void GamePreview::onProjectOpened(const fs::path& projectPath) {
    stopGame();
    joinAndReset();
    m_projectPath = projectPath;
    {
        std::lock_guard<std::mutex> lock(m_outputMutex);
        m_pendingOutput.clear();
        m_recentOutput.clear();
    }
}

// ─── Drawing ────────────────────────────────────────────────────────────────

void GamePreview::draw() {
    if (!m_visible) return;
    ImGui::Begin("Game Preview", &m_visible);

    const bool running = m_running.load();

    // ── Toolbar ─────────────────────────────────────────────────────────────
    if (!running) {
        ImGui::PushStyleColor(ImGuiCol_Button,        {0.15f, 0.65f, 0.15f, 1.f});
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, {0.25f, 0.80f, 0.25f, 1.f});
        if (ImGui::Button("  \xE2\x96\xB6  Run Game  ")) launchGame();
        ImGui::PopStyleColor(2);
    } else {
        ImGui::PushStyleColor(ImGuiCol_Button,        {0.75f, 0.15f, 0.15f, 1.f});
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, {0.90f, 0.25f, 0.25f, 1.f});
        if (ImGui::Button("  \xE2\x96\xA0  Stop  ")) stopGame();
        ImGui::PopStyleColor(2);

        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button,        {0.25f, 0.45f, 0.85f, 1.f});
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, {0.35f, 0.60f, 1.00f, 1.f});
        if (ImGui::Button("  \xE2\x86\xBB  Reload  ")) reload();   // ↻
        ImGui::PopStyleColor(2);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Stop and relaunch with the current sources");
    }

    ImGui::SameLine(0, 16);
    ImGui::Checkbox("Auto-reload on save", &m_autoReloadOnSave);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Ctrl+S in the code editor relaunches the game");

    ImGui::SameLine(0, 16);

    const bool packing = m_packFuture.valid() &&
        m_packFuture.wait_for(std::chrono::seconds(0)) != std::future_status::ready;

    ImGui::BeginDisabled(packing);
    if (ImGui::Button(packing ? "Packing..." : "Pack ROM (.tcr)")) packRom();
    ImGui::EndDisabled();

    // Runtime binary path (editable)
    ImGui::SameLine(0, 20);
    ImGui::TextDisabled("Runtime:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(250);
    char buf[512];
    std::snprintf(buf, sizeof(buf), "%s", m_editor->runtimeBinary.c_str());
    if (ImGui::InputText("##rtbin", buf, sizeof(buf)))
        m_editor->runtimeBinary = buf;

    ImGui::Separator();

    // ── Display area ────────────────────────────────────────────────────────
    ImVec2 avail = ImGui::GetContentRegionAvail();
    ImGui::BeginChild("##preview_area", {avail.x, avail.y}, true);
    if (running) drawRunningDisplay();
    else         drawIdleDisplay();
    ImGui::EndChild();

    ImGui::End();

    // Drain async channels — must happen every frame, even if window was
    // closed, because the reader thread is still pushing output.
    drainOutput();
    pollPack();

    // Auto-reset state if the reader thread has stopped — whether because
    // the child exited naturally or because stopGame() was called.
    if (!m_running.load() && m_readerThread.joinable()) {
        joinAndReset();
    }
}

void GamePreview::drawMenuItem() {
    ImGui::MenuItem("Game Preview", nullptr, &m_visible);
}

void GamePreview::drawRunningDisplay() {
    ImVec2 avail = ImGui::GetContentRegionAvail();
    const float t = static_cast<float>(SDL_GetTicks()) / 500.f;
    const float pulse = 0.5f + 0.5f * std::sin(t * 3.14159f);

    // Heartbeat: how long since the runtime last emitted output. A silent
    // game looks indistinguishable from a hung one without this hint.
    const uint64_t lastTick = m_lastOutputTick.load(std::memory_order_relaxed);
    const uint64_t nowTick  = SDL_GetTicks64();
    const float quietSec    = lastTick == 0
        ? -1.f
        : (float)(nowTick - lastTick) / 1000.f;

    // Color the dot: green = recent (<2s), amber = quiet (2–10s), red = stale.
    ImVec4 dotColor = {0.2f + pulse * 0.5f, 0.8f, 0.2f, 1.f};
    const char* statusSuffix = "";
    if (quietSec >= 0.f && quietSec > 10.f) {
        dotColor = {0.95f, 0.30f, 0.30f, 1.f};
        statusSuffix = "  (no output)";
    } else if (quietSec >= 0.f && quietSec > 2.f) {
        dotColor = {0.95f, 0.75f, 0.20f, 1.f};
    }

    const char* msg = "\xE2\x97\x8F Game Running...";   // ● prefix
    ImVec2 ts = ImGui::CalcTextSize(msg);
    ImGui::SetCursorPosY(avail.y * 0.4f);
    ImGui::SetCursorPosX((avail.x - ts.x) * 0.5f);
    ImGui::TextColored(dotColor, "%s%s", msg, statusSuffix);

    // Sub-line: timestamp of last output (or "waiting for first output...")
    char hb[64];
    if (quietSec < 0.f)        std::snprintf(hb, sizeof(hb), "waiting for first output...");
    else if (quietSec < 1.f)   std::snprintf(hb, sizeof(hb), "last output: just now");
    else if (quietSec < 60.f)  std::snprintf(hb, sizeof(hb), "last output: %.1fs ago", quietSec);
    else                       std::snprintf(hb, sizeof(hb), "last output: %.0fs ago", quietSec);
    ImVec2 hts = ImGui::CalcTextSize(hb);
    ImGui::SetCursorPosX((avail.x - hts.x) * 0.5f);
    ImGui::TextDisabled("%s", hb);

    // Show last few lines of output
    ImGui::SetCursorPosY(avail.y * 0.55f);
    std::lock_guard<std::mutex> lock(m_outputMutex);
    for (const auto& line : m_recentOutput)
        ImGui::TextDisabled("  %s", line.c_str());
}

void GamePreview::drawIdleDisplay() {
    ImVec2 avail = ImGui::GetContentRegionAvail();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 orig = ImGui::GetCursorScreenPos();

    // Stylised 480×640 screen
    const float aspect = 480.f / 640.f;
    const float maxH   = std::max(10.f, avail.y - 40.f);
    const float maxW   = std::max(10.f, avail.x - 20.f);
    float h = std::min(maxH, maxW / aspect);
    float w = h * aspect;
    const float ox = orig.x + (avail.x - w) * 0.5f;
    const float oy = orig.y + 10.f;

    dl->AddRectFilled({ox, oy}, {ox + w, oy + h}, IM_COL32(0, 0, 0, 255), 6.f);
    dl->AddRect      ({ox, oy}, {ox + w, oy + h}, IM_COL32(60, 60, 80, 255), 6.f, 0, 2.f);

    // Palette stripe on the left edge
    const float stripe_h = h / 16.f;
    for (int i = 0; i < 16; ++i) {
        const auto& c = THERMO_PALETTE[i];
        dl->AddRectFilled({ox,            oy + i * stripe_h},
                          {ox + w * 0.08f, oy + (i + 1) * stripe_h},
                          IM_COL32(c.r, c.g, c.b, 255));
    }

    // Center prompt
    const char* prompt = "Press  \xE2\x96\xB6  Run Game";
    ImVec2 ts = ImGui::CalcTextSize(prompt);
    dl->AddText({ox + w * 0.5f - ts.x * 0.5f + 20.f,
                 oy + h * 0.5f - ts.y * 0.5f},
                IM_COL32(200, 200, 210, 200), prompt);

    // Resolution label
    char res[32];
    std::snprintf(res, sizeof(res), "480 x 640");
    ImVec2 rs = ImGui::CalcTextSize(res);
    dl->AddText({ox + w - rs.x - 6.f, oy + h - rs.y - 4.f},
                IM_COL32(80, 80, 100, 200), res);

    ImGui::Dummy({avail.x, h + 20.f});
}

// ─── Launch / stop ──────────────────────────────────────────────────────────

void GamePreview::launchGame() {
    if (m_running.load()) return;

    // If a previous run already exited, clean up before starting a new one
    if (m_readerThread.joinable()) joinAndReset();

    std::error_code ec;
    if (m_editor->runtimeBinary.empty() ||
        !fs::exists(m_editor->runtimeBinary, ec))
    {
        m_editor->log("Runtime not found: " + m_editor->runtimeBinary
                      + "  (set the path in the Game Preview toolbar)", true);
        return;
    }
    if (m_projectPath.empty()) {
        m_editor->log("No project open.", true);
        return;
    }

    m_lastOutputTick.store(0, std::memory_order_relaxed);
    startProcess(m_editor->runtimeBinary, m_projectPath.string());
}

void GamePreview::stopGame() {
    std::lock_guard<std::mutex> lock(m_procMutex);
    if (m_running.load()) {
        m_shutdown.store(true);
        killProcessLocked();
    }
}

void GamePreview::reload() {
    if (!m_running.load()) return;          // nothing to restart
    m_editor->log("Reloading game...");
    stopGame();
    joinAndReset();
    launchGame();
}

void GamePreview::onSourceSaved() {
    if (m_autoReloadOnSave && m_running.load()) reload();
}

void GamePreview::joinAndReset() {
    if (m_readerThread.joinable()) m_readerThread.join();
    m_shutdown.store(false);
    m_running.store(false);

    std::lock_guard<std::mutex> lock(m_procMutex);
#ifdef _WIN32
    if (m_hStdout)  { CloseHandle(m_hStdout);  m_hStdout  = nullptr; }
    if (m_hProcess) { CloseHandle(m_hProcess); m_hProcess = nullptr; }
#else
    if (m_pipe >= 0) { close(m_pipe); m_pipe = -1; }
    if (m_pid  >  0) {
        int status;
        waitpid(m_pid, &status, WNOHANG);
        m_pid = -1;
    }
#endif
}

// ─── POSIX process management ───────────────────────────────────────────────

#ifndef _WIN32

void GamePreview::startProcess(const std::string& binary, const std::string& arg) {
    int pipefd[2];
    if (pipe(pipefd) < 0) {
        m_editor->log(std::string("pipe() failed: ") + std::strerror(errno), true);
        return;
    }

    pid_t pid = fork();
    if (pid < 0) {
        m_editor->log(std::string("fork() failed: ") + std::strerror(errno), true);
        close(pipefd[0]); close(pipefd[1]);
        return;
    }

    if (pid == 0) {
        // Child: redirect stdout/stderr -> write end, keep it open
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[0]);
        close(pipefd[1]);
        execl(binary.c_str(), binary.c_str(), arg.c_str(), (char*)nullptr);
        // exec failed
        std::fprintf(stderr, "exec failed: %s\n", std::strerror(errno));
        _exit(127);
    }

    // Parent
    close(pipefd[1]);

    // Set read end non-blocking so the reader thread can honor m_shutdown
    int flags = fcntl(pipefd[0], F_GETFL, 0);
    if (flags >= 0) fcntl(pipefd[0], F_SETFL, flags | O_NONBLOCK);

    {
        std::lock_guard<std::mutex> lock(m_procMutex);
        m_pid  = pid;
        m_pipe = pipefd[0];
    }
    m_running.store(true);
    m_shutdown.store(false);

    m_editor->log("Launched: " + binary + " " + arg);

    m_readerThread = std::thread(&GamePreview::readerLoopPosix, this);
}

void GamePreview::readerLoopPosix() {
    int pipefd;
    {
        std::lock_guard<std::mutex> lock(m_procMutex);
        pipefd = m_pipe;
    }
    if (pipefd < 0) { m_running.store(false); return; }

    std::array<char, 512> buf{};
    while (!m_shutdown.load()) {
        pollfd pfd{ pipefd, POLLIN, 0 };
        int rv = poll(&pfd, 1, 50);   // 50 ms
        if (rv > 0 && (pfd.revents & POLLIN)) {
            ssize_t n = read(pipefd, buf.data(), buf.size());
            if (n > 0) {
                appendOutput(buf.data(), (size_t)n);
            } else if (n == 0) {
                break;   // EOF — child closed stdout
            } else if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
                break;
            }
        } else if (rv < 0 && errno != EINTR) {
            break;
        }

        // Check whether the child exited even if no new output is buffered
        pid_t pid;
        {
            std::lock_guard<std::mutex> lock(m_procMutex);
            pid = m_pid;
        }
        if (pid > 0) {
            int status;
            pid_t r = waitpid(pid, &status, WNOHANG);
            if (r == pid) break;
        }
    }
    m_running.store(false);
}

void GamePreview::killProcessLocked() {
    if (m_pid > 0) {
        ::kill(m_pid, SIGTERM);
        // Give it a moment, then SIGKILL if still alive
        for (int i = 0; i < 10; ++i) {
            int status;
            pid_t r = waitpid(m_pid, &status, WNOHANG);
            if (r == m_pid) { m_pid = -1; break; }
            SDL_Delay(20);
        }
        if (m_pid > 0) {
            ::kill(m_pid, SIGKILL);
            int status;
            waitpid(m_pid, &status, 0);
            m_pid = -1;
        }
    }
    if (m_pipe >= 0) { close(m_pipe); m_pipe = -1; }
    m_running.store(false);
    m_editor->log("Game stopped.");
}

#else   // ─── Windows process management ────────────────────────────────────

void GamePreview::startProcess(const std::string& binary, const std::string& arg) {
    SECURITY_ATTRIBUTES sa{ sizeof(sa), nullptr, TRUE };
    HANDLE hRead = nullptr, hWrite = nullptr;
    if (!CreatePipe(&hRead, &hWrite, &sa, 0)) {
        m_editor->log("CreatePipe failed", true);
        return;
    }
    SetHandleInformation(hRead, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA si{};
    si.cb         = sizeof(si);
    si.hStdOutput = hWrite;
    si.hStdError  = hWrite;
    si.dwFlags    = STARTF_USESTDHANDLES;

    PROCESS_INFORMATION pi{};
    std::string cmd = "\"" + binary + "\" \"" + arg + "\"";
    if (!CreateProcessA(nullptr, cmd.data(), nullptr, nullptr, TRUE,
                        CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
    {
        DWORD err = GetLastError();
        m_editor->log("CreateProcess failed (" + std::to_string(err) + ")", true);
        CloseHandle(hRead); CloseHandle(hWrite);
        return;
    }
    CloseHandle(hWrite);

    {
        std::lock_guard<std::mutex> lock(m_procMutex);
        m_hProcess = pi.hProcess;
        m_hStdout  = hRead;
    }
    CloseHandle(pi.hThread);

    m_running.store(true);
    m_shutdown.store(false);

    m_editor->log("Launched: " + cmd);
    m_readerThread = std::thread(&GamePreview::readerLoopWin, this);
}

void GamePreview::readerLoopWin() {
    HANDLE h;
    {
        std::lock_guard<std::mutex> lock(m_procMutex);
        h = m_hStdout;
    }
    if (!h) { m_running.store(false); return; }

    std::array<char, 512> buf{};
    DWORD nRead = 0;
    while (!m_shutdown.load()) {
        // Peek so we don't block indefinitely when shutdown is requested
        DWORD avail = 0;
        if (!PeekNamedPipe(h, nullptr, 0, nullptr, &avail, nullptr)) break;
        if (avail == 0) {
            Sleep(30);
            // Child may have exited even when no output is buffered
            HANDLE proc;
            {
                std::lock_guard<std::mutex> lock(m_procMutex);
                proc = m_hProcess;
            }
            if (proc && WaitForSingleObject(proc, 0) == WAIT_OBJECT_0) break;
            continue;
        }
        DWORD toRead = std::min<DWORD>(avail, (DWORD)buf.size());
        if (!ReadFile(h, buf.data(), toRead, &nRead, nullptr) || nRead == 0) break;
        appendOutput(buf.data(), (size_t)nRead);
    }
    m_running.store(false);
}

void GamePreview::killProcessLocked() {
    if (m_hProcess) {
        TerminateProcess(m_hProcess, 0);
        WaitForSingleObject(m_hProcess, 500);
    }
    m_running.store(false);
    m_editor->log("Game stopped.");
}

#endif

// ─── Output plumbing ────────────────────────────────────────────────────────

void GamePreview::appendOutput(const char* text, size_t n) {
    if (n == 0) return;
    std::string chunk(text, n);
    std::lock_guard<std::mutex> lock(m_outputMutex);
    size_t pos = 0;
    while (pos < chunk.size()) {
        size_t nl = chunk.find('\n', pos);
        if (nl == std::string::npos) nl = chunk.size();
        std::string line = chunk.substr(pos, nl - pos);
        // Trim trailing \r (redundant with LF on Windows pipes)
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!line.empty()) {
            m_pendingOutput.push_back(line);
            m_recentOutput.push_back(line);
            while (m_recentOutput.size() > 6) m_recentOutput.pop_front();
        }
        pos = nl + 1;
    }
    m_lastOutputTick.store(SDL_GetTicks64(), std::memory_order_relaxed);
}

void GamePreview::drainOutput() {
    std::deque<std::string> local;
    {
        std::lock_guard<std::mutex> lock(m_outputMutex);
        local.swap(m_pendingOutput);
    }
    while (!local.empty()) {
        m_editor->log("[game] " + local.front());
        local.pop_front();
    }
}

// ─── ROM packing (async — doesn't freeze the UI) ────────────────────────────

void GamePreview::packRom() {
    if (m_projectPath.empty()) {
        m_editor->log("No project open.", true);
        return;
    }
    if (m_packFuture.valid() &&
        m_packFuture.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
    {
        m_editor->log("Pack already in progress.", true);
        return;
    }

    fs::path sdk = m_projectPath.parent_path().parent_path() / "sdk" / "pack_rom.py";
    std::error_code ec;
    if (!fs::exists(sdk, ec)) {
        m_editor->log("pack_rom.py not found at: " + sdk.string(), true);
        return;
    }

    std::string cmd = "python3 \"" + sdk.string() + "\" \"" + m_projectPath.string() + "\"";
    m_editor->log("Packing ROM: " + cmd);

    m_packFuture = std::async(std::launch::async,
                              [cmd]() { return std::system(cmd.c_str()); });
}

void GamePreview::pollPack() {
    if (!m_packFuture.valid()) return;
    if (m_packFuture.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
        return;

    int ret = 0;
    try { ret = m_packFuture.get(); }
    catch (const std::exception& e) {
        m_editor->log(std::string("Pack failed (exception): ") + e.what(), true);
        return;
    }
    if (ret == 0) m_editor->log("ROM packed successfully.");
    else          m_editor->log("ROM pack failed (exit "
                                + std::to_string(ret) + ")", true);
}
