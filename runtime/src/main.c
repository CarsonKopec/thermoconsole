/*
 * ThermoConsole Runtime
 * Main entry point and game loop
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_mixer.h>
#include <SDL2/SDL_image.h>
#include "thermo.h"
#include "platform.h"

/* Global engine instance */
ThermoEngine* g_thermo = NULL;

/* Global settings */
int g_show_fps = 0;  /* Show FPS counter overlay */

/* ─────────────────────────────────────────────────────────────────────────────
 * Utility Functions
 * ───────────────────────────────────────────────────────────────────────────── */

uint64_t get_time_ms(void) {
    return SDL_GetTicks64();
}

double thermo_time(void) {
    return g_thermo->state.time_elapsed;
}

double thermo_dt(void) {
    return g_thermo->state.delta_time;
}

int thermo_fps(void) {
    return g_thermo->state.current_fps;
}

static unsigned int rng_state = 12345;

void thermo_srand(unsigned int seed) {
    rng_state = seed;
}

float thermo_rnd(float max) {
    rng_state = rng_state * 1103515245 + 12345;
    float t = (float)(rng_state & 0x7FFFFFFF) / (float)0x7FFFFFFF;
    return t * max;
}

int thermo_irnd(int max) {
    if (max <= 0) return 0;
    return (int)thermo_rnd((float)max);
}

ThermoColor color_from_index(int index) {
    if (index < 0 || index >= THERMO_PALETTE_SIZE) {
        return g_thermo->gfx.palette[0];
    }
    /* Check for palette remapping */
    return g_thermo->gfx.palette_remap[index];
}

ThermoColor color_from_hex(uint32_t hex) {
    ThermoColor c;
    c.r = (hex >> 16) & 0xFF;
    c.g = (hex >> 8) & 0xFF;
    c.b = hex & 0xFF;
    c.a = 0xFF;
    return c;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Engine Initialization
 * ───────────────────────────────────────────────────────────────────────────── */

int thermo_init(const char* rom_path) {
    printf("ThermoConsole Runtime v%s\n", THERMO_VERSION_STRING);
    printf("Platform: %s\n", platform_get_name());
    printf("────────────────────────────────────\n");
    
    /* Allocate engine */
    g_thermo = (ThermoEngine*)calloc(1, sizeof(ThermoEngine));
    if (!g_thermo) {
        fprintf(stderr, "ERROR: Failed to allocate engine\n");
        return -1;
    }
    
    /* Initialize platform-specific graphics hints (before SDL_Init) */
    platform_gfx_init();
    
    /* Initialize SDL */
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) < 0) {
        fprintf(stderr, "ERROR: SDL_Init failed: %s\n", SDL_GetError());
        return -1;
    }
    printf("[OK] SDL initialized\n");
    
    /* Initialize SDL_image */
    int img_flags = IMG_INIT_PNG;
    if (!(IMG_Init(img_flags) & img_flags)) {
        fprintf(stderr, "ERROR: SDL_image init failed: %s\n", IMG_GetError());
        return -1;
    }
    printf("[OK] SDL_image initialized\n");
    
    /* Initialize SDL_mixer */
    if (Mix_OpenAudio(THERMO_SAMPLE_RATE, MIX_DEFAULT_FORMAT, 2, 2048) < 0) {
        fprintf(stderr, "ERROR: SDL_mixer init failed: %s\n", Mix_GetError());
        return -1;
    }
    Mix_AllocateChannels(THERMO_AUDIO_CHANNELS);
    printf("[OK] SDL_mixer initialized\n");
    
    /* Initialize platform-specific audio */
    platform_audio_init();
    
    /* Initialize graphics */
    if (gfx_init() < 0) {
        return -1;
    }
    printf("[OK] Graphics initialized (%dx%d)\n", 
           THERMO_SCREEN_WIDTH, THERMO_SCREEN_HEIGHT);
    
    /* Initialize input */
    input_init();
    printf("[OK] Input initialized\n");
    
    /* Initialize audio */
    if (audio_init() < 0) {
        return -1;
    }
    printf("[OK] Audio initialized\n");
    
    /* Load ROM */
    if (rom_path) {
        printf("[..] Loading ROM: %s\n", rom_path);
        g_thermo->rom = rom_load(rom_path);
        if (!g_thermo->rom) {
            fprintf(stderr, "ERROR: Failed to load ROM\n");
            return -1;
        }
        printf("[OK] ROM loaded: %s v%s by %s\n",
               g_thermo->rom->manifest.name,
               g_thermo->rom->manifest.version,
               g_thermo->rom->manifest.author);
        
        /* Load sprites if present */
        char sprites_path[512];
        snprintf(sprites_path, sizeof(sprites_path), "%s/%s",
                 g_thermo->rom->base_path,
                 g_thermo->rom->manifest.sprites_file);
        
        g_thermo->rom->sprites.texture = gfx_load_texture(sprites_path);
        if (g_thermo->rom->sprites.texture) {
            g_thermo->rom->sprites.grid_size = g_thermo->rom->manifest.sprite_grid_size;
            SDL_QueryTexture(g_thermo->rom->sprites.texture, NULL, NULL,
                           &g_thermo->rom->sprites.width,
                           &g_thermo->rom->sprites.height);
            printf("[OK] Sprites loaded (%dx%d, grid %d)\n",
                   g_thermo->rom->sprites.width,
                   g_thermo->rom->sprites.height,
                   g_thermo->rom->sprites.grid_size);
        }
        
        /* Initialize script */
        char entry_path[512];
        snprintf(entry_path, sizeof(entry_path), "%s/%s",
                 g_thermo->rom->base_path,
                 g_thermo->rom->manifest.entry);
        
        if (script_init(entry_path) < 0) {
            fprintf(stderr, "ERROR: Failed to initialize script\n");
            return -1;
        }
        printf("[OK] Script loaded: %s\n", g_thermo->rom->manifest.entry);
    }
    
    /* Initialize state */
    g_thermo->state.running = true;
    g_thermo->state.paused = false;
    g_thermo->state.frame_count = 0;
    g_thermo->state.time_elapsed = 0.0;
    g_thermo->state.delta_time = 1.0 / THERMO_TARGET_FPS;
    g_thermo->state.last_frame_time = get_time_ms();
    g_thermo->state.current_fps = THERMO_TARGET_FPS;
    g_thermo->state.fps_counter = 0;
    g_thermo->state.fps_timer = get_time_ms();
    
    /* Seed RNG */
    thermo_srand((unsigned int)SDL_GetTicks());
    
    printf("────────────────────────────────────\n");
    printf("Ready!\n\n");
    
    return 0;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Main Game Loop
 * ───────────────────────────────────────────────────────────────────────────── */

