# ThermoConsole Pi Zero Setup Guide

Complete guide for setting up the ThermoConsole on Raspberry Pi Zero with a 2.8" DPI LCD (480x640).

## Hardware Requirements

- Raspberry Pi Zero (W or 2 W recommended for WiFi)
- 2.8" DPI LCD (480x640 portrait) - typically ILI9341 or ST7789 based
- Pi Pico for controller input
- MicroSD card (8GB+)
- Buttons (10x tactile switches)
- Power supply (5V 2A recommended)

## Part 1: Pi Zero OS Setup

### 1.1 Flash Raspberry Pi OS Lite

```bash
# On your PC, use Raspberry Pi Imager
# Choose: Raspberry Pi OS Lite (64-bit) for Pi Zero 2 W
# Or: Raspberry Pi OS Lite (32-bit) for original Pi Zero

# Enable SSH and configure WiFi in Imager settings
```

### 1.2 First Boot Configuration

```bash
# SSH into your Pi
ssh pi@thermoconsole.local

# Update system
sudo apt update && sudo apt upgrade -y

# Install dependencies
sudo apt install -y \
    build-essential \
    cmake \
    libsdl2-dev \
    libsdl2-image-dev \
    libsdl2-mixer-dev \
    liblua5.4-dev \
    git \
    unzip
```

## Part 2: DPI LCD Configuration

### 2.1 Understanding DPI Mode

The DPI (Display Parallel Interface) uses GPIO pins directly for display output. This gives better performance than SPI displays but uses more pins.

### 2.2 Typical DPI Pin Mapping (Mode 5 - RGB565)

| Function | GPIO | Physical Pin |
|----------|------|--------------|
| CLK      | 0    | -            |
| DEN      | 1    | -            |
| HSYNC    | 2    | -            |
| VSYNC    | 3    | -            |
| D0 (B3)  | 4    | 7            |
| D1 (B4)  | 5    | 29           |
| D2 (B5)  | 6    | 31           |
| D3 (B6)  | 7    | 26           |
| D4 (B7)  | 8    | 24           |
| D5 (G2)  | 9    | 21           |
| D6 (G3)  | 10   | 19           |
| D7 (G4)  | 11   | 23           |
| D8 (G5)  | 12   | 32           |
| D9 (G6)  | 13   | 33           |
| D10 (G7) | 14   | 8            |
| D11 (R3) | 15   | 10           |
| D12 (R4) | 16   | 36           |
| D13 (R5) | 17   | 11           |
| D14 (R6) | 18   | 12           |
| D15 (R7) | 19   | 35           |

### 2.3 Configure /boot/config.txt

```bash
sudo nano /boot/config.txt
```

Add/modify these lines:

```ini
# Disable default display drivers
dtoverlay=vc4-fkms-v3d
max_framebuffers=1

# Enable DPI display
dtoverlay=dpi24
overscan_left=0
overscan_right=0
overscan_top=0
overscan_bottom=0

# DPI Mode configuration
# Mode 5 = RGB565 (16-bit color, uses fewer pins)
# Mode 6 = RGB666 (18-bit color)
# Mode 7 = RGB888 (24-bit color, uses most pins)
dpi_output_format=0x6f015

# Display timings for 480x640 @ 60Hz
# Adjust these based on your specific LCD datasheet
dpi_group=2
dpi_mode=87

# Custom timing: 480x640 60Hz
#   h_active_pixels h_sync_polarity h_front_porch h_sync_pulse h_back_porch
#   v_active_pixels v_sync_polarity v_front_porch v_sync_pulse v_back_porch
#   v_sync_offset_a v_sync_offset_b pixel_rep framerate interlace pixel_freq
#   aspect_ratio

hdmi_timings=480 0 10 10 20 640 0 4 2 4 0 0 0 60 0 20000000 1

# Force HDMI even without monitor
hdmi_force_hotplug=1

# Disable HDMI audio (we use analog or USB audio)
hdmi_drive=1

# GPU memory (minimum for headless + SDL2)
gpu_mem=64

# Disable Bluetooth to free up UART (if using UART for Pico)
dtoverlay=disable-bt
```

### 2.4 Alternative: SPI Display Setup

If using an SPI display instead of DPI:

```ini
# In /boot/config.txt
dtoverlay=spi0-1cs
dtparam=spi=on

# For ILI9341 based displays
dtoverlay=fbtft,ili9341,rotate=0,speed=40000000,fps=60
```

### 2.5 Verify Display

```bash
# Reboot
sudo reboot

# Check framebuffer
ls -la /dev/fb*

# Test display (shows rainbow)
cat /dev/urandom > /dev/fb0

# Check resolution
fbset -fb /dev/fb0
```

## Part 3: Build ThermoConsole Runtime

### 3.1 Transfer Source Code

```bash
# On your PC
scp thermoconsole-full.zip pi@thermoconsole.local:~/

# On the Pi
cd ~
unzip thermoconsole-full.zip
cd thermoconsole/runtime
```

### 3.2 Build for Pi

```bash
mkdir build && cd build

# Build with Pi platform flag
cmake .. -DTHERMO_BUILD_PI=ON

# Compile (this takes a while on Pi Zero)
make -j2

# Or use the Makefile
cd ..
make PLATFORM=pi
```

### 3.3 Test

```bash
# Run the hello demo
./thermoconsole ../games/hello

# Or with a .tcr file
./thermoconsole ../games/hello.tcr
```

## Part 4: Pi Pico Controller Setup

### 4.1 Build Pico Firmware (on PC)

