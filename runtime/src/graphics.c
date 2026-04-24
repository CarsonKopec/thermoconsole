/*
 * ThermoConsole Runtime
 * Graphics module - all drawing functions
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <SDL.h>
#include <SDL_image.h>
#include "thermo.h"
#include "platform.h"

/* Built-in 8x8 font (ASCII 32-127) */
static const uint8_t FONT_DATA[] = {
    /* Space (32) */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    /* ! */
    0x18, 0x18, 0x18, 0x18, 0x18, 0x00, 0x18, 0x00,
    /* " */
    0x6C, 0x6C, 0x24, 0x00, 0x00, 0x00, 0x00, 0x00,
    /* # */
    0x6C, 0x6C, 0xFE, 0x6C, 0xFE, 0x6C, 0x6C, 0x00,
    /* $ */
    0x18, 0x7E, 0xC0, 0x7C, 0x06, 0xFC, 0x18, 0x00,
    /* % */
    0x00, 0xC6, 0xCC, 0x18, 0x30, 0x66, 0xC6, 0x00,
    /* & */
    0x38, 0x6C, 0x38, 0x76, 0xDC, 0xCC, 0x76, 0x00,
    /* ' */
    0x18, 0x18, 0x30, 0x00, 0x00, 0x00, 0x00, 0x00,
    /* ( */
    0x0C, 0x18, 0x30, 0x30, 0x30, 0x18, 0x0C, 0x00,
    /* ) */
    0x30, 0x18, 0x0C, 0x0C, 0x0C, 0x18, 0x30, 0x00,
    /* * */
    0x00, 0x66, 0x3C, 0xFF, 0x3C, 0x66, 0x00, 0x00,
    /* + */
    0x00, 0x18, 0x18, 0x7E, 0x18, 0x18, 0x00, 0x00,
    /* , */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x18, 0x30,
    /* - */
    0x00, 0x00, 0x00, 0x7E, 0x00, 0x00, 0x00, 0x00,
    /* . */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x18, 0x00,
    /* / */
    0x06, 0x0C, 0x18, 0x30, 0x60, 0xC0, 0x80, 0x00,
    /* 0 */
    0x7C, 0xCE, 0xDE, 0xF6, 0xE6, 0xC6, 0x7C, 0x00,
    /* 1 */
    0x18, 0x38, 0x18, 0x18, 0x18, 0x18, 0x7E, 0x00,
    /* 2 */
    0x7C, 0xC6, 0x06, 0x1C, 0x70, 0xC6, 0xFE, 0x00,
    /* 3 */
    0x7C, 0xC6, 0x06, 0x3C, 0x06, 0xC6, 0x7C, 0x00,
    /* 4 */
    0x1C, 0x3C, 0x6C, 0xCC, 0xFE, 0x0C, 0x1E, 0x00,
    /* 5 */
    0xFE, 0xC0, 0xFC, 0x06, 0x06, 0xC6, 0x7C, 0x00,
    /* 6 */
    0x38, 0x60, 0xC0, 0xFC, 0xC6, 0xC6, 0x7C, 0x00,
    /* 7 */
    0xFE, 0xC6, 0x0C, 0x18, 0x30, 0x30, 0x30, 0x00,
    /* 8 */
    0x7C, 0xC6, 0xC6, 0x7C, 0xC6, 0xC6, 0x7C, 0x00,
    /* 9 */
    0x7C, 0xC6, 0xC6, 0x7E, 0x06, 0x0C, 0x78, 0x00,
    /* : */
    0x00, 0x18, 0x18, 0x00, 0x00, 0x18, 0x18, 0x00,
    /* ; */
    0x00, 0x18, 0x18, 0x00, 0x00, 0x18, 0x18, 0x30,
    /* < */
    0x0C, 0x18, 0x30, 0x60, 0x30, 0x18, 0x0C, 0x00,
    /* = */
    0x00, 0x00, 0x7E, 0x00, 0x00, 0x7E, 0x00, 0x00,
    /* > */
    0x60, 0x30, 0x18, 0x0C, 0x18, 0x30, 0x60, 0x00,
    /* ? */
    0x7C, 0xC6, 0x0C, 0x18, 0x18, 0x00, 0x18, 0x00,
    /* @ */
    0x7C, 0xC6, 0xDE, 0xDE, 0xDC, 0xC0, 0x7C, 0x00,
    /* A */
    0x38, 0x6C, 0xC6, 0xFE, 0xC6, 0xC6, 0xC6, 0x00,
    /* B */
    0xFC, 0x66, 0x66, 0x7C, 0x66, 0x66, 0xFC, 0x00,
    /* C */
    0x3C, 0x66, 0xC0, 0xC0, 0xC0, 0x66, 0x3C, 0x00,
    /* D */
    0xF8, 0x6C, 0x66, 0x66, 0x66, 0x6C, 0xF8, 0x00,
    /* E */
    0xFE, 0x62, 0x68, 0x78, 0x68, 0x62, 0xFE, 0x00,
    /* F */
    0xFE, 0x62, 0x68, 0x78, 0x68, 0x60, 0xF0, 0x00,
    /* G */
    0x3C, 0x66, 0xC0, 0xC0, 0xCE, 0x66, 0x3E, 0x00,
    /* H */
    0xC6, 0xC6, 0xC6, 0xFE, 0xC6, 0xC6, 0xC6, 0x00,
    /* I */
    0x3C, 0x18, 0x18, 0x18, 0x18, 0x18, 0x3C, 0x00,
    /* J */
    0x1E, 0x0C, 0x0C, 0x0C, 0xCC, 0xCC, 0x78, 0x00,
    /* K */
    0xE6, 0x66, 0x6C, 0x78, 0x6C, 0x66, 0xE6, 0x00,
    /* L */
    0xF0, 0x60, 0x60, 0x60, 0x62, 0x66, 0xFE, 0x00,
    /* M */
    0xC6, 0xEE, 0xFE, 0xD6, 0xC6, 0xC6, 0xC6, 0x00,
    /* N */
    0xC6, 0xE6, 0xF6, 0xDE, 0xCE, 0xC6, 0xC6, 0x00,
    /* O */
    0x7C, 0xC6, 0xC6, 0xC6, 0xC6, 0xC6, 0x7C, 0x00,
    /* P */
    0xFC, 0x66, 0x66, 0x7C, 0x60, 0x60, 0xF0, 0x00,
    /* Q */
    0x7C, 0xC6, 0xC6, 0xC6, 0xD6, 0xDE, 0x7C, 0x06,
    /* R */
    0xFC, 0x66, 0x66, 0x7C, 0x6C, 0x66, 0xE6, 0x00,
    /* S */
    0x7C, 0xC6, 0x60, 0x38, 0x0C, 0xC6, 0x7C, 0x00,
    /* T */
    0x7E, 0x5A, 0x18, 0x18, 0x18, 0x18, 0x3C, 0x00,
    /* U */
    0xC6, 0xC6, 0xC6, 0xC6, 0xC6, 0xC6, 0x7C, 0x00,
    /* V */
    0xC6, 0xC6, 0xC6, 0xC6, 0x6C, 0x38, 0x10, 0x00,
    /* W */
    0xC6, 0xC6, 0xC6, 0xD6, 0xFE, 0xEE, 0xC6, 0x00,
    /* X */
    0xC6, 0xC6, 0x6C, 0x38, 0x6C, 0xC6, 0xC6, 0x00,
    /* Y */
    0x66, 0x66, 0x66, 0x3C, 0x18, 0x18, 0x3C, 0x00,
    /* Z */
    0xFE, 0xC6, 0x8C, 0x18, 0x32, 0x66, 0xFE, 0x00,
    /* [ */
    0x3C, 0x30, 0x30, 0x30, 0x30, 0x30, 0x3C, 0x00,
    /* \ */
    0xC0, 0x60, 0x30, 0x18, 0x0C, 0x06, 0x02, 0x00,
    /* ] */
    0x3C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x3C, 0x00,
    /* ^ */
    0x10, 0x38, 0x6C, 0xC6, 0x00, 0x00, 0x00, 0x00,
    /* _ */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF,
    /* ` */
    0x30, 0x18, 0x0C, 0x00, 0x00, 0x00, 0x00, 0x00,
    /* a */
    0x00, 0x00, 0x78, 0x0C, 0x7C, 0xCC, 0x76, 0x00,
    /* b */
    0xE0, 0x60, 0x7C, 0x66, 0x66, 0x66, 0xDC, 0x00,
    /* c */
    0x00, 0x00, 0x7C, 0xC6, 0xC0, 0xC6, 0x7C, 0x00,
    /* d */
    0x1C, 0x0C, 0x7C, 0xCC, 0xCC, 0xCC, 0x76, 0x00,
    /* e */
    0x00, 0x00, 0x7C, 0xC6, 0xFE, 0xC0, 0x7C, 0x00,
    /* f */
    0x1C, 0x36, 0x30, 0x78, 0x30, 0x30, 0x78, 0x00,
    /* g */
    0x00, 0x00, 0x76, 0xCC, 0xCC, 0x7C, 0x0C, 0x78,
    /* h */
    0xE0, 0x60, 0x6C, 0x76, 0x66, 0x66, 0xE6, 0x00,
    /* i */
    0x18, 0x00, 0x38, 0x18, 0x18, 0x18, 0x3C, 0x00,
    /* j */
    0x06, 0x00, 0x0E, 0x06, 0x06, 0x66, 0x66, 0x3C,
    /* k */
    0xE0, 0x60, 0x66, 0x6C, 0x78, 0x6C, 0xE6, 0x00,
    /* l */
    0x38, 0x18, 0x18, 0x18, 0x18, 0x18, 0x3C, 0x00,
    /* m */
    0x00, 0x00, 0xEC, 0xFE, 0xD6, 0xD6, 0xD6, 0x00,
    /* n */
    0x00, 0x00, 0xDC, 0x66, 0x66, 0x66, 0x66, 0x00,
    /* o */
    0x00, 0x00, 0x7C, 0xC6, 0xC6, 0xC6, 0x7C, 0x00,
    /* p */
    0x00, 0x00, 0xDC, 0x66, 0x66, 0x7C, 0x60, 0xF0,
    /* q */
    0x00, 0x00, 0x76, 0xCC, 0xCC, 0x7C, 0x0C, 0x1E,
    /* r */
    0x00, 0x00, 0xDC, 0x76, 0x60, 0x60, 0xF0, 0x00,
    /* s */
    0x00, 0x00, 0x7C, 0xC0, 0x7C, 0x06, 0xFC, 0x00,
    /* t */
    0x30, 0x30, 0x7C, 0x30, 0x30, 0x36, 0x1C, 0x00,
    /* u */
    0x00, 0x00, 0xCC, 0xCC, 0xCC, 0xCC, 0x76, 0x00,
    /* v */
    0x00, 0x00, 0xC6, 0xC6, 0xC6, 0x6C, 0x38, 0x00,
    /* w */
    0x00, 0x00, 0xC6, 0xD6, 0xD6, 0xFE, 0x6C, 0x00,
    /* x */
    0x00, 0x00, 0xC6, 0x6C, 0x38, 0x6C, 0xC6, 0x00,
    /* y */
    0x00, 0x00, 0xC6, 0xC6, 0xC6, 0x7E, 0x06, 0xFC,
    /* z */
    0x00, 0x00, 0xFE, 0x8C, 0x18, 0x32, 0xFE, 0x00,
    /* { */
    0x0E, 0x18, 0x18, 0x70, 0x18, 0x18, 0x0E, 0x00,
    /* | */
    0x18, 0x18, 0x18, 0x00, 0x18, 0x18, 0x18, 0x00,
    /* } */
    0x70, 0x18, 0x18, 0x0E, 0x18, 0x18, 0x70, 0x00,
    /* ~ */
    0x76, 0xDC, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

static SDL_Texture* font_texture = NULL;

/* ─────────────────────────────────────────────────────────────────────────────
 * Initialization
 * ───────────────────────────────────────────────────────────────────────────── */

int gfx_init(void) {
    ThermoGraphics* gfx = &g_thermo->gfx;
    
    /* Get platform-specific scale and window flags */
    gfx->scale = platform_get_display_scale();
    uint32_t window_flags = platform_get_window_flags();
    
    gfx->window_width = THERMO_SCREEN_WIDTH * gfx->scale;
    gfx->window_height = THERMO_SCREEN_HEIGHT * gfx->scale;
    
    /* Create window */
    gfx->window = SDL_CreateWindow(
        "ThermoConsole",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        gfx->window_width, gfx->window_height,
        window_flags
    );
    
    if (!gfx->window) {
        fprintf(stderr, "ERROR: SDL_CreateWindow failed: %s\n", SDL_GetError());
        return -1;
    }
    
    /* Create renderer */
    gfx->renderer = SDL_CreateRenderer(
        gfx->window, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC
    );
    
    if (!gfx->renderer) {
        fprintf(stderr, "ERROR: SDL_CreateRenderer failed: %s\n", SDL_GetError());
        return -1;
    }
    
    /* Set render quality */
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0"); /* nearest neighbor */
    
    /* Create render target canvas */
    gfx->canvas = SDL_CreateTexture(
        gfx->renderer,
        SDL_PIXELFORMAT_RGBA8888,
        SDL_TEXTUREACCESS_TARGET,
        THERMO_SCREEN_WIDTH, THERMO_SCREEN_HEIGHT
    );
    
    if (!gfx->canvas) {
        fprintf(stderr, "ERROR: Failed to create canvas: %s\n", SDL_GetError());
        return -1;
    }
    
    /* Initialize palette */
    memcpy(gfx->palette, THERMO_DEFAULT_PALETTE, sizeof(THERMO_DEFAULT_PALETTE));
    memcpy(gfx->palette_remap, THERMO_DEFAULT_PALETTE, sizeof(THERMO_DEFAULT_PALETTE));
    
    /* Initialize camera */
    gfx->camera_x = 0;
    gfx->camera_y = 0;
    
    /* Initialize clipping */
    gfx->clip_enabled = false;
    gfx->clip_rect.x = 0;
    gfx->clip_rect.y = 0;
    gfx->clip_rect.w = THERMO_SCREEN_WIDTH;
    gfx->clip_rect.h = THERMO_SCREEN_HEIGHT;
    
    /* Create font texture */
    /* For simplicity, we'll render text directly from font data */
    /* A full implementation would create a texture from FONT_DATA */
    
    return 0;
}

void gfx_shutdown(void) {
    ThermoGraphics* gfx = &g_thermo->gfx;
    
    if (font_texture) {
        SDL_DestroyTexture(font_texture);
        font_texture = NULL;
    }
    
    if (gfx->canvas) {
        SDL_DestroyTexture(gfx->canvas);
    }
    
    if (gfx->renderer) {
        SDL_DestroyRenderer(gfx->renderer);
    }
    
    if (gfx->window) {
        SDL_DestroyWindow(gfx->window);
    }
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Frame Management
 * ───────────────────────────────────────────────────────────────────────────── */

void gfx_begin_frame(void) {
    ThermoGraphics* gfx = &g_thermo->gfx;
    
    /* Set render target to canvas */
    SDL_SetRenderTarget(gfx->renderer, gfx->canvas);
}

void gfx_end_frame(void) {
    ThermoGraphics* gfx = &g_thermo->gfx;
    
    /* Reset render target to window */
    SDL_SetRenderTarget(gfx->renderer, NULL);
    
    /* Clear window */
    SDL_SetRenderDrawColor(gfx->renderer, 0, 0, 0, 255);
    SDL_RenderClear(gfx->renderer);
    
    /* Calculate scaling to fit window while maintaining aspect ratio */
    float scale_x = (float)gfx->window_width / THERMO_SCREEN_WIDTH;
    float scale_y = (float)gfx->window_height / THERMO_SCREEN_HEIGHT;
    float scale = (scale_x < scale_y) ? scale_x : scale_y;
    
    /* Use integer scaling if possible */
    int int_scale = (int)scale;
    if (int_scale < 1) int_scale = 1;
    
    int dest_w = THERMO_SCREEN_WIDTH * int_scale;
    int dest_h = THERMO_SCREEN_HEIGHT * int_scale;
    int dest_x = (gfx->window_width - dest_w) / 2;
    int dest_y = (gfx->window_height - dest_h) / 2;
    
    SDL_Rect dest = {dest_x, dest_y, dest_w, dest_h};
    
    /* Render canvas to window */
    SDL_RenderCopy(gfx->renderer, gfx->canvas, NULL, &dest);
    
    /* Present */
    SDL_RenderPresent(gfx->renderer);
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Drawing Functions
 * ───────────────────────────────────────────────────────────────────────────── */

void gfx_cls(int color) {
    ThermoGraphics* gfx = &g_thermo->gfx;
    ThermoColor c = color_from_index(color);
    
    SDL_SetRenderDrawColor(gfx->renderer, c.r, c.g, c.b, c.a);
    SDL_RenderClear(gfx->renderer);
}

void gfx_pset(int x, int y, int color) {
    ThermoGraphics* gfx = &g_thermo->gfx;
    
    x -= gfx->camera_x;
    y -= gfx->camera_y;
    
    if (x < 0 || x >= THERMO_SCREEN_WIDTH || y < 0 || y >= THERMO_SCREEN_HEIGHT) {
        return;
    }
    
    ThermoColor c = color_from_index(color);
    SDL_SetRenderDrawColor(gfx->renderer, c.r, c.g, c.b, c.a);
    SDL_RenderDrawPoint(gfx->renderer, x, y);
}

int gfx_pget(int x, int y) {
    /* This would require reading from the render target, which is expensive */
    /* For now, return 0 */
    return 0;
}

void gfx_line(int x1, int y1, int x2, int y2, int color) {
    ThermoGraphics* gfx = &g_thermo->gfx;
    ThermoColor c = color_from_index(color);
    
    x1 -= gfx->camera_x;
    y1 -= gfx->camera_y;
    x2 -= gfx->camera_x;
    y2 -= gfx->camera_y;
    
    SDL_SetRenderDrawColor(gfx->renderer, c.r, c.g, c.b, c.a);
    SDL_RenderDrawLine(gfx->renderer, x1, y1, x2, y2);
}

void gfx_rect(int x, int y, int w, int h, int color) {
    ThermoGraphics* gfx = &g_thermo->gfx;
    ThermoColor c = color_from_index(color);
    
    x -= gfx->camera_x;
    y -= gfx->camera_y;
    
    SDL_Rect rect = {x, y, w, h};
    SDL_SetRenderDrawColor(gfx->renderer, c.r, c.g, c.b, c.a);
    SDL_RenderDrawRect(gfx->renderer, &rect);
}

void gfx_rectfill(int x, int y, int w, int h, int color) {
    ThermoGraphics* gfx = &g_thermo->gfx;
    ThermoColor c = color_from_index(color);
    
    x -= gfx->camera_x;
    y -= gfx->camera_y;
    
    SDL_Rect rect = {x, y, w, h};
    SDL_SetRenderDrawColor(gfx->renderer, c.r, c.g, c.b, c.a);
    SDL_RenderFillRect(gfx->renderer, &rect);
}

void gfx_circ(int cx, int cy, int r, int color) {
    ThermoGraphics* gfx = &g_thermo->gfx;
    ThermoColor c = color_from_index(color);
    
    cx -= gfx->camera_x;
    cy -= gfx->camera_y;
    
    SDL_SetRenderDrawColor(gfx->renderer, c.r, c.g, c.b, c.a);
    
    /* Midpoint circle algorithm */
    int x = r;
    int y = 0;
    int err = 0;
    
    while (x >= y) {
        SDL_RenderDrawPoint(gfx->renderer, cx + x, cy + y);
        SDL_RenderDrawPoint(gfx->renderer, cx + y, cy + x);
        SDL_RenderDrawPoint(gfx->renderer, cx - y, cy + x);
        SDL_RenderDrawPoint(gfx->renderer, cx - x, cy + y);
        SDL_RenderDrawPoint(gfx->renderer, cx - x, cy - y);
        SDL_RenderDrawPoint(gfx->renderer, cx - y, cy - x);
        SDL_RenderDrawPoint(gfx->renderer, cx + y, cy - x);
        SDL_RenderDrawPoint(gfx->renderer, cx + x, cy - y);
        
        y++;
        err += 1 + 2 * y;
        if (2 * (err - x) + 1 > 0) {
            x--;
            err += 1 - 2 * x;
        }
    }
}

void gfx_circfill(int cx, int cy, int r, int color) {
    ThermoGraphics* gfx = &g_thermo->gfx;
    ThermoColor c = color_from_index(color);
    
    cx -= gfx->camera_x;
    cy -= gfx->camera_y;
    
    SDL_SetRenderDrawColor(gfx->renderer, c.r, c.g, c.b, c.a);
    
    /* Draw filled circle using horizontal lines */
    for (int y = -r; y <= r; y++) {
        int dx = (int)sqrt(r * r - y * y);
        SDL_RenderDrawLine(gfx->renderer, cx - dx, cy + y, cx + dx, cy + y);
    }
}

void gfx_spr(int id, int x, int y, int w, int h, bool flip_x, bool flip_y) {
    if (!g_thermo->rom || !g_thermo->rom->sprites.texture) return;
    
    ThermoGraphics* gfx = &g_thermo->gfx;
    ThermoSpriteSheet* sheet = &g_thermo->rom->sprites;
    
    int grid = sheet->grid_size;
    int cols = sheet->width / grid;
    
    /* Calculate source rectangle */
    int sx = (id % cols) * grid;
    int sy = (id / cols) * grid;
    
    SDL_Rect src = {sx, sy, grid * w, grid * h};
    SDL_Rect dst = {
        x - gfx->camera_x,
        y - gfx->camera_y,
        grid * w,
        grid * h
    };
    
    SDL_RendererFlip flip = SDL_FLIP_NONE;
    if (flip_x) flip |= SDL_FLIP_HORIZONTAL;
    if (flip_y) flip |= SDL_FLIP_VERTICAL;
    
    SDL_RenderCopyEx(gfx->renderer, sheet->texture, &src, &dst, 0, NULL, flip);
}

void gfx_sspr(int sx, int sy, int sw, int sh, int dx, int dy, int dw, int dh, bool flip_x, bool flip_y) {
    if (!g_thermo->rom || !g_thermo->rom->sprites.texture) return;
    
    ThermoGraphics* gfx = &g_thermo->gfx;
    
    SDL_Rect src = {sx, sy, sw, sh};
    SDL_Rect dst = {
        dx - gfx->camera_x,
        dy - gfx->camera_y,
        dw, dh
    };
    
    SDL_RendererFlip flip = SDL_FLIP_NONE;
    if (flip_x) flip |= SDL_FLIP_HORIZONTAL;
    if (flip_y) flip |= SDL_FLIP_VERTICAL;
    
    SDL_RenderCopyEx(gfx->renderer, g_thermo->rom->sprites.texture, &src, &dst, 0, NULL, flip);
}

void gfx_print(const char* text, int x, int y, int color) {
    ThermoGraphics* gfx = &g_thermo->gfx;
    ThermoColor c = color_from_index(color);
    
    x -= gfx->camera_x;
    y -= gfx->camera_y;
    
    SDL_SetRenderDrawColor(gfx->renderer, c.r, c.g, c.b, c.a);
    
    int cursor_x = x;
    
    for (const char* p = text; *p; p++) {
        unsigned char ch = (unsigned char)*p;
        
        if (ch == '\n') {
            cursor_x = x;
            y += 8;
            continue;
        }
        
        if (ch < 32 || ch > 126) {
            ch = '?';
        }
        
        int font_idx = (ch - 32) * 8;
        
        /* Draw character from font data */
        for (int row = 0; row < 8; row++) {
            uint8_t bits = FONT_DATA[font_idx + row];
            for (int col = 0; col < 8; col++) {
                if (bits & (0x80 >> col)) {
                    SDL_RenderDrawPoint(gfx->renderer, cursor_x + col, y + row);
                }
            }
        }
        
        cursor_x += 8;
    }
}

void gfx_map(int mx, int my, int dx, int dy, int mw, int mh, const char* layer) {
    /* TODO: Implement tilemap rendering */
}

void gfx_camera(int x, int y) {
    g_thermo->gfx.camera_x = x;
    g_thermo->gfx.camera_y = y;
}

void gfx_clip(int x, int y, int w, int h) {
    ThermoGraphics* gfx = &g_thermo->gfx;
    gfx->clip_enabled = true;
    gfx->clip_rect.x = x;
    gfx->clip_rect.y = y;
    gfx->clip_rect.w = w;
    gfx->clip_rect.h = h;
    SDL_RenderSetClipRect(gfx->renderer, &gfx->clip_rect);
}

void gfx_clip_reset(void) {
    g_thermo->gfx.clip_enabled = false;
    SDL_RenderSetClipRect(g_thermo->gfx.renderer, NULL);
}

void gfx_pal(int c1, int c2) {
    if (c1 >= 0 && c1 < THERMO_PALETTE_SIZE && c2 >= 0 && c2 < THERMO_PALETTE_SIZE) {
        g_thermo->gfx.palette_remap[c1] = g_thermo->gfx.palette[c2];
    }
}

void gfx_pal_reset(void) {
    memcpy(g_thermo->gfx.palette_remap, g_thermo->gfx.palette, sizeof(g_thermo->gfx.palette));
}

SDL_Texture* gfx_load_texture(const char* path) {
    SDL_Surface* surface = IMG_Load(path);
    if (!surface) {
        return NULL;
    }
    
    SDL_Texture* texture = SDL_CreateTextureFromSurface(g_thermo->gfx.renderer, surface);
    SDL_FreeSurface(surface);
    
    return texture;
}
