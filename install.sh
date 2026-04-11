#!/bin/bash
#
# ThermoConsole One-Line Installer
#
# Run on a fresh Raspberry Pi OS Lite installation:
#   curl -sSL https://your-url/install.sh | sudo bash
#
# Or download and run interactively:
#   wget https://your-url/install.sh
#   chmod +x install.sh
#   sudo ./install.sh
#
# Options:
#   -y, --yes    Skip confirmation prompts (for automated installs)
#

set -e

# ═══════════════════════════════════════════════════════════════════════════════
# Configuration
# ═══════════════════════════════════════════════════════════════════════════════

TC_VERSION="1.0.0"
TC_REPO="https://github.com/CarsonKopec/thermoconsole"
TC_DOWNLOAD="https://github.com/CarsonKopec/thermoconsole/releases/download/v${TC_VERSION}/thermoconsole-v${TC_VERSION}.tar.gz"
TC_ZIP_URL="https://github.com/CarsonKopec/thermoconsole/archive/refs/heads/main.zip"

INSTALL_DIR="/opt/thermoconsole"
GAMES_DIR="/home/pi/games"
SAVE_DIR="/home/pi/.thermoconsole"

# Auto-yes mode (skip prompts)
AUTO_YES=false

# Check if running non-interactively (piped input)
if [ ! -t 0 ]; then
    AUTO_YES=true
fi

# Parse arguments
for arg in "$@"; do
    case $arg in
        -y|--yes)
            AUTO_YES=true
            shift
            ;;
    esac
done

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
BOLD='\033[1m'
NC='\033[0m'

# Detect Bookworm (uses /boot/firmware/) vs older (uses /boot/)
BOOT_CONFIG="/boot/config.txt"
BOOT_CMDLINE="/boot/cmdline.txt"

if [ -d "/boot/firmware" ] && [ -f "/boot/firmware/config.txt" ]; then
    BOOT_CONFIG="/boot/firmware/config.txt"
    BOOT_CMDLINE="/boot/firmware/cmdline.txt"
fi

# ═══════════════════════════════════════════════════════════════════════════════
# Functions
# ═══════════════════════════════════════════════════════════════════════════════

