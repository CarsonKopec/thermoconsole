#!/bin/bash
#
# ThermoConsole Image Builder
# Creates a flashable SD card image with ThermoConsole pre-installed
#
# Usage: sudo ./build-image.sh [options]
#
# Options:
#   --pi-version <zero|zero2>   Target Pi version (default: zero2)
#   --output <path>             Output image path (default: thermoconsole-<version>.img)
#   --size <MB>                 Image size in MB (default: 2048)
#   --skip-download             Skip downloading base image (use cached)
#   --games <path>              Include games from directory
#
# Requirements:
#   - Linux host (Ubuntu/Debian recommended)
#   - Root privileges (for loopback mounting)
#   - ~4GB free disk space
#   - Internet connection (for base image download)
#
# Packages needed:
#   sudo apt install qemu-user-static binfmt-support parted kpartx xz-utils wget unzip
#

set -e

# ═══════════════════════════════════════════════════════════════════════════════
# Configuration
# ═══════════════════════════════════════════════════════════════════════════════

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WORK_DIR="${SCRIPT_DIR}/build-work"
CACHE_DIR="${SCRIPT_DIR}/build-cache"

PI_VERSION="zero2"
IMAGE_SIZE_MB=2048
OUTPUT_IMAGE=""
SKIP_DOWNLOAD=false
GAMES_DIR=""

# Base image URLs (Raspberry Pi OS Lite)
declare -A BASE_IMAGES
BASE_IMAGES["zero"]="https://downloads.raspberrypi.com/raspios_lite_armhf/images/raspios_lite_armhf-2024-03-15/2024-03-15-raspios-bookworm-armhf-lite.img.xz"
BASE_IMAGES["zero2"]="https://downloads.raspberrypi.com/raspios_lite_arm64/images/raspios_lite_arm64-2024-03-15/2024-03-15-raspios-bookworm-arm64-lite.img.xz"

# ThermoConsole version
TC_VERSION="1.0.0"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# ═══════════════════════════════════════════════════════════════════════════════
# Helper Functions
# ═══════════════════════════════════════════════════════════════════════════════

log_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

log_success() {
    echo -e "${GREEN}[OK]${NC} $1"
}

log_warn() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

cleanup() {
    log_info "Cleaning up..."
    
    # Unmount if mounted
    if mountpoint -q "${WORK_DIR}/rootfs" 2>/dev/null; then
        umount -R "${WORK_DIR}/rootfs" 2>/dev/null || true
    fi
    if mountpoint -q "${WORK_DIR}/boot" 2>/dev/null; then
        umount "${WORK_DIR}/boot" 2>/dev/null || true
    fi
    
    # Detach loop device
    if [ -n "$LOOP_DEV" ]; then
        kpartx -d "$LOOP_DEV" 2>/dev/null || true
        losetup -d "$LOOP_DEV" 2>/dev/null || true
    fi
}

trap cleanup EXIT

check_requirements() {
    log_info "Checking requirements..."
    
    local missing=()
    
    for cmd in qemu-arm-static parted kpartx xz wget unzip; do
        if ! command -v $cmd &>/dev/null; then
            missing+=($cmd)
        fi
    done
    
    if [ ${#missing[@]} -gt 0 ]; then
        log_error "Missing required tools: ${missing[*]}"
        echo "Install with: sudo apt install qemu-user-static binfmt-support parted kpartx xz-utils wget unzip"
        exit 1
    fi
    
    if [ "$EUID" -ne 0 ]; then
        log_error "This script must be run as root (sudo)"
        exit 1
    fi
    
    # Check binfmt is set up for ARM
    if [ ! -f /proc/sys/fs/binfmt_misc/qemu-arm ] && [ ! -f /proc/sys/fs/binfmt_misc/qemu-aarch64 ]; then
        log_warn "ARM binfmt may not be configured. Trying to enable..."
        update-binfmts --enable qemu-arm 2>/dev/null || true
        update-binfmts --enable qemu-aarch64 2>/dev/null || true
    fi
    
    log_success "All requirements met"
}

parse_args() {
    while [[ $# -gt 0 ]]; do
        case $1 in
            --pi-version)
                PI_VERSION="$2"
                shift 2
                ;;
            --output)
                OUTPUT_IMAGE="$2"
                shift 2
                ;;
            --size)
                IMAGE_SIZE_MB="$2"
                shift 2
                ;;
            --skip-download)
                SKIP_DOWNLOAD=true
                shift
                ;;
            --games)
                GAMES_DIR="$2"
                shift 2
                ;;
            -h|--help)
                echo "ThermoConsole Image Builder"
                echo ""
                echo "Usage: sudo $0 [options]"
                echo ""
                echo "Options:"
                echo "  --pi-version <zero|zero2>  Target Pi (default: zero2)"
                echo "  --output <path>            Output image path"
                echo "  --size <MB>                Image size (default: 2048)"
                echo "  --skip-download            Use cached base image"
                echo "  --games <path>             Include games from directory"
                echo "  -h, --help                 Show this help"
                exit 0
                ;;
            *)
                log_error "Unknown option: $1"
                exit 1
                ;;
        esac
    done
    
    # Set default output name
    if [ -z "$OUTPUT_IMAGE" ]; then
        OUTPUT_IMAGE="thermoconsole-${PI_VERSION}-v${TC_VERSION}.img"
    fi
    
    # Validate PI version
    if [ -z "${BASE_IMAGES[$PI_VERSION]}" ]; then
        log_error "Invalid Pi version: $PI_VERSION (use 'zero' or 'zero2')"
        exit 1
    fi
}

