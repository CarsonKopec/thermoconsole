/*
 * ThermoConsole Editor — ManifestEditor panel implementation
 */

#include "ManifestEditor.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>

namespace {

// snprintf-based string copy that always null-terminates and truncates cleanly.
// strncpy doesn't guarantee null termination — this one does.
void copyTo(char* dst, size_t cap, const std::string& src) {
    if (cap == 0) return;
    std::snprintf(dst, cap, "%s", src.c_str());
}

} // namespace

ManifestEditor::ManifestEditor(ThermoEditor* editor) : m_editor(editor) {}

void ManifestEditor::onProjectOpened(const fs::path& projectPath) {
    m_projectPath = projectPath;
    syncFromManifest();
    captureDiskMtime();
}

void ManifestEditor::draw() {
    if (!m_visible) return;
    pollManifestOnDisk();
    ImGui::Begin("Manifest", &m_visible);
    ImGui::TextColored({0.6f, 0.8f, 1.f, 1.f}, "manifest.json");
    ImGui::Separator();

    const float labelW = 120.f;

    auto field = [&](const char* label, char* buf, size_t sz) {
        ImGui::Text("%s", label);
        ImGui::SameLine(labelW);
        ImGui::SetNextItemWidth(-1);
        std::string id = std::string("##mf_") + label;
        return ImGui::InputText(id.c_str(), buf, sz);
    };
    auto ifield = [&](const char* label, int* v, int lo, int hi) {
        ImGui::Text("%s", label);
        ImGui::SameLine(labelW);
        ImGui::SetNextItemWidth(-1);
        std::string id = std::string("##mi_") + label;
        bool ch = ImGui::InputInt(id.c_str(), v, 0);
        *v = std::clamp(*v, lo, hi);
        return ch;
    };

    bool changed = false;
    changed |= field ("Name",          m_name,    sizeof(m_name));
    changed |= field ("Author",        m_author,  sizeof(m_author));
    changed |= field ("Version",       m_version, sizeof(m_version));
    changed |= field ("Entry",         m_entry,   sizeof(m_entry));

    ImGui::Separator();
    changed |= ifield("Width",         &m_dispW, 1, 4096);
    changed |= ifield("Height",        &m_dispH, 1, 4096);

    // Orientation — dropdown rather than free text
    ImGui::Text("Orientation");
    ImGui::SameLine(labelW);
    ImGui::SetNextItemWidth(-1);
    const char* orients[] = { "portrait", "landscape" };
    int currOrient = (std::strncmp(m_orient, "landscape", 9) == 0) ? 1 : 0;
    if (ImGui::Combo("##mo_orient", &currOrient, orients, 2)) {
        copyTo(m_orient, sizeof(m_orient), orients[currOrient]);
        changed = true;
    }

    ImGui::Separator();
    changed |= ifield("Sprite Grid",   &m_gridSz, 1, 256);
    changed |= field ("Sprites File",  m_sprites, sizeof(m_sprites));
    changed |= field ("Tiles File",    m_tiles,   sizeof(m_tiles));

    ImGui::Separator();
    ImGui::Spacing();

    bool saveNow = ImGui::Button("Save manifest.json", {200, 0});
    if (changed) {
        ImGui::SameLine();
        ImGui::TextColored({1.f, 0.85f, 0.2f, 1.f}, "(unsaved changes)");
    }

    if (saveNow) {
        syncToManifest();
        fs::path manifestPath = m_projectPath / "manifest.json";
        if (m_editor->manifest().save(manifestPath)) {
            m_editor->log("Saved manifest.json");
            captureDiskMtime();   // our own write is not an "external change"
            m_editor->notifySourceSaved();
        } else {
            m_editor->log("Failed to save manifest.json", true);
        }
    }

    ImGui::End();

    drawReloadPrompt();
}

void ManifestEditor::drawMenuItem() {
    ImGui::MenuItem("Manifest", nullptr, &m_visible);
}

