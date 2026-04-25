#pragma once

/*
 * ThermoConsole Editor
 * ManifestEditor panel — edit manifest.json fields in-place.
 */

#include "../ThermoEditor.h"

class ManifestEditor {
public:
    explicit ManifestEditor(ThermoEditor* editor);

    void onProjectOpened(const fs::path& projectPath);
    void draw();
    void drawMenuItem();

private:
    ThermoEditor* m_editor;
    fs::path      m_projectPath;
    bool          m_visible = true;

    // Editable char buffers (guaranteed null-terminated after every sync)
    char m_name    [64]  {};
    char m_author  [64]  {};
    char m_version [16]  {};
    char m_entry   [64]  {};
    char m_orient  [16]  {};
    char m_sprites [64]  {};
    char m_tiles   [64]  {};
    int  m_dispW   = 480;
    int  m_dispH   = 640;
    int  m_gridSz  = 16;

    void syncFromManifest();
    void syncToManifest();

    // External-file watcher: poll manifest.json's mtime each frame; if it
    // changed under us (e.g. another editor, git pull), prompt the user to
    // reload — or auto-reload silently when there are no local edits.
    fs::file_time_type m_lastDiskMtime {};
    bool               m_reloadPromptOpen = false;

    void captureDiskMtime();          // refresh m_lastDiskMtime from disk
    bool buffersDirty() const;        // any local edit not yet saved?
    void pollManifestOnDisk();        // call once per frame
    void drawReloadPrompt();
};
