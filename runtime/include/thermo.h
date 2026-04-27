/*
 * ThermoConsole Runtime
 * Main header file
 */

#ifndef THERMO_H
#define THERMO_H

#include <stdint.h>
#include <stdbool.h>
#include <SDL.h>
#include <SDL_mixer.h>
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>

/* ─────────────────────────────────────────────────────────────────────────────
 * Version & Constants
 * ───────────────────────────────────────────────────────────────────────────── */

#define THERMO_VERSION_MAJOR 1
#define THERMO_VERSION_MINOR 0
#define THERMO_VERSION_PATCH 0
#define THERMO_VERSION_STRING "1.0.0"

/* Display */
#define THERMO_SCREEN_WIDTH  480
#define THERMO_SCREEN_HEIGHT 640
#define THERMO_TARGET_FPS    60
#define THERMO_FRAME_TIME    (1000.0 / THERMO_TARGET_FPS)

/* Sprites */
#define THERMO_SPRITE_SHEET_SIZE 256
#define THERMO_DEFAULT_GRID_SIZE 16
#define THERMO_MAX_SPRITES       256

/* Audio */
#define THERMO_AUDIO_CHANNELS    4
#define THERMO_MUSIC_CHANNELS    1
#define THERMO_SAMPLE_RATE       44100

/* Input */
#define THERMO_BTN_UP     0
#define THERMO_BTN_DOWN   1
#define THERMO_BTN_LEFT   2
#define THERMO_BTN_RIGHT  3
#define THERMO_BTN_A      4
#define THERMO_BTN_B      5
#define THERMO_BTN_X      6
#define THERMO_BTN_Y      7
#define THERMO_BTN_START  8
#define THERMO_BTN_SELECT 9
#define THERMO_BTN_COUNT  10

/* Colors (default PICO-8 style palette) */
#define THERMO_PALETTE_SIZE 16

/* Save data */
#define THERMO_SAVE_SLOTS    4
#define THERMO_SAVE_MAX_SIZE (64 * 1024)

/* ─────────────────────────────────────────────────────────────────────────────
 * Types
 * ───────────────────────────────────────────────────────────────────────────── */

typedef struct {
    uint8_t r, g, b, a;
} ThermoColor;

typedef struct {
    int width;
    int height;
    int grid_size;
    SDL_Texture* texture;
    uint32_t transparent_color;
} ThermoSpriteSheet;

typedef struct {
    int width;
    int height;
    int tile_count;
    int* layers[8];      /* up to 8 layers */
    int layer_count;
    char* layer_names[8];
} ThermoMap;

typedef struct {
    char name[64];
    char author[64];
    char version[16];
    char entry[64];
    int display_width;
    int display_height;
    char orientation[16];
    int sprite_grid_size;
    char sprites_file[64];
    char tiles_file[64];
} ThermoManifest;

typedef struct {
    char* base_path;           /* extracted ROM temp directory */
    ThermoManifest manifest;
    ThermoSpriteSheet sprites;
    ThermoSpriteSheet tiles;
    ThermoMap* current_map;
} ThermoROM;

typedef struct {
    bool held;        /* currently held */
    bool pressed;     /* just pressed this frame */
    bool released;    /* just released this frame */
} ThermoButton;

typedef struct {
    ThermoButton buttons[THERMO_BTN_COUNT];
} ThermoInput;

typedef struct {
    SDL_Window* window;
    SDL_Renderer* renderer;
    SDL_Texture* canvas;       /* render target at native res */
    int window_width;
    int window_height;
    int scale;
    ThermoColor palette[THERMO_PALETTE_SIZE];
    ThermoColor palette_remap[THERMO_PALETTE_SIZE]; /* for pal() */
    int camera_x;
    int camera_y;
    SDL_Rect clip_rect;
    bool clip_enabled;
} ThermoGraphics;

typedef struct {
    Mix_Chunk* sfx[128];       /* loaded sound effects */
    int sfx_count;
    Mix_Music* music;          /* current music */
    float master_volume;
    float channel_volume[THERMO_AUDIO_CHANNELS];
} ThermoAudio;

typedef struct {
    lua_State* L;
    bool has_init;
    bool has_update;
    bool has_draw;
} ThermoScript;

typedef struct {
    bool running;
    bool paused;
    uint64_t frame_count;
    double time_elapsed;
    double delta_time;
    uint64_t last_frame_time;
    int current_fps;
    int fps_counter;
    uint64_t fps_timer;
} ThermoState;

/* Main engine context */
typedef struct {
    ThermoState state;
    ThermoGraphics gfx;
    ThermoInput input;
    ThermoAudio audio;
    ThermoScript script;
    ThermoROM* rom;
} ThermoEngine;

/* Global engine instance */
extern ThermoEngine* g_thermo;

/* ─────────────────────────────────────────────────────────────────────────────
 * Core Functions
 * ───────────────────────────────────────────────────────────────────────────── */

/* Engine lifecycle */
int thermo_init(const char* rom_path);
void thermo_run(void);
void thermo_shutdown(void);

