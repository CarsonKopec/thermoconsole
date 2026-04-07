#!/bin/bash
#
# ThermoConsole USB Gadget Mode
# 
# Makes the Pi Zero appear as a USB drive when connected to a PC,
# allowing drag-and-drop game loading.
#
# The games partition is exposed as a mass storage device.
# When the user ejects the drive, games are automatically detected
# and ThermoConsole restarts with the new content.
#

set -e

# ═══════════════════════════════════════════════════════════════════════════════
# Configuration
# ═══════════════════════════════════════════════════════════════════════════════

GADGET_NAME="thermoconsole"
GADGET_DIR="/sys/kernel/config/usb_gadget/${GADGET_NAME}"

# Storage image for games (will be mounted as USB drive)
STORAGE_IMAGE="/opt/thermoconsole/games.img"
STORAGE_SIZE_MB=256
STORAGE_MOUNT="/home/pi/games"

# USB IDs (using Linux Foundation test IDs)
USB_VENDOR_ID="0x1d6b"   # Linux Foundation
USB_PRODUCT_ID="0x0104"  # Multifunction Composite Gadget
USB_VERSION="0x0100"

# Device strings
MANUFACTURER="ThermoConsole"
PRODUCT="ThermoConsole Game Console"
SERIAL="TC$(cat /proc/cpuinfo | grep Serial | cut -d: -f2 | tr -d ' ' | tail -c 9)"

# ═══════════════════════════════════════════════════════════════════════════════
# Helper Functions
# ═══════════════════════════════════════════════════════════════════════════════

log() {
    echo "[USB-GADGET] $1"
}

error() {
    echo "[USB-GADGET ERROR] $1" >&2
    exit 1
}

# ═══════════════════════════════════════════════════════════════════════════════
# Storage Image Management
# ═══════════════════════════════════════════════════════════════════════════════

create_storage_image() {
    if [ -f "$STORAGE_IMAGE" ]; then
        log "Storage image already exists"
        return 0
    fi
    
    log "Creating ${STORAGE_SIZE_MB}MB storage image..."
    
    # Create sparse file
    dd if=/dev/zero of="$STORAGE_IMAGE" bs=1M count=0 seek="$STORAGE_SIZE_MB" 2>/dev/null
    
    # Format as FAT32 (most compatible with Windows/Mac/Linux)
    mkfs.vfat -F 32 -n "TCGAMES" "$STORAGE_IMAGE"
    
    log "Storage image created: $STORAGE_IMAGE"
}

