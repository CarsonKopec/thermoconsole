# ThermoConsole

A custom handheld game console built with Raspberry Pi Zero, featuring a 2.8" DPI LCD and Lua-based game development.

```
┌─────────────────────────────────────────────────────────────────┐
│                     ┌─────────────────┐                         │
│                     │   2.8" LCD      │                         │
│                     │   640 x 480     │                         │
│                     │   (landscape)   │                         │
│                     └─────────────────┘                         │
│     ┌───┐                                         ┌───┐         │
│     │ ▲ │                                         │ Y │         │
│   ┌─┴───┴─┐                                     ┌─┴───┴─┐       │
│   │ ◄   ► │        [SELECT]  [START]            │ X   A │       │
│   └─┬───┬─┘                                     └─┬───┬─┘       │
│     │ ▼ │                                         │ B │         │
│     └───┘                                         └───┘         │
│                      ThermoConsole                              │
└─────────────────────────────────────────────────────────────────┘
```

## Features

- **640x480 landscape display** — Waveshare 2.8" DPI LCD
- **Lua scripting** — PICO-8 inspired API, easy to learn
- **60 FPS target** — smooth gameplay
- **10 button input** — D-pad + A/B/X/Y + Start/Select (via Pi Pico I2C)
- **Save system** — persistent game saves
- **Audio** — SFX and music support

## Installation

### Option 1: Flash Pre-built Image (Easiest)

Download and flash like any other OS:

```bash
# Download latest release
wget https://github.com/user/thermoconsole/releases/latest/thermoconsole.img.xz

# Flash to SD card
xzcat thermoconsole.img.xz | sudo dd of=/dev/sdX bs=4M status=progress
```

Or use **Raspberry Pi Imager** / **balenaEtcher** with the `.img.xz` file.

### Option 2: One-Line Installer

Flash Raspberry Pi OS Lite first, then:

```bash
curl -sSL https://thermoconsole.io/install.sh | sudo bash
```

### Option 3: Build Your Own Image

```bash
cd image-builder
sudo ./build-image.sh --pi-version zero2
```

See [image-builder/README.md](image-builder/README.md) for details.

## Hardware

| Component | Specification |
|-----------|---------------|
| Computer | Raspberry Pi Zero 2 W (or Zero W) |
| Display | Waveshare 2.8" DPI LCD (480×640, rotated to landscape) |
| Controller | Pi Pico via I2C (address 0x42) |
| Buttons | 10× tactile switches |
| Storage | MicroSD 8GB+ |

### Wiring

**Display**: Plugs directly onto Pi 40-pin GPIO header.

**Controller** (Pico → Pi):
| Pico | Pi | Function |
|------|-----|----------|
| GP14 | GP2 | SDA |
| GP15 | GP3 | SCL |
| GND | GND | Ground |
| VSYS | 5V | Power |

## Quick Start (Development)

### 1. Install Dependencies

```bash
# Ubuntu/Debian
sudo apt install build-essential cmake libsdl2-dev libsdl2-image-dev \
                 libsdl2-mixer-dev liblua5.4-dev

# macOS
brew install cmake sdl2 sdl2_image sdl2_mixer lua
```

### 2. Build the Runtime

```bash
cd runtime && mkdir build && cd build
cmake ..
make
```

### 3. Run a Game

```bash
./thermoconsole ../../games/space_dodge
```

### 4. Create Your Own Game

```bash
python3 sdk/new_game.py mygame --template shooter
# Edit games/mygame/main.lua
./runtime/build/thermoconsole games/mygame
```

## Game Development

### Minimal Game

```lua
function _init()
    x, y = 320, 240
end

function _update()
    if btn(0) then y = y - 2 end  -- up
    if btn(1) then y = y + 2 end  -- down
    if btn(2) then x = x - 2 end  -- left
    if btn(3) then x = x + 2 end  -- right
end

function _draw()
    cls(1)
    circfill(x, y, 10, 11)
    print("Hello!", 280, 20, 7)
end
```

### Key API Functions

```lua
-- Graphics (640x480, 16 colors)
cls(color)                      -- Clear screen
rectfill(x, y, w, h, color)     -- Filled rectangle
circfill(x, y, r, color)        -- Filled circle
spr(id, x, y)                   -- Draw sprite
print(text, x, y, color)        -- Draw text

-- Input (buttons 0-9)
btn(id)   -- Is button held?
btnp(id)  -- Was button just pressed?

-- Audio
sfx("name")      -- Play sound effect
music("name")    -- Play/stop music
```

Full API: [docs/API.md](docs/API.md)

## Controls

### PC Development

| Button | Keys |
|--------|------|
| D-Pad | Arrow keys / WASD |
| A/B/X/Y | Z/X/C/V or J/K/L |
| Start/Select | Enter / Shift |
| Quit | Escape |

### Hardware

10 buttons wired to Pi Pico GPIOs 2-11, active LOW.

## Project Structure

```
thermoconsole/
├── install.sh              # One-line installer
├── image-builder/          # Build flashable images
│   ├── build-image.sh      # Main image builder
│   └── build-pigen.sh      # pi-gen based builder
├── runtime/                # C game engine
├── sdk/                    # Python dev tools
├── games/                  # Example games
├── pico-controller/        # Pi Pico I2C controller firmware
└── docs/                   # Documentation
```

## License

MIT License

## Credits

Built with [SDL2](https://libsdl.org/), [Lua 5.4](https://lua.org/), [Raspberry Pi Pico SDK](https://github.com/raspberrypi/pico-sdk)

Inspired by [PICO-8](https://www.lexaloffle.com/pico-8.php), [TIC-80](https://tic80.com/), [Playdate](https://play.date/)
