/*
 * ThermoConsole Editor
 * ThermoEditor.cpp — main editor implementation
 */

#include "ThermoEditor.h"
#include "panels/CodeEditor.h"
#include "panels/Console.h"
#include "panels/FileBrowser.h"
#include "panels/GamePreview.h"
#include "panels/ManifestEditor.h"
#include "panels/SpriteEditor.h"

#include <imgui_internal.h>   // DockBuilder* — docking branch only

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <stdexcept>

// ─── JSON helpers (tiny, robust, exception-safe) ────────────────────────────
//
// Intentionally minimal — we only need flat string/int scalars from
// manifest.json. Anything richer would want nlohmann/json as a dep.

namespace {

size_t skipWs(const std::string& s, size_t p) {
    while (p < s.size() && std::isspace(static_cast<unsigned char>(s[p]))) ++p;
    return p;
}

// Locate the *value* for a top-level key. Returns npos if not found.
size_t findValue(const std::string& json, const std::string& key) {
    const std::string needle = "\"" + key + "\"";
    size_t pos = json.find(needle);
    if (pos == std::string::npos) return std::string::npos;
    pos = json.find(':', pos + needle.size());
    if (pos == std::string::npos) return std::string::npos;
    return skipWs(json, pos + 1);
}

std::string jsonGetStr(const std::string& json, const std::string& key,
                       const std::string& def = {})
{
    size_t p = findValue(json, key);
    if (p == std::string::npos || p >= json.size() || json[p] != '"') return def;
    size_t start = p + 1;
    // Find unescaped closing quote
    size_t end = start;
    while (end < json.size()) {
        if (json[end] == '\\' && end + 1 < json.size()) { end += 2; continue; }
        if (json[end] == '"') break;
        ++end;
    }
    if (end >= json.size()) return def;
    return json.substr(start, end - start);
}

int jsonGetInt(const std::string& json, const std::string& key, int def = 0) {
    size_t p = findValue(json, key);
    if (p == std::string::npos) return def;
    try {
        size_t consumed = 0;
        int v = std::stoi(json.substr(p), &consumed);
        return consumed > 0 ? v : def;
    } catch (const std::exception&) {
        return def;
    }
}

std::string jsonEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 4);
    for (char c : s) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '"':  out += "\\\""; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:   out += c;      break;
        }
    }
    return out;
}

} // anonymous namespace

// ─── GameManifest ───────────────────────────────────────────────────────────

bool GameManifest::load(const fs::path& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::string json((std::istreambuf_iterator<char>(f)),
                      std::istreambuf_iterator<char>());

    name         = jsonGetStr(json, "name",         name);
    author       = jsonGetStr(json, "author",       author);
    version      = jsonGetStr(json, "version",      version);
    entry        = jsonGetStr(json, "entry",        entry);
    orientation  = jsonGetStr(json, "orientation",  orientation);
    sprites_file = jsonGetStr(json, "sprites_file", sprites_file);
    tiles_file   = jsonGetStr(json, "tiles_file",   tiles_file);

    display_width    = std::clamp(jsonGetInt(json, "display_width",    display_width),    1, 4096);
    display_height   = std::clamp(jsonGetInt(json, "display_height",   display_height),   1, 4096);
    sprite_grid_size = std::clamp(jsonGetInt(json, "sprite_grid_size", sprite_grid_size), 1, 256);

    if (name.empty())    name    = "unknown";
    if (entry.empty())   entry   = "main.lua";
    if (version.empty()) version = "1.0.0";
    return true;
}

bool GameManifest::save(const fs::path& path) const {
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    f << "{\n"
      << "  \"name\": \""           << jsonEscape(name)         << "\",\n"
      << "  \"author\": \""         << jsonEscape(author)       << "\",\n"
      << "  \"version\": \""        << jsonEscape(version)      << "\",\n"
      << "  \"entry\": \""          << jsonEscape(entry)        << "\",\n"
      << "  \"display_width\": "    << display_width            << ",\n"
      << "  \"display_height\": "   << display_height           << ",\n"
      << "  \"orientation\": \""    << jsonEscape(orientation)  << "\",\n"
      << "  \"sprite_grid_size\": " << sprite_grid_size         << ",\n"
      << "  \"sprites_file\": \""   << jsonEscape(sprites_file) << "\",\n"
      << "  \"tiles_file\": \""     << jsonEscape(tiles_file)   << "\"\n"
      << "}\n";
    return static_cast<bool>(f);
}

