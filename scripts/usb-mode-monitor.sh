#!/bin/bash
#
# ThermoConsole USB Mode Monitor
#
# Watches for Start+Select button combo (held for 3 seconds) to toggle USB mode.
# Also monitors USB connection state for auto-switching.
#

GADGET_SCRIPT="/opt/thermoconsole/scripts/usb-gadget.sh"
I2C_BUS=1
PICO_ADDR=0x42

# Button bits
BTN_START=8
BTN_SELECT=9
COMBO_MASK=$(( (1 << BTN_START) | (1 << BTN_SELECT) ))

HOLD_TIME=3  # Seconds to hold combo
LED_PIN=25   # Status LED (optional)

log() {
    echo "[USB-MONITOR] $1"
}

read_buttons() {
    # Read 2 bytes from Pico controller via I2C
    local data=$(i2cget -y $I2C_BUS $PICO_ADDR 0x00 w 2>/dev/null)
    if [ -z "$data" ]; then
        echo 0
        return
    fi
    
    # i2cget returns in format 0xHHLL (little endian word)
    # Convert to button state
    local raw=$((data))
    echo $raw
}

is_combo_pressed() {
    local buttons=$(read_buttons)
    local masked=$((buttons & COMBO_MASK))
    [ $masked -eq $COMBO_MASK ]
}

is_usb_mode() {
    [ -d "/sys/kernel/config/usb_gadget/thermoconsole" ] && \
    [ -n "$(cat /sys/kernel/config/usb_gadget/thermoconsole/UDC 2>/dev/null)" ]
}

toggle_usb_mode() {
    log "Toggling USB mode..."
    
    if is_usb_mode; then
        log "Switching to GAME mode"
        $GADGET_SCRIPT disable
    else
        log "Switching to USB mode"
        $GADGET_SCRIPT enable
    fi
}

# Visual feedback (blink LED if available)
blink_led() {
    local count=${1:-3}
    local gpio_path="/sys/class/gpio/gpio${LED_PIN}"
    
    if [ ! -d "$gpio_path" ]; then
        echo $LED_PIN > /sys/class/gpio/export 2>/dev/null || return
        echo out > "${gpio_path}/direction" 2>/dev/null || return
    fi
    
    for ((i=0; i<count; i++)); do
        echo 1 > "${gpio_path}/value" 2>/dev/null
        sleep 0.1
        echo 0 > "${gpio_path}/value" 2>/dev/null
        sleep 0.1
    done
}

main() {
    log "Starting USB mode monitor"
    log "Hold Start+Select for ${HOLD_TIME}s to toggle USB mode"
    
    local combo_start=0
    local combo_active=false
    
    while true; do
        if is_combo_pressed; then
            if ! $combo_active; then
                # Combo just started
                combo_start=$(date +%s)
                combo_active=true
                log "Combo detected, hold for ${HOLD_TIME}s..."
            else
                # Check if held long enough
                local now=$(date +%s)
                local held=$((now - combo_start))
                
                if [ $held -ge $HOLD_TIME ]; then
                    log "Combo held for ${HOLD_TIME}s - toggling!"
                    blink_led 5
                    toggle_usb_mode
                    
                    # Wait for buttons to be released
                    while is_combo_pressed; do
                        sleep 0.1
                    done
                    
                    combo_active=false
                    sleep 1
                fi
            fi
        else
            combo_active=false
        fi
        
        sleep 0.2
    done
}

main "$@"
