/*
 * ThermoConsole System Menu API
 * 
 * Provides Lua bindings for system-level operations needed by the menu:
 * - Game scanning and launching
 * - Settings persistence
 * - USB mode control
 * - System information
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>

#include "thermo.h"

/* ═══════════════════════════════════════════════════════════════════════════════
 * Configuration
 * ═══════════════════════════════════════════════════════════════════════════════ */

#define GAMES_DIR "/home/pi/games"
#define SETTINGS_FILE "/home/pi/.thermoconsole/settings.json"
#define USB_GADGET_SCRIPT "/opt/thermoconsole/scripts/usb-gadget.sh"

/* ═══════════════════════════════════════════════════════════════════════════════
 * Game Scanner
 * ═══════════════════════════════════════════════════════════════════════════════ */

static int is_valid_game_dir(const char* path) {
    char manifest_path[512];
    char main_path[512];
    
    snprintf(manifest_path, sizeof(manifest_path), "%s/manifest.json", path);
    snprintf(main_path, sizeof(main_path), "%s/main.lua", path);
    
    struct stat st;
    return (stat(manifest_path, &st) == 0 || stat(main_path, &st) == 0);
}

static int is_tcr_file(const char* name) {
    size_t len = strlen(name);
    return len > 4 && strcmp(name + len - 4, ".tcr") == 0;
}

/*
 * _get_games() -> table
 * Returns a table of available games
 */
static int l_get_games(lua_State* L) {
    DIR* dir = opendir(GAMES_DIR);
    if (!dir) {
        lua_newtable(L);
        return 1;
    }
    
    lua_newtable(L);
    int idx = 1;
    
    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
        /* Skip hidden files and special entries */
        if (entry->d_name[0] == '.') continue;
        if (strcmp(entry->d_name, "current") == 0) continue;
        if (strcmp(entry->d_name, "README.txt") == 0) continue;
        if (strcmp(entry->d_name, "CURRENT.txt") == 0) continue;
        
        char full_path[512];
        snprintf(full_path, sizeof(full_path), "%s/%s", GAMES_DIR, entry->d_name);
        
        struct stat st;
        if (stat(full_path, &st) != 0) continue;
        
        int valid = 0;
        
        if (S_ISDIR(st.st_mode)) {
            valid = is_valid_game_dir(full_path);
        } else if (S_ISREG(st.st_mode)) {
            valid = is_tcr_file(entry->d_name);
        }
        
        if (valid) {
            lua_newtable(L);
            
            /* Name (strip .tcr extension if present) */
            char name[256];
            strncpy(name, entry->d_name, sizeof(name) - 1);
            char* ext = strstr(name, ".tcr");
            if (ext) *ext = '\0';
            
            lua_pushstring(L, name);
            lua_setfield(L, -2, "name");
            
            lua_pushstring(L, entry->d_name);
            lua_setfield(L, -2, "path");
            
            /* Try to read manifest for more info */
            char manifest_path[512];
            snprintf(manifest_path, sizeof(manifest_path), "%s/manifest.json", full_path);
            
            FILE* mf = fopen(manifest_path, "r");
            if (mf) {
                /* Simple JSON parsing for author/version */
                char line[256];
                while (fgets(line, sizeof(line), mf)) {
                    char* author = strstr(line, "\"author\"");
                    if (author) {
                        char* start = strchr(author + 8, '"');
                        if (start) {
                            start++;
                            char* end = strchr(start, '"');
                            if (end) {
                                *end = '\0';
                                lua_pushstring(L, start);
                                lua_setfield(L, -2, "author");
                            }
                        }
                    }
                    
                    char* version = strstr(line, "\"version\"");
                    if (version) {
                        char* start = strchr(version + 9, '"');
                        if (start) {
                            start++;
                            char* end = strchr(start, '"');
                            if (end) {
                                *end = '\0';
                                lua_pushstring(L, start);
                                lua_setfield(L, -2, "version");
                            }
                        }
                    }
                }
                fclose(mf);
            }
            
            lua_rawseti(L, -2, idx++);
        }
    }
    
    closedir(dir);
    return 1;
}

/* ═══════════════════════════════════════════════════════════════════════════════
 * Game Launcher
 * ═══════════════════════════════════════════════════════════════════════════════ */

/* Global flag for game launch request */
static char g_launch_game[512] = {0};

/*
 * _launch_game(path) -> nil
 * Requests the runtime to launch a different game
 */
