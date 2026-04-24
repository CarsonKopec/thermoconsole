/*
 * ThermoConsole Runtime
 * Pi Zero Platform Implementation
 *
 * Handles input (from Pico controller via USB Serial), graphics (DPI/KMS),
 * audio (ALSA), and filesystem for Raspberry Pi deployment.
 *
 * Display: Waveshare 2.8" DPI LCD (480x640 native, rotated to 640x480 landscape)
 * Controller: Pi Pico connected via USB Serial (/dev/ttyACM0)
 *
 * Serial protocol (binary, 115200 baud):
 *   Pi sends  0x20          → button state request
 *   Pico sends 0x20 lo hi   → button state response (10 buttons across 2 bytes)
 */

#ifdef THERMO_PLATFORM_PI

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <termios.h>

#include <SDL.h>
#include "thermo.h"
#include "platform.h"

/* ═══════════════════════════════════════════════════════════════════════════
 * CONFIGURATION
 * ═══════════════════════════════════════════════════════════════════════════ */

/* Serial port where Pico appears after USB enumeration */
#define PICO_SERIAL_PORT        "/dev/ttyACM0"
#define PICO_BAUD               B115200
#define PICO_RECONNECT_INTERVAL 60  /* frames between reconnect attempts */

/* Command bytes (shared with audio_pico.c) */
#define CMD_READ_BUTTONS  0x20

/* Paths */
#define PI_TEMP_PATH "/tmp/thermo/"
#define PI_SAVE_PATH "/home/pi/.thermoconsole/"

/* ═══════════════════════════════════════════════════════════════════════════
 * SERIAL — shared with audio_pico.c via extern
 * ═══════════════════════════════════════════════════════════════════════════ */

/* Exported so audio_pico.c can send commands on the same fd */
int pico_serial_fd = -1;

static bool pico_connected = false;
static uint16_t last_button_state = 0;
static int reconnect_counter = 0;
static bool use_keyboard_fallback = true;

static int open_serial(void) {
    int fd = open(PICO_SERIAL_PORT, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) {
        fprintf(stderr, "[WARN] Cannot open %s: %s\n", PICO_SERIAL_PORT, strerror(errno));
        return -1;
    }

    struct termios tty;
    if (tcgetattr(fd, &tty) != 0) {
        fprintf(stderr, "[WARN] tcgetattr failed: %s\n", strerror(errno));
        close(fd);
        return -1;
    }

    cfsetospeed(&tty, PICO_BAUD);
    cfsetispeed(&tty, PICO_BAUD);

    /* 8N1, no flow control */
    tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS8;
    tty.c_cflag &= ~(PARENB | CSTOPB | CRTSCTS);
    tty.c_cflag |= (CLOCAL | CREAD);

    /* Raw mode */
    tty.c_iflag &= ~(IXON | IXOFF | IXANY | IGNBRK | BRKINT | PARMRK |
                     ISTRIP | INLCR | IGNCR | ICRNL);
    tty.c_oflag &= ~OPOST;
    tty.c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);

    /* Non-blocking reads */
    tty.c_cc[VMIN]  = 0;
    tty.c_cc[VTIME] = 0;

    if (tcsetattr(fd, TCSANOW, &tty) != 0) {
        fprintf(stderr, "[WARN] tcsetattr failed: %s\n", strerror(errno));
        close(fd);
        return -1;
    }

    tcflush(fd, TCIOFLUSH);

    printf("[OK] Pico controller on %s (115200 baud)\n", PICO_SERIAL_PORT);
    return fd;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * INPUT
 * ═══════════════════════════════════════════════════════════════════════════ */

int platform_input_init(void) {
    pico_serial_fd = open_serial();
    pico_connected = (pico_serial_fd >= 0);

    if (!pico_connected) {
        printf("[WARN] Pico controller not found, using keyboard fallback\n");
        use_keyboard_fallback = true;
    } else {
        use_keyboard_fallback = false;
    }

    return 0;  /* Don't fail if controller not found */
}

void platform_input_shutdown(void) {
    if (pico_serial_fd >= 0) {
        close(pico_serial_fd);
        pico_serial_fd = -1;
    }
    pico_connected = false;
}