// ─── ThermoEditor ───────────────────────────────────────────────────────────

ThermoEditor::ThermoEditor() {
#ifdef _WIN32
    runtimeBinary = "../runtime/build/thermoconsole.exe";
#else
    runtimeBinary = "../runtime/build/thermoconsole";
#endif
}

ThermoEditor::~ThermoEditor() {
    shutdown();
}

bool ThermoEditor::init() {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        std::fprintf(stderr, "SDL_Init error: %s\n", SDL_GetError());
        return false;
    }

    if (!(IMG_Init(IMG_INIT_PNG) & IMG_INIT_PNG)) {
        std::fprintf(stderr, "SDL_image init error: %s\n", IMG_GetError());
        SDL_Quit();
        return false;
    }

    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");

    m_window = SDL_CreateWindow(
        "ThermoConsole Editor",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        1400, 900,
        SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!m_window) {
        std::fprintf(stderr, "SDL_CreateWindow error: %s\n", SDL_GetError());
        IMG_Quit(); SDL_Quit();
        return false;
    }

    m_renderer = SDL_CreateRenderer(
        m_window, -1,
        SDL_RENDERER_PRESENTVSYNC | SDL_RENDERER_ACCELERATED);
    if (!m_renderer) {
        std::fprintf(stderr, "SDL_CreateRenderer error: %s\n", SDL_GetError());
        SDL_DestroyWindow(m_window); m_window = nullptr;
        IMG_Quit(); SDL_Quit();
        return false;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    // Nice-to-have docking behaviour:
    //   - tabs next to a docked window's title keep the window visible
    //   - auto-hide the tab bar when only one window is docked in a node
    io.ConfigDockingWithShift            = false;
    io.ConfigDockingAlwaysTabBar         = false;
    io.ConfigDockingTransparentPayload   = true;
    io.IniFilename = "imgui.ini";

    setupImGuiStyle();

    ImGui_ImplSDL2_InitForSDLRenderer(m_window, m_renderer);
    ImGui_ImplSDLRenderer2_Init(m_renderer);

    // Panels
    m_console        = std::make_unique<Console>(this);        // first: log() target
    m_codeEditor     = std::make_unique<CodeEditor>(this);
    m_spriteEditor   = std::make_unique<SpriteEditor>(this);
    m_fileBrowser    = std::make_unique<FileBrowser>(this);
    m_gamePreview    = std::make_unique<GamePreview>(this);
    m_manifestEditor = std::make_unique<ManifestEditor>(this);

    log("ThermoConsole Editor started. Open a game folder to begin.");
    m_running = true;
    return true;
}

void ThermoEditor::run() {
    while (m_running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT) m_running = false;
            if (event.type == SDL_WINDOWEVENT &&
                event.window.event == SDL_WINDOWEVENT_CLOSE &&
                event.window.windowID == SDL_GetWindowID(m_window))
                m_running = false;
        }

        ImGui_ImplSDLRenderer2_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        handleShortcuts();
        updateWindowTitle();

        // ── Full-window DockSpace host ──────────────────────────────────────
        // The host window fills the main viewport, hosts the menu bar, and
        // carries a DockSpace that every panel docks into. Users can:
        //   - drag a panel's tab to snap it next to another (split docking)
        //   - drag a panel out of the tab bar to tear it off into a floating
        //     window they can move freely ("pop out")
        //   - tab multiple panels together by dropping one on another
        //   - double-click a floating window's title to re-dock it
        ImGuiViewport* vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos (vp->WorkPos);
        ImGui::SetNextWindowSize(vp->WorkSize);
        ImGui::SetNextWindowViewport(vp->ID);

        const ImGuiWindowFlags host_flags =
            ImGuiWindowFlags_NoTitleBar            |
            ImGuiWindowFlags_NoCollapse            |
            ImGuiWindowFlags_NoResize              |
            ImGuiWindowFlags_NoMove                |
            ImGuiWindowFlags_NoDocking             |
            ImGuiWindowFlags_NoBringToFrontOnFocus |
            ImGuiWindowFlags_NoNavFocus            |
            ImGuiWindowFlags_MenuBar;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding,   0.f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,    {0.f, 0.f});
        ImGui::Begin("##DockHost", nullptr, host_flags);
        ImGui::PopStyleVar(3);

        drawMenuBar();

        m_dockspaceId = ImGui::GetID("MainDockspace");
        // Build the default layout *before* submitting the DockSpace so the
        // dockspace's first render already reflects it. Otherwise panels
        // would flicker through a one-frame "unrooted" state.
        if (m_buildDefaultLayout) {
            buildDefaultDockLayout(m_dockspaceId);
            m_buildDefaultLayout = false;
        }
        ImGui::DockSpace(m_dockspaceId, {0.f, 0.f},
                         ImGuiDockNodeFlags_PassthruCentralNode);

        // Open-project modal is owned by the host window so the popup has a
        // consistent parent regardless of which path requested it.
        drawOpenProjectModal();

        ImGui::End();

        if (hasProject()) {
            m_fileBrowser->draw();
            m_codeEditor->draw();
            m_spriteEditor->draw();
            m_gamePreview->draw();
            m_console->draw();
            m_manifestEditor->draw();
        } else {
            drawWelcomeScreen();
            // Console is still useful in welcome mode so we can surface errors
            m_console->draw();
        }

        if (m_showAbout)     drawAboutModal();
        if (m_showImGuiDemo) ImGui::ShowDemoWindow(&m_showImGuiDemo);

        // Render
        ImGui::Render();
        SDL_SetRenderDrawColor(m_renderer, 18, 18, 24, 255);
        SDL_RenderClear(m_renderer);
        ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), m_renderer);
        SDL_RenderPresent(m_renderer);
    }
}