/* ROM loading */
ThermoROM* rom_load(const char* path);
void rom_free(ThermoROM* rom);

/* Graphics */
int gfx_init(void);
void gfx_shutdown(void);
void gfx_begin_frame(void);
void gfx_end_frame(void);
void gfx_cls(int color);
void gfx_pset(int x, int y, int color);
int gfx_pget(int x, int y);
void gfx_line(int x1, int y1, int x2, int y2, int color);
void gfx_rect(int x, int y, int w, int h, int color);
void gfx_rectfill(int x, int y, int w, int h, int color);
void gfx_circ(int x, int y, int r, int color);
void gfx_circfill(int x, int y, int r, int color);
void gfx_spr(int id, int x, int y, int w, int h, bool flip_x, bool flip_y);
void gfx_sspr(int sx, int sy, int sw, int sh, int dx, int dy, int dw, int dh, bool flip_x, bool flip_y);
void gfx_print(const char* text, int x, int y, int color);
void gfx_map(int mx, int my, int dx, int dy, int mw, int mh, const char* layer);
void gfx_camera(int x, int y);
void gfx_clip(int x, int y, int w, int h);
void gfx_clip_reset(void);
void gfx_pal(int c1, int c2);
void gfx_pal_reset(void);
SDL_Texture* gfx_load_texture(const char* path);

/* Input */
void input_init(void);
void input_shutdown(void);
void input_update(void);
bool input_btn(int id);
bool input_btnp(int id);
bool input_btnr(int id);
uint16_t input_get_state(void);
bool input_any_pressed(void);
const char* input_button_name(int id);

/* Audio */
int audio_init(void);
void audio_shutdown(void);
void audio_sfx(const char* name, int channel, bool loop);
/* Synthesised SFX from <rom>/sounds.json (PICO-8-style). Loaded by main.c
 * after the ROM mounts. Callable from Lua via sfx(int_id, ...). */
void audio_load_chiptune(const char* rom_base_path);
void audio_sfx_id(int sfx_id, int channel, bool loop);
void audio_music(const char* name, bool loop);
void audio_stop(int channel);
void audio_volume(float level, int channel);

/* Map */
int map_load(const char* name);
void map_free(ThermoMap* map);
int map_get(int x, int y, const char* layer);
void map_set(int x, int y, int tile, const char* layer);
bool map_fget(int tile, const char* flag);

/* Save/Load */
int save_data(int slot, const char* json_data);
char* load_data(int slot);
int delete_data(int slot);

/* Lua API */
int lua_api_register(lua_State* L);

/* Script */
int script_init(const char* entry_path);
void script_shutdown(void);
void script_call_init(void);
void script_call_update(void);
void script_call_draw(void);

/* Utility */
uint64_t get_time_ms(void);
double thermo_time(void);
double thermo_dt(void);
int thermo_fps(void);
float thermo_rnd(float max);
int thermo_irnd(int max);
void thermo_srand(unsigned int seed);
ThermoColor color_from_index(int index);
ThermoColor color_from_hex(uint32_t hex);

/* ─────────────────────────────────────────────────────────────────────────────
 * Default Palette (PICO-8 colors)
 * ───────────────────────────────────────────────────────────────────────────── */

/* Renamed from DEFAULT_PALETTE — Windows' wingdi.h #defines that to the
 * integer 15 for a GDI stock-object constant, which made this line parse
 * as  static const ThermoColor 15[...] = {...}  on MSVC. */
static const ThermoColor THERMO_DEFAULT_PALETTE[THERMO_PALETTE_SIZE] = {
    {0x00, 0x00, 0x00, 0xFF},  /* 0  - Black */
    {0x1D, 0x2B, 0x53, 0xFF},  /* 1  - Dark Blue */
    {0x7E, 0x25, 0x53, 0xFF},  /* 2  - Dark Purple */
    {0x00, 0x87, 0x51, 0xFF},  /* 3  - Dark Green */
    {0xAB, 0x52, 0x36, 0xFF},  /* 4  - Brown */
    {0x5F, 0x57, 0x4F, 0xFF},  /* 5  - Dark Gray */
    {0xC2, 0xC3, 0xC7, 0xFF},  /* 6  - Light Gray */
    {0xFF, 0xF1, 0xE8, 0xFF},  /* 7  - White */
    {0xFF, 0x00, 0x4D, 0xFF},  /* 8  - Red */
    {0xFF, 0xA3, 0x00, 0xFF},  /* 9  - Orange */
    {0xFF, 0xEC, 0x27, 0xFF},  /* 10 - Yellow */
    {0x00, 0xE4, 0x36, 0xFF},  /* 11 - Green */
    {0x29, 0xAD, 0xFF, 0xFF},  /* 12 - Blue */
    {0x83, 0x76, 0x9C, 0xFF},  /* 13 - Indigo */
    {0xFF, 0x77, 0xA8, 0xFF},  /* 14 - Pink */
    {0xFF, 0xCC, 0xAA, 0xFF},  /* 15 - Peach */
};

#endif /* THERMO_H */
