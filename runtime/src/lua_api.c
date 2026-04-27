/*
 * ThermoConsole Runtime
 * Lua API bindings - exposes all C functions to Lua
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
#include "thermo.h"

/* ─────────────────────────────────────────────────────────────────────────────
 * Graphics API
 * ───────────────────────────────────────────────────────────────────────────── */

static int l_cls(lua_State* L) {
    int color = luaL_optinteger(L, 1, 0);
    gfx_cls(color);
    return 0;
}

static int l_pset(lua_State* L) {
    int x = (int)luaL_checknumber(L, 1);
    int y = (int)luaL_checknumber(L, 2);
    int color = (int)luaL_optnumber(L, 3, 7);
    gfx_pset(x, y, color);
    return 0;
}

static int l_pget(lua_State* L) {
    int x = (int)luaL_checknumber(L, 1);
    int y = (int)luaL_checknumber(L, 2);
    lua_pushinteger(L, gfx_pget(x, y));
    return 1;
}

static int l_line(lua_State* L) {
    int x1 = (int)luaL_checknumber(L, 1);
    int y1 = (int)luaL_checknumber(L, 2);
    int x2 = (int)luaL_checknumber(L, 3);
    int y2 = (int)luaL_checknumber(L, 4);
    int color = (int)luaL_optnumber(L, 5, 7);
    gfx_line(x1, y1, x2, y2, color);
    return 0;
}

static int l_rect(lua_State* L) {
    int x = (int)luaL_checknumber(L, 1);
    int y = (int)luaL_checknumber(L, 2);
    int w = (int)luaL_checknumber(L, 3);
    int h = (int)luaL_checknumber(L, 4);
    int color = (int)luaL_optnumber(L, 5, 7);
    gfx_rect(x, y, w, h, color);
    return 0;
}

static int l_rectfill(lua_State* L) {
    int x = (int)luaL_checknumber(L, 1);
    int y = (int)luaL_checknumber(L, 2);
    int w = (int)luaL_checknumber(L, 3);
    int h = (int)luaL_checknumber(L, 4);
    int color = (int)luaL_optnumber(L, 5, 7);
    gfx_rectfill(x, y, w, h, color);
    return 0;
}

static int l_circ(lua_State* L) {
    int x = (int)luaL_checknumber(L, 1);
    int y = (int)luaL_checknumber(L, 2);
    int r = (int)luaL_checknumber(L, 3);
    int color = (int)luaL_optnumber(L, 4, 7);
    gfx_circ(x, y, r, color);
    return 0;
}

static int l_circfill(lua_State* L) {
    int x = (int)luaL_checknumber(L, 1);
    int y = (int)luaL_checknumber(L, 2);
    int r = (int)luaL_checknumber(L, 3);
    int color = (int)luaL_optnumber(L, 4, 7);
    gfx_circfill(x, y, r, color);
    return 0;
}

static int l_spr(lua_State* L) {
    int id = (int)luaL_checknumber(L, 1);
    int x = (int)luaL_checknumber(L, 2);
    int y = (int)luaL_checknumber(L, 3);
    int w = (int)luaL_optnumber(L, 4, 1);
    int h = (int)luaL_optnumber(L, 5, 1);
    bool flip_x = lua_toboolean(L, 6);
    bool flip_y = lua_toboolean(L, 7);
    gfx_spr(id, x, y, w, h, flip_x, flip_y);
    return 0;
}

static int l_sspr(lua_State* L) {
    int sx = (int)luaL_checknumber(L, 1);
    int sy = (int)luaL_checknumber(L, 2);
    int sw = (int)luaL_checknumber(L, 3);
    int sh = (int)luaL_checknumber(L, 4);
    int dx = (int)luaL_checknumber(L, 5);
    int dy = (int)luaL_checknumber(L, 6);
    int dw = (int)luaL_optnumber(L, 7, sw);
    int dh = (int)luaL_optnumber(L, 8, sh);
    bool flip_x = lua_toboolean(L, 9);
    bool flip_y = lua_toboolean(L, 10);
    gfx_sspr(sx, sy, sw, sh, dx, dy, dw, dh, flip_x, flip_y);
    return 0;
}

static int l_print(lua_State* L) {
    const char* text = luaL_checkstring(L, 1);
    int x = (int)luaL_optnumber(L, 2, 0);
    int y = (int)luaL_optnumber(L, 3, 0);
    int color = (int)luaL_optnumber(L, 4, 7);
    gfx_print(text, x, y, color);
    return 0;
}