void ThermoEditor::shutdown() {
    // Tear down panels first — GamePreview needs to join its reader thread
    // before SDL is gone.
    m_manifestEditor.reset();
    m_gamePreview.reset();
    m_fileBrowser.reset();
    m_spriteEditor.reset();
    m_codeEditor.reset();
    m_console.reset();

    if (m_renderer) {
        ImGui_ImplSDLRenderer2_Shutdown();
        ImGui_ImplSDL2_Shutdown();
        ImGui::DestroyContext();
        SDL_DestroyRenderer(m_renderer);
        m_renderer = nullptr;
    }
    if (m_window) {
        SDL_DestroyWindow(m_window);
        m_window = nullptr;
    }
    IMG_Quit();
    SDL_Quit();
}

// ─── Project management ─────────────────────────────────────────────────────

bool ThermoEditor::openProject(const fs::path& path) {
    std::error_code ec;
    if (path.empty() || !fs::is_directory(path, ec)) {
        log("Error: not a directory: " + path.string(), true);
        return false;
    }

    fs::path manifest_path = path / "manifest.json";
    if (!fs::exists(manifest_path, ec)) {
        log("Warning: no manifest.json found — using defaults.");
        m_manifest = {};
        m_manifest.name = path.filename().string();
    } else {
        GameManifest loaded;
        if (!loaded.load(manifest_path)) {
            log("Error: failed to read manifest.json", true);
            return false;
        }
        m_manifest = std::move(loaded);
    }

    m_projectPath = path;

    // Notify panels
    m_fileBrowser->onProjectOpened(path);
    m_codeEditor->onProjectOpened(path);
    m_spriteEditor->onProjectOpened(path);
    m_gamePreview->onProjectOpened(path);
    m_manifestEditor->onProjectOpened(path);

    // Auto-open the entry file
    fs::path entry = path / m_manifest.entry;
    if (fs::exists(entry, ec)) openFile(entry);

    log("Opened project: " + m_manifest.name + "  (" + path.string() + ")");
    return true;
}

void ThermoEditor::closeProject() {
    if (m_gamePreview) m_gamePreview->stopGame();
    m_projectPath.clear();
    if (m_codeEditor) m_codeEditor->closeAll();
    log("Project closed.");
}

// ─── Inter-panel messaging ──────────────────────────────────────────────────

void ThermoEditor::log(const std::string& msg, bool error) {
    if (m_console) m_console->addLine(msg, error);
    else           std::fprintf(error ? stderr : stdout,
                                "[editor] %s\n", msg.c_str());
}

void ThermoEditor::openFile(const fs::path& path) {
    if (m_codeEditor) m_codeEditor->openFile(path);
}

void ThermoEditor::refreshFileTree() {
    if (m_fileBrowser) m_fileBrowser->refresh();
}

