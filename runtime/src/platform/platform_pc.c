/*
 * ThermoConsole Runtime
 * PC Platform Implementation
 * 
 * Handles input, graphics, audio, and filesystem for desktop development.
 */

#ifdef THERMO_PLATFORM_PC

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <windows.h>
#include <shlobj.h>
#define PATH_SEP '\\'
#else
#include <unistd.h>
#include <pwd.h>
#define PATH_SEP '/'
#endif

#include <SDL2/SDL.h>
#include "thermo.h"
#include "platform.h"

/* ═══════════════════════════════════════════════════════════════════════════
 * INPUT
 * ═══════════════════════════════════════════════════════════════════════════ */

static SDL_GameController* gamepad = NULL;
static int gamepad_instance_id = -1;

int platform_input_init(void) {
    /* Try to open first available game controller */
    for (int i = 0; i < SDL_NumJoysticks(); i++) {
        if (SDL_IsGameController(i)) {
            gamepad = SDL_GameControllerOpen(i);
            if (gamepad) {
                gamepad_instance_id = SDL_JoystickInstanceID(
                    SDL_GameControllerGetJoystick(gamepad));
                printf("[OK] Gamepad: %s\n", SDL_GameControllerName(gamepad));
                break;
            }
        }
    }
    return 0;
}

void platform_input_shutdown(void) {
    if (gamepad) {
        SDL_GameControllerClose(gamepad);
        gamepad = NULL;
        gamepad_instance_id = -1;
    }
}

