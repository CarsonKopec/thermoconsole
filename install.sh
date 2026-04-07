#!/bin/bash
#
# ThermoConsole One-Line Installer
#
# Run on a fresh Raspberry Pi OS Lite installation:
#   curl -sSL https://raw.githubusercontent.com/user/thermoconsole/main/install.sh | sudo bash
#
# Or download and run:
#   wget https://raw.githubusercontent.com/user/thermoconsole/main/install.sh
#   chmod +x install.sh
#   sudo ./install.sh
#

set -e

# ═══════════════════════════════════════════════════════════════════════════════
# Configuration
# ═══════════════════════════════════════════════════════════════════════════════

TC_VERSION="1.0.0"
TC_REPO="https://github.com/user/thermoconsole"  # Update with actual repo
TC_DOWNLOAD="https://github.com/user/thermoconsole/releases/download/v${TC_VERSION}/thermoconsole-v${TC_VERSION}.tar.gz"

INSTALL_DIR="/opt/thermoconsole"
GAMES_DIR="/home/pi/games"
SAVE_DIR="/home/pi/.thermoconsole"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
BOLD='\033[1m'
NC='\033[0m'

# ═══════════════════════════════════════════════════════════════════════════════
# Functions
# ═══════════════════════════════════════════════════════════════════════════════

print_banner() {
    echo ""
    echo -e "${BOLD}╔═══════════════════════════════════════════════════════════════╗${NC}"
    echo -e "${BOLD}║           ThermoConsole Installer v${TC_VERSION}                  ║${NC}"
    echo -e "${BOLD}╚═══════════════════════════════════════════════════════════════╝${NC}"
    echo ""
}

log() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

success() {
    echo -e "${GREEN}[OK]${NC} $1"
}

warn() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

error() {
    echo -e "${RED}[ERROR]${NC} $1"
    exit 1
}

check_root() {
    if [ "$EUID" -ne 0 ]; then
        error "Please run as root: sudo $0"
    fi
}

check_pi() {
    if [ ! -f /proc/device-tree/model ]; then
        warn "Not running on Raspberry Pi - some features may not work"
        return
    fi
    
    local model=$(cat /proc/device-tree/model)
    log "Detected: $model"
    
    if [[ ! "$model" =~ "Zero" ]]; then
        warn "ThermoConsole is optimized for Pi Zero. Other models may work but are untested."
    fi
}

install_dependencies() {
    log "Installing dependencies..."
    
    apt-get update
    apt-get install -y \
        build-essential \
        cmake \
        libsdl2-dev \
        libsdl2-image-dev \
        libsdl2-mixer-dev \
        liblua5.4-dev \
        i2c-tools \
        python3-smbus \
        git \
        wget \
        unzip
    
    success "Dependencies installed"
}

download_thermoconsole() {
    log "Downloading ThermoConsole..."
    
    mkdir -p "$INSTALL_DIR"
    cd /tmp
    
    # Try release download first
    if wget -q "$TC_DOWNLOAD" -O thermoconsole.tar.gz 2>/dev/null; then
        tar -xzf thermoconsole.tar.gz -C "$INSTALL_DIR"
        rm thermoconsole.tar.gz
    else
        # Fall back to git clone
        warn "Release not found, cloning from git..."
        if [ -d "${INSTALL_DIR}/.git" ]; then
            cd "$INSTALL_DIR"
            git pull
        else
            rm -rf "$INSTALL_DIR"
            git clone "$TC_REPO" "$INSTALL_DIR"
        fi
    fi
    
    success "ThermoConsole downloaded"
}

build_runtime() {
    log "Building ThermoConsole runtime (this may take a while on Pi Zero)..."
    
    cd "${INSTALL_DIR}/runtime"
    mkdir -p build && cd build
    
    cmake .. -DTHERMO_BUILD_PI=ON
    make -j$(nproc)
    
    # Create symlink
    ln -sf "${INSTALL_DIR}/runtime/build/thermoconsole" /usr/local/bin/thermoconsole
    
    success "Runtime built"
}