print_banner() {
    echo ""
    echo -e "${BOLD}╔═══════════════════════════════════════════════════════════════╗${NC}"
    echo -e "${BOLD}║           ThermoConsole Installer v${TC_VERSION}                       ║${NC}"
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

confirm() {
    if [ "$AUTO_YES" = true ]; then
        return 0
    fi
    
    echo -en "${YELLOW}$1 [y/N]${NC} "
    read -r response
    if [[ "$response" =~ ^[Yy]$ ]]; then
        return 0
    else
        return 1
    fi
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
    
    # Read model and strip null bytes
    local model
    model=$(tr -d '\0' < /proc/device-tree/model)
    log "Detected: $model"
    log "Boot config: $BOOT_CONFIG"
    
    # Check OS version
    if [ -f /etc/os-release ]; then
        local version
        version=$(grep "VERSION_CODENAME" /etc/os-release | cut -d= -f2)
        log "OS Version: $version"
    fi
    
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
        log "Downloaded release archive"
        tar -xzf thermoconsole.tar.gz -C "$INSTALL_DIR" --strip-components=1
        rm thermoconsole.tar.gz
    # Try zip download
    elif wget -q "$TC_ZIP_URL" -O thermoconsole.zip 2>/dev/null; then
        log "Downloaded zip archive"
        unzip -q -o thermoconsole.zip -d /tmp/tc-extract
        # Handle different extraction structures
        if [ -d "/tmp/tc-extract/thermoconsole-main" ]; then
            cp -r /tmp/tc-extract/thermoconsole-main/* "$INSTALL_DIR/"
        else
            cp -r /tmp/tc-extract/*/* "$INSTALL_DIR/" 2>/dev/null || cp -r /tmp/tc-extract/* "$INSTALL_DIR/"
        fi
        rm -rf thermoconsole.zip /tmp/tc-extract
    # Fall back to git clone
    elif command -v git &>/dev/null; then
        warn "Release not found, cloning from git..."
        if [ -d "${INSTALL_DIR}/.git" ]; then
            cd "$INSTALL_DIR"
            git pull
        else
            rm -rf "$INSTALL_DIR"
            git clone "$TC_REPO" "$INSTALL_DIR"
        fi
    else
        error "Could not download ThermoConsole. Please check your internet connection."
    fi
    
    success "ThermoConsole downloaded to $INSTALL_DIR"
    
    # List what we got
    log "Contents of $INSTALL_DIR:"
    ls -la "$INSTALL_DIR" || true
}

build_runtime() {
    log "Building ThermoConsole runtime (this may take a while on Pi Zero)..."
    
    if [ ! -d "${INSTALL_DIR}/runtime" ]; then
        error "Runtime source not found at ${INSTALL_DIR}/runtime. Check download."
    fi
    
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
        cp -r "${INSTALL_DIR}/games/"* "$GAMES_DIR/" 2>/dev/null || true
    fi
    
    # Set default game
    if [ -d "${GAMES_DIR}/space_dodge" ]; then
        ln -sf "${GAMES_DIR}/space_dodge" "${GAMES_DIR}/current"
    elif [ -d "${GAMES_DIR}/hello" ]; then
        ln -sf "${GAMES_DIR}/hello" "${GAMES_DIR}/current"
    fi
    
    # Fix permissions
    chown -R pi:pi "$GAMES_DIR" 2>/dev/null || true
    chown -R pi:pi "$SAVE_DIR" 2>/dev/null || true
    
    success "Directories created"
}

configure_display() {
    log "Configuring display (Waveshare 2.8\" DPI LCD)..."
    log "Using config file: $BOOT_CONFIG"
    
    if [ ! -f "$BOOT_CONFIG" ]; then
        warn "config.txt not found at $BOOT_CONFIG, skipping display configuration"
        return
    fi
    
    # Backup original
    if [ ! -f "${BOOT_CONFIG}.backup" ]; then
        cp "$BOOT_CONFIG" "${BOOT_CONFIG}.backup"
    fi
    
    # Check if already configured
    if grep -q "ThermoConsole" "$BOOT_CONFIG"; then
        warn "Display already configured, skipping"
        return
    fi
    
    # Comment out conflicting vc4 settings FIRST
    sed -i 's/^dtoverlay=vc4-kms-v3d/#dtoverlay=vc4-kms-v3d  # Disabled for ThermoConsole/' "$BOOT_CONFIG"
    sed -i 's/^dtoverlay=vc4-fkms-v3d/#dtoverlay=vc4-fkms-v3d  # Disabled for ThermoConsole/' "$BOOT_CONFIG"
    
    # Add ThermoConsole config
    cat >> "$BOOT_CONFIG" << 'EOF'

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

# USB Gadget Mode
dtoverlay=dwc2
EOF

    success "Display configured in $BOOT_CONFIG"
}

configure_boot() {
    log "Configuring boot options..."
    log "Using cmdline file: $BOOT_CMDLINE"
    
    if [ ! -f "$BOOT_CMDLINE" ]; then
        warn "cmdline.txt not found at $BOOT_CMDLINE, skipping boot configuration"
        return
    fi
    
    # Add quiet boot options if not present
    if ! grep -q "quiet" "$BOOT_CMDLINE"; then
        sed -i 's/$/ quiet loglevel=3 vt.global_cursor_default=0 logo.nologo/' "$BOOT_CMDLINE"
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

setup_usb_gadget() {
    log "Setting up USB gadget mode..."
    
    # USB gadget config is now added in configure_display()
    # Just need to add modules
    
    # Add dwc2 and libcomposite to modules
    if ! grep -q "dwc2" /etc/modules 2>/dev/null; then
        echo "dwc2" >> /etc/modules
    fi
    if ! grep -q "libcomposite" /etc/modules 2>/dev/null; then
        echo "libcomposite" >> /etc/modules
    fi
    
    # Copy USB gadget scripts if they exist
    mkdir -p /opt/thermoconsole/scripts
    for script in usb-gadget.sh usb-mode-monitor.sh usb-mode-display.py; do
        if [ -f "${INSTALL_DIR}/scripts/${script}" ]; then
            cp "${INSTALL_DIR}/scripts/${script}" /opt/thermoconsole/scripts/
            chmod +x "/opt/thermoconsole/scripts/${script}"
        fi
    done
    
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
    
    # Only enable if scripts exist
    if [ -f "/opt/thermoconsole/scripts/usb-gadget.sh" ]; then
        systemctl enable thermoconsole-usb.service 2>/dev/null || true
        systemctl enable thermoconsole-usb-monitor.service 2>/dev/null || true
    fi
    
    success "USB gadget mode configured"
}

configure_user() {
    log "Configuring user..."
    
    # Check if pi user exists
    if ! id -u pi &>/dev/null; then
        warn "User 'pi' not found, skipping user configuration"
        return
    fi
    
    # Add to required groups (ignore errors for missing groups)
    for group in i2c gpio video audio input; do
        usermod -a -G "$group" pi 2>/dev/null || true
    done
    
    # Add helper commands to .bashrc
    if [ -f /home/pi/.bashrc ] && ! grep -q "ThermoConsole" /home/pi/.bashrc; then
        cat >> /home/pi/.bashrc << 'EOF'

# ═══════════════════════════════════════════════════════════════════════════════
# ThermoConsole Commands
# ═══════════════════════════════════════════════════════════════════════════════

alias tc-restart='sudo systemctl restart thermoconsole'
alias tc-stop='sudo systemctl stop thermoconsole'
alias tc-start='sudo systemctl start thermoconsole'
alias tc-logs='journalctl -u thermoconsole -f'
alias tc-status='systemctl status thermoconsole'
alias tc-usb='sudo /opt/thermoconsole/scripts/usb-gadget.sh toggle 2>/dev/null || echo "USB gadget not configured"'

tc-switch() {
    local games_dir="/home/pi/games"
    if [ -z "$1" ]; then
        echo "Available games:"
        for game in "$games_dir"/*; do
            [ -d "$game" ] || [ "${game##*.}" = "tcr" ] || continue
            local name
            name=$(basename "$game")
            [ "$name" = "current" ] && continue
            [ "$name" = "README.txt" ] && continue
            [ "$name" = "CURRENT.txt" ] && continue
            if [ "$(readlink -f "$games_dir/current" 2>/dev/null)" = "$(readlink -f "$game")" ]; then
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
    echo ""
}

# Show welcome on login
if [ -t 0 ]; then
    echo ""
    echo "════════════════════════════════════════"
    echo "  ThermoConsole Game Console"
    echo "════════════════════════════════════════"
    echo ""
    echo "Commands: tc-switch, tc-restart, tc-logs, tc-info"
    echo ""
fi
EOF
        chown pi:pi /home/pi/.bashrc
    fi
    
    success "User configured"
}

disable_unnecessary_services() {
    log "Optimizing system..."
    
    # Disable services we don't need (ignore errors)
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
    echo "Useful commands:"
    echo "  tc-switch <game>   - Switch games"
    echo "  tc-restart         - Restart ThermoConsole"
    echo "  tc-logs            - View logs"
    echo "  tc-info            - System info"
    echo ""
    echo "Games are stored in: /home/pi/games/"
    echo ""
    
    if confirm "Reboot now?"; then
        reboot
    fi
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
    
    if ! confirm "Continue?"; then
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