void ManifestEditor::syncFromManifest() {
    const auto& m = m_editor->manifest();
    copyTo(m_name,    sizeof(m_name),    m.name);
    copyTo(m_author,  sizeof(m_author),  m.author);
    copyTo(m_version, sizeof(m_version), m.version);
    copyTo(m_entry,   sizeof(m_entry),   m.entry);
    copyTo(m_orient,  sizeof(m_orient),  m.orientation);
    copyTo(m_sprites, sizeof(m_sprites), m.sprites_file);
    copyTo(m_tiles,   sizeof(m_tiles),   m.tiles_file);
    m_dispW  = std::clamp(m.display_width,    1, 4096);
    m_dispH  = std::clamp(m.display_height,   1, 4096);
    m_gridSz = std::clamp(m.sprite_grid_size, 1, 256);
}

// ─── External-file watcher ──────────────────────────────────────────────────

void ManifestEditor::captureDiskMtime() {
    if (m_projectPath.empty()) return;
    std::error_code ec;
    auto t = fs::last_write_time(m_projectPath / "manifest.json", ec);
    if (!ec) m_lastDiskMtime = t;
}

bool ManifestEditor::buffersDirty() const {
    const auto& m = m_editor->manifest();
    if (m.name             != m_name)    return true;
    if (m.author           != m_author)  return true;
    if (m.version          != m_version) return true;
    if (m.entry            != m_entry)   return true;
    if (m.orientation      != m_orient)  return true;
    if (m.sprites_file     != m_sprites) return true;
    if (m.tiles_file       != m_tiles)   return true;
    if (m.display_width    != m_dispW)   return true;
    if (m.display_height   != m_dispH)   return true;
    if (m.sprite_grid_size != m_gridSz)  return true;
    return false;
}

void ManifestEditor::pollManifestOnDisk() {
    if (m_projectPath.empty()) return;
    if (m_reloadPromptOpen)    return;   // already prompting

    std::error_code ec;
    auto now = fs::last_write_time(m_projectPath / "manifest.json", ec);
    if (ec) return;
    if (now == m_lastDiskMtime) return;

    // Disk changed under us. If we have no local edits, just reload silently.
    // Otherwise pop a modal so the user picks reload-vs-keep.
    if (!buffersDirty()) {
        fs::path p = m_projectPath / "manifest.json";
        if (m_editor->manifest().load(p)) {
            syncFromManifest();
            m_lastDiskMtime = now;
            m_editor->log("Manifest reloaded from disk.");
        } else {
            m_lastDiskMtime = now;   // avoid retry-loop on a broken file
            m_editor->log("manifest.json changed on disk but failed to parse.",
                          true);
        }
    } else {
        m_reloadPromptOpen = true;
    }
}

void ManifestEditor::drawReloadPrompt() {
    if (m_reloadPromptOpen) ImGui::OpenPopup("Manifest changed on disk");

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, {0.5f, 0.5f});
    if (ImGui::BeginPopupModal("Manifest changed on disk", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::TextWrapped(
            "manifest.json was modified outside the editor while you have "
            "unsaved changes. Reload from disk (lose local edits) or keep "
            "your edits (next save overwrites the disk version)?");
        ImGui::Spacing();

        if (ImGui::Button("Reload from disk", {180, 0})) {
            fs::path p = m_projectPath / "manifest.json";
            if (m_editor->manifest().load(p)) {
                syncFromManifest();
                m_editor->log("Manifest reloaded from disk.");
            } else {
                m_editor->log("Failed to reload manifest.json", true);
            }
            captureDiskMtime();
            m_reloadPromptOpen = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Keep my edits", {180, 0})) {
            captureDiskMtime();   // accept the new mtime so we don't re-prompt
            m_reloadPromptOpen = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void ManifestEditor::syncToManifest() {
    auto& m = m_editor->manifest();
    m.name             = m_name;
    m.author           = m_author;
    m.version          = m_version;
    m.entry            = m_entry;
    m.orientation      = m_orient;
    m.sprites_file     = m_sprites;
    m.tiles_file       = m_tiles;
    m.display_width    = std::clamp(m_dispW,  1, 4096);
    m.display_height   = std::clamp(m_dispH,  1, 4096);
    m.sprite_grid_size = std::clamp(m_gridSz, 1, 256);
}
