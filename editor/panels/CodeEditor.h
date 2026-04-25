#pragma once

/*
 * ThermoConsole Editor
 * CodeEditor panel — multi-tab Lua editor.
 *
 * Design change from the original: the editor is now a single widget
 * (InputTextMultiline) rather than a broken highlighted-view-overlaid-on-edit
 * stack. A read-only "Preview" mode with syntax highlighting is still
 * available per-tab via a toolbar toggle. The plain edit mode still uses a
 * monospace-friendly layout and supports auto-growing the buffer.
 */

#include "../ThermoEditor.h"

#include <filesystem>
#include <string>
#include <vector>

class CodeEditor {
public:
    explicit CodeEditor(ThermoEditor* editor);

    // Lifecycle
    void onProjectOpened(const fs::path& projectPath);

    // Draw (called every frame)
    void draw();

    // API called by ThermoEditor
    void openFile(const fs::path& path);
    void saveCurrentFile();
    void saveAll();
    void closeAll();
    bool anyModified() const;

    // Editing actions (bound to Ctrl+Z / Ctrl+Y / Ctrl+F and Ctrl+Tab)
    void undo();
    void redo();
    void nextTab();
    void prevTab();
    void toggleFind();      // Ctrl+F

    void drawMenuItem();

private:
    enum class ViewMode { Edit, Preview };

    // Per-buffer find state. Kept here (not as ephemeral UI state) so switching
    // tabs preserves the query and match position.
    struct FindState {
        char                query[128] {};
        bool                active      = false;  // find bar visible
        bool                focusInput  = false;  // request focus next frame
        bool                caseSens    = false;
        std::vector<size_t> matches;              // byte offsets of each hit
        int                 current     = -1;     // index into matches
        // Cursor-move request applied via InputText callback
        bool                pendingSelect = false;
        size_t              pendingStart  = 0;
        size_t              pendingEnd    = 0;
    };

    struct FileBuffer {
        fs::path                   path;
        std::string                text;
        bool                       modified = false;
        ViewMode                   mode     = ViewMode::Edit;

        // Undo / redo — full-text snapshots. Capped at kMaxUndo to bound memory.
        std::vector<std::string>   undoStack;
        std::vector<std::string>   redoStack;
        double                     lastSnapshotTime = 0.0;  // ImGui::GetTime()

        FindState                  find;

        bool load();
        bool save();
    };

    ThermoEditor*            m_editor;
    std::vector<FileBuffer>  m_buffers;
    int                      m_activeTab    = -1;
    int                      m_pendingFocus = -1;
    bool                     m_visible      = true;

    // Close confirmation
    bool  m_wantClose      = false;
    int   m_closeIndex     = -1;  // index into m_buffers

    // Limits
    static constexpr size_t kMaxUndo    = 64;
    // Debounce: don't snapshot on every keystroke — group typing bursts.
    static constexpr double kSnapshotDebounceSec = 0.5;

    // Helpers
    void drawEditor(FileBuffer& buf);
    void drawFindBar(FileBuffer& buf);
    void drawEditPane(FileBuffer& buf);
    void drawPreviewPane(FileBuffer& buf);
    void drawCloseConfirm();

    void captureSnapshot(FileBuffer& buf, std::string preEdit);
    void rebuildFindMatches(FileBuffer& buf);    // rerun search across text
    void jumpToMatch(FileBuffer& buf, int which); // focus a specific match

    // Tokenizer (used only in preview mode)
    enum class TokenType {
        Default, Keyword, String, Number, Comment, Function, Operator, Builtin
    };
    template <class F>
    static void tokenizeLine(const std::string& line, F&& emit);
};