# ═══════════════════════════════════════════════════════════════════════════════
# Main Build Steps
# ═══════════════════════════════════════════════════════════════════════════════

download_base_image() {
    log_info "Setting up base image..."
    
    mkdir -p "$CACHE_DIR"
    
    local url="${BASE_IMAGES[$PI_VERSION]}"
    local filename=$(basename "$url")
    local cached_xz="${CACHE_DIR}/${filename}"
    local cached_img="${CACHE_DIR}/${filename%.xz}"
    
    if [ "$SKIP_DOWNLOAD" = true ] && [ -f "$cached_img" ]; then
        log_info "Using cached image: $cached_img"
    else
        if [ ! -f "$cached_xz" ]; then
            log_info "Downloading Raspberry Pi OS Lite..."
            wget -O "$cached_xz" "$url"
        fi
        
        log_info "Extracting image..."
        xz -dk "$cached_xz" 2>/dev/null || true
    fi
    
    BASE_IMAGE="$cached_img"
    log_success "Base image ready: $BASE_IMAGE"
}

create_working_image() {
    log_info "Creating working image (${IMAGE_SIZE_MB}MB)..."
    
    mkdir -p "$WORK_DIR"
    
    WORKING_IMAGE="${WORK_DIR}/thermoconsole.img"
    
    # Create blank image
    dd if=/dev/zero of="$WORKING_IMAGE" bs=1M count="$IMAGE_SIZE_MB" status=progress
    
    # Copy partition table and boot partition from base image
    log_info "Copying base image structure..."
    
    # Get partition info from base image
    local base_loop=$(losetup -f --show -P "$BASE_IMAGE")
    
    # Copy MBR and partition table
    dd if="$BASE_IMAGE" of="$WORKING_IMAGE" bs=512 count=1 conv=notrunc
    
    # Use parted to recreate partitions at correct sizes
    parted -s "$WORKING_IMAGE" mklabel msdos
    parted -s "$WORKING_IMAGE" mkpart primary fat32 4MiB 260MiB
    parted -s "$WORKING_IMAGE" mkpart primary ext4 260MiB 100%
    parted -s "$WORKING_IMAGE" set 1 boot on
    
    losetup -d "$base_loop"
    
    # Set up loop device for our image
    LOOP_DEV=$(losetup -f --show -P "$WORKING_IMAGE")
    
    # Wait for partitions to appear
    sleep 2
    kpartx -av "$LOOP_DEV"
    sleep 2
    
    # Format partitions
    local mapper_name=$(basename "$LOOP_DEV")
    BOOT_PART="/dev/mapper/${mapper_name}p1"
    ROOT_PART="/dev/mapper/${mapper_name}p2"
    
    log_info "Formatting boot partition (FAT32)..."
    mkfs.vfat -F 32 -n "boot" "$BOOT_PART"
    
    log_info "Formatting root partition (ext4)..."
    mkfs.ext4 -L "rootfs" "$ROOT_PART"
    
    log_success "Partitions created"
}

mount_image() {
    log_info "Mounting image..."
    
    mkdir -p "${WORK_DIR}/boot"
    mkdir -p "${WORK_DIR}/rootfs"
    
    mount "$ROOT_PART" "${WORK_DIR}/rootfs"
    mount "$BOOT_PART" "${WORK_DIR}/boot"
    
    log_success "Image mounted"
}

