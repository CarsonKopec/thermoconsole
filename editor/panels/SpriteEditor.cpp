/*
 * ThermoConsole Editor — SpriteEditor panel implementation
 */

#include "SpriteEditor.h"
#include <SDL_image.h>

#include <algorithm>
#include <climits>
#include <cstdio>
#include <cstring>
#include <utility>

// ─── Construction / destruction ─────────────────────────────────────────────

SpriteEditor::SpriteEditor(ThermoEditor* editor) : m_editor(editor) {
    m_pixels.assign(kPixelCount, 0);
}

SpriteEditor::~SpriteEditor() {
    freeTextures();
}

void SpriteEditor::onProjectOpened(const fs::path& projectPath) {
    freeTextures();
    m_pixels.assign(kPixelCount, 0);
    m_undoStack.clear();
    m_redoStack.clear();

    m_projectPath = projectPath;

    fs::path spritePath = projectPath / m_editor->manifest().sprites_file;
    std::error_code ec;
    if (fs::exists(spritePath, ec)) loadSheet(spritePath);

    m_gridSize = std::clamp(m_editor->manifest().sprite_grid_size, 1, kSize);
    // Snap to a valid divisor
    int best = 16, bestDist = INT_MAX;
    for (int s : validGridSizes()) {
        int d = std::abs(s - m_gridSize);
        if (d < bestDist) { bestDist = d; best = s; }
    }
    m_gridSize = best;

    m_sheetDirty = false;
    m_texDirty   = true;
}

// ─── SDL texture management ─────────────────────────────────────────────────

void SpriteEditor::freeTextures() {
    if (m_sheetTex) {
        SDL_DestroyTexture(m_sheetTex);
        m_sheetTex = nullptr;
    }
}

void SpriteEditor::rebuildTexture() {
    if (!m_texDirty) return;
    freeTextures();

    SDL_Renderer* rend = m_editor->renderer();
    m_sheetTex = SDL_CreateTexture(rend, SDL_PIXELFORMAT_RGBA8888,
                                   SDL_TEXTUREACCESS_STREAMING, kSize, kSize);
    if (!m_sheetTex) return;

    // Enable alpha blending so transparent pixels let the canvas background through
    SDL_SetTextureBlendMode(m_sheetTex, SDL_BLENDMODE_BLEND);

    void* pixels = nullptr;
    int   pitch  = 0;
    if (SDL_LockTexture(m_sheetTex, nullptr, &pixels, &pitch) != 0) return;

    auto* dst = static_cast<uint32_t*>(pixels);
    const int stride = pitch / 4;
    for (int y = 0; y < kSize; ++y) {
        for (int x = 0; x < kSize; ++x) {
            const uint8_t idx = m_pixels[y * kSize + x] & 0x0F;
            const auto& c = THERMO_PALETTE[idx];
            const uint32_t alpha = (idx == 0) ? 0u : 0xFFu;   // index 0 = transparent
            dst[y * stride + x] = (uint32_t(c.r) << 24) |
                                  (uint32_t(c.g) << 16) |
                                  (uint32_t(c.b) <<  8) |
                                   alpha;
        }
    }
    SDL_UnlockTexture(m_sheetTex);
    m_texDirty = false;
}

// ─── File IO ────────────────────────────────────────────────────────────────

void SpriteEditor::loadSheet(const fs::path& path) {
    SDL_Surface* surf = IMG_Load(path.string().c_str());
    if (!surf) {
        m_editor->log("Sprite load failed: " + std::string(IMG_GetError()), true);
        return;
    }
    SDL_Surface* conv = SDL_ConvertSurfaceFormat(surf, SDL_PIXELFORMAT_RGBA8888, 0);
    SDL_FreeSurface(surf);
    if (!conv) return;

    const int w = std::min(conv->w, kSize);
    const int h = std::min(conv->h, kSize);
    SDL_LockSurface(conv);
    const auto* src = static_cast<const uint32_t*>(conv->pixels);
    const int stride = conv->pitch / 4;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const uint32_t px = src[y * stride + x];
            const uint8_t r = (px >> 24) & 0xFF;
            const uint8_t g = (px >> 16) & 0xFF;
            const uint8_t b = (px >>  8) & 0xFF;
            const uint8_t a = (px >>  0) & 0xFF;
            // Transparent pixels map to palette index 0
            m_pixels[y * kSize + x] = (a < 128) ? 0 : nearestPaletteColor(r, g, b);
        }
    }
    SDL_UnlockSurface(conv);
    SDL_FreeSurface(conv);
    m_texDirty = true;
    m_editor->log("Loaded sprite sheet: " + path.filename().string());
}

