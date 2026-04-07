/*
 * ThermoConsole Runtime
 * Audio module - sound effects and music
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_mixer.h>
#include "thermo.h"

/* Sound effect cache */
typedef struct {
    char name[64];
    Mix_Chunk* chunk;
} SfxEntry;

static SfxEntry sfx_cache[128];
static int sfx_cache_count = 0;

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
    sfx_cache_count = 0;
    
    return 0;
}

void audio_shutdown(void) {
    /* Free cached sound effects */
    for (int i = 0; i < sfx_cache_count; i++) {
        if (sfx_cache[i].chunk) {
            Mix_FreeChunk(sfx_cache[i].chunk);
        }
    }
    sfx_cache_count = 0;
    
    /* Free music */
    ThermoAudio* audio = &g_thermo->audio;
    if (audio->music) {
        Mix_FreeMusic(audio->music);
        audio->music = NULL;
    }
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Sound Effect Loading
 * ───────────────────────────────────────────────────────────────────────────── */

static Mix_Chunk* load_sfx(const char* name) {
    /* Check cache first */
    for (int i = 0; i < sfx_cache_count; i++) {
        if (strcmp(sfx_cache[i].name, name) == 0) {
            return sfx_cache[i].chunk;
        }
    }
    
    /* Not cached, load it */
    if (!g_thermo->rom) return NULL;
    
    char path[512];
    
    /* Try .wav first */
    snprintf(path, sizeof(path), "%s/sfx/%s.wav", g_thermo->rom->base_path, name);
    Mix_Chunk* chunk = Mix_LoadWAV(path);
    
    /* Try .ogg if wav not found */
    if (!chunk) {
        snprintf(path, sizeof(path), "%s/sfx/%s.ogg", g_thermo->rom->base_path, name);
        chunk = Mix_LoadWAV(path);
    }
    
    if (!chunk) {
        fprintf(stderr, "WARN: Could not load sfx '%s': %s\n", name, Mix_GetError());
        return NULL;
    }
    
    /* Cache it */
    if (sfx_cache_count < 128) {
        strncpy(sfx_cache[sfx_cache_count].name, name, 63);
        sfx_cache[sfx_cache_count].chunk = chunk;
        sfx_cache_count++;
    }
    
    return chunk;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Playback Functions
 * ───────────────────────────────────────────────────────────────────────────── */

void audio_sfx(const char* name, int channel, bool loop) {
    Mix_Chunk* chunk = load_sfx(name);
    if (!chunk) return;
    
    int loops = loop ? -1 : 0;
    
    if (channel < 0) {
        /* Auto-select channel */
        Mix_PlayChannel(-1, chunk, loops);
    } else if (channel < THERMO_AUDIO_CHANNELS) {
        Mix_PlayChannel(channel, chunk, loops);
    }
}

void audio_music(const char* name, bool loop) {
    if (!g_thermo->rom) return;
    
    ThermoAudio* audio = &g_thermo->audio;
    
    /* Stop current music */
    if (audio->music) {
        Mix_FreeMusic(audio->music);
        audio->music = NULL;
    }
    
    char path[512];
    
    /* Try .ogg first (preferred for music) */
    snprintf(path, sizeof(path), "%s/music/%s.ogg", g_thermo->rom->base_path, name);
    audio->music = Mix_LoadMUS(path);
    
    /* Try .wav if ogg not found */
    if (!audio->music) {
        snprintf(path, sizeof(path), "%s/music/%s.wav", g_thermo->rom->base_path, name);
        audio->music = Mix_LoadMUS(path);
    }
    
    if (!audio->music) {
        fprintf(stderr, "WARN: Could not load music '%s': %s\n", name, Mix_GetError());
        return;
    }
    
    Mix_PlayMusic(audio->music, loop ? -1 : 1);
}

void audio_stop(int channel) {
    if (channel < 0) {
        /* Stop music */
        Mix_HaltMusic();
    } else if (channel < THERMO_AUDIO_CHANNELS) {
        /* Stop specific channel */
        Mix_HaltChannel(channel);
    } else {
        /* Stop all */
        Mix_HaltChannel(-1);
        Mix_HaltMusic();
    }
}

void audio_volume(float level, int channel) {
    ThermoAudio* audio = &g_thermo->audio;
    
    if (level < 0.0f) level = 0.0f;
    if (level > 1.0f) level = 1.0f;
    
    int vol = (int)(level * MIX_MAX_VOLUME);
    
    if (channel < 0) {
        /* Set master volume */
        audio->master_volume = level;
        Mix_Volume(-1, vol);
        Mix_VolumeMusic(vol);
    } else if (channel < THERMO_AUDIO_CHANNELS) {
        /* Set channel volume */
        audio->channel_volume[channel] = level;
        Mix_Volume(channel, vol);
    }
}
