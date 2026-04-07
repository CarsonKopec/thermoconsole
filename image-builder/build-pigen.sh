#!/bin/bash
#
# ThermoConsole pi-gen Builder
#
# Uses the official Raspberry Pi image building tool (pi-gen) to create
# a custom ThermoConsole OS image.
#
# This is the recommended approach for production images.
#
# Prerequisites:
#   - Docker (recommended) OR
#   - Debian/Ubuntu host with: quilt parted qemu-user-static debootstrap
#
# Usage:
#   ./build-pigen.sh [--docker]
#

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TC_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="${SCRIPT_DIR}/pigen-build"

USE_DOCKER=false
if [ "$1" = "--docker" ]; then
    USE_DOCKER=true
fi

echo "╔═══════════════════════════════════════╗"
echo "║  ThermoConsole pi-gen Image Builder   ║"
echo "╚═══════════════════════════════════════╝"
echo ""

# Clone pi-gen if needed
if [ ! -d "${BUILD_DIR}/pi-gen" ]; then
    echo "[INFO] Cloning pi-gen..."
    mkdir -p "$BUILD_DIR"
    git clone --depth=1 https://github.com/RPi-Distro/pi-gen.git "${BUILD_DIR}/pi-gen"
fi

cd "${BUILD_DIR}/pi-gen"

# Create config file
cat > config << 'EOF'
IMG_NAME="thermoconsole"
RELEASE="bookworm"
TARGET_HOSTNAME="thermoconsole"
KEYBOARD_KEYMAP="us"
KEYBOARD_LAYOUT="English (US)"
TIMEZONE_DEFAULT="UTC"
FIRST_USER_NAME="pi"
FIRST_USER_PASS="raspberry"
ENABLE_SSH=1
LOCALE_DEFAULT="en_US.UTF-8"

# Skip desktop stages - we want Lite
STAGE_LIST="stage0 stage1 stage2 stage-thermoconsole"
EOF

# Skip stages we don't need
touch ./stage3/SKIP ./stage4/SKIP ./stage5/SKIP
touch ./stage3/SKIP_IMAGES ./stage4/SKIP_IMAGES ./stage5/SKIP_IMAGES

# Create ThermoConsole stage
TC_STAGE="./stage-thermoconsole"
mkdir -p "$TC_STAGE"

# Stage marker
touch "${TC_STAGE}/EXPORT_IMAGE"

# Package installation
mkdir -p "${TC_STAGE}/00-packages"
cat > "${TC_STAGE}/00-packages/00-packages" << 'EOF'
build-essential
cmake
libsdl2-dev
libsdl2-image-dev
libsdl2-mixer-dev
liblua5.4-dev
i2c-tools
python3-smbus
EOF

# Copy ThermoConsole files
mkdir -p "${TC_STAGE}/01-thermoconsole/files"
cp -r "$TC_DIR/runtime" "${TC_STAGE}/01-thermoconsole/files/"
cp -r "$TC_DIR/sdk" "${TC_STAGE}/01-thermoconsole/files/"
cp -r "$TC_DIR/games" "${TC_STAGE}/01-thermoconsole/files/"
cp -r "$TC_DIR/docs" "${TC_STAGE}/01-thermoconsole/files/"
cp -r "$TC_DIR/pico-controller" "${TC_STAGE}/01-thermoconsole/files/"

# Installation script
cat > "${TC_STAGE}/01-thermoconsole/00-run.sh" << 'INSTALL_SCRIPT'
#!/bin/bash -e

# Install ThermoConsole
install -d "${ROOTFS_DIR}/opt/thermoconsole"
cp -r files/runtime "${ROOTFS_DIR}/opt/thermoconsole/"
cp -r files/sdk "${ROOTFS_DIR}/opt/thermoconsole/"
cp -r files/games "${ROOTFS_DIR}/opt/thermoconsole/"
cp -r files/docs "${ROOTFS_DIR}/opt/thermoconsole/"
cp -r files/pico-controller "${ROOTFS_DIR}/opt/thermoconsole/"

# Build runtime (in chroot)
on_chroot << CHROOT
cd /opt/thermoconsole/runtime
mkdir -p build && cd build
cmake .. -DTHERMO_BUILD_PI=ON
make -j\$(nproc)
ln -sf /opt/thermoconsole/runtime/build/thermoconsole /usr/local/bin/thermoconsole
CHROOT

# Create user directories
install -d -o 1000 -g 1000 "${ROOTFS_DIR}/home/${FIRST_USER_NAME}/games"
install -d -o 1000 -g 1000 "${ROOTFS_DIR}/home/${FIRST_USER_NAME}/.thermoconsole"