void SpriteEditor::saveSheet() {
    if (m_projectPath.empty()) return;
    fs::path outPath = m_projectPath / m_editor->manifest().sprites_file;

    SDL_Surface* surf = SDL_CreateRGBSurfaceWithFormat(
        0, kSize, kSize, 32, SDL_PIXELFORMAT_RGBA8888);
    if (!surf) {
        m_editor->log("Sprite save failed: surface alloc", true);
        return;
    }

    SDL_LockSurface(surf);
    auto* dst = static_cast<uint32_t*>(surf->pixels);
    const int stride = surf->pitch / 4;
    for (int y = 0; y < kSize; ++y) {
        for (int x = 0; x < kSize; ++x) {
            const uint8_t idx = m_pixels[y * kSize + x] & 0x0F;
            const auto& c = THERMO_PALETTE[idx];
            const uint32_t alpha = (idx == 0) ? 0u : 0xFFu;
            dst[y * stride + x] = (uint32_t(c.r) << 24) |
                                  (uint32_t(c.g) << 16) |
                                  (uint32_t(c.b) <<  8) |
                                   alpha;
        }
    }
    SDL_UnlockSurface(surf);

    if (IMG_SavePNG(surf, outPath.string().c_str()) == 0) {
        m_editor->log("Saved sprite sheet: " + outPath.string());
        m_sheetDirty = false;
    } else {
        m_editor->log("Save failed: " + std::string(IMG_GetError()), true);
    }
    SDL_FreeSurface(surf);
}

uint8_t SpriteEditor::nearestPaletteColor(uint8_t r, uint8_t g, uint8_t b) {
    int best = 0, bestDist = INT_MAX;
    for (int i = 0; i < 16; ++i) {
        int dr = r - THERMO_PALETTE[i].r;
        int dg = g - THERMO_PALETTE[i].g;
        int db = b - THERMO_PALETTE[i].b;
        int d  = dr * dr + dg * dg + db * db;
        if (d < bestDist) { bestDist = d; best = i; }
    }
    return static_cast<uint8_t>(best);
}

// ─── Undo / redo ────────────────────────────────────────────────────────────

void SpriteEditor::beginStroke() {
    if (m_strokeActive) return;
    m_strokeBefore = m_pixels;
    m_strokeActive = true;
}

void SpriteEditor::endStroke() {
    if (!m_strokeActive) return;
    m_strokeActive = false;
    if (m_strokeBefore != m_pixels) {
        pushUndoSnapshot(m_strokeBefore);
    }
    m_strokeBefore.clear();
}

void SpriteEditor::pushUndoSnapshot(const std::vector<uint8_t>& before) {
    m_undoStack.push_back(before);
    if (m_undoStack.size() > kMaxUndo) m_undoStack.erase(m_undoStack.begin());
    m_redoStack.clear();
}

void SpriteEditor::undo() {
    if (m_undoStack.empty()) return;
    m_redoStack.push_back(m_pixels);
    m_pixels = std::move(m_undoStack.back());
    m_undoStack.pop_back();
    m_texDirty = m_sheetDirty = true;
}

void SpriteEditor::redo() {
    if (m_redoStack.empty()) return;
    m_undoStack.push_back(m_pixels);
    m_pixels = std::move(m_redoStack.back());
    m_redoStack.pop_back();
    m_texDirty = m_sheetDirty = true;
}

// ─── Draw ───────────────────────────────────────────────────────────────────