void ThermoEditor::notifySourceSaved() {
    if (m_gamePreview) m_gamePreview->onSourceSaved();
}

// ─── Menu bar ───────────────────────────────────────────────────────────────

void ThermoEditor::drawMenuBar() {
    if (!ImGui::BeginMenuBar()) return;

    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("Open Project...", "Ctrl+O")) m_wantOpenDialog = true;
        if (ImGui::MenuItem("Close Project", nullptr, false, hasProject())) closeProject();
        ImGui::Separator();
        if (ImGui::MenuItem("Save File", "Ctrl+S", false, hasProject())) {
            if (m_codeEditor) m_codeEditor->saveCurrentFile();
        }
        if (ImGui::MenuItem("Save All", "Ctrl+Shift+S", false, hasProject())) {
            if (m_codeEditor) m_codeEditor->saveAll();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Quit", "Ctrl+Q")) m_running = false;
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("View")) {
        if (m_codeEditor)     m_codeEditor->drawMenuItem();
        if (m_spriteEditor)   m_spriteEditor->drawMenuItem();
        if (m_fileBrowser)    m_fileBrowser->drawMenuItem();
        if (m_gamePreview)    m_gamePreview->drawMenuItem();
        if (m_console)        m_console->drawMenuItem();
        if (m_manifestEditor) m_manifestEditor->drawMenuItem();
        ImGui::Separator();
        if (ImGui::MenuItem("Reset Layout")) {
            m_buildDefaultLayout = true;
            log("Dock layout reset to default.");
        }
        ImGui::MenuItem("ImGui Demo", nullptr, &m_showImGuiDemo);
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Run")) {
        bool canRun = hasProject();
        if (ImGui::MenuItem("Run Game", "F5", false, canRun))
            if (m_gamePreview) m_gamePreview->launchGame();
        if (ImGui::MenuItem("Stop Game", "F6", false, canRun))
            if (m_gamePreview) m_gamePreview->stopGame();
        ImGui::Separator();
        if (ImGui::MenuItem("Pack ROM (.tcr)", nullptr, false, canRun))
            if (m_gamePreview) m_gamePreview->packRom();
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Help")) {
        if (ImGui::MenuItem("About")) m_showAbout = true;
        ImGui::EndMenu();
    }

    // Project name + Run button in the menu bar
    if (hasProject()) {
        ImGui::Separator();
        ImGui::TextDisabled("%s", m_manifest.name.c_str());
        ImGui::Separator();

        ImGui::PushStyleColor(ImGuiCol_Button,        {0.20f, 0.70f, 0.20f, 1.f});
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, {0.30f, 0.85f, 0.30f, 1.f});
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  {0.15f, 0.55f, 0.15f, 1.f});
        if (ImGui::SmallButton("  \xE2\x96\xB6  Run  "))  // ▶
            if (m_gamePreview) m_gamePreview->launchGame();
        ImGui::PopStyleColor(3);
    }

    ImGui::EndMenuBar();
}

// ─── Open-project modal (single source of truth) ───────────────────────────