setup_directories() {
    log "Setting up directories..."
    
    # Create user directories
    mkdir -p "$GAMES_DIR"
    mkdir -p "$SAVE_DIR"
    
    # Copy example games
    if [ -d "${INSTALL_DIR}/games" ]; then
        cp -r "${INSTALL_DIR}/games/"* "$GAMES_DIR/"
    fi
    
    # Set default game
    if [ -d "${GAMES_DIR}/space_dodge" ]; then
        ln -sf "${GAMES_DIR}/space_dodge" "${GAMES_DIR}/current"
    elif [ -d "${GAMES_DIR}/hello" ]; then
        ln -sf "${GAMES_DIR}/hello" "${GAMES_DIR}/current"
    fi
    
    # Fix permissions
    chown -R pi:pi "$GAMES_DIR"
    chown -R pi:pi "$SAVE_DIR"
    
    success "Directories created"
}

configure_display() {
    log "Configuring display (Waveshare 2.8\" DPI LCD)..."
    
    local config="/boot/config.txt"
    
    # Backup original
    if [ ! -f "${config}.backup" ]; then
        cp "$config" "${config}.backup"
    fi
    
    # Check if already configured
    if grep -q "ThermoConsole" "$config"; then
        warn "Display already configured, skipping"
        return
    fi
    
    # Add ThermoConsole config
    cat >> "$config" << 'EOF'

# ═══════════════════════════════════════════════════════════════════════════════
# ThermoConsole Display Configuration
# Waveshare 2.8" DPI LCD (480x640 native -> 640x480 landscape)
# ═══════════════════════════════════════════════════════════════════════════════

# DPI LCD pins
gpio=0-9=a2
gpio=12-17=a2
gpio=20-25=a2
dtoverlay=dpi24
enable_dpi_lcd=1
display_default_lcd=1

# Display timing
dpi_group=2
dpi_mode=87
dpi_output_format=0x7F216
hdmi_timings=480 0 26 16 10 640 0 25 10 15 0 0 0 60 0 32000000 1

# Waveshare overlay
dtoverlay=waveshare-28dpi-3b-4b

# I2C for controller (400kHz fast mode)
dtparam=i2c_arm=on
dtparam=i2c_arm_baudrate=400000

# Audio via PWM
dtoverlay=pwm-2chan,pin=18,func=2,pin2=19,func2=2

# GPU memory
gpu_mem=128

# Boot optimization
disable_splash=1
boot_delay=0

# Disable unused
dtoverlay=disable-bt
EOF

    # Comment out conflicting settings
    sed -i 's/^dtoverlay=vc4-kms-v3d/#dtoverlay=vc4-kms-v3d  # Disabled for ThermoConsole/' "$config"
    sed -i 's/^dtoverlay=vc4-fkms-v3d/#dtoverlay=vc4-fkms-v3d  # Disabled for ThermoConsole/' "$config"
    
    success "Display configured"
}

configure_boot() {
    log "Configuring boot options..."
    
    local cmdline="/boot/cmdline.txt"
    
    # Add quiet boot options if not present
    if ! grep -q "quiet" "$cmdline"; then
        sed -i 's/$/ quiet loglevel=3 vt.global_cursor_default=0 logo.nologo/' "$cmdline"
    fi
    
    success "Boot options configured"
}

create_service() {
    log "Creating systemd service..."
    
    cat > /etc/systemd/system/thermoconsole.service << 'EOF'
[Unit]
Description=ThermoConsole Game Runtime
After=multi-user.target
ConditionPathExists=/home/pi/games/current

[Service]
Type=simple
User=pi
Group=pi
WorkingDirectory=/home/pi
ExecStart=/usr/local/bin/thermoconsole /home/pi/games/current
Restart=always
RestartSec=3

# SDL2 environment
Environment=SDL_VIDEODRIVER=kmsdrm
Environment=SDL_AUDIODRIVER=alsa
Environment=HOME=/home/pi

# Performance
Nice=-5

[Install]
WantedBy=multi-user.target
EOF

    systemctl daemon-reload
    systemctl enable thermoconsole.service
    
    success "Service created and enabled"
}