static uint16_t read_pico_state(void) {
    if (!pico_connected) {
        /* Attempt reconnection periodically */
        if (++reconnect_counter >= PICO_RECONNECT_INTERVAL) {
            reconnect_counter = 0;
            pico_serial_fd = open_serial();
            if (pico_serial_fd >= 0) {
                pico_connected = true;
                use_keyboard_fallback = false;
            }
        }
        return last_button_state;
    }

    /* Request button state: send 0x20, expect back 0x20 <lo> <hi> */
    uint8_t req = CMD_READ_BUTTONS;
    if (write(pico_serial_fd, &req, 1) != 1) {
        fprintf(stderr, "[WARN] Pico serial write error: %s\n", strerror(errno));
        goto disconnected;
    }

    /* Read 3-byte response with a short retry loop (non-blocking port) */
    uint8_t resp[3];
    int got = 0;
    for (int retry = 0; retry < 20 && got < 3; retry++) {
        ssize_t n = read(pico_serial_fd, resp + got, 3 - got);
        if (n > 0) {
            got += n;
        } else if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
            fprintf(stderr, "[WARN] Pico serial read error: %s\n", strerror(errno));
            goto disconnected;
        } else {
            usleep(500);  /* wait 0.5 ms and retry */
        }
    }

    if (got != 3 || resp[0] != CMD_READ_BUTTONS) {
        /* Stale/partial data — flush and skip this frame */
        tcflush(pico_serial_fd, TCIFLUSH);
        return last_button_state;
    }

    /* Parse button state: resp[1] = buttons 0-7, resp[2] = buttons 8-9 */
    last_button_state = resp[1] | ((uint16_t)(resp[2] & 0x03) << 8);
    return last_button_state;

disconnected:
    close(pico_serial_fd);
    pico_serial_fd = -1;
    pico_connected = false;
    use_keyboard_fallback = true;
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

        if (keys[SDL_SCANCODE_UP]    || keys[SDL_SCANCODE_W]) key_states[THERMO_BTN_UP]     = true;
        if (keys[SDL_SCANCODE_DOWN]  || keys[SDL_SCANCODE_S]) key_states[THERMO_BTN_DOWN]   = true;
        if (keys[SDL_SCANCODE_LEFT]  || keys[SDL_SCANCODE_A]) key_states[THERMO_BTN_LEFT]   = true;
        if (keys[SDL_SCANCODE_RIGHT] || keys[SDL_SCANCODE_D]) key_states[THERMO_BTN_RIGHT]  = true;
        if (keys[SDL_SCANCODE_Z] || keys[SDL_SCANCODE_J])     key_states[THERMO_BTN_A]      = true;
        if (keys[SDL_SCANCODE_X] || keys[SDL_SCANCODE_K])     key_states[THERMO_BTN_B]      = true;
        if (keys[SDL_SCANCODE_C] || keys[SDL_SCANCODE_L])     key_states[THERMO_BTN_X]      = true;
        if (keys[SDL_SCANCODE_V])                              key_states[THERMO_BTN_Y]      = true;
        if (keys[SDL_SCANCODE_RETURN])                         key_states[THERMO_BTN_START]  = true;
        if (keys[SDL_SCANCODE_RSHIFT] || keys[SDL_SCANCODE_LSHIFT]) key_states[THERMO_BTN_SELECT] = true;
    }

    /* Update button states */
    for (int i = 0; i < THERMO_BTN_COUNT; i++) {
        ThermoButton* btn = &g_thermo->input.buttons[i];
        btn->pressed  = key_states[i] && !btn->held;
        btn->released = !key_states[i] && btn->held;
        btn->held     = key_states[i];
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * GRAPHICS - Waveshare 2.8" DPI LCD
 * ═══════════════════════════════════════════════════════════════════════════ */

int platform_gfx_init(void) {
    SDL_SetHint(SDL_HINT_VIDEODRIVER, "kmsdrm");
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest");
    SDL_SetHint(SDL_HINT_VIDEO_ALLOW_SCREENSAVER, "0");
    SDL_ShowCursor(SDL_DISABLE);
    return 0;
}

void platform_gfx_shutdown(void) {
    SDL_ShowCursor(SDL_ENABLE);
}

uint32_t platform_get_window_flags(void) {
    return SDL_WINDOW_FULLSCREEN_DESKTOP | SDL_WINDOW_SHOWN;
}

int platform_get_display_scale(void) {
    return 1;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * AUDIO
 * ═══════════════════════════════════════════════════════════════════════════ */

int platform_audio_init(void) {
    const char* audio_dev = getenv("THERMO_AUDIO_DEVICE");
    if (audio_dev) {
        printf("[INFO] Using audio device: %s\n", audio_dev);
    }
    return 0;
}

void platform_audio_shutdown(void) {}

/* ═══════════════════════════════════════════════════════════════════════════
 * FILESYSTEM
 * ═══════════════════════════════════════════════════════════════════════════ */

const char* platform_get_temp_path(void) {
    mkdir(PI_TEMP_PATH, 0755);
    return PI_TEMP_PATH;
}

const char* platform_get_save_path(void) {
    mkdir(PI_SAVE_PATH, 0755);
    return PI_SAVE_PATH;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * UTILITIES
 * ═══════════════════════════════════════════════════════════════════════════ */

const char* platform_get_name(void) {
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

bool platform_pico_connected(void) {
    return pico_connected;
}

bool platform_is_throttled(void) {
    FILE* f = fopen("/sys/devices/platform/soc/soc:firmware/get_throttled", "r");
    if (f) {
        unsigned int throttled;
        if (fscanf(f, "%x", &throttled) == 1) {
            fclose(f);
            return (throttled & 0x0F) != 0;
        }
        fclose(f);
    }
    return false;
}

#endif /* THERMO_PLATFORM_PI */