static int l_camera(lua_State* L) {
    int x = (int)luaL_optnumber(L, 1, 0);
    int y = (int)luaL_optnumber(L, 2, 0);
    gfx_camera(x, y);
    return 0;
}

static int l_clip(lua_State* L) {
    if (lua_gettop(L) == 0) {
        gfx_clip_reset();
    } else {
        int x = (int)luaL_checknumber(L, 1);
        int y = (int)luaL_checknumber(L, 2);
        int w = (int)luaL_checknumber(L, 3);
        int h = (int)luaL_checknumber(L, 4);
        gfx_clip(x, y, w, h);
    }
    return 0;
}

static int l_pal(lua_State* L) {
    if (lua_gettop(L) == 0) {
        gfx_pal_reset();
    } else {
        int c1 = (int)luaL_checknumber(L, 1);
        int c2 = (int)luaL_checknumber(L, 2);
        gfx_pal(c1, c2);
    }
    return 0;
}

static int l_map(lua_State* L) {
    int mx = (int)luaL_optnumber(L, 1, 0);
    int my = (int)luaL_optnumber(L, 2, 0);
    int dx = (int)luaL_optnumber(L, 3, 0);
    int dy = (int)luaL_optnumber(L, 4, 0);
    int mw = (int)luaL_optnumber(L, 5, 30);
    int mh = (int)luaL_optnumber(L, 6, 40);
    const char* layer = luaL_optstring(L, 7, NULL);
    gfx_map(mx, my, dx, dy, mw, mh, layer);
    return 0;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Input API
 * ───────────────────────────────────────────────────────────────────────────── */

static int l_btn(lua_State* L) {
    int id = luaL_checkinteger(L, 1);
    lua_pushboolean(L, input_btn(id));
    return 1;
}

static int l_btnp(lua_State* L) {
    int id = luaL_checkinteger(L, 1);
    lua_pushboolean(L, input_btnp(id));
    return 1;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Audio API
 * ───────────────────────────────────────────────────────────────────────────── */

/* sfx() accepts either:
 *   sfx(integer_id [, channel [, loop]])  → synthesised slot from sounds.json
 *   sfx("name"     [, channel [, loop]])  → file-based <rom>/sfx/<name>.wav
 * The integer overload is what the SoundEditor produces; the string form
 * is the legacy file-loading path. */
static int l_sfx(lua_State* L) {
    int  channel = luaL_optinteger(L, 2, -1);
    bool loop    = lua_toboolean(L, 3);
    if (lua_type(L, 1) == LUA_TNUMBER) {
        audio_sfx_id((int)lua_tointeger(L, 1), channel, loop);
    } else {
        const char* name = luaL_checkstring(L, 1);
        audio_sfx(name, channel, loop);
    }
    return 0;
}

static int l_music(lua_State* L) {
    const char* name = luaL_checkstring(L, 1);
    bool loop = lua_isnone(L, 2) ? true : lua_toboolean(L, 2);
    audio_music(name, loop);
    return 0;
}

static int l_stop(lua_State* L) {
    int channel = luaL_optinteger(L, 1, -1);
    audio_stop(channel);
    return 0;
}

static int l_volume(lua_State* L) {
    float level = luaL_checknumber(L, 1);
    int channel = luaL_optinteger(L, 2, -1);
    audio_volume(level, channel);
    return 0;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Map API
 * ───────────────────────────────────────────────────────────────────────────── */

static int l_mapload(lua_State* L) {
    const char* name = luaL_checkstring(L, 1);
    lua_pushboolean(L, map_load(name) == 0);
    return 1;
}

static int l_mget(lua_State* L) {
    int x = luaL_checkinteger(L, 1);
    int y = luaL_checkinteger(L, 2);
    const char* layer = luaL_optstring(L, 3, NULL);
    lua_pushinteger(L, map_get(x, y, layer));
    return 1;
}

static int l_mset(lua_State* L) {
    int x = luaL_checkinteger(L, 1);
    int y = luaL_checkinteger(L, 2);
    int tile = luaL_checkinteger(L, 3);
    const char* layer = luaL_optstring(L, 4, NULL);
    map_set(x, y, tile, layer);
    return 0;
}

static int l_fget(lua_State* L) {
    int tile = luaL_checkinteger(L, 1);
    const char* flag = luaL_checkstring(L, 2);
    lua_pushboolean(L, map_fget(tile, flag));
    return 1;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Save/Load API
 * ───────────────────────────────────────────────────────────────────────────── */

static int l_save(lua_State* L) {
    int slot = luaL_checkinteger(L, 1);
    
    /* Convert Lua table to JSON (simple implementation) */
    /* For now, just check if it's a table and convert to string */
    luaL_checktype(L, 2, LUA_TTABLE);
    
    /* Use Lua's JSON encoding (would need cjson or custom) */
    /* For now, return false */
    lua_pushboolean(L, false);
    return 1;
}

static int l_load(lua_State* L) {
    int slot = luaL_checkinteger(L, 1);
    char* data = load_data(slot);
    
    if (data) {
        /* Parse JSON to Lua table (would need cjson or custom) */
        /* For now, return nil */
        free(data);
    }
    
    lua_pushnil(L);
    return 1;
}

static int l_delete(lua_State* L) {
    int slot = luaL_checkinteger(L, 1);
    lua_pushboolean(L, delete_data(slot) == 0);
    return 1;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Math Helpers
 * ───────────────────────────────────────────────────────────────────────────── */

static int l_rnd(lua_State* L) {
    float max = luaL_optnumber(L, 1, 1.0);
    lua_pushnumber(L, thermo_rnd(max));
    return 1;
}

static int l_irnd(lua_State* L) {
    int max = luaL_checkinteger(L, 1);
    lua_pushinteger(L, thermo_irnd(max));
    return 1;
}

static int l_flr(lua_State* L) {
    lua_pushnumber(L, floor(luaL_checknumber(L, 1)));
    return 1;
}

static int l_ceil(lua_State* L) {
    lua_pushnumber(L, ceil(luaL_checknumber(L, 1)));
    return 1;
}

/* PICO-8 style sin/cos (0-1 range, not radians) */
static int l_sin(lua_State* L) {
    double x = luaL_checknumber(L, 1);
    lua_pushnumber(L, -sin(x * 2.0 * M_PI));  /* PICO-8 convention */
    return 1;
}

static int l_cos(lua_State* L) {
    double x = luaL_checknumber(L, 1);
    lua_pushnumber(L, cos(x * 2.0 * M_PI));
    return 1;
}

static int l_atan2(lua_State* L) {
    double y = luaL_checknumber(L, 1);
    double x = luaL_checknumber(L, 2);
    lua_pushnumber(L, atan2(y, x) / (2.0 * M_PI));
    return 1;
}

static int l_sqrt(lua_State* L) {
    lua_pushnumber(L, sqrt(luaL_checknumber(L, 1)));
    return 1;
}

static int l_abs(lua_State* L) {
    lua_pushnumber(L, fabs(luaL_checknumber(L, 1)));
    return 1;
}

static int l_min(lua_State* L) {
    double a = luaL_checknumber(L, 1);
    double b = luaL_checknumber(L, 2);
    lua_pushnumber(L, a < b ? a : b);
    return 1;
}

static int l_max(lua_State* L) {
    double a = luaL_checknumber(L, 1);
    double b = luaL_checknumber(L, 2);
    lua_pushnumber(L, a > b ? a : b);
    return 1;
}

static int l_mid(lua_State* L) {
    double a = luaL_checknumber(L, 1);
    double b = luaL_checknumber(L, 2);
    double c = luaL_checknumber(L, 3);
    
    if (a > b) { double t = a; a = b; b = t; }
    if (b > c) { double t = b; b = c; c = t; }
    if (a > b) { double t = a; a = b; b = t; }
    
    lua_pushnumber(L, b);
    return 1;
}

static int l_sgn(lua_State* L) {
    double x = luaL_checknumber(L, 1);
    lua_pushinteger(L, x > 0 ? 1 : (x < 0 ? -1 : 0));
    return 1;
}

static int l_distance(lua_State* L) {
    double x1 = luaL_checknumber(L, 1);
    double y1 = luaL_checknumber(L, 2);
    double x2 = luaL_checknumber(L, 3);
    double y2 = luaL_checknumber(L, 4);
    double dx = x2 - x1;
    double dy = y2 - y1;
    lua_pushnumber(L, sqrt(dx * dx + dy * dy));
    return 1;
}

static int l_overlap(lua_State* L) {
    double x1 = luaL_checknumber(L, 1);
    double y1 = luaL_checknumber(L, 2);
    double w1 = luaL_checknumber(L, 3);
    double h1 = luaL_checknumber(L, 4);
    double x2 = luaL_checknumber(L, 5);
    double y2 = luaL_checknumber(L, 6);
    double w2 = luaL_checknumber(L, 7);
    double h2 = luaL_checknumber(L, 8);
    
    bool hit = !(x1 + w1 <= x2 || x2 + w2 <= x1 || 
                 y1 + h1 <= y2 || y2 + h2 <= y1);
    lua_pushboolean(L, hit);
    return 1;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * System
 * ───────────────────────────────────────────────────────────────────────────── */

static int l_time(lua_State* L) {
    lua_pushnumber(L, thermo_time());
    return 1;
}

static int l_dt(lua_State* L) {
    lua_pushnumber(L, thermo_dt());
    return 1;
}

static int l_fps(lua_State* L) {
    lua_pushinteger(L, thermo_fps());
    return 1;
}

static int l_srand(lua_State* L) {
    unsigned int seed = luaL_checkinteger(L, 1);
    thermo_srand(seed);
    return 0;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Registration
 * ───────────────────────────────────────────────────────────────────────────── */

static const luaL_Reg thermo_funcs[] = {
    /* Graphics */
    {"cls", l_cls},
    {"pset", l_pset},
    {"pget", l_pget},
    {"line", l_line},
    {"rect", l_rect},
    {"rectfill", l_rectfill},
    {"circ", l_circ},
    {"circfill", l_circfill},
    {"spr", l_spr},
    {"sspr", l_sspr},
    {"print", l_print},
    {"camera", l_camera},
    {"clip", l_clip},
    {"pal", l_pal},
    {"map", l_map},
    
    /* Input */
    {"btn", l_btn},
    {"btnp", l_btnp},
    
    /* Audio */
    {"sfx", l_sfx},
    {"music", l_music},
    {"stop", l_stop},
    {"volume", l_volume},
    
    /* Map */
    {"mapload", l_mapload},
    {"mget", l_mget},
    {"mset", l_mset},
    {"fget", l_fget},
    
    /* Save/Load */
    {"save", l_save},
    {"load", l_load},
    {"delete", l_delete},
    
    /* Math */
    {"rnd", l_rnd},
    {"irnd", l_irnd},
    {"flr", l_flr},
    {"ceil", l_ceil},
    {"sin", l_sin},
    {"cos", l_cos},
    {"atan2", l_atan2},
    {"sqrt", l_sqrt},
    {"abs", l_abs},
    {"min", l_min},
    {"max", l_max},
    {"mid", l_mid},
    {"sgn", l_sgn},
    {"distance", l_distance},
    {"overlap", l_overlap},
    
    /* System */
    {"time", l_time},
    {"dt", l_dt},
    {"fps", l_fps},
    {"srand", l_srand},
    
    {NULL, NULL}
};

int lua_api_register(lua_State* L) {
    /* Register all functions as globals */
    const luaL_Reg* reg = thermo_funcs;
    while (reg->name != NULL) {
        lua_pushcfunction(L, reg->func);
        lua_setglobal(L, reg->name);
        reg++;
    }
    
    /* Add some useful constants */
    lua_pushinteger(L, THERMO_SCREEN_WIDTH);
    lua_setglobal(L, "SCREEN_WIDTH");
    
    lua_pushinteger(L, THERMO_SCREEN_HEIGHT);
    lua_setglobal(L, "SCREEN_HEIGHT");
    
    /* Button constants */
    lua_pushinteger(L, THERMO_BTN_UP);
    lua_setglobal(L, "BTN_UP");
    lua_pushinteger(L, THERMO_BTN_DOWN);
    lua_setglobal(L, "BTN_DOWN");
    lua_pushinteger(L, THERMO_BTN_LEFT);
    lua_setglobal(L, "BTN_LEFT");
    lua_pushinteger(L, THERMO_BTN_RIGHT);
    lua_setglobal(L, "BTN_RIGHT");
    lua_pushinteger(L, THERMO_BTN_A);
    lua_setglobal(L, "BTN_A");
    lua_pushinteger(L, THERMO_BTN_B);
    lua_setglobal(L, "BTN_B");
    lua_pushinteger(L, THERMO_BTN_X);
    lua_setglobal(L, "BTN_X");
    lua_pushinteger(L, THERMO_BTN_Y);
    lua_setglobal(L, "BTN_Y");
    lua_pushinteger(L, THERMO_BTN_START);
    lua_setglobal(L, "BTN_START");
    lua_pushinteger(L, THERMO_BTN_SELECT);
    lua_setglobal(L, "BTN_SELECT");
    
    return 0;
}
