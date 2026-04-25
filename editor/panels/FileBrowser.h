#pragma once

/*
 * ThermoConsole Editor
 * FileBrowser panel — recursive collapsible project tree.
 */

#include "../ThermoEditor.h"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

class FileBrowser {
public:
    explicit FileBrowser(ThermoEditor* editor);

    void onProjectOpened(const fs::path& projectPath);
    void refresh();
    void draw();
    void drawMenuItem();

private:
    // Real tree so directories can collapse. Children are built lazily on
    // first expansion so huge projects don't thrash fs on every refresh.
    struct Node {
        fs::path                            path;
        std::string                         name;      // cached filename
        bool                                isDir = false;
        bool                                childrenBuilt = false;
        bool                                open = false;   // expansion state
        std::vector<std::unique_ptr<Node>>  children;
    };

    // Operations queued during iteration and executed afterwards so we never
    // invalidate iterators while walking the tree.
    enum class OpKind { Delete, Rename };
    struct PendingOp {
        OpKind   kind;
        fs::path target;
        fs::path newName;   // for rename
    };

    ThermoEditor*           m_editor;
    fs::path                m_root;
    std::unique_ptr<Node>   m_rootNode;
    bool                    m_visible = true;
    char                    m_filter[128] {};

    // Dialogs
    bool m_newFileOpen = false;
    char m_newFilePath[256] {};

    bool m_newDirOpen  = false;
    char m_newDirPath[256] {};

    bool       m_renameOpen = false;
    fs::path   m_renameTarget;
    char       m_renameBuf[128] {};

    bool       m_deleteOpen = false;
    fs::path   m_deleteTarget;

    std::vector<PendingOp> m_pending;

    // Helpers
    void buildChildren(Node& n);
    void drawNode(Node& n, const std::string& filterLower);
    bool matchesFilter(const Node& n, const std::string& filterLower) const;
    void openNewFileDialog(const fs::path& baseDir);
    void drawNewFileDialog();
    void openNewDirDialog(const fs::path& baseDir);
    void drawNewDirDialog();
    void drawRenameDialog();
    void drawDeleteDialog();
    void applyPendingOps();

    static ImVec4 colorForExt(const std::string& ext);
    static const char* iconForNode(const Node& n);
};
