/*
 * ThermoConsole Runtime
 * Pi Zero Platform Implementation
 * 
 * Handles input (from Pico controller via I2C), graphics (DPI/KMS),
 * audio (ALSA), and filesystem for Raspberry Pi deployment.
 * 
 * Display: Waveshare 2.8" DPI LCD (480x640 native, rotated to 640x480 landscape)
 * Controller: Pi Pico connected via I2C (address 0x42)
 */

#ifdef THERMO_PLATFORM_PI

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>

#include <SDL2/SDL.h>
#include "thermo.h"
#include "platform.h"

/* ═══════════════════════════════════════════════════════════════════════════
 * CONFIGURATION
 * ═══════════════════════════════════════════════════════════════════════════ */

/* I2C configuration for Pico controller */
#define PICO_I2C_BUS          "/dev/i2c-1"
#define PICO_I2C_ADDRESS      0x42
#define PICO_PACKET_SIZE      2
#define PICO_RECONNECT_INTERVAL 60  /* frames between reconnect attempts */

/* Paths */
#define PI_TEMP_PATH "/tmp/thermo/"
#define PI_SAVE_PATH "/home/pi/.thermoconsole/"

/* ═══════════════════════════════════════════════════════════════════════════
 * INPUT - Pico Controller over I2C
 * ═══════════════════════════════════════════════════════════════════════════ */

static int i2c_fd = -1;
static bool pico_connected = false;
static uint16_t last_button_state = 0;
static int reconnect_counter = 0;

/* Also support fallback to keyboard (for debugging) */
static bool use_keyboard_fallback = true;

static int open_i2c(void) {
    int fd = open(PICO_I2C_BUS, O_RDWR);
    if (fd < 0) {
        fprintf(stderr, "[WARN] Cannot open %s: %s\n", PICO_I2C_BUS, strerror(errno));
        return -1;
    }
    
    /* Set the I2C slave address */
    if (ioctl(fd, I2C_SLAVE, PICO_I2C_ADDRESS) < 0) {
        fprintf(stderr, "[WARN] Cannot set I2C address 0x%02X: %s\n", 
                PICO_I2C_ADDRESS, strerror(errno));
        close(fd);
        return -1;
    }
    
    printf("[OK] Pico controller on %s @ 0x%02X\n", PICO_I2C_BUS, PICO_I2C_ADDRESS);
    return fd;
}

int platform_input_init(void) {
    i2c_fd = open_i2c();
    pico_connected = (i2c_fd >= 0);
    
    if (!pico_connected) {
        printf("[WARN] Pico controller not found, using keyboard fallback\n");
        use_keyboard_fallback = true;
    } else {
        use_keyboard_fallback = false;
    }
    
    return 0;  /* Don't fail if controller not found */
}

void platform_input_shutdown(void) {
    if (i2c_fd >= 0) {
        close(i2c_fd);
        i2c_fd = -1;
    }
    pico_connected = false;
}

static uint16_t read_pico_state(void) {
    if (!pico_connected) {
        /* Attempt reconnection periodically */
        if (++reconnect_counter >= PICO_RECONNECT_INTERVAL) {
            reconnect_counter = 0;
            i2c_fd = open_i2c();
            if (i2c_fd >= 0) {
                pico_connected = true;
                use_keyboard_fallback = false;
            }
        }
        return last_button_state;
    }
    
    /* Read button state from Pico via I2C */
    uint8_t buffer[PICO_PACKET_SIZE];
    ssize_t n = read(i2c_fd, buffer, PICO_PACKET_SIZE);
    
    if (n != PICO_PACKET_SIZE) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            fprintf(stderr, "[WARN] Pico I2C read error: %s\n", strerror(errno));
            close(i2c_fd);
            i2c_fd = -1;
            pico_connected = false;
            use_keyboard_fallback = true;
        }
        return last_button_state;
    }
    
    /* Parse button state: byte0 = buttons 0-7, byte1 = buttons 8-9 + flags */
    last_button_state = buffer[0] | ((uint16_t)(buffer[1] & 0x03) << 8);
    
    return last_button_state;
}