static int l_launch_game(lua_State* L) {
    const char* path = luaL_checkstring(L, 1);
    
    /* Build full path */
    char full_path[512];
    
    /* Check if it's a .tcr file or directory */
    if (strchr(path, '/') || strchr(path, '.')) {
        /* Already a path */
        snprintf(full_path, sizeof(full_path), "%s", path);
    } else {
        /* Just a name - try directory first, then .tcr */
        snprintf(full_path, sizeof(full_path), "%s/%s", GAMES_DIR, path);
        struct stat st;
        if (stat(full_path, &st) != 0 || !S_ISDIR(st.st_mode)) {
            snprintf(full_path, sizeof(full_path), "%s/%s.tcr", GAMES_DIR, path);
        }
    }
    
    /* Update the current game symlink */
    char current_path[512];
    snprintf(current_path, sizeof(current_path), "%s/current", GAMES_DIR);
    unlink(current_path);
    symlink(full_path, current_path);
    
    /* Also update CURRENT.txt for USB mode */
    char current_txt[512];
    snprintf(current_txt, sizeof(current_txt), "%s/CURRENT.txt", GAMES_DIR);
    FILE* f = fopen(current_txt, "w");
    if (f) {
        fprintf(f, "%s\n", path);
        fclose(f);
    }
    
    /* Set launch flag - runtime will check this and restart */
    strncpy(g_launch_game, full_path, sizeof(g_launch_game) - 1);
    
    return 0;
}

/*
 * Check if a game launch was requested
 */
const char* system_get_launch_request(void) {
    if (g_launch_game[0] != '\0') {
        return g_launch_game;
    }
    return NULL;
}

void system_clear_launch_request(void) {
    g_launch_game[0] = '\0';
}

/* ═══════════════════════════════════════════════════════════════════════════════
 * Settings
 * ═══════════════════════════════════════════════════════════════════════════════ */

/*
 * _load_settings() -> table or nil
 * Loads saved settings
 */
static int l_load_settings(lua_State* L) {
    FILE* f = fopen(SETTINGS_FILE, "r");
    if (!f) {
        lua_pushnil(L);
        return 1;
    }
    
    lua_newtable(L);
    
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        /* Simple key=value parsing */
        char* eq = strchr(line, '=');
        if (eq) {
            *eq = '\0';
            char* key = line;
            char* value = eq + 1;
            
            /* Trim whitespace */
            while (*key == ' ' || *key == '\t') key++;
            while (*value == ' ' || *value == '\t') value++;
            char* end = value + strlen(value) - 1;
            while (end > value && (*end == '\n' || *end == '\r' || *end == ' ')) {
                *end = '\0';
                end--;
            }
            
            /* Try to parse as number or boolean */
            if (strcmp(value, "true") == 0) {
                lua_pushboolean(L, 1);
            } else if (strcmp(value, "false") == 0) {
                lua_pushboolean(L, 0);
            } else {
                char* endptr;
                long num = strtol(value, &endptr, 10);
                if (*endptr == '\0') {
                    lua_pushinteger(L, num);
                } else {
                    lua_pushstring(L, value);
                }
            }
            lua_setfield(L, -2, key);
        }
    }
    
    fclose(f);
    return 1;
}

/*
 * _apply_setting(key, value) -> nil
 * Applies and saves a setting
 */
static int l_apply_setting(lua_State* L) {
    const char* key = luaL_checkstring(L, 1);
    
    /* Determine value type and get it */
    char value_str[64];
    if (lua_isboolean(L, 2)) {
        snprintf(value_str, sizeof(value_str), "%s", lua_toboolean(L, 2) ? "true" : "false");
    } else if (lua_isnumber(L, 2)) {
        snprintf(value_str, sizeof(value_str), "%d", (int)lua_tointeger(L, 2));
    } else {
        snprintf(value_str, sizeof(value_str), "%s", lua_tostring(L, 2));
    }
    
    /* Apply setting immediately */
    if (strcmp(key, "brightness") == 0) {
        /* Could write to /sys/class/backlight if available */
    } else if (strcmp(key, "volume") == 0) {
        int vol = atoi(value_str);
        /* Set mixer volume */
        char cmd[128];
        snprintf(cmd, sizeof(cmd), "amixer set Master %d%% 2>/dev/null", vol);
        system(cmd);
    } else if (strcmp(key, "show_fps") == 0) {
        /* Toggle FPS display - g_show_fps is defined in main.c */
        extern int g_show_fps;
        g_show_fps = (strcmp(value_str, "true") == 0);
    }
    
    /* Read existing settings */
    FILE* rf = fopen(SETTINGS_FILE, "r");
    char lines[32][256];
    int line_count = 0;
    int found = 0;
    
    if (rf) {
        while (line_count < 32 && fgets(lines[line_count], sizeof(lines[0]), rf)) {
            /* Check if this is the setting we're updating */
            if (strncmp(lines[line_count], key, strlen(key)) == 0 &&
                lines[line_count][strlen(key)] == '=') {
                snprintf(lines[line_count], sizeof(lines[0]), "%s=%s\n", key, value_str);
                found = 1;
            }
            line_count++;
        }
        fclose(rf);
    }
    
    /* Add new setting if not found */
    if (!found && line_count < 32) {
        snprintf(lines[line_count], sizeof(lines[0]), "%s=%s\n", key, value_str);
        line_count++;
    }
    
    /* Write back */
    FILE* wf = fopen(SETTINGS_FILE, "w");
    if (wf) {
        for (int i = 0; i < line_count; i++) {
            fputs(lines[i], wf);
        }
        fclose(wf);
    }
    
    return 0;
}

