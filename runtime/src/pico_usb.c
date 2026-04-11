/*
 * ThermoConsole - Pico USB Serial Communication
 * Handles both sound and button input over USB serial
 * 
 * Pico appears as /dev/ttyACM0
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <errno.h>
#include "thermo.h"

/* Serial port */
static int serial_fd = -1;
static const char* SERIAL_PORT = "/dev/ttyACM0";

/* Commands */
#define CMD_UPLOAD_CHUNK  0x01
#define CMD_PARSE_JSON    0x02
#define CMD_PLAY_SOUND    0x10
#define CMD_STOP_SOUND    0x11
#define CMD_SET_VOLUME    0x12
#define CMD_READ_BUTTONS  0x20
#define CMD_GET_COUNT     0x30
#define CMD_GET_NAME      0x31

/* Sound name mapping */
#define MAX_SOUNDS 32
static char sound_names[MAX_SOUNDS][32];
static int sound_count = 0;

/* Cached button state */
static uint16_t cached_buttons = 0;

/* ─────────────────────────────────────────────────────────────────────────────
 * Serial Helpers
 * ───────────────────────────────────────────────────────────────────────────── */

static int serial_open(const char* port) {
    int fd = open(port, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) {
        return -1;
    }
    
    /* Configure serial port */
    struct termios tty;
    memset(&tty, 0, sizeof(tty));
    
    if (tcgetattr(fd, &tty) != 0) {
        close(fd);
        return -1;
    }
    
    /* 115200 baud, 8N1 */
    cfsetospeed(&tty, B115200);
    cfsetispeed(&tty, B115200);
    
    tty.c_cflag &= ~PARENB;        /* No parity */
    tty.c_cflag &= ~CSTOPB;        /* 1 stop bit */
    tty.c_cflag &= ~CSIZE;
    tty.c_cflag |= CS8;            /* 8 bits */
    tty.c_cflag &= ~CRTSCTS;       /* No hardware flow control */
    tty.c_cflag |= CREAD | CLOCAL; /* Enable receiver, ignore modem lines */
    
    tty.c_lflag &= ~ICANON;        /* Raw mode */
    tty.c_lflag &= ~ECHO;
    tty.c_lflag &= ~ECHOE;
    tty.c_lflag &= ~ECHONL;
    tty.c_lflag &= ~ISIG;
    
    tty.c_iflag &= ~(IXON | IXOFF | IXANY);
    tty.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL);
    
    tty.c_oflag &= ~OPOST;
    tty.c_oflag &= ~ONLCR;
    
    tty.c_cc[VTIME] = 1;           /* 100ms timeout */
    tty.c_cc[VMIN] = 0;
    
    if (tcsetattr(fd, TCSANOW, &tty) != 0) {
        close(fd);
        return -1;
    }
    
    /* Flush any existing data */
    tcflush(fd, TCIOFLUSH);
    
    return fd;
}

static int serial_write_bytes(uint8_t* data, int len) {
    if (serial_fd < 0) return -1;
    return write(serial_fd, data, len);
}