mount_storage() {
    if mountpoint -q "$STORAGE_MOUNT" 2>/dev/null; then
        log "Storage already mounted"
        return 0
    fi
    
    log "Mounting storage to $STORAGE_MOUNT..."
    
    mkdir -p "$STORAGE_MOUNT"
    mount -o loop,uid=1000,gid=1000 "$STORAGE_IMAGE" "$STORAGE_MOUNT"
    
    # Create default structure if empty
    if [ ! -f "$STORAGE_MOUNT/README.txt" ]; then
        cat > "$STORAGE_MOUNT/README.txt" << 'EOF'
╔═══════════════════════════════════════════════════════════════╗
║                    ThermoConsole Games                        ║
╚═══════════════════════════════════════════════════════════════╝

Drop your .tcr game files or game folders here!

SUPPORTED FORMATS:
  • .tcr files (ThermoConsole ROM archives)
  • Game folders containing main.lua and manifest.json

HOW TO USE:
  1. Copy games to this drive
  2. Safely eject the drive
  3. ThermoConsole will detect new games automatically

SWITCHING GAMES:
  • Edit the file "CURRENT.txt" with the game name
  • Or use the console's menu (Start + Select)

CURRENT GAME:
  Check CURRENT.txt to see which game is active.

Need help? Visit: https://thermoconsole.io/docs
EOF
        
        # Create current game marker
        echo "space_dodge" > "$STORAGE_MOUNT/CURRENT.txt"
        
        # Copy default games
        if [ -d "/opt/thermoconsole/games" ]; then
            cp -r /opt/thermoconsole/games/* "$STORAGE_MOUNT/" 2>/dev/null || true
        fi
    fi
    
    log "Storage mounted"
}

unmount_storage() {
    if mountpoint -q "$STORAGE_MOUNT" 2>/dev/null; then
        log "Unmounting storage..."
        sync
        umount "$STORAGE_MOUNT" || umount -l "$STORAGE_MOUNT"
    fi
}

# ═══════════════════════════════════════════════════════════════════════════════
# USB Gadget Setup
# ═══════════════════════════════════════════════════════════════════════════════

setup_gadget() {
    log "Setting up USB gadget..."
    
    # Load required modules
    modprobe libcomposite
    modprobe usb_f_mass_storage
    
    # Check if configfs is mounted
    if [ ! -d /sys/kernel/config/usb_gadget ]; then
        mount -t configfs none /sys/kernel/config
    fi
    
    # Remove existing gadget if present
    if [ -d "$GADGET_DIR" ]; then
        remove_gadget
    fi
    
    # Create gadget
    mkdir -p "$GADGET_DIR"
    cd "$GADGET_DIR"
    
    # Set USB IDs
    echo "$USB_VENDOR_ID" > idVendor
    echo "$USB_PRODUCT_ID" > idProduct
    echo "$USB_VERSION" > bcdDevice
    echo "0x0200" > bcdUSB  # USB 2.0
    
    # Set device class (use interface class)
    echo "0xEF" > bDeviceClass
    echo "0x02" > bDeviceSubClass
    echo "0x01" > bDeviceProtocol
    
    # Create English strings
    mkdir -p strings/0x409
    echo "$SERIAL" > strings/0x409/serialnumber
    echo "$MANUFACTURER" > strings/0x409/manufacturer
    echo "$PRODUCT" > strings/0x409/product
    
    # Create configuration
    mkdir -p configs/c.1/strings/0x409
    echo "ThermoConsole Config" > configs/c.1/strings/0x409/configuration
    echo 250 > configs/c.1/MaxPower  # 250mA
    
    # Create mass storage function
    mkdir -p functions/mass_storage.usb0
    echo 1 > functions/mass_storage.usb0/stall
    echo 0 > functions/mass_storage.usb0/lun.0/cdrom
    echo 0 > functions/mass_storage.usb0/lun.0/ro
    echo 0 > functions/mass_storage.usb0/lun.0/nofua
    echo 1 > functions/mass_storage.usb0/lun.0/removable
    echo "$STORAGE_IMAGE" > functions/mass_storage.usb0/lun.0/file
    
    # Link function to configuration
    ln -sf functions/mass_storage.usb0 configs/c.1/
    
    # Enable gadget (bind to UDC)
    ls /sys/class/udc > UDC
    
    log "USB gadget enabled"
}

remove_gadget() {
    log "Removing USB gadget..."
    
    if [ ! -d "$GADGET_DIR" ]; then
        return 0
    fi
    
    cd "$GADGET_DIR"
    
    # Disable gadget
    echo "" > UDC 2>/dev/null || true
    
    # Remove function links
    rm -f configs/c.1/mass_storage.usb0 2>/dev/null || true
    
    # Remove configurations
    rmdir configs/c.1/strings/0x409 2>/dev/null || true
    rmdir configs/c.1 2>/dev/null || true
    
    # Remove functions
    rmdir functions/mass_storage.usb0 2>/dev/null || true
    
    # Remove strings
    rmdir strings/0x409 2>/dev/null || true
    
    # Remove gadget
    cd /
    rmdir "$GADGET_DIR" 2>/dev/null || true
    
    log "USB gadget removed"
}

# ═══════════════════════════════════════════════════════════════════════════════
# Mode Switching
# ═══════════════════════════════════════════════════════════════════════════════

enable_usb_mode() {
    log "Enabling USB storage mode..."
    
    # Stop ThermoConsole
    systemctl stop thermoconsole.service 2>/dev/null || true
    
    # Unmount storage from local filesystem
    unmount_storage
    
    # Sync and wait
    sync
    sleep 1
    
    # Enable USB gadget
    setup_gadget
    
    log "USB mode enabled - console appears as USB drive on PC"
    log "Eject the drive on PC to return to game mode"
}

disable_usb_mode() {
    log "Disabling USB storage mode..."
    
    # Remove USB gadget
    remove_gadget
    
    # Wait for USB to disconnect
    sleep 1
    
    # Mount storage locally
    mount_storage
    
    # Sync games and detect new ones
    sync_games
    
    # Restart ThermoConsole
    systemctl start thermoconsole.service 2>/dev/null || true
    
    log "Game mode enabled"
}

# ═══════════════════════════════════════════════════════════════════════════════
# Game Synchronization
# ═══════════════════════════════════════════════════════════════════════════════

sync_games() {
    log "Scanning for games..."
    
    local games_found=0
    local current_game=""
    
    # Read current game selection
    if [ -f "$STORAGE_MOUNT/CURRENT.txt" ]; then
        current_game=$(cat "$STORAGE_MOUNT/CURRENT.txt" | tr -d '\r\n' | head -1)
    fi
    
    # Scan for valid games
    for item in "$STORAGE_MOUNT"/*; do
        [ -e "$item" ] || continue
        
        local name=$(basename "$item")
        
        # Skip system files
        [[ "$name" == "README.txt" ]] && continue
        [[ "$name" == "CURRENT.txt" ]] && continue
        [[ "$name" == "System Volume Information" ]] && continue
        [[ "$name" == ".Spotlight-V100" ]] && continue
        [[ "$name" == ".fseventsd" ]] && continue
        [[ "$name" == ".Trashes" ]] && continue
        
        # Check if valid game
        if [ -f "$item" ] && [[ "$name" == *.tcr ]]; then
            log "  Found ROM: $name"
            ((games_found++))
        elif [ -d "$item" ] && [ -f "$item/main.lua" ]; then
            log "  Found game folder: $name"
            ((games_found++))
        fi
    done
    
    log "Found $games_found game(s)"
    
    # Update current game symlink
    if [ -n "$current_game" ]; then
        local game_path=""
        
        if [ -f "$STORAGE_MOUNT/${current_game}.tcr" ]; then
            game_path="$STORAGE_MOUNT/${current_game}.tcr"
        elif [ -d "$STORAGE_MOUNT/$current_game" ]; then
            game_path="$STORAGE_MOUNT/$current_game"
        fi
        
        if [ -n "$game_path" ]; then
            ln -sf "$game_path" "$STORAGE_MOUNT/current"
            log "Current game: $current_game"
        else
            log "Warning: Current game '$current_game' not found"
        fi
    fi
}

# ═══════════════════════════════════════════════════════════════════════════════
# USB Connection Monitor
# ═══════════════════════════════════════════════════════════════════════════════

monitor_usb() {
    log "Monitoring USB connection..."
    
    local was_connected=false
    
    while true; do
        # Check if we're connected to a USB host
        local vbus_file="/sys/class/power_supply/usb/online"
        local connected=false
        
        if [ -f "$vbus_file" ]; then
            [ "$(cat $vbus_file)" = "1" ] && connected=true
        else
            # Alternative: check for USB configuration
            if [ -f /sys/class/udc/*/state ]; then
                local state=$(cat /sys/class/udc/*/state 2>/dev/null)
                [ "$state" = "configured" ] && connected=true
            fi
        fi
        
        # State change detection
        if $connected && ! $was_connected; then
            log "USB connected to host"
            was_connected=true
        elif ! $connected && $was_connected; then
            log "USB disconnected (ejected)"
            was_connected=false
            
            # Return to game mode
            disable_usb_mode
        fi
        
        sleep 2
    done
}

# ═══════════════════════════════════════════════════════════════════════════════
# Button Trigger (Start + Select to toggle USB mode)
# ═══════════════════════════════════════════════════════════════════════════════

check_button_combo() {
    # This would read from the Pico controller via I2C
    # For now, use a file-based trigger
    
    local trigger_file="/tmp/usb_mode_trigger"
    
    if [ -f "$trigger_file" ]; then
        rm -f "$trigger_file"
        return 0  # Trigger detected
    fi
    
    return 1
}

# ═══════════════════════════════════════════════════════════════════════════════
# Main / Commands
# ═══════════════════════════════════════════════════════════════════════════════

usage() {
    echo "ThermoConsole USB Gadget Manager"
    echo ""
    echo "Usage: $0 <command>"
    echo ""
    echo "Commands:"
    echo "  setup     - Initial setup (create storage image)"
    echo "  enable    - Enable USB storage mode"
    echo "  disable   - Disable USB mode, return to games"
    echo "  toggle    - Toggle between USB and game mode"
    echo "  status    - Show current mode"
    echo "  monitor   - Run USB connection monitor daemon"
    echo "  sync      - Scan and sync games"
    echo ""
}

case "${1:-}" in
    setup)
        create_storage_image
        mount_storage
        sync_games
        log "Setup complete"
        ;;
        
    enable)
        enable_usb_mode
        ;;
        
    disable)
        disable_usb_mode
        ;;
        
    toggle)
        if [ -d "$GADGET_DIR" ] && [ -n "$(cat $GADGET_DIR/UDC 2>/dev/null)" ]; then
            disable_usb_mode
        else
            enable_usb_mode
        fi
        ;;
        
    status)
        if [ -d "$GADGET_DIR" ] && [ -n "$(cat $GADGET_DIR/UDC 2>/dev/null)" ]; then
            echo "Mode: USB Storage"
            echo "Connect to PC to access games"
        else
            echo "Mode: Game Console"
            if mountpoint -q "$STORAGE_MOUNT" 2>/dev/null; then
                echo "Games mounted at: $STORAGE_MOUNT"
                echo "Games found: $(ls -1 $STORAGE_MOUNT/*.tcr $STORAGE_MOUNT/*/main.lua 2>/dev/null | wc -l)"
            fi
        fi
        ;;
        
    monitor)
        monitor_usb
        ;;
        
    sync)
        mount_storage
        sync_games
        ;;
        
    *)
        usage
        exit 1
        ;;
esac
