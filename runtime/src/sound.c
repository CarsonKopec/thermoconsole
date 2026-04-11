/*
 * ThermoConsole Sound Module
 * Communicates with Pico sound chip over I2C
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>
#include "sound.h"

/* I2C file descriptor */
static int i2c_fd = -1;

/* Sound name to ID mapping */
#define MAX_SOUNDS 32
static char sound_names[MAX_SOUNDS][32];
static int sound_count = 0;

/* ─────────────────────────────────────────────────────────────────────────────
 * I2C Helpers
 * ───────────────────────────────────────────────────────────────────────────── */

static int i2c_write(uint8_t* data, int len) {
    if (i2c_fd < 0) return -1;
    return write(i2c_fd, data, len);
}

static int i2c_read(uint8_t* data, int len) {
    if (i2c_fd < 0) return -1;
    return read(i2c_fd, data, len);
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Public API
 * ───────────────────────────────────────────────────────────────────────────── */

int sound_init(void) {
    i2c_fd = open(SOUND_I2C_BUS, O_RDWR);
    if (i2c_fd < 0) {
        perror("Failed to open I2C bus");
        return -1;
    }
    
    if (ioctl(i2c_fd, I2C_SLAVE, SOUND_I2C_ADDR) < 0) {
        perror("Failed to set I2C slave address");
        close(i2c_fd);
        i2c_fd = -1;
        return -1;
    }
    
    printf("[SOUND] Initialized on %s @ 0x%02X\n", SOUND_I2C_BUS, SOUND_I2C_ADDR);
    
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

void sound_shutdown(void) {
    if (i2c_fd >= 0) {
        close(i2c_fd);
        i2c_fd = -1;
    }
}

bool sound_load_json(const char* json_path) {
    FILE* f = fopen(json_path, "r");
    if (!f) {
        printf("[SOUND] No sounds.json found at %s, using defaults\n", json_path);
        return false;
    }
    
    /* Read entire file */
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    char* json = malloc(size + 1);
    if (!json) {
        fclose(f);
        return false;
    }
    
    fread(json, 1, size, f);
    json[size] = '\0';
    fclose(f);
    
    printf("[SOUND] Loading sounds from %s (%ld bytes)\n", json_path, size);
    
    /* Send JSON to Pico in chunks */
    int chunk_size = 28;  /* Leave room for command header */
    int offset = 0;
    
    while (offset < size) {
        int remaining = size - offset;
        int len = (remaining > chunk_size) ? chunk_size : remaining;
        
        uint8_t buf[32];
        buf[0] = CMD_UPLOAD_CHUNK;
        buf[1] = (len >> 8) & 0xFF;
        buf[2] = len & 0xFF;
        memcpy(&buf[3], &json[offset], len);
        
        if (i2c_write(buf, 3 + len) < 0) {
            printf("[SOUND] Failed to send chunk at offset %d\n", offset);
            free(json);
            return false;
        }
        
        offset += len;
        usleep(5000);  /* Give Pico time to process */
    }
    
    /* Tell Pico to parse the JSON */
    uint8_t cmd = CMD_PARSE_JSON;
    i2c_write(&cmd, 1);
    
    /* Parse locally to build name->ID mapping */
    sound_count = 0;
    
    /* Simple JSON parsing - look for sound names */
    char* p = strstr(json, "\"sounds\"");
    if (p) {
        p = strchr(p, '{');
        if (p) {
            p++;
            while (*p && sound_count < MAX_SOUNDS) {
                /* Find next quoted string */
                char* q = strchr(p, '"');
                if (!q) break;
                q++;
                char* end = strchr(q, '"');
                if (!end) break;
                
                int name_len = end - q;
                if (name_len < 31 && name_len > 0) {
                    /* Check if this is a sound name (not a property) */
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
    
    printf("[SOUND] Loaded %d sounds\n", sound_count);
    for (int i = 0; i < sound_count; i++) {
        printf("  [%d] %s\n", i, sound_names[i]);
    }
    
    free(json);
    return true;
}

void sound_play(int sound_id) {
    if (i2c_fd < 0) return;
    
    uint8_t buf[2] = { CMD_PLAY_SOUND, (uint8_t)sound_id };
    i2c_write(buf, 2);
}

void sound_play_name(const char* name) {
    for (int i = 0; i < sound_count; i++) {
        if (strcmp(sound_names[i], name) == 0) {
            sound_play(i);
            return;
        }
    }
    printf("[SOUND] Unknown sound: %s\n", name);
}

void sound_stop(void) {
    if (i2c_fd < 0) return;
    
    uint8_t cmd = CMD_STOP_SOUND;
    i2c_write(&cmd, 1);
}

void sound_set_volume(int volume) {
    if (i2c_fd < 0) return;
    
    uint8_t buf[2] = { CMD_SET_VOLUME, (uint8_t)volume };
    i2c_write(buf, 2);
}

uint16_t sound_read_buttons(void) {
    if (i2c_fd < 0) return 0;
    
    uint8_t cmd = CMD_READ_BUTTONS;
    i2c_write(&cmd, 1);
    
    usleep(100);
    
    uint8_t buf[2] = {0, 0};
    i2c_read(buf, 2);
    
    return buf[0] | (buf[1] << 8);
}