/* ═══════════════════════════════════════════════════════════════════════════════
 * USB Mode
 * ═══════════════════════════════════════════════════════════════════════════════ */

/*
 * _toggle_usb(enable) -> boolean
 * Toggles USB storage mode
 */
static int l_toggle_usb(lua_State* L) {
    int enable = lua_toboolean(L, 1);
    
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "%s %s 2>/dev/null", 
             USB_GADGET_SCRIPT, 
             enable ? "enable" : "disable");
    
    int result = system(cmd);
    lua_pushboolean(L, result == 0);
    return 1;
}

/*
 * _get_usb_status() -> boolean
 * Returns whether USB mode is active
 */
static int l_get_usb_status(lua_State* L) {
    /* Check if USB gadget is active */
    struct stat st;
    int active = (stat("/sys/kernel/config/usb_gadget/thermoconsole/UDC", &st) == 0);
    
    if (active) {
        /* Check if UDC file has content */
        FILE* f = fopen("/sys/kernel/config/usb_gadget/thermoconsole/UDC", "r");
        if (f) {
            char buf[64];
            active = (fgets(buf, sizeof(buf), f) != NULL && buf[0] != '\0' && buf[0] != '\n');
            fclose(f);
        }
    }
    
    lua_pushboolean(L, active);
    return 1;
}

/* ═══════════════════════════════════════════════════════════════════════════════
 * System Information
 * ═══════════════════════════════════════════════════════════════════════════════ */

/*
 * _get_system_info() -> table
 * Returns system information
 */
static int l_get_system_info(lua_State* L) {
    lua_newtable(L);
    
    /* CPU Temperature */
    FILE* f = fopen("/sys/class/thermal/thermal_zone0/temp", "r");
    if (f) {
        int temp;
        if (fscanf(f, "%d", &temp) == 1) {
            lua_pushnumber(L, temp / 1000.0);
            lua_setfield(L, -2, "cpu_temp");
        }
        fclose(f);
    }
    
    /* Memory info */
    f = fopen("/proc/meminfo", "r");
    if (f) {
        char line[128];
        while (fgets(line, sizeof(line), f)) {
            if (strncmp(line, "MemTotal:", 9) == 0) {
                long total;
                sscanf(line + 9, "%ld", &total);
                lua_pushinteger(L, total / 1024);  /* MB */
                lua_setfield(L, -2, "mem_total");
            } else if (strncmp(line, "MemAvailable:", 13) == 0) {
                long avail;
                sscanf(line + 13, "%ld", &avail);
                lua_pushinteger(L, avail / 1024);  /* MB */
                lua_setfield(L, -2, "mem_free");
            }
        }
        fclose(f);
    }
    
    /* Hostname */
    char hostname[64];
    if (gethostname(hostname, sizeof(hostname)) == 0) {
        lua_pushstring(L, hostname);
        lua_setfield(L, -2, "hostname");
    }
    
    /* Version */
    lua_pushstring(L, "1.0.0");
    lua_setfield(L, -2, "version");
    
    return 1;
}

/* ═══════════════════════════════════════════════════════════════════════════════
 * Registration
 * ═══════════════════════════════════════════════════════════════════════════════ */

static const luaL_Reg system_funcs[] = {
    /* Game management */
    {"_get_games", l_get_games},
    {"_launch_game", l_launch_game},
    
    /* Settings */
    {"_load_settings", l_load_settings},
    {"_apply_setting", l_apply_setting},
    
    /* USB mode */
    {"_toggle_usb", l_toggle_usb},
    {"_get_usb_status", l_get_usb_status},
    
    /* System info */
    {"_get_system_info", l_get_system_info},
    
    {NULL, NULL}
};

void system_api_register(lua_State* L) {
    /* Register all functions as globals */
    for (const luaL_Reg* func = system_funcs; func->name != NULL; func++) {
        lua_pushcfunction(L, func->func);
        lua_setglobal(L, func->name);
    }
}