# Copy default games
cp -r "${ROOTFS_DIR}/opt/thermoconsole/games/"* "${ROOTFS_DIR}/home/${FIRST_USER_NAME}/games/"
chown -R 1000:1000 "${ROOTFS_DIR}/home/${FIRST_USER_NAME}/games"

# Set default game
ln -sf /home/${FIRST_USER_NAME}/games/space_dodge "${ROOTFS_DIR}/home/${FIRST_USER_NAME}/games/current"
INSTALL_SCRIPT
chmod +x "${TC_STAGE}/01-thermoconsole/00-run.sh"

# Display configuration
mkdir -p "${TC_STAGE}/02-display"
cat > "${TC_STAGE}/02-display/00-run.sh" << 'DISPLAY_SCRIPT'
#!/bin/bash -e

# Configure /boot/config.txt for Waveshare 2.8" DPI LCD
cat >> "${ROOTFS_DIR}/boot/config.txt" << 'BOOT_CONFIG'

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
dtoverlay=waveshare-28dpi-3b-4b

# I2C for controller
dtparam=i2c_arm=on
dtparam=i2c_arm_baudrate=400000

# Audio
dtoverlay=pwm-2chan,pin=18,func=2,pin2=19,func2=2

# Boot optimization
disable_splash=1
boot_delay=0

# Disable unused features
dtoverlay=disable-bt
BOOT_CONFIG

# Quiet boot
sed -i 's/$/ quiet loglevel=3 vt.global_cursor_default=0 logo.nologo/' "${ROOTFS_DIR}/boot/cmdline.txt"
DISPLAY_SCRIPT
chmod +x "${TC_STAGE}/02-display/00-run.sh"

# Systemd service
mkdir -p "${TC_STAGE}/03-service"
cat > "${TC_STAGE}/03-service/00-run.sh" << 'SERVICE_SCRIPT'
#!/bin/bash -e

# Create ThermoConsole service
cat > "${ROOTFS_DIR}/etc/systemd/system/thermoconsole.service" << 'SERVICE'
[Unit]
Description=ThermoConsole Game Runtime
After=multi-user.target

[Service]
Type=simple
User=pi
Group=pi
WorkingDirectory=/home/pi
ExecStart=/usr/local/bin/thermoconsole /home/pi/games/current
Restart=always
RestartSec=3
Environment=SDL_VIDEODRIVER=kmsdrm
Environment=SDL_AUDIODRIVER=alsa
Nice=-5

[Install]
WantedBy=multi-user.target
SERVICE

# Enable service
on_chroot << CHROOT
systemctl enable thermoconsole.service
systemctl disable bluetooth.service || true
systemctl disable apt-daily.service || true
systemctl disable apt-daily-upgrade.service || true
CHROOT
SERVICE_SCRIPT
chmod +x "${TC_STAGE}/03-service/00-run.sh"

# User configuration
mkdir -p "${TC_STAGE}/04-user"
cat > "${TC_STAGE}/04-user/00-run.sh" << 'USER_SCRIPT'
#!/bin/bash -e

# Add user to groups
on_chroot << CHROOT
usermod -a -G i2c,gpio,video,audio,input pi
CHROOT

# Add utility commands to bashrc
cat >> "${ROOTFS_DIR}/home/${FIRST_USER_NAME}/.bashrc" << 'BASHRC'

# ThermoConsole commands
alias tc-restart='sudo systemctl restart thermoconsole'
alias tc-stop='sudo systemctl stop thermoconsole'
alias tc-logs='journalctl -u thermoconsole -f'

tc-switch() {
    if [ -z "$1" ]; then
        echo "Games in /home/pi/games:"
        ls -1 /home/pi/games/ | grep -v current
        echo ""
        echo "Usage: tc-switch <game-name>"
        return
    fi
    ln -sf "/home/pi/games/$1" /home/pi/games/current
    sudo systemctl restart thermoconsole
    echo "Switched to: $1"
}

echo ""
echo "════════════════════════════════════════"
echo "  ThermoConsole - Game Console OS"
echo "════════════════════════════════════════"
echo ""
BASHRC
USER_SCRIPT
chmod +x "${TC_STAGE}/04-user/00-run.sh"

echo ""
echo "[INFO] pi-gen stages configured"
echo ""

# Build
if [ "$USE_DOCKER" = true ]; then
    echo "[INFO] Building with Docker..."
    ./build-docker.sh
else
    echo "[INFO] Building natively..."
    echo "[INFO] This requires: quilt parted qemu-user-static debootstrap zerofree"
    sudo ./build.sh
fi

echo ""
echo "════════════════════════════════════════════════════════════"
echo " Build complete!"
echo " Image location: ${BUILD_DIR}/pi-gen/deploy/"
echo "════════════════════════════════════════════════════════════"