void ThermoEditor::drawOpenProjectModal() {
    if (m_wantOpenDialog) {
        ImGui::OpenPopup("Open Project");
        m_wantOpenDialog = false;
    }

    // Center the modal
    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, {0.5f, 0.5f});
    ImGui::SetNextWindowSize({520, 0}, ImGuiCond_Appearing);

    if (ImGui::BeginPopupModal("Open Project", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::TextUnformatted("Game folder path:");
        ImGui::SetNextItemWidth(-1);
        bool enter = ImGui::InputText("##proj_path",
                                      m_openProjectBuf, sizeof(m_openProjectBuf),
                                      ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::Spacing();

        bool ok = enter || ImGui::Button("Open", {120, 0});
        ImGui::SameLine();
        if (ImGui::Button("Cancel", {120, 0})) {
            m_openProjectBuf[0] = '\0';
            ImGui::CloseCurrentPopup();
        }

        if (ok && m_openProjectBuf[0] != '\0') {
            bool ok2 = openProject(fs::path(m_openProjectBuf));
            if (ok2) {
                m_openProjectBuf[0] = '\0';
                ImGui::CloseCurrentPopup();
            }
            // If it failed, keep the dialog open; log() surfaced the error.
        }
        ImGui::EndPopup();
    }
}

// ─── Welcome screen ─────────────────────────────────────────────────────────

void ThermoEditor::drawWelcomeScreen() {
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);

    ImGui::Begin("##Welcome", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove     |
        ImGuiWindowFlags_NoResize   | ImGuiWindowFlags_NoDocking  |
        ImGuiWindowFlags_NoBringToFrontOnFocus);

    const ImVec2 center = { vp->WorkSize.x * 0.5f, vp->WorkSize.y * 0.45f };

    const char* title = "ThermoConsole Editor";
    ImGui::SetWindowFontScale(2.0f);
    ImVec2 ts = ImGui::CalcTextSize(title);
    ImGui::SetCursorPos({center.x - ts.x * 0.5f, center.y - 110.f});
    ImGui::TextUnformatted(title);
    ImGui::SetWindowFontScale(1.0f);

    // Palette swatch row (16 × 18px = 288 wide)
    const float sw = 18.f;
    ImGui::SetCursorPos({ center.x - 16 * sw * 0.5f, center.y - 50.f });
    for (int i = 0; i < 16; i++) {
        if (i > 0) ImGui::SameLine(0, 0);
        ImVec4 col = PaletteImColor(i);
        ImGui::PushStyleColor(ImGuiCol_Button, col);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, col);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  col);
        char id[8]; std::snprintf(id, sizeof(id), "##sw%d", i);
        ImGui::Button(id, {sw, sw});
        ImGui::PopStyleColor(3);
    }

    ImGui::SetCursorPos({ center.x - 110.f, center.y });
    ImGui::PushStyleColor(ImGuiCol_Button,        {0.20f, 0.50f, 0.90f, 1.f});
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, {0.30f, 0.60f, 1.00f, 1.f});
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  {0.15f, 0.40f, 0.80f, 1.f});
    if (ImGui::Button("Open Game Folder...", {220, 40})) m_wantOpenDialog = true;
    ImGui::PopStyleColor(3);

    // Quick tip
    const char* tip = "File > Open Project    or    Ctrl+O";
    ts = ImGui::CalcTextSize(tip);
    ImGui::SetCursorPos({ center.x - ts.x * 0.5f, center.y + 55.f });
    ImGui::TextDisabled("%s", tip);

    ImGui::End();
}

// ─── About modal ────────────────────────────────────────────────────────────

void ThermoEditor::drawAboutModal() {
    ImGui::OpenPopup("About ThermoConsole Editor");

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, {0.5f, 0.5f});

    if (ImGui::BeginPopupModal("About ThermoConsole Editor",
                               &m_showAbout, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::Text("ThermoConsole Editor  v1.1.0");
        ImGui::Separator();
        ImGui::TextWrapped("A game development tool for the ThermoConsole handheld.");
        ImGui::Spacing();
        ImGui::TextDisabled("Built with Dear ImGui + SDL2");
        ImGui::Spacing();
        if (ImGui::Button("Close", {120, 0})) {
            m_showAbout = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

// ─── Keyboard shortcuts ─────────────────────────────────────────────────────
//
// The old implementation early-returned on io.WantCaptureKeyboard, which was
// always true when any ImGui window had focus — so the shortcuts basically
// never fired. We now use io.WantTextInput (only true when a text widget is
// editing), which lets Ctrl+S/Ctrl+O/F5/F6 work as users expect without
// swallowing keystrokes while they're typing code.

void ThermoEditor::handleShortcuts() {
    ImGuiIO& io = ImGui::GetIO();

    // Don't intercept keystrokes while the user is typing into a text field —
    // except for shortcuts that SHOULD work during editing (save, find,
    // undo/redo, tab nav, run/stop).
    const bool textInput = io.WantTextInput;
    const bool ctrl  = io.KeyCtrl;
    const bool shift = io.KeyShift;

    if (!textInput) {
        if (ctrl && ImGui::IsKeyPressed(ImGuiKey_O, false))
            m_wantOpenDialog = true;
        if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Q, false))
            m_running = false;
    }

    // Ctrl+S works even while the code editor has focus — saving should not
    // be swallowed by the text widget.
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_S, false)) {
        if (m_codeEditor) {
            if (shift) m_codeEditor->saveAll();
            else       m_codeEditor->saveCurrentFile();
        }
    }

    // Ctrl+F — toggle find bar in the active code buffer (works while typing)
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_F, false)) {
        if (m_codeEditor) m_codeEditor->toggleFind();
    }

    // Ctrl+Z / Ctrl+Y / Ctrl+Shift+Z — undo/redo on the active code buffer.
    // InputTextMultiline has its own intra-widget undo; our stack augments it
    // with whole-buffer restore points per edit-burst.
    if (ctrl && !shift && ImGui::IsKeyPressed(ImGuiKey_Z, false)) {
        if (m_codeEditor) m_codeEditor->undo();
    }
    if (ctrl && (ImGui::IsKeyPressed(ImGuiKey_Y, false) ||
                 (shift && ImGui::IsKeyPressed(ImGuiKey_Z, false)))) {
        if (m_codeEditor) m_codeEditor->redo();
    }

    // Ctrl+Tab / Ctrl+Shift+Tab — cycle open code tabs
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Tab, false)) {
        if (m_codeEditor) {
            if (shift) m_codeEditor->prevTab();
            else       m_codeEditor->nextTab();
        }
    }

    // F5/F6 are always live
    if (ImGui::IsKeyPressed(ImGuiKey_F5, false) && m_gamePreview) m_gamePreview->launchGame();
    if (ImGui::IsKeyPressed(ImGuiKey_F6, false) && m_gamePreview) m_gamePreview->stopGame();
}

