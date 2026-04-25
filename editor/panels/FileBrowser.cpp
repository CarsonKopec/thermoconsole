/*
 * ThermoConsole Editor — FileBrowser panel implementation
 */

#include "FileBrowser.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <fstream>

FileBrowser::FileBrowser(ThermoEditor* editor) : m_editor(editor) {}

void FileBrowser::onProjectOpened(const fs::path& projectPath) {
    m_root = projectPath;
    refresh();
}

void FileBrowser::refresh() {
    m_rootNode.reset();
    std::error_code ec;
    if (!m_root.empty() && fs::is_directory(m_root, ec)) {
        m_rootNode = std::make_unique<Node>();
        m_rootNode->path  = m_root;
        m_rootNode->name  = m_root.filename().string();
        m_rootNode->isDir = true;
        m_rootNode->open  = true;
        buildChildren(*m_rootNode);
    }
}

void FileBrowser::buildChildren(Node& n) {
    n.children.clear();
    n.childrenBuilt = true;
    if (!n.isDir) return;

    std::error_code ec;
    std::vector<fs::directory_entry> entries;
    for (auto it = fs::directory_iterator(n.path, ec);
         it != fs::directory_iterator(); it.increment(ec))
    {
        if (ec) break;
        entries.push_back(*it);
    }

    // Directories first, then alphabetical
    std::sort(entries.begin(), entries.end(),
              [](const fs::directory_entry& a, const fs::directory_entry& b) {
                  std::error_code e1, e2;
                  bool ad = a.is_directory(e1);
                  bool bd = b.is_directory(e2);
                  if (ad != bd) return ad;  // true (dir) sorts before false
                  return a.path().filename().string() < b.path().filename().string();
              });

    for (auto& e : entries) {
        std::string name = e.path().filename().string();
        if (name.empty() || name[0] == '.') continue;
        if (name == "build" || name == "__pycache__" || name == "node_modules") continue;

        auto child = std::make_unique<Node>();
        child->path  = e.path();
        child->name  = std::move(name);
        std::error_code de;
        child->isDir = e.is_directory(de);
        n.children.push_back(std::move(child));
    }
}

ImVec4 FileBrowser::colorForExt(const std::string& ext) {
    if (ext == ".lua")                   return {0.45f, 0.85f, 0.45f, 1.f};
    if (ext == ".json")                  return {0.95f, 0.75f, 0.35f, 1.f};
    if (ext == ".png")                   return {0.65f, 0.45f, 0.95f, 1.f};
    if (ext == ".wav" || ext == ".ogg")  return {0.45f, 0.75f, 0.95f, 1.f};
    if (ext == ".md")                    return {0.75f, 0.75f, 0.75f, 1.f};
    return {0.85f, 0.85f, 0.85f, 1.f};
}

const char* FileBrowser::iconForNode(const Node& n) {
    if (n.isDir) return n.open ? "\xE2\x96\xBE " : "\xE2\x96\xB8 "; // ▾ / ▸
    const std::string ext = n.path.extension().string();
    if (ext == ".lua")                   return "\xE2\x9C\x8E ";    // ✎
    if (ext == ".png")                   return "# ";
    if (ext == ".json")                  return "{ ";
    if (ext == ".wav" || ext == ".ogg")  return "\xE2\x99\xAA ";    // ♪
    return "  ";
}

bool FileBrowser::matchesFilter(const Node& n, const std::string& filterLower) const {
    if (filterLower.empty()) return true;
    std::string lower = n.name;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c){ return std::tolower(c); });
    if (lower.find(filterLower) != std::string::npos) return true;
    // A directory matches if any descendant does (force it to remain visible)
    for (auto& c : n.children)
        if (matchesFilter(*c, filterLower)) return true;
    return false;
}

void FileBrowser::draw() {
    if (!m_visible) return;
    ImGui::Begin("Files", &m_visible);

    if (!m_rootNode) {
        ImGui::TextDisabled("No project open.");
        ImGui::End();
        return;
    }

    // Header: project name + refresh
    ImGui::TextColored({0.55f, 0.80f, 1.00f, 1.f}, "%s", m_rootNode->name.c_str());
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 60.f);
    if (ImGui::SmallButton("Refresh")) refresh();

    ImGui::Separator();

    // Filter
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##filter", "filter by name...",
                             m_filter, sizeof(m_filter));
    if (m_filter[0]) {
        if (ImGui::SmallButton("Clear filter")) m_filter[0] = '\0';
    }

    ImGui::Separator();

    std::string filterLower = m_filter;
    std::transform(filterLower.begin(), filterLower.end(), filterLower.begin(),
                   [](unsigned char c){ return std::tolower(c); });

    ImGui::BeginChild("##file_tree");
    // Render the root's children directly (hide the root itself — the header
    // already shows the project name).
    for (auto& c : m_rootNode->children)
        drawNode(*c, filterLower);
    ImGui::EndChild();

    // Background context menu (right-click empty area)
    if (ImGui::BeginPopupContextWindow("##fb_ctx",
            ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems))
    {
        if (ImGui::MenuItem("New File..."))    openNewFileDialog(m_root);
        if (ImGui::MenuItem("New Folder..."))  openNewDirDialog(m_root);
        if (ImGui::MenuItem("Refresh"))        refresh();
        ImGui::EndPopup();
    }

    drawNewFileDialog();
    drawNewDirDialog();
    drawRenameDialog();
    drawDeleteDialog();
    applyPendingOps();

    ImGui::End();
}

