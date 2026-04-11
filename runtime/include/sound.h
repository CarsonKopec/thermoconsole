/*
 * ThermoConsole Sound Module
 * Communicates with Pico sound chip over I2C
 */

#ifndef THERMO_SOUND_H
#define THERMO_SOUND_H

#include <stdint.h>
#include <stdbool.h>

/* I2C Configuration */
#define SOUND_I2C_BUS     "/dev/i2c-3"
#define SOUND_I2C_ADDR    0x42

/* Commands */
#define CMD_UPLOAD_CHUNK  0x01
#define CMD_PARSE_JSON    0x02
#define CMD_PLAY_SOUND    0x10
#define CMD_STOP_SOUND    0x11
#define CMD_SET_VOLUME    0x12
#define CMD_READ_BUTTONS  0x20
#define CMD_GET_STATUS    0x21

/* Sound IDs (default sounds) */
#define SFX_JUMP       0
#define SFX_COIN       1
#define SFX_HIT        2
#define SFX_POWERUP    3
#define SFX_DEATH      4
#define SFX_SELECT     5
#define SFX_START      6
#define SFX_EXPLOSION  7
#define SFX_LASER      8
#define SFX_PICKUP     9
#define SFX_MENU_MOVE  10
#define SFX_MENU_SELECT 11
#define SFX_GAME_OVER  12
#define SFX_VICTORY    13
#define SFX_LEVEL_START 14

/* Initialize sound system */
int sound_init(void);

/* Shutdown sound system */
void sound_shutdown(void);

/* Load sounds from JSON file */
bool sound_load_json(const char* json_path);

/* Play a sound by ID */
void sound_play(int sound_id);

/* Play a sound by name (looks up in loaded sounds) */
void sound_play_name(const char* name);

/* Stop all sounds */
void sound_stop(void);

/* Set volume (0-255) */
void sound_set_volume(int volume);

/* Read buttons (returns 16-bit button state) */
uint16_t sound_read_buttons(void);

#endif /* THERMO_SOUND_H */
