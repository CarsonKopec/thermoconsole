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
}

void ManifestEditor::draw() {
    if (!m_visible) return;
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
        if (m_editor->manifest().save(manifestPath))
            m_editor->log("Saved manifest.json");
        else
            m_editor->log("Failed to save manifest.json", true);
    }

    ImGui::End();
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