void platform_input_update(void) {
    bool key_states[THERMO_BTN_COUNT] = {0};
    
    /* Read from Pico controller */
    uint16_t pico_state = read_pico_state();
    for (int i = 0; i < THERMO_BTN_COUNT; i++) {
        if (pico_state & (1 << i)) {
            key_states[i] = true;
        }
    }
    
    /* Keyboard fallback (also works alongside Pico for debugging) */
    if (use_keyboard_fallback) {
        const Uint8* keys = SDL_GetKeyboardState(NULL);
        
        if (keys[SDL_SCANCODE_UP]    || keys[SDL_SCANCODE_W]) key_states[THERMO_BTN_UP] = true;
        if (keys[SDL_SCANCODE_DOWN]  || keys[SDL_SCANCODE_S]) key_states[THERMO_BTN_DOWN] = true;
        if (keys[SDL_SCANCODE_LEFT]  || keys[SDL_SCANCODE_A]) key_states[THERMO_BTN_LEFT] = true;
        if (keys[SDL_SCANCODE_RIGHT] || keys[SDL_SCANCODE_D]) key_states[THERMO_BTN_RIGHT] = true;
        if (keys[SDL_SCANCODE_Z] || keys[SDL_SCANCODE_J]) key_states[THERMO_BTN_A] = true;
        if (keys[SDL_SCANCODE_X] || keys[SDL_SCANCODE_K]) key_states[THERMO_BTN_B] = true;
        if (keys[SDL_SCANCODE_C] || keys[SDL_SCANCODE_L]) key_states[THERMO_BTN_X] = true;
        if (keys[SDL_SCANCODE_V]) key_states[THERMO_BTN_Y] = true;
        if (keys[SDL_SCANCODE_RETURN]) key_states[THERMO_BTN_START] = true;
        if (keys[SDL_SCANCODE_RSHIFT] || keys[SDL_SCANCODE_LSHIFT]) key_states[THERMO_BTN_SELECT] = true;
    }
    
    /* Update button states */
    for (int i = 0; i < THERMO_BTN_COUNT; i++) {
        ThermoButton* btn = &g_thermo->input.buttons[i];
        btn->pressed = key_states[i] && !btn->held;
        btn->released = !key_states[i] && btn->held;
        btn->held = key_states[i];
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * GRAPHICS - Waveshare 2.8" DPI LCD Configuration
 * Display is 480x640 native (portrait), rotated to 640x480 (landscape)
 * ═══════════════════════════════════════════════════════════════════════════ */

int platform_gfx_init(void) {
    /* Force KMS/DRM driver for best performance on Pi */
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "kmsdrm");
    
    /* Use nearest-neighbor scaling */
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest");
    
    /* Disable screen saver */
    SDL_SetHint(SDL_HINT_VIDEO_ALLOW_SCREENSAVER, "0");
    
    /* Disable mouse cursor */
    SDL_ShowCursor(SDL_DISABLE);
    
    return 0;
}

void platform_gfx_shutdown(void) {
    SDL_ShowCursor(SDL_ENABLE);
}

uint32_t platform_get_window_flags(void) {
    /* Fullscreen for console mode */
    return SDL_WINDOW_FULLSCREEN_DESKTOP | SDL_WINDOW_SHOWN;
}

int platform_get_display_scale(void) {
    /* On Pi with 640x480 (rotated DPI display), use native resolution (scale 1) */
    return 1;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * AUDIO - ALSA Configuration
 * ═══════════════════════════════════════════════════════════════════════════ */

int platform_audio_init(void) {
    /* Check if we should use a specific audio device */
    const char* audio_dev = getenv("THERMO_AUDIO_DEVICE");
    if (audio_dev) {
        printf("[INFO] Using audio device: %s\n", audio_dev);
    }
    
    return 0;
}

void platform_audio_shutdown(void) {
    /* Nothing special needed */
}

/* ═══════════════════════════════════════════════════════════════════════════
 * FILESYSTEM
 * ═══════════════════════════════════════════════════════════════════════════ */

const char* platform_get_temp_path(void) {
    /* Ensure temp directory exists */
    mkdir(PI_TEMP_PATH, 0755);
    return PI_TEMP_PATH;
}

const char* platform_get_save_path(void) {
    /* Ensure save directory exists */
    mkdir(PI_SAVE_PATH, 0755);
    return PI_SAVE_PATH;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * UTILITIES
 * ═══════════════════════════════════════════════════════════════════════════ */

const char* platform_get_name(void) {
    /* Detect Pi model */
    FILE* f = fopen("/proc/device-tree/model", "r");
    if (f) {
        static char model[128];
        if (fgets(model, sizeof(model), f)) {
            fclose(f);
            for (int i = 0; i < (int)sizeof(model); i++) {
                if (model[i] == '\n' || model[i] == '\0') {
                    model[i] = '\0';
                    break;
                }
            }
            return model;
        }
        fclose(f);
    }
    return "Raspberry Pi";
}

bool platform_on_battery(void) {
    return false;
}

float platform_get_cpu_temp(void) {
    FILE* f = fopen("/sys/class/thermal/thermal_zone0/temp", "r");
    if (f) {
        int temp_millic;
        if (fscanf(f, "%d", &temp_millic) == 1) {
            fclose(f);
            return temp_millic / 1000.0f;
        }
        fclose(f);
    }
    return -1.0f;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * ADDITIONAL PI-SPECIFIC FUNCTIONS
 * ═══════════════════════════════════════════════════════════════════════════ */

bool platform_pico_connected(void) {
    return pico_connected;
}

bool platform_is_throttled(void) {
    FILE* f = fopen("/sys/devices/platform/soc/soc:firmware/get_throttled", "r");
    if (f) {
        int throttled;
        if (fscanf(f, "%x", &throttled) == 1) {
            fclose(f);
            return (throttled & 0x0F) != 0;
        }
        fclose(f);
    }
    return false;
}

#endif /* THERMO_PLATFORM_PI */
