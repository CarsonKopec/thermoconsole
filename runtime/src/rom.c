/*
 * ThermoConsole Runtime
 * ROM loader - extracts and parses .tcr files
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>

#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#define mkdir(path, mode) _mkdir(path)
#define PATH_SEP '\\'
#else
#include <unistd.h>
#include <dirent.h>
#define PATH_SEP '/'
#endif

#include "thermo.h"

/* We'll use miniz for ZIP extraction (single header library) */
/* For simplicity, we'll shell out to unzip or use a simple implementation */

/* ─────────────────────────────────────────────────────────────────────────────
 * Simple JSON Parser (minimal, for manifest.json)
 * ───────────────────────────────────────────────────────────────────────────── */

static void skip_whitespace(const char** p) {
    while (**p && (**p == ' ' || **p == '\t' || **p == '\n' || **p == '\r')) {
        (*p)++;
    }
}

static char* parse_string(const char** p) {
    if (**p != '"') return NULL;
    (*p)++;
    
    const char* start = *p;
    while (**p && **p != '"') {
        if (**p == '\\') (*p)++;
        if (**p) (*p)++;
    }
    
    int len = *p - start;
    char* result = (char*)malloc(len + 1);
    strncpy(result, start, len);
    result[len] = '\0';
    
    if (**p == '"') (*p)++;
    
    return result;
}

static int parse_int(const char** p) {
    int result = 0;
    int sign = 1;
    
    if (**p == '-') {
        sign = -1;
        (*p)++;
    }
    
    while (**p >= '0' && **p <= '9') {
        result = result * 10 + (**p - '0');
        (*p)++;
    }
    
    return result * sign;
}

