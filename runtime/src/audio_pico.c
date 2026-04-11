/*
 * ThermoConsole Runtime
 * Audio module - uses Pico as sound chip over USB Serial
 *
 * Compile with -DTHERMO_PICO_AUDIO to use this instead of audio.c
 *
 * The serial port is opened and owned by platform_pi.c (pico_serial_fd).
 * This module just writes commands on that shared fd and reads ACKs.
 *
 * Commands sent to Pico:
 *   0x01 <len_hi> <len_lo> <data...>  - Upload JSON chunk
 *   0x02                              - Parse uploaded JSON
 *   0x10 <sound_id>                   - Play sound
 *   0x11                              - Stop sound
 *   0x12 <volume>                     - Set volume (0-255)
 *
 * Pico replies 0x01 (ACK) or 0xFF (error) for every command.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include "thermo.h"

/* Commands */
#define CMD_UPLOAD_CHUNK  0x01
#define CMD_PARSE_JSON    0x02
#define CMD_PLAY_SOUND    0x10
#define CMD_STOP_SOUND    0x11
#define CMD_SET_VOLUME    0x12

/* Shared serial fd — opened/closed by platform_pi.c */
extern int pico_serial_fd;

/* Sound name to ID mapping */
#define MAX_SOUNDS 32
static char sound_names[MAX_SOUNDS][32];
static int  sound_count = 0;

/* ─────────────────────────────────────────────────────────────────────────────
 * Serial helpers
 * ───────────────────────────────────────────────────────────────────────────── */

static int serial_write(const uint8_t* data, int len) {
    if (pico_serial_fd < 0) return -1;
    return (int)write(pico_serial_fd, data, len);
}

/* Read one ACK byte (0x01 = ok, 0xFF = error). Retry a few ms. */
static int read_ack(void) {
    if (pico_serial_fd < 0) return -1;
    uint8_t ack = 0;
    for (int retry = 0; retry < 40; retry++) {
        ssize_t n = read(pico_serial_fd, &ack, 1);
        if (n == 1) return ack;
        if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) return -1;
        usleep(500);
    }
    return -1;  /* timeout */
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Initialization
 * ───────────────────────────────────────────────────────────────────────────── */

int audio_init(void) {
    ThermoAudio* audio = &g_thermo->audio;

    audio->master_volume = 1.0f;
    for (int i = 0; i < THERMO_AUDIO_CHANNELS; i++) {
        audio->channel_volume[i] = 1.0f;
    }
    audio->music = NULL;

    /* Serial port is opened by platform_input_init() in platform_pi.c.
     * If it's not open yet we just log a warning — audio is optional. */
    if (pico_serial_fd < 0) {
        printf("[AUDIO] Pico not connected, audio disabled\n");
        return 0;
    }

    printf("[AUDIO] Pico sound chip ready on serial\n");

    /* Default sound name list (matches main_usb.py defaults) */
    const char* defaults[] = {
        "jump", "coin", "hit", "powerup", "death",
        "select", "start", "explosion", "laser", "pickup",
        "menu_move", "menu_select", "game_over", "victory", "level_start"
    };
    sound_count = (int)(sizeof(defaults) / sizeof(defaults[0]));
    for (int i = 0; i < sound_count; i++) {
        strncpy(sound_names[i], defaults[i], sizeof(sound_names[i]) - 1);
        sound_names[i][sizeof(sound_names[i]) - 1] = '\0';
    }

    return 0;
}