void SpriteEditor::draw() {
    if (!m_visible) return;
    ImGui::Begin("Sprite Editor", &m_visible);

    // Keyboard shortcuts (only when this window has focus)
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
        ImGuiIO& io = ImGui::GetIO();
        if (io.KeyCtrl && !io.WantTextInput) {
            if (ImGui::IsKeyPressed(ImGuiKey_Z, false)) {
                if (io.KeyShift) redo();
                else             undo();
            }
            if (ImGui::IsKeyPressed(ImGuiKey_Y, false)) redo();
        }
    }

    drawToolbar();
    ImGui::Separator();

    ImVec2 avail = ImGui::GetContentRegionAvail();
    const float left_w = std::min(avail.x * 0.45f, 300.f);

    ImGui::BeginChild("##sheet_overview", {left_w, avail.y - 36.f}, true);
    drawSheetOverview();
    ImGui::EndChild();

    ImGui::SameLine();

    ImGui::BeginChild("##pixel_canvas",
                      {avail.x - left_w - 8.f, avail.y - 36.f}, true);
    drawPixelCanvas();
    ImGui::EndChild();

    ImGui::Separator();
    drawPaletteRow();

    ImGui::End();
}

void SpriteEditor::drawMenuItem() {
    ImGui::MenuItem("Sprite Editor", nullptr, &m_visible);
}

// ─── Toolbar ────────────────────────────────────────────────────────────────