static int parse_manifest(const char* json, ThermoManifest* manifest) {
    /* Set defaults */
    memset(manifest, 0, sizeof(ThermoManifest));
    manifest->display_width = THERMO_SCREEN_WIDTH;
    manifest->display_height = THERMO_SCREEN_HEIGHT;
    manifest->sprite_grid_size = THERMO_DEFAULT_GRID_SIZE;
    strcpy(manifest->sprites_file, "sprites.png");
    strcpy(manifest->tiles_file, "tiles.png");
    strcpy(manifest->entry, "main.lua");
    strcpy(manifest->orientation, "portrait");
    
    const char* p = json;
    
    skip_whitespace(&p);
    if (*p != '{') return -1;
    p++;
    
    while (*p && *p != '}') {
        skip_whitespace(&p);
        
        /* Parse key */
        char* key = parse_string(&p);
        if (!key) break;
        
        skip_whitespace(&p);
        if (*p != ':') {
            free(key);
            break;
        }
        p++;
        skip_whitespace(&p);
        
        /* Parse value based on key */
        if (strcmp(key, "name") == 0) {
            char* val = parse_string(&p);
            if (val) {
                strncpy(manifest->name, val, 63);
                free(val);
            }
        } else if (strcmp(key, "author") == 0) {
            char* val = parse_string(&p);
            if (val) {
                strncpy(manifest->author, val, 63);
                free(val);
            }
        } else if (strcmp(key, "version") == 0) {
            char* val = parse_string(&p);
            if (val) {
                strncpy(manifest->version, val, 15);
                free(val);
            }
        } else if (strcmp(key, "entry") == 0) {
            char* val = parse_string(&p);
            if (val) {
                strncpy(manifest->entry, val, 63);
                free(val);
            }
        } else if (strcmp(key, "display") == 0) {
            /* Parse nested object */
            skip_whitespace(&p);
            if (*p == '{') {
                p++;
                while (*p && *p != '}') {
                    skip_whitespace(&p);
                    char* subkey = parse_string(&p);
                    if (!subkey) break;
                    
                    skip_whitespace(&p);
                    if (*p == ':') p++;
                    skip_whitespace(&p);
                    
                    if (strcmp(subkey, "width") == 0) {
                        manifest->display_width = parse_int(&p);
                    } else if (strcmp(subkey, "height") == 0) {
                        manifest->display_height = parse_int(&p);
                    } else if (strcmp(subkey, "orientation") == 0) {
                        char* val = parse_string(&p);
                        if (val) {
                            strncpy(manifest->orientation, val, 15);
                            free(val);
                        }
                    } else {
                        /* Skip value */
                        while (*p && *p != ',' && *p != '}') p++;
                    }
                    
                    free(subkey);
                    skip_whitespace(&p);
                    if (*p == ',') p++;
                }
                if (*p == '}') p++;
            }
        } else if (strcmp(key, "sprites") == 0) {
            /* Parse nested object */
            skip_whitespace(&p);
            if (*p == '{') {
                p++;
                while (*p && *p != '}') {
                    skip_whitespace(&p);
                    char* subkey = parse_string(&p);
                    if (!subkey) break;
                    
                    skip_whitespace(&p);
                    if (*p == ':') p++;
                    skip_whitespace(&p);
                    
                    if (strcmp(subkey, "file") == 0) {
                        char* val = parse_string(&p);
                        if (val) {
                            strncpy(manifest->sprites_file, val, 63);
                            free(val);
                        }
                    } else if (strcmp(subkey, "grid_size") == 0) {
                        manifest->sprite_grid_size = parse_int(&p);
                    } else {
                        /* Skip value */
                        while (*p && *p != ',' && *p != '}') p++;
                    }
                    
                    free(subkey);
                    skip_whitespace(&p);
                    if (*p == ',') p++;
                }
                if (*p == '}') p++;
            }
        } else {
            /* Skip unknown value */
            if (*p == '"') {
                char* val = parse_string(&p);
                if (val) free(val);
            } else if (*p == '{') {
                int depth = 1;
                p++;
                while (*p && depth > 0) {
                    if (*p == '{') depth++;
                    else if (*p == '}') depth--;
                    p++;
                }
            } else if (*p == '[') {
                int depth = 1;
                p++;
                while (*p && depth > 0) {
                    if (*p == '[') depth++;
                    else if (*p == ']') depth--;
                    p++;
                }
            } else {
                while (*p && *p != ',' && *p != '}') p++;
            }
        }
        
        free(key);
        skip_whitespace(&p);
        if (*p == ',') p++;
    }
    
    return 0;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * File Utilities
 * ───────────────────────────────────────────────────────────────────────────── */

static char* read_file(const char* path, size_t* out_size) {
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;
    
    fseek(f, 0, SEEK_END);
    size_t size = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    char* data = (char*)malloc(size + 1);
    if (!data) {
        fclose(f);
        return NULL;
    }
    
    fread(data, 1, size, f);
    data[size] = '\0';
    fclose(f);
    
    if (out_size) *out_size = size;
    return data;
}

static int is_directory(const char* path) {
    struct stat st;
    if (stat(path, &st) != 0) return 0;
    return S_ISDIR(st.st_mode);
}

static int is_file(const char* path) {
    struct stat st;
    if (stat(path, &st) != 0) return 0;
    return S_ISREG(st.st_mode);
}

static char* get_temp_dir(void) {
    static char temp_path[512];
    
#ifdef _WIN32
    GetTempPathA(sizeof(temp_path), temp_path);
    strcat(temp_path, "thermoconsole_");
#else
    strcpy(temp_path, "/tmp/thermoconsole_");
#endif
    
    /* Add random suffix */
    char suffix[16];
    snprintf(suffix, sizeof(suffix), "%d", (int)time(NULL) ^ (int)getpid());
    strcat(temp_path, suffix);
    
    return temp_path;
}

static int recursive_delete(const char* path) {
    /* Simple implementation - just leave temp files for now */
    /* A full implementation would recursively delete the directory */
    return 0;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * ROM Loading
 * ───────────────────────────────────────────────────────────────────────────── */

ThermoROM* rom_load(const char* path) {
    ThermoROM* rom = (ThermoROM*)calloc(1, sizeof(ThermoROM));
    if (!rom) return NULL;
    
    char* base_path = NULL;
    bool is_archive = false;
    
    /* Check if path is a directory (development mode) or .tcr file */
    if (is_directory(path)) {
        /* Development mode - use directory directly */
        base_path = strdup(path);
    } else if (is_file(path)) {
        /* Archive mode - extract to temp directory */
        is_archive = true;
        
        char* temp_dir = get_temp_dir();
        mkdir(temp_dir, 0755);
        
        /* Extract using system unzip (cross-platform would use miniz) */
        char cmd[1024];
#ifdef _WIN32
        snprintf(cmd, sizeof(cmd), "powershell -Command \"Expand-Archive -Path '%s' -DestinationPath '%s' -Force\"", path, temp_dir);
#else
        snprintf(cmd, sizeof(cmd), "unzip -q -o '%s' -d '%s' 2>/dev/null", path, temp_dir);
#endif
        
        int result = system(cmd);
        if (result != 0) {
            fprintf(stderr, "ERROR: Failed to extract ROM\n");
            free(rom);
            return NULL;
        }
        
        base_path = strdup(temp_dir);
    } else {
        fprintf(stderr, "ERROR: ROM path not found: %s\n", path);
        free(rom);
        return NULL;
    }
    
    rom->base_path = base_path;
    
    /* Load manifest */
    char manifest_path[512];
    snprintf(manifest_path, sizeof(manifest_path), "%s/manifest.json", base_path);
    
    char* manifest_json = read_file(manifest_path, NULL);
    if (!manifest_json) {
        fprintf(stderr, "ERROR: manifest.json not found\n");
        free(base_path);
        free(rom);
        return NULL;
    }
    
    if (parse_manifest(manifest_json, &rom->manifest) < 0) {
        fprintf(stderr, "ERROR: Failed to parse manifest.json\n");
        free(manifest_json);
        free(base_path);
        free(rom);
        return NULL;
    }
    
    free(manifest_json);
    
    return rom;
}

void rom_free(ThermoROM* rom) {
    if (!rom) return;
    
    /* Free sprites texture */
    if (rom->sprites.texture) {
        SDL_DestroyTexture(rom->sprites.texture);
    }
    
    /* Free tiles texture */
    if (rom->tiles.texture) {
        SDL_DestroyTexture(rom->tiles.texture);
    }
    
    /* Free current map */
    if (rom->current_map) {
        map_free(rom->current_map);
    }
    
    /* Clean up temp directory if it was extracted */
    if (rom->base_path) {
        /* Check if it's in temp */
        if (strstr(rom->base_path, "thermoconsole_")) {
            recursive_delete(rom->base_path);
        }
        free(rom->base_path);
    }
    
    free(rom);
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Map Functions (stubs for now)
 * ───────────────────────────────────────────────────────────────────────────── */

int map_load(const char* name) {
    /* TODO: Implement map loading from JSON */
    return -1;
}

void map_free(ThermoMap* map) {
    if (!map) return;
    
    for (int i = 0; i < map->layer_count; i++) {
        if (map->layers[i]) free(map->layers[i]);
        if (map->layer_names[i]) free(map->layer_names[i]);
    }
    
    free(map);
}

int map_get(int x, int y, const char* layer) {
    /* TODO: Implement */
    return 0;
}

void map_set(int x, int y, int tile, const char* layer) {
    /* TODO: Implement */
}

bool map_fget(int tile, const char* flag) {
    /* TODO: Implement */
    return false;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Save/Load Functions (stubs for now)
 * ───────────────────────────────────────────────────────────────────────────── */

int save_data(int slot, const char* json_data) {
    /* TODO: Implement persistent save */
    return -1;
}

char* load_data(int slot) {
    /* TODO: Implement persistent load */
    return NULL;
}

int delete_data(int slot) {
    /* TODO: Implement */
    return -1;
}