static int serial_read_bytes(uint8_t* data, int len, int timeout_ms) {
    if (serial_fd < 0) return -1;
    
    int total = 0;
    int elapsed = 0;
    
    while (total < len && elapsed < timeout_ms) {
        int n = read(serial_fd, data + total, len - total);
        if (n > 0) {
            total += n;
        } else {
            usleep(1000);
            elapsed++;
        }
    }
    
    return total;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Pico Communication Init
 * ───────────────────────────────────────────────────────────────────────────── */

int pico_init(void) {
    /* Try to open serial port */
    serial_fd = serial_open(SERIAL_PORT);
    
    if (serial_fd < 0) {
        /* Try alternate port */
        serial_fd = serial_open("/dev/ttyACM1");
    }
    
    if (serial_fd < 0) {
        printf("[PICO] No Pico found on USB serial\n");
        return -1;
    }
    
    printf("[PICO] Connected on %s\n", SERIAL_PORT);
    
    /* Wait for Pico to be ready */
    usleep(100000);
    
    /* Set default sound names */
    strcpy(sound_names[0], "jump");
    strcpy(sound_names[1], "coin");
    strcpy(sound_names[2], "hit");
    strcpy(sound_names[3], "powerup");
    strcpy(sound_names[4], "death");
    strcpy(sound_names[5], "select");
    strcpy(sound_names[6], "start");
    strcpy(sound_names[7], "explosion");
    strcpy(sound_names[8], "laser");
    strcpy(sound_names[9], "pickup");
    strcpy(sound_names[10], "menu_move");
    strcpy(sound_names[11], "menu_select");
    strcpy(sound_names[12], "game_over");
    strcpy(sound_names[13], "victory");
    strcpy(sound_names[14], "level_start");
    sound_count = 15;
    
    return 0;
}

void pico_shutdown(void) {
    if (serial_fd >= 0) {
        /* Stop any playing sound */
        uint8_t cmd = CMD_STOP_SOUND;
        serial_write_bytes(&cmd, 1);
        
        close(serial_fd);
        serial_fd = -1;
    }
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Sound Functions
 * ───────────────────────────────────────────────────────────────────────────── */

static int find_sound_id(const char* name) {
    for (int i = 0; i < sound_count; i++) {
        if (strcmp(sound_names[i], name) == 0) {
            return i;
        }
    }
    return -1;
}

int pico_load_sounds(const char* json_path) {
    if (serial_fd < 0) return -1;
    
    FILE* f = fopen(json_path, "r");
    if (!f) {
        printf("[PICO] No sounds.json at %s\n", json_path);
        return 0;
    }
    
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    char* json = malloc(size + 1);
    if (!json) {
        fclose(f);
        return -1;
    }
    
    fread(json, 1, size, f);
    json[size] = '\0';
    fclose(f);
    
    printf("[PICO] Loading sounds (%ld bytes)\n", size);
    
    /* Send in chunks */
    int chunk_size = 60;
    int offset = 0;
    
    while (offset < size) {
        int remaining = size - offset;
        int len = (remaining > chunk_size) ? chunk_size : remaining;
        
        uint8_t buf[64];
        buf[0] = CMD_UPLOAD_CHUNK;
        buf[1] = (len >> 8) & 0xFF;
        buf[2] = len & 0xFF;
        memcpy(&buf[3], &json[offset], len);
        
        serial_write_bytes(buf, 3 + len);
        
        /* Wait for ACK */
        uint8_t ack;
        serial_read_bytes(&ack, 1, 100);
        
        offset += len;
    }
    
    /* Tell Pico to parse */
    uint8_t cmd = CMD_PARSE_JSON;
    serial_write_bytes(&cmd, 1);
    
    uint8_t ack;
    serial_read_bytes(&ack, 1, 100);
    
    /* Parse locally for name mapping */
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
                
                int name_len = end - q;
                if (name_len < 31 && name_len > 0) {
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
    
    printf("[PICO] Loaded %d sounds\n", sound_count);
    free(json);
    return 0;
}

void pico_play_sound(const char* name) {
    if (serial_fd < 0) return;
    
    int id = find_sound_id(name);
    if (id < 0) {
        printf("[PICO] Unknown sound: %s\n", name);
        return;
    }
    
    uint8_t buf[2] = { CMD_PLAY_SOUND, (uint8_t)id };
    serial_write_bytes(buf, 2);
}

void pico_play_sound_id(int id) {
    if (serial_fd < 0) return;
    
    uint8_t buf[2] = { CMD_PLAY_SOUND, (uint8_t)id };
    serial_write_bytes(buf, 2);
}

void pico_stop_sound(void) {
    if (serial_fd < 0) return;
    
    uint8_t cmd = CMD_STOP_SOUND;
    serial_write_bytes(&cmd, 1);
}

void pico_set_volume(int volume) {
    if (serial_fd < 0) return;
    
    uint8_t buf[2] = { CMD_SET_VOLUME, (uint8_t)volume };
    serial_write_bytes(buf, 2);
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Button Functions
 * ───────────────────────────────────────────────────────────────────────────── */

uint16_t pico_read_buttons(void) {
    if (serial_fd < 0) return 0;
    
    uint8_t cmd = CMD_READ_BUTTONS;
    serial_write_bytes(&cmd, 1);
    
    uint8_t response[3];
    int n = serial_read_bytes(response, 3, 10);
    
    if (n == 3 && response[0] == 0x20) {
        cached_buttons = response[1] | (response[2] << 8);
    }
    
    return cached_buttons;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Integration with existing audio.c API
 * ───────────────────────────────────────────────────────────────────────────── */

int audio_init(void) {
    ThermoAudio* audio = &g_thermo->audio;
    
    audio->master_volume = 1.0f;
    for (int i = 0; i < THERMO_AUDIO_CHANNELS; i++) {
        audio->channel_volume[i] = 1.0f;
    }
    audio->music = NULL;
    
    return pico_init();
}

void audio_shutdown(void) {
    pico_shutdown();
}

void audio_sfx(const char* name, int channel, bool loop) {
    pico_play_sound(name);
}

void audio_music(const char* name, bool loop) {
    /* Music as sound for now */
    pico_play_sound(name);
}

void audio_stop(int channel) {
    pico_stop_sound();
}

void audio_volume(float level, int channel) {
    if (level < 0.0f) level = 0.0f;
    if (level > 1.0f) level = 1.0f;
    
    g_thermo->audio.master_volume = level;
    pico_set_volume((int)(level * 255));
}
