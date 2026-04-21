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

    void drawMenuItem();

private:
    enum class ViewMode { Edit, Preview };

    struct FileBuffer {
        fs::path           path;
        std::string        text;
        bool               modified = false;
        ViewMode           mode     = ViewMode::Edit;

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

    // Helpers
    void drawEditor(FileBuffer& buf);
    void drawEditPane(FileBuffer& buf);
    void drawPreviewPane(const FileBuffer& buf);
    void drawCloseConfirm();

    // Tokenizer (used only in preview mode)
    enum class TokenType {
        Default, Keyword, String, Number, Comment, Function, Operator, Builtin
    };
    template <class F>
    static void tokenizeLine(const std::string& line, F&& emit);
};
