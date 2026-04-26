#pragma once

/*
 * ThermoConsole Editor
 * Main editor class — owns all panels and shared state.
 */

#include <SDL.h>
#include <SDL_image.h>
#include <imgui.h>
#include <imgui_impl_sdl2.h>
#include <imgui_impl_opengl3.h>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

namespace fs = std::filesystem;

// ─── Forward declarations (keep TU dependencies small) ──────────────────────

class CodeEditor;
class SpriteEditor;
class FileBrowser;
class GamePreview;
class Console;
class ManifestEditor;

// ─── Shared palette (PICO-8 compatible) ─────────────────────────────────────

struct PaletteColor {
    uint8_t     r, g, b;
    const char* name;
};

inline constexpr PaletteColor THERMO_PALETTE[16] = {
    {0x00, 0x00, 0x00, "Black"      },
    {0x1D, 0x2B, 0x53, "Dark Blue"  },
    {0x7E, 0x25, 0x53, "Dark Purple"},
    {0x00, 0x87, 0x51, "Dark Green" },
    {0xAB, 0x52, 0x36, "Brown"      },
    {0x5F, 0x57, 0x4F, "Dark Gray"  },
    {0xC2, 0xC3, 0xC7, "Light Gray" },
    {0xFF, 0xF1, 0xE8, "White"      },
    {0xFF, 0x00, 0x4D, "Red"        },
    {0xFF, 0xA3, 0x00, "Orange"     },
    {0xFF, 0xEC, 0x27, "Yellow"     },
    {0x00, 0xE4, 0x36, "Green"      },
    {0x29, 0xAD, 0xFF, "Blue"       },
    {0x83, 0x76, 0x9C, "Indigo"     },
    {0xFF, 0x77, 0xA8, "Pink"       },
    {0xFF, 0xCC, 0xAA, "Peach"      },
};

inline ImVec4 PaletteImColor(int idx) {
    idx = (idx < 0 || idx > 15) ? 0 : idx;
    const auto& c = THERMO_PALETTE[idx];
    return { c.r / 255.f, c.g / 255.f, c.b / 255.f, 1.f };
}

// ─── Project / ROM manifest ─────────────────────────────────────────────────

struct GameManifest {
    std::string name             = "my_game";
    std::string author           = "";
    std::string version          = "1.0.0";
    std::string entry            = "main.lua";
    int         display_width    = 480;
    int         display_height   = 640;
    std::string orientation      = "portrait";
    int         sprite_grid_size = 16;
    std::string sprites_file     = "sprites.png";
    std::string tiles_file       = "tiles.png";

    bool load(const fs::path& path);
    bool save(const fs::path& path) const;
};

// ─── ThermoEditor ───────────────────────────────────────────────────────────

class ThermoEditor {
public:
    ThermoEditor();
    ~ThermoEditor();

    ThermoEditor(const ThermoEditor&) = delete;
    ThermoEditor& operator=(const ThermoEditor&) = delete;

    // Lifecycle
    bool init();
    void run();
    void shutdown();

    // Project management
    bool                  openProject(const fs::path& path);
    void                  closeProject();
    bool                  hasProject() const { return !m_projectPath.empty(); }
    const fs::path&       projectPath() const { return m_projectPath; }
    const GameManifest&   manifest() const { return m_manifest; }
    GameManifest&         manifest()       { return m_manifest; }

    // Inter-panel messaging (safe to call from any thread — output is deferred
    // to the next frame via a thread-safe queue)
    void log(const std::string& msg, bool error = false);
    void openFile(const fs::path& path);
    void refreshFileTree();

    // Called by panels that mutate on-disk project state (code save, manifest
    // save). Triggers a live-reload in GamePreview if the game is running and
    // auto-reload is enabled.
    void notifySourceSaved();

    // Spawn the configured external editor (default: VS Code via `code`) on
    // a path. If line > 0 the editor is jumped to that line. Fire-and-forget;
    // logs an error if the spawn fails (typically: `code` not on PATH).
    void openInExternalEditor(const fs::path& path, int line = 0);

    // SDL accessors
    SDL_Window*   window()    { return m_window;    }
    SDL_GLContext glContext() { return m_glContext; }

    // Runtime binary path (auto-detected next to editor or user-configurable)
    std::string runtimeBinary;

    // Used by CodeEditor when trying to close a modified buffer
    void requestOpenDialog() { m_wantOpenDialog = true; }

private:
    // SDL / ImGui — using OpenGL backend so multi-viewport works.
    SDL_Window*   m_window    = nullptr;
    SDL_GLContext m_glContext = nullptr;
    bool          m_running   = false;

    // Project state
    fs::path     m_projectPath;
    GameManifest m_manifest;

    // Panels (owned)
    std::unique_ptr<CodeEditor>     m_codeEditor;
    std::unique_ptr<SpriteEditor>   m_spriteEditor;
    std::unique_ptr<FileBrowser>    m_fileBrowser;
    std::unique_ptr<GamePreview>    m_gamePreview;
    std::unique_ptr<Console>        m_console;
    std::unique_ptr<ManifestEditor> m_manifestEditor;

    // UI state
    bool m_showImGuiDemo  = false;
    bool m_showAbout      = false;
    bool m_wantOpenDialog = false;  // next frame, pop the Open-project modal
    char m_openProjectBuf[512] {};

    // Cached title-bar state — only re-set the SDL window title when the
    // (dirty?, project name) tuple changes, so we don't pay a system call each
    // frame.
    bool        m_titleLastDirty = false;
    std::string m_titleLastProj;

    // Internal
    void setupImGuiStyle();
    void drawMenuBar();
    void drawWelcomeScreen();
    void drawAboutModal();
    void drawOpenProjectModal();   // centralized so it works from menu, welcome, shortcut
    void handleShortcuts();

    // Docking — a DockSpace hosts every panel. The first time the editor runs
    // (or after "Reset Layout") we call buildDefaultDockLayout() to arrange
    // the panels in a sensible IDE layout. After that ImGui persists the
    // user's layout to imgui.ini.
    ImGuiID m_dockspaceId = 0;
    bool    m_buildDefaultLayout = true;  // true on first frame & after reset
    void    buildDefaultDockLayout(ImGuiID dockspaceId);

    // Refresh the SDL window title based on dirty state. No-op when nothing
    // changed since last frame.
    void    updateWindowTitle();
};
