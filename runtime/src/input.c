/*
 * ThermoConsole Runtime
 * Input module - unified input handling with platform abstraction
 * 
 * This module delegates actual input reading to platform-specific code:
 *   - PC: SDL keyboard and gamepad
 *   - Pi: Pico controller over serial + keyboard fallback
 */

#include <stdio.h>
#include <string.h>
#include "thermo.h"
#include "platform.h"

/* ─────────────────────────────────────────────────────────────────────────────
 * Initialization
 * ───────────────────────────────────────────────────────────────────────────── */

void input_init(void) {
    ThermoInput* input = &g_thermo->input;
    
    /* Clear all button states */
    memset(input, 0, sizeof(ThermoInput));
    
    /* Initialize platform-specific input */
    platform_input_init();
}

void input_shutdown(void) {
    platform_input_shutdown();
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Update
 * ───────────────────────────────────────────────────────────────────────────── */

void input_update(void) {
    /* Platform-specific code handles reading input and updating
     * g_thermo->input.buttons[] with held/pressed/released states */
    platform_input_update();
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Query Functions
 * ───────────────────────────────────────────────────────────────────────────── */

bool input_btn(int id) {
    if (id < 0 || id >= THERMO_BTN_COUNT) return false;
    return g_thermo->input.buttons[id].held;
}

bool input_btnp(int id) {
    if (id < 0 || id >= THERMO_BTN_COUNT) return false;
    return g_thermo->input.buttons[id].pressed;
}

bool input_btnr(int id) {
    if (id < 0 || id >= THERMO_BTN_COUNT) return false;
    return g_thermo->input.buttons[id].released;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Utility Functions
 * ───────────────────────────────────────────────────────────────────────────── */

/*
 * Get a bitmask of all currently held buttons.
 */
uint16_t input_get_state(void) {
    uint16_t state = 0;
    for (int i = 0; i < THERMO_BTN_COUNT; i++) {
        if (g_thermo->input.buttons[i].held) {
            state |= (1 << i);
        }
    }
    return state;
}

/*
 * Check if any button is pressed.
 */
bool input_any_pressed(void) {
    for (int i = 0; i < THERMO_BTN_COUNT; i++) {
        if (g_thermo->input.buttons[i].pressed) {
            return true;
        }
    }
    return false;
}

/*
 * Get the name of a button for display.
 */
const char* input_button_name(int id) {
    static const char* names[] = {
        "Up", "Down", "Left", "Right",
        "A", "B", "X", "Y",
        "Start", "Select"
    };
    
    if (id < 0 || id >= THERMO_BTN_COUNT) return "?";
    return names[id];
}