// ─── Window title (reflects dirty state) ────────────────────────────────────

void ThermoEditor::updateWindowTitle() {
    if (!m_window) return;
    const bool dirty = m_codeEditor && m_codeEditor->anyModified();
    const std::string proj = hasProject() ? m_manifest.name : std::string{};

    if (dirty == m_titleLastDirty && proj == m_titleLastProj) return;
    m_titleLastDirty = dirty;
    m_titleLastProj  = proj;

    std::string title = "ThermoConsole Editor";
    if (!proj.empty()) title += " — " + proj;
    if (dirty)         title += "  \xE2\x80\xA2";   // • bullet indicates unsaved
    SDL_SetWindowTitle(m_window, title.c_str());
}

// ─── Default dock layout ────────────────────────────────────────────────────
//
// Called on the first frame (or after "Reset Layout") to arrange the panels
// in an IDE-style layout:
//
//   +--------+-----------------------+----------------+
//   |        |                       |                |
//   | Files  |     Code Editor       | Game Preview   |
//   |        |                       | / Manifest     |
//   |        |                       | (tabbed)       |
//   |        +-----------------------+----------------+
//   |        |        Console        | Sprite Editor  |
//   +--------+-----------------------+----------------+
//
// After this first pass ImGui persists the user's layout to imgui.ini, so
// their customizations (splits, pop-outs, tab groupings) stick across runs.

void ThermoEditor::buildDefaultDockLayout(ImGuiID dockspaceId) {
    ImGui::DockBuilderRemoveNode(dockspaceId);
    ImGui::DockBuilderAddNode(dockspaceId,
        ImGuiDockNodeFlags_DockSpace | ImGuiDockNodeFlags_PassthruCentralNode);
    ImGui::DockBuilderSetNodeSize(dockspaceId,
        ImGui::GetMainViewport()->WorkSize);

    ImGuiID dock_main  = dockspaceId;
    ImGuiID dock_left  = ImGui::DockBuilderSplitNode(dock_main,  ImGuiDir_Left,  0.17f, nullptr, &dock_main);
    ImGuiID dock_right = ImGui::DockBuilderSplitNode(dock_main,  ImGuiDir_Right, 0.28f, nullptr, &dock_main);
    ImGuiID dock_btm   = ImGui::DockBuilderSplitNode(dock_main,  ImGuiDir_Down,  0.25f, nullptr, &dock_main);
    ImGuiID dock_rbtm  = ImGui::DockBuilderSplitNode(dock_right, ImGuiDir_Down,  0.50f, nullptr, &dock_right);

    ImGui::DockBuilderDockWindow("Files",         dock_left);
    ImGui::DockBuilderDockWindow("Code Editor",   dock_main);
    ImGui::DockBuilderDockWindow("Console",       dock_btm);
    ImGui::DockBuilderDockWindow("Game Preview",  dock_right);
    ImGui::DockBuilderDockWindow("Manifest",      dock_right);   // tabbed with Game Preview
    ImGui::DockBuilderDockWindow("Sprite Editor", dock_rbtm);

    ImGui::DockBuilderFinish(dockspaceId);
}

