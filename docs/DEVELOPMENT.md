# ThermoConsole Development Without Hardware

You don't need a physical Raspberry Pi to develop for ThermoConsole. Here are your options:

## Quick Start

```bash
./setup-dev.sh
# Choose option 1 for native PC build
```

## Options Overview

| Method | Speed | Accuracy | Best For |
|--------|-------|----------|----------|
| **Native PC** | ⚡ Fast | Good | Game development, UI |
| **Docker ARM** | 🐢 Slow | Better | Cross-compilation |
| **QEMU Full** | 🐌 Very slow | Best | System testing |
| **Mock Hardware** | ⚡ Fast | Limited | CI/CD, headless |

---

## 1. Native PC Build (Recommended)

Build and run ThermoConsole directly on your PC. Games run at full speed with keyboard/gamepad input.

### Setup

```bash
# Linux (Ubuntu/Debian)
sudo apt install build-essential cmake libsdl2-dev libsdl2-image-dev \
                 libsdl2-mixer-dev liblua5.4-dev

# macOS
brew install cmake sdl2 sdl2_image sdl2_mixer lua

# Windows (MSYS2)
pacman -S mingw-w64-x86_64-{gcc,cmake,SDL2,SDL2_image,SDL2_mixer,lua}
```

### Build & Run

```bash
cd runtime
mkdir build && cd build
cmake ..
make
./thermoconsole ../../games/space_dodge
```

### Differences from Pi

- Uses keyboard/gamepad instead of I2C controller
- Runs in a window (not fullscreen DPI display)
- No USB gadget mode
- Faster performance

---

## 2. Docker ARM Emulation

Cross-compile ARM binaries on x86 using QEMU user-mode emulation.

### Setup

```bash
# Enable ARM emulation
docker run --rm --privileged multiarch/qemu-user-static --reset -p yes

# Build ARM container
docker build -f Dockerfile.arm -t thermoconsole-arm .
```

### Usage

```bash
# Interactive shell
docker run -it thermoconsole-arm bash

# Run the Pi build
docker run -it thermoconsole-arm ./runtime/build/thermoconsole --help
```

### When to Use

- Testing Pi-specific compilation
- Checking for ARM-specific issues
- Building release binaries

---

## 3. QEMU Full System Emulation

Run a complete Raspberry Pi OS in a virtual machine.

### Setup

```bash
# Install QEMU
sudo apt install qemu-system-arm qemu-utils

# Download kernel and image (automated by setup-dev.sh)
cd qemu-pi
./run-qemu.sh
```

### Access

```bash
# SSH into the emulated Pi
ssh -p 2222 pi@localhost
# Password: raspberry
```

### Limitations

- **Very slow** (no hardware acceleration)
- Limited to Pi 1 level CPU (arm1176)
- No GPU emulation (software rendering only)
- No GPIO/I2C hardware

### When to Use

- Testing systemd services
- Testing boot process
- Testing full installation

---

## 4. Mock Hardware

Simulate the Pico controller for headless testing.

### Setup

```bash
pip install keyboard  # Optional, for keyboard input

python3 scripts/mock-i2c.py
```

### How It Works

The mock script creates a TCP server that responds like the I2C controller. ThermoConsole can connect to it instead of real hardware.

### Controls

| Key | Button |
|-----|--------|
| W/↑ | Up |
| S/↓ | Down |
| A/← | Left |
| D/→ | Right |
| J/Z | A |
| K/X | B |
| L/C | X |
| ;/V | Y |
| Enter | Start |
| Shift | Select |

---

## Online Emulators

### Wokwi (Pi Pico)

Test the controller firmware online:
1. Go to https://wokwi.com
2. Create new Pi Pico project
3. Paste `pico-controller/controller.c`
4. Add buttons in the simulator
5. Run and test I2C output

### Replit / GitPod

Run the native build in a cloud IDE:
- SDL2 works in web browsers via Emscripten
- Good for sharing demos

---

## Comparison: PC vs Pi

| Feature | PC Build | Pi Build |
|---------|----------|----------|
| Resolution | 640x480 window | 640x480 fullscreen |
| Input | Keyboard/Gamepad | I2C Pico controller |
| Display | SDL2 window | DPI LCD |
| Audio | System audio | PWM/HDMI |
| USB Mode | Not available | Mass storage gadget |
| Performance | ~1000 FPS (uncapped) | 60 FPS target |
| Save location | `~/.thermoconsole/` | `/home/pi/.thermoconsole/` |

---

## CI/CD Testing

For automated testing without display:

```bash
# Use virtual framebuffer
Xvfb :99 -screen 0 640x480x24 &
export DISPLAY=:99

# Run with dummy audio
export SDL_AUDIODRIVER=dummy

# Run tests
./thermoconsole --test games/hello
```

---

## Tips

1. **Develop on PC first** - It's 10x faster
2. **Test on Docker ARM** - Catch ARM-specific issues
3. **Final test on real Pi** - Only needed for hardware integration
4. **Use the mock controller** - For automated testing

The PC build is functionally identical for game logic - if it works on PC, it'll work on Pi (with minor platform differences).