void thermo_run(void) {
    if (!g_thermo) return;
    
    /* Call Lua _init() */
    script_call_init();
    
    while (g_thermo->state.running) {
        uint64_t frame_start = get_time_ms();
        
        /* ─── Event Handling ─── */
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
                case SDL_QUIT:
                    g_thermo->state.running = false;
                    break;
                    
                case SDL_KEYDOWN:
                    if (event.key.keysym.sym == SDLK_ESCAPE) {
                        g_thermo->state.running = false;
                    }
                    break;
                    
                case SDL_WINDOWEVENT:
                    if (event.window.event == SDL_WINDOWEVENT_RESIZED) {
                        g_thermo->gfx.window_width = event.window.data1;
                        g_thermo->gfx.window_height = event.window.data2;
                    }
                    break;
            }
        }
        
        /* ─── Input ─── */
        input_update();
        
        /* ─── Update ─── */
        if (!g_thermo->state.paused) {
            script_call_update();
        }
        
        /* ─── Render ─── */
        gfx_begin_frame();
        script_call_draw();

        /* Draw FPS overlay if enabled */
        if (g_show_fps) {
            char fps_text[16];
            snprintf(fps_text, sizeof(fps_text), "FPS:%d", g_thermo->state.current_fps);
            /* Draw in top-right corner with black background */
            gfx_rectfill(THERMO_WIDTH - 50, 0, 50, 10, 0);  /* Black bg */
            gfx_print(fps_text, THERMO_WIDTH - 48, 1, 11);   /* Green text */
        }

        gfx_end_frame();
        
        /* ─── Timing ─── */
        g_thermo->state.frame_count++;
        g_thermo->state.fps_counter++;
        
        /* Calculate FPS every second */
        uint64_t now = get_time_ms();
        if (now - g_thermo->state.fps_timer >= 1000) {
            g_thermo->state.current_fps = g_thermo->state.fps_counter;
            g_thermo->state.fps_counter = 0;
            g_thermo->state.fps_timer = now;
        }
        
        /* Frame limiting */
        uint64_t frame_time = get_time_ms() - frame_start;
        if (frame_time < (uint64_t)THERMO_FRAME_TIME) {
            SDL_Delay((uint32_t)(THERMO_FRAME_TIME - frame_time));
        }
        
        /* Update delta time */
        uint64_t current_time = get_time_ms();
        g_thermo->state.delta_time = (current_time - g_thermo->state.last_frame_time) / 1000.0;
        g_thermo->state.last_frame_time = current_time;
        g_thermo->state.time_elapsed += g_thermo->state.delta_time;
    }
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Shutdown
 * ───────────────────────────────────────────────────────────────────────────── */

void thermo_shutdown(void) {
    printf("\nShutting down...\n");
    
    if (g_thermo) {
        /* Cleanup script */
        script_shutdown();
        
        /* Cleanup ROM */
        if (g_thermo->rom) {
            rom_free(g_thermo->rom);
        }
        
        /* Cleanup input */
        input_shutdown();
        
        /* Cleanup audio */
        platform_audio_shutdown();
        audio_shutdown();
        
        /* Cleanup graphics */
        platform_gfx_shutdown();
        gfx_shutdown();
        
        free(g_thermo);
        g_thermo = NULL;
    }
    
    /* Cleanup SDL */
    Mix_CloseAudio();
    Mix_Quit();
    IMG_Quit();
    SDL_Quit();
    
    printf("Goodbye!\n");
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Entry Point
 * ───────────────────────────────────────────────────────────────────────────── */

int main(int argc, char* argv[]) {
    const char* rom_path = NULL;
    
    /* Parse arguments */
    if (argc >= 2) {
        rom_path = argv[1];
    } else {
        printf("Usage: %s <game.tcr>\n", argv[0]);
        printf("       %s <game_folder>\n", argv[0]);
        return 1;
    }
    
    /* Initialize */
    if (thermo_init(rom_path) < 0) {
        thermo_shutdown();
        return 1;
    }
    
    /* Run game loop */
    thermo_run();
    
    /* Cleanup */
    thermo_shutdown();
    
    return 0;
}