configure_user() {
    log "Configuring user..."
    
    # Add to required groups
    usermod -a -G i2c,gpio,video,audio,input pi 2>/dev/null || true
    
    # Add helper commands to .bashrc
    if ! grep -q "ThermoConsole" /home/pi/.bashrc; then
        cat >> /home/pi/.bashrc << 'EOF'

# ═══════════════════════════════════════════════════════════════════════════════
# ThermoConsole Commands
# ═══════════════════════════════════════════════════════════════════════════════

alias tc-restart='sudo systemctl restart thermoconsole'
alias tc-stop='sudo systemctl stop thermoconsole'
alias tc-start='sudo systemctl start thermoconsole'
alias tc-logs='journalctl -u thermoconsole -f'
alias tc-status='systemctl status thermoconsole'
alias tc-usb='sudo /opt/thermoconsole/scripts/usb-gadget.sh toggle'
alias tc-usb-on='sudo /opt/thermoconsole/scripts/usb-gadget.sh enable'
alias tc-usb-off='sudo /opt/thermoconsole/scripts/usb-gadget.sh disable'

tc-switch() {
    local games_dir="/home/pi/games"
    if [ -z "$1" ]; then
        echo "Available games:"
        for game in "$games_dir"/*; do
            [ -d "$game" ] || [ "${game##*.}" = "tcr" ] || continue
            local name=$(basename "$game")
            [ "$name" = "current" ] && continue
            [ "$name" = "README.txt" ] && continue
            [ "$name" = "CURRENT.txt" ] && continue
            if [ "$(readlink -f "$games_dir/current")" = "$(readlink -f "$game")" ]; then
                echo "  * $name (current)"
            else
                echo "    $name"
            fi
        done
        echo ""
        echo "Usage: tc-switch <game-name>"
        return
    fi
    
    if [ ! -e "$games_dir/$1" ]; then
        echo "Game not found: $1"
        return 1
    fi
    
    ln -sf "$games_dir/$1" "$games_dir/current"
    # Also update CURRENT.txt for USB mode
    echo "$1" > "$games_dir/CURRENT.txt" 2>/dev/null || true
    sudo systemctl restart thermoconsole
    echo "Switched to: $1"
}

tc-info() {
    echo ""
    echo "════════════════════════════════════════"
    echo "  ThermoConsole System Info"
    echo "════════════════════════════════════════"
    echo ""
    echo "Status:  $(systemctl is-active thermoconsole 2>/dev/null || echo 'unknown')"
    echo "Game:    $(basename "$(readlink -f /home/pi/games/current 2>/dev/null)" 2>/dev/null || echo 'none')"
    echo "CPU:     $(vcgencmd measure_temp 2>/dev/null | cut -d= -f2 || echo 'N/A')"
    echo "Memory:  $(free -h | awk '/^Mem:/ {print $3 "/" $2}')"
    echo "USB:     $(/opt/thermoconsole/scripts/usb-gadget.sh status 2>/dev/null | head -1 || echo 'N/A')"
    echo ""
}

# Show welcome on login
if [ -t 0 ]; then
    echo ""
    echo "════════════════════════════════════════"
    echo "  ThermoConsole Game Console"
    echo "════════════════════════════════════════"
    echo ""
    echo "Commands: tc-switch, tc-restart, tc-usb, tc-logs, tc-info"
    echo "Hardware: Hold START+SELECT (3s) to toggle USB mode"
    echo ""
fi
EOF
    fi
    
    chown pi:pi /home/pi/.bashrc
    
    success "User configured"
}

disable_unnecessary_services() {
    log "Optimizing system..."
    
    # Disable services we don't need
    systemctl disable bluetooth.service 2>/dev/null || true
    systemctl disable hciuart.service 2>/dev/null || true
    systemctl disable apt-daily.service 2>/dev/null || true
    systemctl disable apt-daily-upgrade.service 2>/dev/null || true
    systemctl disable man-db.timer 2>/dev/null || true
    
    success "System optimized"
}

print_complete() {
    echo ""
    echo -e "${GREEN}╔═══════════════════════════════════════════════════════════════╗${NC}"
    echo -e "${GREEN}║           Installation Complete!                              ║${NC}"
    echo -e "${GREEN}╚═══════════════════════════════════════════════════════════════╝${NC}"
    echo ""
    echo "ThermoConsole has been installed and configured."
    echo ""
    echo "Next steps:"
    echo "  1. Connect your Waveshare 2.8\" DPI LCD"
    echo "  2. Connect your Pi Pico controller (I2C on GPIO 2/3)"
    echo "  3. Reboot: sudo reboot"
    echo ""
    echo "After reboot, ThermoConsole will start automatically."
    echo ""
    echo "Useful commands (after reboot):"
    echo "  tc-switch <game>   - Switch games"
    echo "  tc-restart         - Restart ThermoConsole"
    echo "  tc-logs            - View logs"
    echo "  tc-info            - System info"
    echo ""
    echo "Games are stored in: /home/pi/games/"
    echo "Save data is in:     /home/pi/.thermoconsole/"
    echo ""
    echo "USB Mode: Hold START+SELECT to toggle USB storage mode"
    echo "          (allows copying games from PC)"
    echo ""
    echo -e "${YELLOW}Reboot now? [y/N]${NC} "
    read -r response
    if [[ "$response" =~ ^[Yy]$ ]]; then
        reboot
    fi
}

setup_usb_gadget() {
    log "Setting up USB gadget mode..."
    
    # Enable dwc2 overlay for USB gadget support
    local config="/boot/config.txt"
    if ! grep -q "dtoverlay=dwc2" "$config"; then
        echo "" >> "$config"
        echo "# USB Gadget Mode (allows PC file transfer)" >> "$config"
        echo "dtoverlay=dwc2" >> "$config"
    fi
    
    # Add dwc2 and g_mass_storage to modules
    if ! grep -q "dwc2" /etc/modules; then
        echo "dwc2" >> /etc/modules
    fi
    if ! grep -q "libcomposite" /etc/modules; then
        echo "libcomposite" >> /etc/modules
    fi
    
    # Copy USB gadget scripts
    mkdir -p /opt/thermoconsole/scripts
    cp "${INSTALL_DIR}/scripts/usb-gadget.sh" /opt/thermoconsole/scripts/
    cp "${INSTALL_DIR}/scripts/usb-mode-monitor.sh" /opt/thermoconsole/scripts/
    cp "${INSTALL_DIR}/scripts/usb-mode-display.py" /opt/thermoconsole/scripts/
    chmod +x /opt/thermoconsole/scripts/*.sh
    chmod +x /opt/thermoconsole/scripts/*.py
    
    # Install systemd services for USB gadget
    cat > /etc/systemd/system/thermoconsole-usb.service << 'EOF'
[Unit]
Description=ThermoConsole USB Storage Setup
After=local-fs.target
Before=thermoconsole.service

[Service]
Type=oneshot
RemainAfterExit=yes
ExecStart=/opt/thermoconsole/scripts/usb-gadget.sh setup

[Install]
WantedBy=multi-user.target
EOF

    cat > /etc/systemd/system/thermoconsole-usb-monitor.service << 'EOF'
[Unit]
Description=ThermoConsole USB Mode Toggle Monitor
After=thermoconsole-usb.service thermoconsole.service

[Service]
Type=simple
ExecStart=/opt/thermoconsole/scripts/usb-mode-monitor.sh
Restart=always
RestartSec=5

[Install]
WantedBy=multi-user.target
EOF

    systemctl daemon-reload
    systemctl enable thermoconsole-usb.service
    systemctl enable thermoconsole-usb-monitor.service
    
    success "USB gadget mode configured"
}

# ═══════════════════════════════════════════════════════════════════════════════
# Main
# ═══════════════════════════════════════════════════════════════════════════════

main() {
    print_banner
    check_root
    check_pi
    
    echo "This script will:"
    echo "  - Install required packages"
    echo "  - Download and build ThermoConsole"
    echo "  - Configure the display for Waveshare 2.8\" DPI LCD"
    echo "  - Set up auto-start service"
    echo "  - Enable USB storage mode (drag-and-drop games from PC)"
    echo ""
    echo -e "${YELLOW}Continue? [y/N]${NC} "
    read -r response
    if [[ ! "$response" =~ ^[Yy]$ ]]; then
        echo "Aborted."
        exit 0
    fi
    
    echo ""
    install_dependencies
    download_thermoconsole
    build_runtime
    setup_directories
    configure_display
    configure_boot
    create_service
    setup_usb_gadget
    configure_user
    disable_unnecessary_services
    print_complete
}

main "$@"
