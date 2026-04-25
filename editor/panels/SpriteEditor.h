#pragma once

/*
 * ThermoConsole Editor
 * SpriteEditor — 256x256 sheet editor with 16-colour ThermoConsole palette.
 *
 * Improvements over the original:
 *   - Undo/redo (Ctrl+Z / Ctrl+Shift+Z)
 *   - Transparency is consistent: index 0 is written as alpha=0 on save and
 *     displayed with a checkerboard in the canvas (was inconsistent before)
 *   - Grid-size clamping uses divisors of 256 so tiles align
 *   - Bounded palette lookups (no OOB on tooltip)
 */

#include "../ThermoEditor.h"
#include <SDL.h>

#include <array>
#include <cstdint>
#include <vector>

class SpriteEditor {
public:
    explicit SpriteEditor(ThermoEditor* editor);
    ~SpriteEditor();

    SpriteEditor(const SpriteEditor&) = delete;
    SpriteEditor& operator=(const SpriteEditor&) = delete;

    void onProjectOpened(const fs::path& projectPath);
    void draw();
    void drawMenuItem();

private:
    enum Tool { Draw, Erase, Fill, Eyedropper };

    static constexpr int    kSize       = 256;
    static constexpr size_t kPixelCount = kSize * kSize;

    ThermoEditor*  m_editor;
    fs::path       m_projectPath;
    bool           m_visible = true;

    // Palette-indexed pixel grid (0-15); index 0 denotes transparency on save
    std::vector<uint8_t> m_pixels;
    bool                 m_sheetDirty = false;

    // GL texture (uint to avoid pulling <GL/gl.h> into this header)
    unsigned int m_sheetTex = 0;
    bool         m_texDirty = true;

    // Selection / tools
    int   m_selectedSprite = 0;
    int   m_gridSize       = 16;
    int   m_selectedColor  = 8;
    Tool  m_tool           = Draw;
    float m_zoom           = 16.f;

    // Stroke tracking so one click-drag = one undo step
    bool  m_strokeActive  = false;
    std::vector<uint8_t> m_strokeBefore;

    // Undo / redo stacks (whole-sheet snapshots — simple and fast at 64 KiB)
    std::vector<std::vector<uint8_t>> m_undoStack;
    std::vector<std::vector<uint8_t>> m_redoStack;
    // 256 × 64 KiB = 16 MiB max — generous, but bounded so a long session
    // doesn't grow the undo stack without limit.
    static constexpr size_t kMaxUndo = 256;
    // Log a one-time warning when the cap first overflows in this session
    // (any further drops are silent — the message would just spam the log).
    bool m_undoCapWarned = false;

    // Rendering / IO
    void freeTextures();
    void rebuildTexture();
    void loadSheet(const fs::path& path);
    void saveSheet();
    static uint8_t nearestPaletteColor(uint8_t r, uint8_t g, uint8_t b);

    // Sprite indexing
    int tilesPerRow() const { return kSize / m_gridSize; }
    int spriteX(int id) const { return (id % tilesPerRow()) * m_gridSize; }
    int spriteY(int id) const { return (id / tilesPerRow()) * m_gridSize; }

    // UI helpers
    void drawToolbar();
    void drawSheetOverview();
    void drawPixelCanvas();
    void drawPaletteRow();
    void floodFill(int x, int y, uint8_t target, uint8_t replacement);

    // Undo
    void beginStroke();
    void endStroke();
    void pushUndoSnapshot(const std::vector<uint8_t>& before);
    void undo();
    void redo();

    // Helpers
    std::array<int, 4> validGridSizes() const { return {8, 16, 32, 64}; }
};