copy_base_rootfs() {
    log_info "Copying base system (this takes a while)..."
    
    # Mount base image
    local base_loop=$(losetup -f --show -P "$BASE_IMAGE")
    sleep 2
    kpartx -av "$base_loop"
    sleep 2
    
    local base_mapper=$(basename "$base_loop")
    mkdir -p "${WORK_DIR}/base-boot"
    mkdir -p "${WORK_DIR}/base-rootfs"
    
    mount "/dev/mapper/${base_mapper}p1" "${WORK_DIR}/base-boot"
    mount "/dev/mapper/${base_mapper}p2" "${WORK_DIR}/base-rootfs"
    
    # Copy root filesystem
    log_info "Copying root filesystem..."
    rsync -aHAX "${WORK_DIR}/base-rootfs/" "${WORK_DIR}/rootfs/"
    
    # Copy boot files
    log_info "Copying boot files..."
    rsync -aHAX "${WORK_DIR}/base-boot/" "${WORK_DIR}/boot/"
    
    # Unmount base
    umount "${WORK_DIR}/base-boot"
    umount "${WORK_DIR}/base-rootfs"
    kpartx -d "$base_loop"
    losetup -d "$base_loop"
    rmdir "${WORK_DIR}/base-boot" "${WORK_DIR}/base-rootfs"
    
    log_success "Base system copied"
}

setup_chroot() {
    log_info "Setting up chroot environment..."
    
    local rootfs="${WORK_DIR}/rootfs"
    
    # Mount required filesystems
    mount -t proc /proc "${rootfs}/proc"
    mount -t sysfs /sys "${rootfs}/sys"
    mount -o bind /dev "${rootfs}/dev"
    mount -o bind /dev/pts "${rootfs}/dev/pts"
    
    # Copy QEMU static binary for ARM emulation
    if [ "$PI_VERSION" = "zero" ]; then
        cp /usr/bin/qemu-arm-static "${rootfs}/usr/bin/"
    else
        cp /usr/bin/qemu-aarch64-static "${rootfs}/usr/bin/"
    fi
    
    # Copy resolv.conf for network access
    cp /etc/resolv.conf "${rootfs}/etc/resolv.conf"
    
    log_success "Chroot ready"
}

run_in_chroot() {
    local rootfs="${WORK_DIR}/rootfs"
    chroot "$rootfs" /bin/bash -c "$1"
}