// ─── ImGui style ────────────────────────────────────────────────────────────

void ThermoEditor::setupImGuiStyle() {
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding    = 4.f;
    s.FrameRounding     = 3.f;
    s.ScrollbarRounding = 3.f;
    s.GrabRounding      = 3.f;
    s.TabRounding       = 3.f;
    s.FramePadding      = {6.f, 4.f};
    s.ItemSpacing       = {8.f, 5.f};
    s.WindowBorderSize  = 1.f;
    s.FrameBorderSize   = 0.f;

    ImVec4* c = s.Colors;
    c[ImGuiCol_WindowBg]           = {0.10f, 0.10f, 0.14f, 1.f};
    c[ImGuiCol_ChildBg]            = {0.08f, 0.08f, 0.11f, 1.f};
    c[ImGuiCol_PopupBg]            = {0.12f, 0.12f, 0.17f, 1.f};
    c[ImGuiCol_Border]             = {0.25f, 0.25f, 0.35f, 1.f};
    c[ImGuiCol_FrameBg]            = {0.16f, 0.16f, 0.22f, 1.f};
    c[ImGuiCol_FrameBgHovered]     = {0.22f, 0.22f, 0.30f, 1.f};
    c[ImGuiCol_FrameBgActive]      = {0.26f, 0.26f, 0.36f, 1.f};
    c[ImGuiCol_TitleBg]            = {0.08f, 0.08f, 0.12f, 1.f};
    c[ImGuiCol_TitleBgActive]      = {0.12f, 0.12f, 0.20f, 1.f};
    c[ImGuiCol_MenuBarBg]          = {0.08f, 0.08f, 0.12f, 1.f};
    c[ImGuiCol_ScrollbarBg]        = {0.06f, 0.06f, 0.09f, 1.f};
    c[ImGuiCol_ScrollbarGrab]      = {0.30f, 0.30f, 0.45f, 1.f};
    c[ImGuiCol_CheckMark]          = {0.40f, 0.80f, 0.40f, 1.f};
    c[ImGuiCol_SliderGrab]         = {0.35f, 0.55f, 0.90f, 1.f};
    c[ImGuiCol_Button]             = {0.20f, 0.35f, 0.65f, 1.f};
    c[ImGuiCol_ButtonHovered]      = {0.28f, 0.45f, 0.80f, 1.f};
    c[ImGuiCol_ButtonActive]       = {0.18f, 0.28f, 0.55f, 1.f};
    c[ImGuiCol_Header]             = {0.20f, 0.35f, 0.60f, 0.8f};
    c[ImGuiCol_HeaderHovered]      = {0.28f, 0.45f, 0.75f, 1.f};
    c[ImGuiCol_HeaderActive]       = {0.18f, 0.28f, 0.55f, 1.f};
    c[ImGuiCol_Tab]                = {0.14f, 0.14f, 0.22f, 1.f};
    c[ImGuiCol_TabHovered]         = {0.25f, 0.40f, 0.70f, 1.f};
    c[ImGuiCol_TabActive]          = {0.20f, 0.35f, 0.65f, 1.f};
    c[ImGuiCol_TabUnfocusedActive] = {0.16f, 0.28f, 0.52f, 1.f};
    c[ImGuiCol_Text]               = {0.90f, 0.90f, 0.95f, 1.f};
    c[ImGuiCol_TextDisabled]       = {0.45f, 0.45f, 0.55f, 1.f};
    c[ImGuiCol_Separator]          = {0.25f, 0.25f, 0.35f, 1.f};
    c[ImGuiCol_ResizeGrip]         = {0.20f, 0.35f, 0.65f, 0.5f};
    c[ImGuiCol_ResizeGripHovered]  = {0.28f, 0.45f, 0.80f, 0.8f};
    c[ImGuiCol_ResizeGripActive]   = {0.35f, 0.55f, 0.90f, 1.0f};

    // Docking-branch colors — the drop-zone preview shown while dragging a
    // panel, and the empty region of the dockspace. These enumerators only
    // exist on the docking branch (which setup.sh pins).
    c[ImGuiCol_DockingPreview]     = {0.35f, 0.55f, 0.90f, 0.7f};
    c[ImGuiCol_DockingEmptyBg]     = {0.08f, 0.08f, 0.11f, 1.f};
}
