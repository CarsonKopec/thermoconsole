# ThermoConsole Image Builder

Create flashable SD card images with ThermoConsole pre-installed.

## Installation Methods

### Method 1: Pre-built Image (Easiest)

Download the latest release image and flash it:

```bash
# Download
wget https://github.com/user/thermoconsole/releases/download/v1.0.0/thermoconsole-zero2-v1.0.0.img.xz

# Flash to SD card (replace sdX with your SD card device)
xzcat thermoconsole-zero2-v1.0.0.img.xz | sudo dd of=/dev/sdX bs=4M status=progress

# Or use Raspberry Pi Imager / balenaEtcher
```

### Method 2: One-Line Installer (Recommended)

Flash standard Raspberry Pi OS Lite, then run:

```bash
curl -sSL https://raw.githubusercontent.com/user/thermoconsole/main/install.sh | sudo bash
```

Or download and run manually:

```bash
wget https://raw.githubusercontent.com/user/thermoconsole/main/install.sh
chmod +x install.sh
sudo ./install.sh
```

### Method 3: Build Your Own Image

#### Using pi-gen (Recommended for production)

```bash
# Requires Docker or Debian/Ubuntu host
cd image-builder
./build-pigen.sh --docker
```

#### Using build-image.sh (Simpler)

```bash
# Requires: qemu-user-static, parted, kpartx, xz-utils
sudo apt install qemu-user-static binfmt-support parted kpartx xz-utils wget

cd image-builder
sudo ./build-image.sh --pi-version zero2 --games /path/to/games
```

## Build Options

### build-image.sh

| Option | Description | Default |
|--------|-------------|---------|
| `--pi-version <zero\|zero2>` | Target Pi model | `zero2` |
| `--output <path>` | Output image path | `thermoconsole-<version>.img` |
| `--size <MB>` | Image size in MB | `2048` |
| `--skip-download` | Use cached base image | `false` |
| `--games <path>` | Include additional games | - |

### Examples

```bash
# Build for Pi Zero 2 W
sudo ./build-image.sh --pi-version zero2

# Build for original Pi Zero
sudo ./build-image.sh --pi-version zero

# Build with custom games
sudo ./build-image.sh --games ~/my-games

# Build larger image
sudo ./build-image.sh --size 4096
```

## First Boot

1. Flash image to SD card
2. Insert SD card into Pi Zero
3. Connect hardware:
   - Waveshare 2.8" DPI LCD (via 40-pin GPIO)
   - Pi Pico controller (I2C: GPIO 2/3)
4. Power on

ThermoConsole starts automatically with the default game.

## Default Credentials

- **Username:** `pi`
- **Password:** `raspberry`
- **Hostname:** `thermoconsole` (SSH: `ssh pi@thermoconsole.local`)

## Post-Install Commands

```bash
# Switch games
tc-switch space_dodge

# Restart ThermoConsole
tc-restart

# View logs
tc-logs

# System info
tc-info

# Stop ThermoConsole (for SSH debugging)
tc-stop
```

## Adding Games

Copy games to `/home/pi/games/`:

```bash
# Via SCP
scp -r my-game pi@thermoconsole.local:~/games/

# Then switch to it
ssh pi@thermoconsole.local "tc-switch my-game"
```

## Hardware Requirements

- Raspberry Pi Zero W or Zero 2 W
- Waveshare 2.8" DPI LCD (480×640)
- Pi Pico (for controller)
- MicroSD card (8GB+)
- 10× tactile buttons

## Wiring

### Display (Waveshare 2.8" DPI LCD)

Plugs directly onto 40-pin GPIO header.

### Controller (Pi Pico via I2C)

| Pico | Pi Zero | Function |
|------|---------|----------|
| GPIO 14 | GPIO 2 | SDA |
| GPIO 15 | GPIO 3 | SCL |
| GND | GND | Ground |
| VSYS | 5V | Power |

## Troubleshooting

### Display not working

1. Check LCD is firmly seated on GPIO header
2. Verify `/boot/config.txt` has DPI settings
3. Check `dmesg` for errors

### Controller not detected

```bash
# Check I2C
i2cdetect -y 1
# Should show 42 at address 0x42
```

### Service not starting

```bash
# Check status
systemctl status thermoconsole

# Check logs
journalctl -u thermoconsole -e

# Run manually for debugging
sudo systemctl stop thermoconsole
thermoconsole /home/pi/games/current
```

## Build Requirements (for building images)

### Host system

- Linux (Ubuntu 22.04+ recommended)
- 4GB+ RAM
- 10GB+ free disk space
- Internet connection

### Packages

```bash
# For build-image.sh
sudo apt install qemu-user-static binfmt-support parted kpartx xz-utils wget unzip rsync

# For pi-gen (if not using Docker)
sudo apt install quilt parted qemu-user-static debootstrap zerofree pxz zip \
    dosfstools libarchive-tools libcap2-bin grep rsync xz-utils file git curl bc \
    gpg pigz xxd arch-test
```

## License

ThermoConsole is open source. See LICENSE file.