install_dependencies() {
    log_info "Installing ThermoConsole dependencies..."
    
    run_in_chroot "
        # Update package lists
        apt-get update
        
        # Install build tools and dependencies
        DEBIAN_FRONTEND=noninteractive apt-get install -y \
            build-essential \
            cmake \
            libsdl2-dev \
            libsdl2-image-dev \
            libsdl2-mixer-dev \
            liblua5.4-dev \
            i2c-tools \
            python3-smbus
        
        # Clean up
        apt-get clean
        rm -rf /var/lib/apt/lists/*
    "
    
    log_success "Dependencies installed"
}

install_thermoconsole() {
    log_info "Installing ThermoConsole..."
    
    local rootfs="${WORK_DIR}/rootfs"
    local tc_dir="${rootfs}/opt/thermoconsole"
    
    # Create directories
    mkdir -p "$tc_dir"
    mkdir -p "${rootfs}/home/pi/games"
    mkdir -p "${rootfs}/home/pi/.thermoconsole"
    
    # Copy ThermoConsole source
    cp -r "${SCRIPT_DIR}/../runtime" "$tc_dir/"
    cp -r "${SCRIPT_DIR}/../sdk" "$tc_dir/"
    cp -r "${SCRIPT_DIR}/../games" "$tc_dir/"
    cp -r "${SCRIPT_DIR}/../docs" "$tc_dir/"
    
    # Build runtime in chroot
    run_in_chroot "
        cd /opt/thermoconsole/runtime
        mkdir -p build && cd build
        cmake .. -DTHERMO_BUILD_PI=ON
        make -j\$(nproc)
    "
    
    # Create symlinks
    run_in_chroot "
        ln -sf /opt/thermoconsole/runtime/build/thermoconsole /usr/local/bin/thermoconsole
        
        # Copy example games to user directory
        cp -r /opt/thermoconsole/games/* /home/pi/games/
        
        # Set default game
        ln -sf /home/pi/games/space_dodge /home/pi/games/current
        
        # Set permissions
        chown -R 1000:1000 /home/pi/games
        chown -R 1000:1000 /home/pi/.thermoconsole
    "
    
    # Copy included games if specified
    if [ -n "$GAMES_DIR" ] && [ -d "$GAMES_DIR" ]; then
        log_info "Copying additional games from $GAMES_DIR..."
        cp -r "$GAMES_DIR"/* "${rootfs}/home/pi/games/"
        run_in_chroot "chown -R 1000:1000 /home/pi/games"
    fi
    
    log_success "ThermoConsole installed"
}

configure_display() {
    log_info "Configuring Waveshare 2.8\" DPI LCD..."
    
    local boot="${WORK_DIR}/boot"
    local rootfs="${WORK_DIR}/rootfs"
    
    # Backup original config
    cp "${boot}/config.txt" "${boot}/config.txt.backup"
    
    # Write new config.txt
    cat > "${boot}/config.txt" << 'EOF'
# ThermoConsole Configuration
# Waveshare 2.8" DPI LCD (480x640 -> rotated to 640x480)

# Disable default display drivers (required for DPI)
# dtoverlay=vc4-kms-v3d  # COMMENTED OUT - conflicts with DPI

# GPU memory
gpu_mem=128

# DPI LCD Configuration
gpio=0-9=a2
gpio=12-17=a2
gpio=20-25=a2
dtoverlay=dpi24
enable_dpi_lcd=1
display_default_lcd=1

# Display timing for Waveshare 2.8" (480x640 native, displayed as landscape)
dpi_group=2
dpi_mode=87
dpi_output_format=0x7F216
hdmi_timings=480 0 26 16 10 640 0 25 10 15 0 0 0 60 0 32000000 1

# Waveshare overlay (if available)
dtoverlay=waveshare-28dpi-3b-4b

# Enable I2C for controller
dtparam=i2c_arm=on
dtparam=i2c_arm_baudrate=400000

# Disable Bluetooth (frees UART and saves power)
dtoverlay=disable-bt

# Audio via PWM (GPIO 18/19)
dtoverlay=pwm-2chan,pin=18,func=2,pin2=19,func2=2

# Boot options
disable_splash=1
boot_delay=0

# Overclock (optional - Pi Zero 2 W)
# arm_freq=1200
# over_voltage=2
EOF

    # Download Waveshare overlay if not bundled
    # (In production, we'd include this in the repo)
    log_info "Note: Waveshare DTBO files should be copied to /boot/overlays/"
    
    log_success "Display configured"
}

configure_boot() {
    log_info "Configuring boot options..."
    
    local boot="${WORK_DIR}/boot"
    local rootfs="${WORK_DIR}/rootfs"
    
    # cmdline.txt - quiet boot, no splash
    cat > "${boot}/cmdline.txt" << 'EOF'
console=tty3 root=PARTUUID=PLACEHOLDER rootfstype=ext4 fsck.repair=yes rootwait quiet loglevel=3 vt.global_cursor_default=0 logo.nologo
EOF

    # Get the actual PARTUUID
    local partuuid=$(blkid -s PARTUUID -o value "$ROOT_PART")
    sed -i "s/PLACEHOLDER/${partuuid}/" "${boot}/cmdline.txt"
    
    # Update fstab with correct PARTUUIDs
    local boot_partuuid=$(blkid -s PARTUUID -o value "$BOOT_PART")
    cat > "${rootfs}/etc/fstab" << EOF
proc            /proc           proc    defaults          0       0
PARTUUID=${boot_partuuid}  /boot           vfat    defaults          0       2
PARTUUID=${partuuid}  /               ext4    defaults,noatime  0       1
tmpfs           /tmp            tmpfs   defaults,noatime,nosuid,size=64m 0 0
EOF

    log_success "Boot configured"
}

configure_systemd_service() {
    log_info "Creating ThermoConsole systemd service..."
    
    local rootfs="${WORK_DIR}/rootfs"
    
    # Main service
    cat > "${rootfs}/etc/systemd/system/thermoconsole.service" << 'EOF'
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

# Resource limits
Nice=-5
CPUSchedulingPolicy=fifo
CPUSchedulingPriority=50

# Security
ProtectSystem=strict
ProtectHome=read-only
ReadWritePaths=/home/pi/.thermoconsole /tmp

[Install]
WantedBy=multi-user.target
EOF

    # Game switcher service (triggered by file change)
    cat > "${rootfs}/etc/systemd/system/thermoconsole-reload.path" << 'EOF'
[Unit]
Description=Watch for game changes

[Path]
PathChanged=/home/pi/games/current
Unit=thermoconsole-reload.service

[Install]
WantedBy=multi-user.target
EOF

    cat > "${rootfs}/etc/systemd/system/thermoconsole-reload.service" << 'EOF'
[Unit]
Description=Reload ThermoConsole on game change

[Service]
Type=oneshot
ExecStart=/bin/systemctl restart thermoconsole.service
EOF

    # Enable services
    run_in_chroot "
        systemctl enable thermoconsole.service
        systemctl enable thermoconsole-reload.path
        
        # Disable unnecessary services for faster boot
        systemctl disable bluetooth.service || true
        systemctl disable hciuart.service || true
        systemctl disable apt-daily.service || true
        systemctl disable apt-daily-upgrade.service || true
        systemctl disable man-db.timer || true
        systemctl disable e2scrub_reap.service || true
    "
    
    log_success "Systemd services configured"
}

configure_user() {
    log_info "Configuring user environment..."
    
    local rootfs="${WORK_DIR}/rootfs"
    
    # Hostname
    echo "thermoconsole" > "${rootfs}/etc/hostname"
    sed -i 's/raspberrypi/thermoconsole/g' "${rootfs}/etc/hosts"
    
    # Add pi user to required groups
    run_in_chroot "
        usermod -a -G i2c,gpio,video,audio,input pi
    "
    
    # Bashrc additions for pi user
    cat >> "${rootfs}/home/pi/.bashrc" << 'EOF'

# ThermoConsole aliases
alias tc-restart='sudo systemctl restart thermoconsole'
alias tc-stop='sudo systemctl stop thermoconsole'
alias tc-logs='journalctl -u thermoconsole -f'
alias tc-switch='function _switch() { ln -sf "$1" /home/pi/games/current && sudo systemctl restart thermoconsole; }; _switch'

# Show status on login
echo ""
echo "╔═══════════════════════════════════════╗"
echo "║         ThermoConsole v${TC_VERSION}           ║"
echo "╚═══════════════════════════════════════╝"
echo ""
systemctl is-active thermoconsole >/dev/null && echo "Status: Running" || echo "Status: Stopped"
echo "Current game: $(readlink -f /home/pi/games/current 2>/dev/null || echo 'None')"
echo ""
echo "Commands: tc-restart, tc-stop, tc-logs, tc-switch <game>"
echo ""
EOF

    run_in_chroot "chown 1000:1000 /home/pi/.bashrc"
    
    log_success "User configured"
}

create_utility_scripts() {
    log_info "Creating utility scripts..."
    
    local rootfs="${WORK_DIR}/rootfs"
    
    # Game switcher CLI
    cat > "${rootfs}/usr/local/bin/tc-switch" << 'EOF'
#!/bin/bash
# Switch ThermoConsole game

GAMES_DIR="/home/pi/games"

if [ -z "$1" ]; then
    echo "Available games:"
    for game in "$GAMES_DIR"/*; do
        if [ -d "$game" ] || [ "${game##*.}" = "tcr" ]; then
            name=$(basename "$game")
            if [ "$(readlink -f "$GAMES_DIR/current")" = "$(readlink -f "$game")" ]; then
                echo "  * $name (current)"
            else
                echo "    $name"
            fi
        fi
    done
    echo ""
    echo "Usage: tc-switch <game-name>"
    exit 0
fi

GAME="$GAMES_DIR/$1"

if [ ! -e "$GAME" ]; then
    echo "Game not found: $1"
    exit 1
fi

ln -sf "$GAME" "$GAMES_DIR/current"
echo "Switched to: $1"
echo "Restarting ThermoConsole..."
sudo systemctl restart thermoconsole
EOF

    chmod +x "${rootfs}/usr/local/bin/tc-switch"
    
    # System info script
    cat > "${rootfs}/usr/local/bin/tc-info" << 'EOF'
#!/bin/bash
# ThermoConsole system info

echo "╔═══════════════════════════════════════╗"
echo "║       ThermoConsole System Info       ║"
echo "╚═══════════════════════════════════════╝"
echo ""
echo "Service Status:"
systemctl status thermoconsole --no-pager | head -5
echo ""
echo "Current Game: $(readlink -f /home/pi/games/current 2>/dev/null || echo 'None')"
echo ""
echo "Hardware:"
echo "  CPU Temp:    $(vcgencmd measure_temp | cut -d= -f2)"
echo "  Throttled:   $(vcgencmd get_throttled | cut -d= -f2)"
echo "  Memory Free: $(free -h | awk '/^Mem:/ {print $4}')"
echo ""
echo "I2C Devices:"
i2cdetect -y 1 2>/dev/null | grep -E "^[0-9]" || echo "  (none or I2C disabled)"
echo ""
echo "Display:"
cat /sys/class/graphics/fb0/virtual_size 2>/dev/null || echo "  (no framebuffer)"
EOF

    chmod +x "${rootfs}/usr/local/bin/tc-info"
    
    log_success "Utility scripts created"
}

cleanup_chroot() {
    log_info "Cleaning up chroot..."
    
    local rootfs="${WORK_DIR}/rootfs"
    
    # Remove QEMU binary
    rm -f "${rootfs}/usr/bin/qemu-arm-static"
    rm -f "${rootfs}/usr/bin/qemu-aarch64-static"
    
    # Restore resolv.conf
    rm -f "${rootfs}/etc/resolv.conf"
    ln -sf /run/systemd/resolve/resolv.conf "${rootfs}/etc/resolv.conf" 2>/dev/null || true
    
    # Clean temp files
    rm -rf "${rootfs}/tmp/*"
    rm -rf "${rootfs}/var/tmp/*"
    rm -rf "${rootfs}/var/cache/apt/*"
    
    # Unmount chroot filesystems
    umount "${rootfs}/dev/pts" 2>/dev/null || true
    umount "${rootfs}/dev" 2>/dev/null || true
    umount "${rootfs}/sys" 2>/dev/null || true
    umount "${rootfs}/proc" 2>/dev/null || true
    
    log_success "Chroot cleaned"
}

finalize_image() {
    log_info "Finalizing image..."
    
    # Sync filesystems
    sync
    
    # Unmount partitions
    umount "${WORK_DIR}/boot"
    umount "${WORK_DIR}/rootfs"
    
    # Detach loop device
    kpartx -d "$LOOP_DEV"
    losetup -d "$LOOP_DEV"
    LOOP_DEV=""  # Prevent cleanup from trying again
    
    # Move to output location
    mv "${WORK_DIR}/thermoconsole.img" "${OUTPUT_IMAGE}"
    
    # Compress (optional)
    log_info "Compressing image..."
    xz -k -9 -T0 "${OUTPUT_IMAGE}"
    
    log_success "Image created: ${OUTPUT_IMAGE}"
    log_success "Compressed:    ${OUTPUT_IMAGE}.xz"
    
    # Print summary
    echo ""
    echo "════════════════════════════════════════════════════════════"
    echo " ThermoConsole Image Build Complete!"
    echo "════════════════════════════════════════════════════════════"
    echo ""
    echo " Output:     ${OUTPUT_IMAGE}"
    echo " Compressed: ${OUTPUT_IMAGE}.xz"
    echo " Size:       $(du -h "${OUTPUT_IMAGE}" | cut -f1)"
    echo ""
    echo " Flash to SD card with:"
    echo "   sudo dd if=${OUTPUT_IMAGE} of=/dev/sdX bs=4M status=progress"
    echo ""
    echo " Or use Raspberry Pi Imager / balenaEtcher"
    echo ""
    echo "════════════════════════════════════════════════════════════"
}

# ═══════════════════════════════════════════════════════════════════════════════
# Main
# ═══════════════════════════════════════════════════════════════════════════════

main() {
    echo ""
    echo "╔═══════════════════════════════════════════════════════════════╗"
    echo "║           ThermoConsole Image Builder v${TC_VERSION}               ║"
    echo "╚═══════════════════════════════════════════════════════════════╝"
    echo ""
    
    parse_args "$@"
    
    log_info "Building image for: Pi ${PI_VERSION}"
    log_info "Output: ${OUTPUT_IMAGE}"
    log_info "Size: ${IMAGE_SIZE_MB}MB"
    echo ""
    
    check_requirements
    download_base_image
    create_working_image
    mount_image
    copy_base_rootfs
    setup_chroot
    install_dependencies
    install_thermoconsole
    configure_display
    configure_boot
    configure_systemd_service
    configure_user
    create_utility_scripts
    cleanup_chroot
    finalize_image
}

main "$@"