void SpriteEditor::drawToolbar() {
    const char* tools[] = { "Draw", "Erase", "Fill", "Eyedropper" };
    for (int i = 0; i < 4; ++i) {
        if (i > 0) ImGui::SameLine();
        const bool active = (m_tool == (Tool)i);
        if (active) ImGui::PushStyleColor(ImGuiCol_Button, {0.30f, 0.50f, 0.85f, 1.f});
        if (ImGui::Button(tools[i])) m_tool = (Tool)i;
        if (active) ImGui::PopStyleColor();
    }

    ImGui::SameLine(0, 20);
    ImGui::Text("Grid:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(80);
    // Only allow divisor-of-256 sizes so tiles align perfectly
    const auto sizes = validGridSizes();
    int idx = 1;  // default 16
    for (int i = 0; i < (int)sizes.size(); ++i) if (sizes[i] == m_gridSize) idx = i;
    const char* items[] = { "8", "16", "32", "64" };
    if (ImGui::Combo("##grid", &idx, items, (int)sizes.size()))
        m_gridSize = sizes[idx];

    ImGui::SameLine(0, 20);
    ImGui::Text("Zoom:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(100);
    ImGui::SliderFloat("##zoom", &m_zoom, 4.f, 32.f, "%.0fx");

    ImGui::SameLine(0, 20);
    ImGui::BeginDisabled(m_undoStack.empty());
    if (ImGui::Button("Undo")) undo();
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(m_redoStack.empty());
    if (ImGui::Button("Redo")) redo();
    ImGui::EndDisabled();

    ImGui::SameLine(0, 20);
    if (ImGui::Button("Save Sheet")) saveSheet();
    if (m_sheetDirty) {
        ImGui::SameLine();
        ImGui::TextColored({1.f, 0.8f, 0.2f, 1.f}, "(unsaved)");
    }
}

// ─── Sheet overview (left) ──────────────────────────────────────────────────

void SpriteEditor::drawSheetOverview() {
    rebuildTexture();
    if (!m_sheetTex) {
        ImGui::TextDisabled("No sprite sheet loaded.");
        return;
    }

    ImVec2 sz = ImGui::GetContentRegionAvail();
    const float scale = std::min(sz.x / (float)kSize, sz.y / (float)kSize);
    const float disp  = (float)kSize * scale;
    ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::Image((ImTextureID)(intptr_t)m_sheetTex, {disp, disp});

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float gs = (float)m_gridSize * scale;
    const int cols = tilesPerRow();

    // Grid lines
    for (int x = 0; x <= cols; ++x)
        dl->AddLine({origin.x + x * gs, origin.y},
                    {origin.x + x * gs, origin.y + disp},
                    IM_COL32(80, 80, 100, 120));
    for (int y = 0; y <= cols; ++y)
        dl->AddLine({origin.x,        origin.y + y * gs},
                    {origin.x + disp, origin.y + y * gs},
                    IM_COL32(80, 80, 100, 120));

    // Highlight selected tile
    const int sx = spriteX(m_selectedSprite);
    const int sy = spriteY(m_selectedSprite);
    dl->AddRect(
        {origin.x + sx * scale,                         origin.y + sy * scale},
        {origin.x + (sx + m_gridSize) * scale,          origin.y + (sy + m_gridSize) * scale},
        IM_COL32(255, 220, 60, 220), 0.f, 0, 2.f);

    // Click to select
    if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(0)) {
        ImVec2 mp = ImGui::GetMousePos();
        int px = (int)((mp.x - origin.x) / scale);
        int py = (int)((mp.y - origin.y) / scale);
        if (px >= 0 && py >= 0 && px < kSize && py < kSize) {
            const int tx = px / m_gridSize;
            const int ty = py / m_gridSize;
            m_selectedSprite = ty * cols + tx;
        }
    }

    ImGui::Text("Sprite #%d  (%d,%d)", m_selectedSprite,
                spriteX(m_selectedSprite), spriteY(m_selectedSprite));
}

// ─── Pixel canvas (right) ───────────────────────────────────────────────────

void SpriteEditor::drawPixelCanvas() {
    const int sx = spriteX(m_selectedSprite);
    const int sy = spriteY(m_selectedSprite);

    const float cellSize = m_zoom;
    ImVec2 canvasOrig = ImGui::GetCursorScreenPos();
    const float canvasW = m_gridSize * cellSize;
    const float canvasH = m_gridSize * cellSize;
    ImDrawList* dl = ImGui::GetWindowDrawList();

    // Checkerboard background for transparency
    for (int row = 0; row < m_gridSize; ++row) {
        for (int col = 0; col < m_gridSize; ++col) {
            const bool chk = (row + col) & 1;
            const ImU32 bg = chk ? IM_COL32(80, 80, 90, 255) : IM_COL32(55, 55, 65, 255);
            dl->AddRectFilled(
                {canvasOrig.x + col * cellSize,      canvasOrig.y + row * cellSize},
                {canvasOrig.x + (col + 1) * cellSize, canvasOrig.y + (row + 1) * cellSize},
                bg);
        }
    }

    // Pixels (skip drawing index 0 so checkerboard shows through)
    for (int row = 0; row < m_gridSize; ++row) {
        for (int col = 0; col < m_gridSize; ++col) {
            const int px = sx + col;
            const int py = sy + row;
            if (px < 0 || px >= kSize || py < 0 || py >= kSize) continue;
            const uint8_t idx = m_pixels[py * kSize + px] & 0x0F;
            if (idx == 0) continue;   // transparent
            const auto& c = THERMO_PALETTE[idx];
            dl->AddRectFilled(
                {canvasOrig.x + col * cellSize,         canvasOrig.y + row * cellSize},
                {canvasOrig.x + (col + 1) * cellSize,   canvasOrig.y + (row + 1) * cellSize},
                IM_COL32(c.r, c.g, c.b, 255));
        }
    }

    // Grid overlay
    for (int i = 0; i <= m_gridSize; ++i) {
        dl->AddLine({canvasOrig.x + i * cellSize, canvasOrig.y},
                    {canvasOrig.x + i * cellSize, canvasOrig.y + canvasH},
                    IM_COL32(50, 50, 70, 180));
        dl->AddLine({canvasOrig.x,           canvasOrig.y + i * cellSize},
                    {canvasOrig.x + canvasW, canvasOrig.y + i * cellSize},
                    IM_COL32(50, 50, 70, 180));
    }

    // Interaction button on top of the canvas
    ImGui::InvisibleButton("##canvas", {canvasW, canvasH});
    const bool hovered = ImGui::IsItemHovered();
    const bool mouseDown    = ImGui::IsMouseDown(0);
    const bool mouseClicked = ImGui::IsMouseClicked(0);
    const bool mouseRelease = ImGui::IsMouseReleased(0);

    if (hovered) {
        ImVec2 mp = ImGui::GetMousePos();
        int col = (int)((mp.x - canvasOrig.x) / cellSize);
        int row = (int)((mp.y - canvasOrig.y) / cellSize);
        if (col >= 0 && col < m_gridSize && row >= 0 && row < m_gridSize) {
            const int px = sx + col;
            const int py = sy + row;
            const bool inBounds = (px >= 0 && px < kSize && py >= 0 && py < kSize);

            // Hover highlight
            dl->AddRect(
                {canvasOrig.x + col * cellSize,         canvasOrig.y + row * cellSize},
                {canvasOrig.x + (col + 1) * cellSize,   canvasOrig.y + (row + 1) * cellSize},
                IM_COL32(255, 255, 255, 180));

            if (inBounds && mouseDown) {
                switch (m_tool) {
                    case Draw: {
                        if (mouseClicked) beginStroke();
                        m_pixels[py * kSize + px] = (uint8_t)m_selectedColor;
                        m_texDirty = m_sheetDirty = true;
                        break;
                    }
                    case Erase: {
                        if (mouseClicked) beginStroke();
                        m_pixels[py * kSize + px] = 0;
                        m_texDirty = m_sheetDirty = true;
                        break;
                    }
                    case Fill: {
                        if (mouseClicked) {
                            beginStroke();
                            floodFill(px, py, m_pixels[py * kSize + px],
                                      (uint8_t)m_selectedColor);
                            m_texDirty = m_sheetDirty = true;
                            endStroke();
                        }
                        break;
                    }
                    case Eyedropper: {
                        if (mouseClicked)
                            m_selectedColor = m_pixels[py * kSize + px] & 0x0F;
                        break;
                    }
                }
            }
            // Right-click = eyedrop regardless of active tool
            if (inBounds && ImGui::IsMouseClicked(1))
                m_selectedColor = m_pixels[py * kSize + px] & 0x0F;

            // Tooltip
            if (inBounds) {
                const uint8_t idx = m_pixels[py * kSize + px] & 0x0F;
                ImGui::SetTooltip("(%d,%d)  color %d (%s)", px, py, idx,
                                  THERMO_PALETTE[idx].name);
            }
        }
    }

    if (mouseRelease) endStroke();

    ImGui::Text("Spr #%d  |  Sheet (%d,%d)  |  Grid %dx%d",
                m_selectedSprite, sx, sy, m_gridSize, m_gridSize);
}

// ─── Palette row ────────────────────────────────────────────────────────────

void SpriteEditor::drawPaletteRow() {
    ImGui::Text("Palette:");
    ImGui::SameLine();
    for (int i = 0; i < 16; ++i) {
        if (i > 0) ImGui::SameLine(0, 2);
        ImVec4 col = PaletteImColor(i);
        bool sel = (m_selectedColor == i);

        ImGui::PushStyleColor(ImGuiCol_Button,        col);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, {col.x * 1.2f, col.y * 1.2f, col.z * 1.2f, 1.f});
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  col);
        char id[8]; std::snprintf(id, sizeof(id), "##p%d", i);
        if (ImGui::Button(id, {24, 24})) m_selectedColor = i;
        ImGui::PopStyleColor(3);

        if (sel) {
            ImVec2 bmin = ImGui::GetItemRectMin();
            ImVec2 bmax = ImGui::GetItemRectMax();
            ImGui::GetWindowDrawList()->AddRect(bmin, bmax,
                IM_COL32(255, 255, 255, 255), 0.f, 0, 2.f);
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%d: %s", i, THERMO_PALETTE[i].name);
    }
    ImGui::SameLine(0, 16);
    ImGui::Text("Active: %d (%s)%s",
                m_selectedColor, THERMO_PALETTE[m_selectedColor & 0x0F].name,
                m_selectedColor == 0 ? "  [transparent]" : "");
}

// ─── Flood fill ─────────────────────────────────────────────────────────────

void SpriteEditor::floodFill(int x, int y, uint8_t target, uint8_t replacement) {
    if (target == replacement) return;
    if (x < 0 || y < 0 || x >= kSize || y >= kSize) return;
    if (m_pixels[y * kSize + x] != target) return;

    std::vector<std::pair<int, int>> stack;
    stack.reserve(256);
    stack.emplace_back(x, y);
    while (!stack.empty()) {
        auto [cx, cy] = stack.back();
        stack.pop_back();
        if (cx < 0 || cy < 0 || cx >= kSize || cy >= kSize) continue;
        if (m_pixels[cy * kSize + cx] != target) continue;
        m_pixels[cy * kSize + cx] = replacement;
        stack.emplace_back(cx - 1, cy);
        stack.emplace_back(cx + 1, cy);
        stack.emplace_back(cx, cy - 1);
        stack.emplace_back(cx, cy + 1);
    }
}