void FileBrowser::drawMenuItem() {
    ImGui::MenuItem("Files", nullptr, &m_visible);
}

void FileBrowser::drawNode(Node& n, const std::string& filterLower) {
    if (!matchesFilter(n, filterLower)) return;

    ImGui::PushStyleColor(ImGuiCol_Text,
        n.isDir ? ImVec4{0.78f, 0.88f, 1.00f, 1.f}
                : colorForExt(n.path.extension().string()));

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth |
                               ImGuiTreeNodeFlags_OpenOnArrow    |
                               ImGuiTreeNodeFlags_OpenOnDoubleClick;
    if (!n.isDir) flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    // Force-expand directories when a filter is active (so matches are visible)
    if (n.isDir && !filterLower.empty())
        ImGui::SetNextItemOpen(true, ImGuiCond_Always);

    const std::string label = std::string(iconForNode(n)) + n.name + "##" + n.path.string();
    bool opened = ImGui::TreeNodeEx(label.c_str(), flags);

    ImGui::PopStyleColor();

    // Track expansion state so the icon flips — ImGui tells us via IsItemToggledOpen()
    if (n.isDir && ImGui::IsItemToggledOpen()) n.open = !n.open;

    // Click (non-directory) to open in code editor
    if (!n.isDir && ImGui::IsItemClicked(ImGuiMouseButton_Left) &&
        !ImGui::IsItemToggledOpen())
    {
        m_editor->openFile(n.path);
    }

    // Right-click context menu
    if (ImGui::BeginPopupContextItem()) {
        if (!n.isDir) {
            if (ImGui::MenuItem("Open")) m_editor->openFile(n.path);
            ImGui::Separator();
            if (ImGui::MenuItem("Rename...")) {
                m_renameTarget = n.path;
                std::snprintf(m_renameBuf, sizeof(m_renameBuf),
                              "%s", n.name.c_str());
                m_renameOpen = true;
            }
            if (ImGui::MenuItem("Delete...")) {
                m_deleteTarget = n.path;
                m_deleteOpen = true;
            }
        } else {
            if (ImGui::MenuItem("New file here..."))   openNewFileDialog(n.path);
            if (ImGui::MenuItem("New folder here...")) openNewDirDialog(n.path);
            ImGui::Separator();
            if (ImGui::MenuItem("Rename folder...")) {
                m_renameTarget = n.path;
                std::snprintf(m_renameBuf, sizeof(m_renameBuf),
                              "%s", n.name.c_str());
                m_renameOpen = true;
            }
            if (ImGui::MenuItem("Delete folder...")) {
                m_deleteTarget = n.path;
                m_deleteOpen = true;
            }
        }
        ImGui::EndPopup();
    }

    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", n.path.string().c_str());

    if (opened && n.isDir) {
        // Lazily build children on first expansion
        if (!n.childrenBuilt) buildChildren(n);
        for (auto& c : n.children) drawNode(*c, filterLower);
        ImGui::TreePop();
    }
}

void FileBrowser::openNewFileDialog(const fs::path& baseDir) {
    m_newFileOpen = true;
    std::snprintf(m_newFilePath, sizeof(m_newFilePath),
                  "%s/", baseDir.string().c_str());
}