```bash
# Clone Pico SDK if not already done
git clone https://github.com/raspberrypi/pico-sdk.git
cd pico-sdk
git submodule update --init
export PICO_SDK_PATH=$(pwd)
cd ..

# Build controller firmware
cd thermoconsole/pico-controller
mkdir build && cd build
cmake ..
make
```

### 4.2 Flash Pico

1. Hold BOOTSEL button on Pico
2. Plug into USB
3. Copy `thermo_controller.uf2` to the mounted drive
4. Pico reboots automatically

### 4.3 Connect Pico to Pi Zero

Connect Pico to Pi Zero via USB cable. The Pi will see it as `/dev/ttyACM0`.

```bash
# Verify connection
ls -la /dev/ttyACM0

# Test reading (should see binary data when pressing buttons)
cat /dev/ttyACM0 | xxd | head
```

## Part 5: Auto-Start Configuration

### 5.1 Create Systemd Service

```bash
sudo nano /etc/systemd/system/thermoconsole.service
```

```ini
[Unit]
Description=ThermoConsole Game Runtime
After=multi-user.target

[Service]
Type=simple
User=pi
WorkingDirectory=/home/pi/thermoconsole
ExecStart=/home/pi/thermoconsole/runtime/build/thermoconsole /home/pi/games/current.tcr
Restart=always
RestartSec=3

# Environment for SDL2 on framebuffer
Environment=SDL_VIDEODRIVER=kmsdrm
Environment=SDL_AUDIODRIVER=alsa

[Install]
WantedBy=multi-user.target
```

### 5.2 Enable Service

```bash
sudo systemctl daemon-reload
sudo systemctl enable thermoconsole
sudo systemctl start thermoconsole

# Check status
sudo systemctl status thermoconsole

# View logs
journalctl -u thermoconsole -f
```

### 5.3 Game Switcher Script

```bash
nano ~/switch_game.sh
```

```bash
#!/bin/bash
# Switch to a different game

GAME_PATH="$1"

if [ -z "$GAME_PATH" ]; then
    echo "Usage: switch_game.sh <path/to/game.tcr>"
    exit 1
fi

if [ ! -f "$GAME_PATH" ]; then
    echo "Game not found: $GAME_PATH"
    exit 1
fi

# Update symlink
ln -sf "$GAME_PATH" /home/pi/games/current.tcr

# Restart service
sudo systemctl restart thermoconsole

echo "Switched to: $GAME_PATH"
```

```bash
chmod +x ~/switch_game.sh
```

## Part 6: Audio Setup

### 6.1 PWM Audio (No HAT Required)

```bash
# In /boot/config.txt
dtoverlay=pwm-2chan,pin=18,func=2,pin2=19,func2=2
```

### 6.2 USB Audio Adapter

```bash
# List audio devices
aplay -l

# Set default device
sudo nano /etc/asound.conf
```

```
defaults.pcm.card 1
defaults.ctl.card 1
```

### 6.3 Test Audio

```bash
# Test with speaker-test
speaker-test -t sine -f 440 -c 2

# Adjust volume
alsamixer
```

## Part 7: Performance Optimization

### 7.1 Overclock (Optional, Voids Warranty)

```ini
# In /boot/config.txt (Pi Zero 2 W)
arm_freq=1200
over_voltage=4
```

### 7.2 Reduce Memory Usage

```bash
# Disable unnecessary services
sudo systemctl disable bluetooth
sudo systemctl disable avahi-daemon
sudo systemctl disable triggerhappy

# Reduce logging
sudo nano /etc/systemd/journald.conf
# Set: Storage=volatile
# Set: RuntimeMaxUse=16M
```

### 7.3 Use tmpfs for Game Extraction

```bash
# In /etc/fstab
tmpfs /tmp tmpfs defaults,noatime,nosuid,size=64m 0 0
```

## Part 8: Troubleshooting

### Display Issues

```bash
# Check DPI is enabled
vcgencmd get_config dpi_output_format

# Check display detection
tvservice -s

# Force display on
tvservice -o && tvservice -p

# Check framebuffer info
cat /sys/class/graphics/fb0/virtual_size
```

### SDL2 Issues

```bash
# Force KMS/DRM driver
export SDL_VIDEODRIVER=kmsdrm
./thermoconsole game.tcr

# Or try framebuffer driver
export SDL_VIDEODRIVER=fbcon
```

### Audio Issues

```bash
# List ALSA devices
aplay -L

# Test specific device
aplay -D plughw:1,0 /usr/share/sounds/alsa/Front_Center.wav
```

### Pico Controller Issues

```bash
# Check USB connection
dmesg | tail -20

# Check serial port permissions
sudo usermod -a -G dialout pi
# (logout and login again)

# Test reading
stty -F /dev/ttyACM0 115200 raw
cat /dev/ttyACM0 | xxd
```

## Quick Reference

### File Locations

| Path | Purpose |
|------|---------|
| `/home/pi/thermoconsole/` | Main installation |
| `/home/pi/games/` | Game ROM storage |
| `/home/pi/games/current.tcr` | Currently loaded game |
| `/tmp/thermo/` | ROM extraction temp dir |
| `/home/pi/.thermoconsole/` | Save data |

### Useful Commands

```bash
# Start game manually
./thermoconsole game.tcr

# Switch games
~/switch_game.sh /home/pi/games/newgame.tcr

# View logs
journalctl -u thermoconsole -f

# Restart service
sudo systemctl restart thermoconsole

# Check CPU temp
vcgencmd measure_temp

# Check throttling
vcgencmd get_throttled
```
