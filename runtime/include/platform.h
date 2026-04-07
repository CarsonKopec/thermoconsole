/*
 * ThermoConsole Runtime
 * Platform abstraction header
 * 
 * Declares platform-specific functions that have different implementations
 * for PC (development) and Pi Zero (hardware).
 */

#ifndef THERMO_PLATFORM_H
#define THERMO_PLATFORM_H

#include <stdbool.h>
#include <stdint.h>

/* ─────────────────────────────────────────────────────────────────────────────
 * Platform Detection
 * ───────────────────────────────────────────────────────────────────────────── */

/* Set by CMake or Makefile:
 *   -DTHERMO_PLATFORM_PC   for desktop development
 *   -DTHERMO_PLATFORM_PI   for Raspberry Pi deployment
 * 
 * If neither is set, default to PC.
 */
#if !defined(THERMO_PLATFORM_PC) && !defined(THERMO_PLATFORM_PI)
    #define THERMO_PLATFORM_PC
#endif

/* ─────────────────────────────────────────────────────────────────────────────
 * Platform Input
 * ───────────────────────────────────────────────────────────────────────────── */

/*
 * Initialize platform-specific input system.
 * - PC: Opens SDL game controllers
 * - Pi: Opens serial connection to Pico controller
 * 
 * Returns 0 on success, -1 on failure.
 */
int platform_input_init(void);

/*
 * Shutdown platform-specific input system.
 */
void platform_input_shutdown(void);

/*
 * Update input state from platform-specific sources.
 * Called once per frame before game logic.
 * 
 * - PC: Reads keyboard and gamepad via SDL
 * - Pi: Reads button state from Pico over serial
 */
void platform_input_update(void);

/* ─────────────────────────────────────────────────────────────────────────────
 * Platform Graphics
 * ───────────────────────────────────────────────────────────────────────────── */

/*
 * Initialize platform-specific graphics.
 * Called after SDL is initialized but before window creation.
 * 
 * - PC: Sets window hints for desktop
 * - Pi: Configures KMS/DRM for framebuffer output
 * 
 * Returns 0 on success, -1 on failure.
 */
int platform_gfx_init(void);

/*
 * Shutdown platform-specific graphics.
 */
void platform_gfx_shutdown(void);

/*
 * Get platform-specific SDL window flags.
 * 
 * - PC: Returns 0 or SDL_WINDOW_RESIZABLE
 * - Pi: Returns SDL_WINDOW_FULLSCREEN
 */
uint32_t platform_get_window_flags(void);

/*
 * Get the display scale factor.
 * 
 * - PC: Returns calculated scale to fit window
 * - Pi: Returns 1 (native resolution)
 */
int platform_get_display_scale(void);

/* ─────────────────────────────────────────────────────────────────────────────
 * Platform Audio
 * ───────────────────────────────────────────────────────────────────────────── */

/*
 * Initialize platform-specific audio.
 * Called after SDL_mixer is initialized.
 * 
 * - PC: Default configuration
 * - Pi: May configure ALSA device selection
 * 
 * Returns 0 on success, -1 on failure.
 */
int platform_audio_init(void);

/*
 * Shutdown platform-specific audio.
 */
void platform_audio_shutdown(void);

/* ─────────────────────────────────────────────────────────────────────────────
 * Platform Filesystem
 * ───────────────────────────────────────────────────────────────────────────── */

/*
 * Get the base path for ROM extraction.
 * 
 * - PC: Returns system temp directory
 * - Pi: Returns /tmp/thermo/ (tmpfs for speed)
 */
const char* platform_get_temp_path(void);

/*
 * Get the save data directory.
 * 
 * - PC: Returns ~/.thermoconsole/ or %APPDATA%\thermoconsole\
 * - Pi: Returns /home/pi/.thermoconsole/
 */
const char* platform_get_save_path(void);

/* ─────────────────────────────────────────────────────────────────────────────
 * Platform Utilities
 * ───────────────────────────────────────────────────────────────────────────── */

/*
 * Get platform name string for display.
 */
const char* platform_get_name(void);

/*
 * Check if running on battery power (if detectable).
 * Always returns false on Pi (assumed always plugged in).
 */
bool platform_on_battery(void);

/*
 * Get CPU temperature in Celsius (Pi only).
 * Returns -1 on PC or if unavailable.
 */
float platform_get_cpu_temp(void);

#endif /* THERMO_PLATFORM_H */