void platform_input_update(void) {
    const Uint8* keys = SDL_GetKeyboardState(NULL);
    
    /* Keyboard mapping */
    bool key_states[THERMO_BTN_COUNT] = {0};
    
    /* D-Pad: Arrow keys or WASD */
    key_states[THERMO_BTN_UP]    = keys[SDL_SCANCODE_UP]    || keys[SDL_SCANCODE_W];
    key_states[THERMO_BTN_DOWN]  = keys[SDL_SCANCODE_DOWN]  || keys[SDL_SCANCODE_S];
    key_states[THERMO_BTN_LEFT]  = keys[SDL_SCANCODE_LEFT]  || keys[SDL_SCANCODE_A];
    key_states[THERMO_BTN_RIGHT] = keys[SDL_SCANCODE_RIGHT] || keys[SDL_SCANCODE_D];
    
    /* Face buttons: Z/X/C/V or J/K/L/; */
    key_states[THERMO_BTN_A] = keys[SDL_SCANCODE_Z] || keys[SDL_SCANCODE_J];
    key_states[THERMO_BTN_B] = keys[SDL_SCANCODE_X] || keys[SDL_SCANCODE_K];
    key_states[THERMO_BTN_X] = keys[SDL_SCANCODE_C] || keys[SDL_SCANCODE_L];
    key_states[THERMO_BTN_Y] = keys[SDL_SCANCODE_V] || keys[SDL_SCANCODE_SEMICOLON];
    
    /* Start/Select: Enter/Shift */
    key_states[THERMO_BTN_START]  = keys[SDL_SCANCODE_RETURN];
    key_states[THERMO_BTN_SELECT] = keys[SDL_SCANCODE_RSHIFT] || keys[SDL_SCANCODE_LSHIFT];
    
    /* Gamepad input (if connected) */
    if (gamepad && SDL_GameControllerGetAttached(gamepad)) {
        /* D-Pad */
        if (SDL_GameControllerGetButton(gamepad, SDL_CONTROLLER_BUTTON_DPAD_UP))
            key_states[THERMO_BTN_UP] = true;
        if (SDL_GameControllerGetButton(gamepad, SDL_CONTROLLER_BUTTON_DPAD_DOWN))
            key_states[THERMO_BTN_DOWN] = true;
        if (SDL_GameControllerGetButton(gamepad, SDL_CONTROLLER_BUTTON_DPAD_LEFT))
            key_states[THERMO_BTN_LEFT] = true;
        if (SDL_GameControllerGetButton(gamepad, SDL_CONTROLLER_BUTTON_DPAD_RIGHT))
            key_states[THERMO_BTN_RIGHT] = true;
        
        /* Left stick as D-Pad */
        const int DEADZONE = 8000;
        int lx = SDL_GameControllerGetAxis(gamepad, SDL_CONTROLLER_AXIS_LEFTX);
        int ly = SDL_GameControllerGetAxis(gamepad, SDL_CONTROLLER_AXIS_LEFTY);
        
        if (lx < -DEADZONE) key_states[THERMO_BTN_LEFT] = true;
        if (lx >  DEADZONE) key_states[THERMO_BTN_RIGHT] = true;
        if (ly < -DEADZONE) key_states[THERMO_BTN_UP] = true;
        if (ly >  DEADZONE) key_states[THERMO_BTN_DOWN] = true;
        
        /* Face buttons (Xbox layout) */
        if (SDL_GameControllerGetButton(gamepad, SDL_CONTROLLER_BUTTON_A))
            key_states[THERMO_BTN_A] = true;
        if (SDL_GameControllerGetButton(gamepad, SDL_CONTROLLER_BUTTON_B))
            key_states[THERMO_BTN_B] = true;
        if (SDL_GameControllerGetButton(gamepad, SDL_CONTROLLER_BUTTON_X))
            key_states[THERMO_BTN_X] = true;
        if (SDL_GameControllerGetButton(gamepad, SDL_CONTROLLER_BUTTON_Y))
            key_states[THERMO_BTN_Y] = true;
        
        /* Start/Select */
        if (SDL_GameControllerGetButton(gamepad, SDL_CONTROLLER_BUTTON_START))
            key_states[THERMO_BTN_START] = true;
        if (SDL_GameControllerGetButton(gamepad, SDL_CONTROLLER_BUTTON_BACK))
            key_states[THERMO_BTN_SELECT] = true;
    }
    
    /* Update button states with pressed/released detection */
    for (int i = 0; i < THERMO_BTN_COUNT; i++) {
        ThermoButton* btn = &g_thermo->input.buttons[i];
        btn->pressed = key_states[i] && !btn->held;
        btn->released = !key_states[i] && btn->held;
        btn->held = key_states[i];
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * GRAPHICS
 * ═══════════════════════════════════════════════════════════════════════════ */

int platform_gfx_init(void) {
    /* Set hints for desktop rendering */
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest");
    SDL_SetHint(SDL_HINT_VIDEO_ALLOW_SCREENSAVER, "0");
    return 0;
}

void platform_gfx_shutdown(void) {
    /* Nothing special needed */
}

uint32_t platform_get_window_flags(void) {
    return SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE;
}

int platform_get_display_scale(void) {
    /* Get display size and calculate best integer scale */
    SDL_DisplayMode dm;
    if (SDL_GetCurrentDisplayMode(0, &dm) == 0) {
        int scale_x = (dm.w - 100) / THERMO_SCREEN_WIDTH;
        int scale_y = (dm.h - 100) / THERMO_SCREEN_HEIGHT;
        int scale = (scale_x < scale_y) ? scale_x : scale_y;
        if (scale < 1) scale = 1;
        if (scale > 4) scale = 4;
        return scale;
    }
    return 1;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * AUDIO
 * ═══════════════════════════════════════════════════════════════════════════ */

int platform_audio_init(void) {
    /* Default SDL_mixer configuration works fine on PC */
    return 0;
}

void platform_audio_shutdown(void) {
    /* Nothing special needed */
}

/* ═══════════════════════════════════════════════════════════════════════════
 * FILESYSTEM
 * ═══════════════════════════════════════════════════════════════════════════ */

static char temp_path[512] = {0};
static char save_path[512] = {0};

const char* platform_get_temp_path(void) {
    if (temp_path[0] == '\0') {
#ifdef _WIN32
        GetTempPathA(sizeof(temp_path), temp_path);
        strncat(temp_path, "thermoconsole\\", sizeof(temp_path) - strlen(temp_path) - 1);
#else
        const char* tmp = getenv("TMPDIR");
        if (!tmp) tmp = "/tmp";
        snprintf(temp_path, sizeof(temp_path), "%s/thermoconsole/", tmp);
#endif
        /* Create directory if needed */
#ifdef _WIN32
        CreateDirectoryA(temp_path, NULL);
#else
        mkdir(temp_path, 0755);
#endif
    }
    return temp_path;
}

const char* platform_get_save_path(void) {
    if (save_path[0] == '\0') {
#ifdef _WIN32
        char appdata[MAX_PATH];
        if (SHGetFolderPathA(NULL, CSIDL_APPDATA, NULL, 0, appdata) == S_OK) {
            snprintf(save_path, sizeof(save_path), "%s\\thermoconsole\\", appdata);
        } else {
            snprintf(save_path, sizeof(save_path), ".thermoconsole\\");
        }
        CreateDirectoryA(save_path, NULL);
#else
        const char* home = getenv("HOME");
        if (!home) {
            struct passwd* pw = getpwuid(getuid());
            if (pw) home = pw->pw_dir;
        }
        if (!home) home = ".";
        snprintf(save_path, sizeof(save_path), "%s/.thermoconsole/", home);
        mkdir(save_path, 0755);
#endif
    }
    return save_path;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * UTILITIES
 * ═══════════════════════════════════════════════════════════════════════════ */

const char* platform_get_name(void) {
#ifdef _WIN32
    return "Windows";
#elif defined(__APPLE__)
    return "macOS";
#else
    return "Linux (PC)";
#endif
}

bool platform_on_battery(void) {
    /* Could implement using platform-specific APIs */
    /* For now, assume desktop is plugged in */
    return false;
}

float platform_get_cpu_temp(void) {
    /* Not available on PC */
    return -1.0f;
}

#endif /* THERMO_PLATFORM_PC */