void audio_shutdown(void) {
    if (pico_serial_fd >= 0) {
        uint8_t cmd = CMD_STOP_SOUND;
        serial_write(&cmd, 1);
        read_ack();
        /* fd is closed by platform_input_shutdown() */
    }
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Load Sounds from JSON
 * ───────────────────────────────────────────────────────────────────────────── */

int audio_load_sounds(const char* json_path) {
    if (pico_serial_fd < 0) return -1;

    FILE* f = fopen(json_path, "r");
    if (!f) {
        printf("[AUDIO] No sounds.json at %s, using defaults\n", json_path);
        return 0;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    char* json = malloc(size + 1);
    if (!json) { fclose(f); return -1; }
    fread(json, 1, size, f);
    json[size] = '\0';
    fclose(f);

    printf("[AUDIO] Loading sounds from %s (%ld bytes)\n", json_path, size);

    /* Send JSON in chunks (keep under 64 bytes to stay well within
     * the Pico's serial buffer and MicroPython read() limits)       */
    const int chunk_size = 48;
    int offset = 0;

    while (offset < size) {
        int len = size - offset;
        if (len > chunk_size) len = chunk_size;

        uint8_t buf[3 + chunk_size];
        buf[0] = CMD_UPLOAD_CHUNK;
        buf[1] = (len >> 8) & 0xFF;
        buf[2] = len & 0xFF;
        memcpy(&buf[3], json + offset, len);

        if (serial_write(buf, 3 + len) < 0) {
            printf("[AUDIO] Failed to send chunk at offset %d\n", offset);
            free(json);
            return -1;
        }

        if (read_ack() != 0x01) {
            printf("[AUDIO] Bad ACK at offset %d\n", offset);
            free(json);
            return -1;
        }

        offset += len;
        usleep(2000);  /* give Pico a moment to buffer */
    }

    /* Tell Pico to parse */
    uint8_t cmd = CMD_PARSE_JSON;
    serial_write(&cmd, 1);
    if (read_ack() != 0x01) {
        printf("[AUDIO] Pico failed to parse JSON\n");
        free(json);
        return -1;
    }

    /* Build local name→ID table from JSON */
    sound_count = 0;
    char* p = strstr(json, "\"sounds\"");
    if (p) {
        p = strchr(p, '{');
        if (p) {
            p++;
            while (*p && sound_count < MAX_SOUNDS) {
                char* q = strchr(p, '"');
                if (!q) break;
                q++;
                char* end = strchr(q, '"');
                if (!end) break;
                int name_len = (int)(end - q);
                if (name_len > 0 && name_len < 31) {
                    char* colon = strchr(end, ':');
                    char* brace = strchr(end, '{');
                    if (colon && brace && brace < colon + 5) {
                        strncpy(sound_names[sound_count], q, name_len);
                        sound_names[sound_count][name_len] = '\0';
                        sound_count++;
                    }
                }
                p = end + 1;
            }
        }
    }

    printf("[AUDIO] Loaded %d sounds\n", sound_count);
    free(json);
    return 0;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Find sound ID by name
 * ───────────────────────────────────────────────────────────────────────────── */

static int find_sound_id(const char* name) {
    for (int i = 0; i < sound_count; i++) {
        if (strcmp(sound_names[i], name) == 0) return i;
    }
    return -1;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Playback Functions
 * ───────────────────────────────────────────────────────────────────────────── */

void audio_sfx(const char* name, int channel, bool loop) {
    if (pico_serial_fd < 0) return;

    int id = find_sound_id(name);
    if (id < 0) {
        printf("[AUDIO] Unknown sound: %s\n", name);
        return;
    }

    uint8_t buf[2] = { CMD_PLAY_SOUND, (uint8_t)id };
    serial_write(buf, 2);
    read_ack();
}

void audio_music(const char* name, bool loop) {
    audio_sfx(name, -1, loop);
}

void audio_stop(int channel) {
    if (pico_serial_fd < 0) return;
    uint8_t cmd = CMD_STOP_SOUND;
    serial_write(&cmd, 1);
    read_ack();
}

void audio_volume(float level, int channel) {
    if (pico_serial_fd < 0) return;

    ThermoAudio* audio = &g_thermo->audio;
    if (level < 0.0f) level = 0.0f;
    if (level > 1.0f) level = 1.0f;
    audio->master_volume = level;

    uint8_t vol = (uint8_t)(level * 255);
    uint8_t buf[2] = { CMD_SET_VOLUME, vol };
    serial_write(buf, 2);
    read_ack();
}
