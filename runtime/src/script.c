/*
 * ThermoConsole Runtime
 * Script module - Lua state management and callback execution
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
#include "thermo.h"

/* ─────────────────────────────────────────────────────────────────────────────
 * Error Handling
 * ───────────────────────────────────────────────────────────────────────────── */

static void print_lua_error(lua_State* L, const char* context) {
    const char* msg = lua_tostring(L, -1);
    fprintf(stderr, "\n┌─────────────────────────────────────────────────────────┐\n");
    fprintf(stderr, "│ LUA ERROR: %-46s │\n", context);
    fprintf(stderr, "├─────────────────────────────────────────────────────────┤\n");
    fprintf(stderr, "│ %s\n", msg ? msg : "(no message)");
    fprintf(stderr, "└─────────────────────────────────────────────────────────┘\n\n");
    lua_pop(L, 1);
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Initialization
 * ───────────────────────────────────────────────────────────────────────────── */

int script_init(const char* entry_path) {
    ThermoScript* script = &g_thermo->script;
    
    /* Create Lua state */
    script->L = luaL_newstate();
    if (!script->L) {
        fprintf(stderr, "ERROR: Failed to create Lua state\n");
        return -1;
    }
    
    /* Open standard libraries */
    luaL_openlibs(script->L);
    
    /* Register ThermoConsole API */
    lua_api_register(script->L);
    
    /* Set package path to include ROM directory.
     *
     * Use Lua long-bracket strings ([[ ... ]]) so backslashes in absolute
     * Windows paths (e.g. C:\Users\...) aren't interpreted as Lua escape
     * sequences (\U, \k, etc. are invalid). Long-bracket strings only
     * "fail" if the path itself contains "]]", which is effectively
     * impossible for a filesystem path. */
    if (g_thermo->rom && g_thermo->rom->base_path) {
        char path_code[1024];
        snprintf(path_code, sizeof(path_code),
            "package.path = package.path .. [[;%s/?.lua;%s/scripts/?.lua]]",
            g_thermo->rom->base_path,
            g_thermo->rom->base_path
        );

        if (luaL_dostring(script->L, path_code) != LUA_OK) {
            print_lua_error(script->L, "setting package.path");
        }
    }
    
    /* Load the entry script */
    if (luaL_loadfile(script->L, entry_path) != LUA_OK) {
        print_lua_error(script->L, "loading script");
        return -1;
    }
    
    /* Execute the script (defines functions) */
    if (lua_pcall(script->L, 0, 0, 0) != LUA_OK) {
        print_lua_error(script->L, "executing script");
        return -1;
    }
    
    /* Check for required callbacks */
    lua_getglobal(script->L, "_init");
    script->has_init = lua_isfunction(script->L, -1);
    lua_pop(script->L, 1);
    
    lua_getglobal(script->L, "_update");
    script->has_update = lua_isfunction(script->L, -1);
    lua_pop(script->L, 1);
    
    lua_getglobal(script->L, "_draw");
    script->has_draw = lua_isfunction(script->L, -1);
    lua_pop(script->L, 1);
    
    if (!script->has_init) {
        fprintf(stderr, "WARN: Script missing _init() function\n");
    }
    if (!script->has_update) {
        fprintf(stderr, "WARN: Script missing _update() function\n");
    }
    if (!script->has_draw) {
        fprintf(stderr, "WARN: Script missing _draw() function\n");
    }
    
    return 0;
}

void script_shutdown(void) {
    ThermoScript* script = &g_thermo->script;
    
    if (script->L) {
        lua_close(script->L);
        script->L = NULL;
    }
    
    script->has_init = false;
    script->has_update = false;
    script->has_draw = false;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Callback Execution
 * ───────────────────────────────────────────────────────────────────────────── */

void script_call_init(void) {
    ThermoScript* script = &g_thermo->script;
    
    if (!script->L || !script->has_init) return;
    
    lua_getglobal(script->L, "_init");
    
    if (lua_pcall(script->L, 0, 0, 0) != LUA_OK) {
        print_lua_error(script->L, "_init()");
    }
}

void script_call_update(void) {
    ThermoScript* script = &g_thermo->script;
    
    if (!script->L || !script->has_update) return;
    
    lua_getglobal(script->L, "_update");
    
    if (lua_pcall(script->L, 0, 0, 0) != LUA_OK) {
        print_lua_error(script->L, "_update()");
    }
}

void script_call_draw(void) {
    ThermoScript* script = &g_thermo->script;
    
    if (!script->L || !script->has_draw) return;
    
    lua_getglobal(script->L, "_draw");
    
    if (lua_pcall(script->L, 0, 0, 0) != LUA_OK) {
        print_lua_error(script->L, "_draw()");
    }
}