void FileBrowser::drawNewFileDialog() {
    if (m_newFileOpen) ImGui::OpenPopup("New File");

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, {0.5f, 0.5f});
    ImGui::SetNextWindowSize({520, 0}, ImGuiCond_Appearing);

    if (ImGui::BeginPopupModal("New File", &m_newFileOpen,
                               ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::TextUnformatted("Path (relative to project):");
        ImGui::SetNextItemWidth(-1);
        bool enter = ImGui::InputText("##nf", m_newFilePath, sizeof(m_newFilePath),
                                      ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::Spacing();

        bool create = enter || ImGui::Button("Create", {120, 0});
        ImGui::SameLine();
        if (ImGui::Button("Cancel", {120, 0})) {
            m_newFileOpen = false;
            ImGui::CloseCurrentPopup();
        }

        if (create && m_newFilePath[0] != '\0') {
            fs::path p(m_newFilePath);
            std::error_code ec;
            if (fs::exists(p, ec)) {
                m_editor->log("File already exists: " + p.string(), true);
            } else {
                if (p.has_parent_path()) fs::create_directories(p.parent_path(), ec);
                std::ofstream f(p);
                if (f) {
                    m_editor->log("Created: " + p.filename().string());
                    m_editor->openFile(p);
                    refresh();
                    m_newFileOpen = false;
                    ImGui::CloseCurrentPopup();
                } else {
                    m_editor->log("Failed to create: " + p.string(), true);
                }
            }
        }
        ImGui::EndPopup();
    }
}

void FileBrowser::openNewDirDialog(const fs::path& baseDir) {
    m_newDirOpen = true;
    std::snprintf(m_newDirPath, sizeof(m_newDirPath),
                  "%s/", baseDir.string().c_str());
}

void FileBrowser::drawNewDirDialog() {
    if (m_newDirOpen) ImGui::OpenPopup("New Folder");

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, {0.5f, 0.5f});
    ImGui::SetNextWindowSize({520, 0}, ImGuiCond_Appearing);

    if (ImGui::BeginPopupModal("New Folder", &m_newDirOpen,
                               ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::TextUnformatted("Folder path (relative to project):");
        ImGui::SetNextItemWidth(-1);
        bool enter = ImGui::InputText("##nd", m_newDirPath, sizeof(m_newDirPath),
                                      ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::Spacing();

        bool create = enter || ImGui::Button("Create", {120, 0});
        ImGui::SameLine();
        if (ImGui::Button("Cancel", {120, 0})) {
            m_newDirOpen = false;
            ImGui::CloseCurrentPopup();
        }

        if (create && m_newDirPath[0] != '\0') {
            fs::path p(m_newDirPath);
            std::error_code ec;
            if (fs::exists(p, ec)) {
                m_editor->log("Already exists: " + p.string(), true);
            } else if (fs::create_directories(p, ec)) {
                m_editor->log("Created folder: " + p.string());
                refresh();
                m_newDirOpen = false;
                ImGui::CloseCurrentPopup();
            } else {
                m_editor->log("Failed to create folder: " + ec.message(), true);
            }
        }
        ImGui::EndPopup();
    }
}

void FileBrowser::drawRenameDialog() {
    if (m_renameOpen) ImGui::OpenPopup("Rename");

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, {0.5f, 0.5f});

    if (ImGui::BeginPopupModal("Rename", &m_renameOpen,
                               ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::Text("Rename: %s", m_renameTarget.filename().string().c_str());
        ImGui::SetNextItemWidth(360);
        bool enter = ImGui::InputText("##rn", m_renameBuf, sizeof(m_renameBuf),
                                      ImGuiInputTextFlags_EnterReturnsTrue);

        bool ok = enter || ImGui::Button("Rename", {120, 0});
        ImGui::SameLine();
        if (ImGui::Button("Cancel", {120, 0})) {
            m_renameOpen = false;
            ImGui::CloseCurrentPopup();
        }
        if (ok && m_renameBuf[0] != '\0') {
            m_pending.push_back({OpKind::Rename, m_renameTarget,
                                 m_renameTarget.parent_path() / m_renameBuf});
            m_renameOpen = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void FileBrowser::drawDeleteDialog() {
    if (m_deleteOpen) ImGui::OpenPopup("Delete?");

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, {0.5f, 0.5f});

    if (ImGui::BeginPopupModal("Delete?", &m_deleteOpen,
                               ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::TextColored({1.f, 0.7f, 0.4f, 1.f},
                           "This cannot be undone.");
        ImGui::Spacing();
        ImGui::Text("Delete: %s", m_deleteTarget.string().c_str());
        ImGui::Spacing();

        if (ImGui::Button("Delete", {120, 0})) {
            m_pending.push_back({OpKind::Delete, m_deleteTarget, {}});
            m_deleteOpen = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", {120, 0})) {
            m_deleteOpen = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void FileBrowser::applyPendingOps() {
    if (m_pending.empty()) return;

    for (const auto& op : m_pending) {
        std::error_code ec;
        switch (op.kind) {
            case OpKind::Delete: {
                if (fs::is_directory(op.target, ec)) {
                    uintmax_t n = fs::remove_all(op.target, ec);
                    if (ec) m_editor->log("Delete failed: " + ec.message(), true);
                    else    m_editor->log("Deleted folder ("
                                          + std::to_string(n) + " items): "
                                          + op.target.string());
                } else {
                    if (!fs::remove(op.target, ec) || ec)
                        m_editor->log("Delete failed: " + ec.message(), true);
                    else
                        m_editor->log("Deleted: " + op.target.string());
                }
                break;
            }
            case OpKind::Rename: {
                fs::rename(op.target, op.newName, ec);
                if (ec) m_editor->log("Rename failed: " + ec.message(), true);
                else    m_editor->log("Renamed: "
                                      + op.target.filename().string() + " -> "
                                      + op.newName.filename().string());
                break;
            }
        }
    }
    m_pending.clear();
    refresh();
}
