# ThermoConsole Runtime

The C engine that runs ThermoConsole games on PC and Raspberry Pi.

## Requirements

### Ubuntu/Debian
```bash
sudo apt install build-essential pkg-config \
    libsdl2-dev libsdl2-image-dev libsdl2-mixer-dev \
    liblua5.4-dev
```

### Arch Linux
```bash
sudo pacman -S sdl2 sdl2_image sdl2_mixer lua
```

### macOS (Homebrew)
```bash
brew install sdl2 sdl2_image sdl2_mixer lua
```

### Windows (MSYS2)
```bash
pacman -S mingw-w64-x86_64-SDL2 mingw-w64-x86_64-SDL2_image \
    mingw-w64-x86_64-SDL2_mixer mingw-w64-x86_64-lua
```

## Building

### Using Make (simple)
```bash
cd runtime
make check-deps   # verify dependencies
make              # build release
make debug        # build with debug symbols
```

### Using CMake
```bash
cd runtime
mkdir build && cd build
cmake ..
make
```

### For Raspberry Pi
```bash
# Cross-compile from PC
make pi

# Or on the Pi itself
make
```

## Running

```bash
# Run a .tcr ROM file
./thermoconsole path/to/game.tcr

# Run from game folder (development mode)
./thermoconsole path/to/game/
```

## Controls

| Button   | Keyboard          | Controller    |
|----------|-------------------|---------------|
| D-Pad    | Arrow keys / WASD | D-Pad / Stick |
| A        | Z / J             | A             |
| B        | X / K             | B             |
| X        | C / L             | X             |
| Y        | V / ;             | Y             |
| Start    | Enter             | Start         |
| Select   | Shift             | Back/Select   |
| Quit     | Escape            | -             |

## Architecture

```
runtime/
├── include/
│   └── thermo.h      # All types and function declarations
├── src/
│   ├── main.c        # Entry point and game loop
│   ├── graphics.c    # SDL2 rendering (cls, spr, print, etc.)
│   ├── input.c       # Keyboard and gamepad input
│   ├── audio.c       # SDL_mixer sound effects and music
│   ├── rom.c         # .tcr extraction and manifest parsing
│   ├── lua_api.c     # Lua bindings for all C functions
│   └── script.c      # Lua state management and callbacks
├── CMakeLists.txt    # CMake build
└── Makefile          # Simple make build
```

## Debug Mode

Build with debug symbols:
```bash
make debug
# or
cmake -DTHERMO_DEBUG=ON ..
```

Debug build enables:
- Extra logging
- Lua error traces
- Slower but more informative

## Performance Notes

- Target: 60 FPS on Raspberry Pi Zero
- Canvas resolution: 480x640 (portrait)
- Integer scaling for sharp pixels
- VSync enabled by default
