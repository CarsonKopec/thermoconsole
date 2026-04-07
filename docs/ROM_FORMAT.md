# ThermoConsole ROM Format (.tcr)

## Overview

A `.tcr` file is a ZIP archive with a specific structure. Games are packaged into this format for distribution and loaded by the ThermoConsole runtime on both PC (development) and Pi Zero (hardware).

## File Extension

`.tcr` — ThermoConsole ROM

## Archive Structure

```
game.tcr (ZIP archive)
├── manifest.json       # REQUIRED - Game metadata and configuration
├── main.lua            # REQUIRED - Entry point, must define _init/_update/_draw
├── sprites.png         # OPTIONAL - Sprite sheet (256x256 recommended)
├── tiles.png           # OPTIONAL - Tilemap graphics (256x256 recommended)
├── font.png            # OPTIONAL - Custom font (overrides default)
├── maps/               # OPTIONAL - Level data
│   ├── level1.json
│   └── level2.json
├── sfx/                # OPTIONAL - Sound effects
│   ├── jump.wav
│   └── coin.wav
├── music/              # OPTIONAL - Background music
│   └── theme.ogg
└── scripts/            # OPTIONAL - Additional Lua modules
    ├── player.lua
    └── enemies.lua
```

## manifest.json Schema

```json
{
  "name": "My Awesome Game",
  "author": "Your Name",
  "version": "1.0.0",
  "description": "A cool platformer for ThermoConsole",
  "entry": "main.lua",
  
  "display": {
    "width": 480,
    "height": 640,
    "orientation": "portrait",
    "scale_mode": "integer"
  },
  
  "sprites": {
    "file": "sprites.png",
    "grid_size": 16,
    "transparent_color": "#000000"
  },
  
  "tiles": {
    "file": "tiles.png",
    "grid_size": 16
  },
  
  "audio": {
    "sfx_channels": 4,
    "music_channels": 1
  },
  
  "save_slots": 4,
  
  "thermo": {
    "min_runtime": "1.0.0"
  }
}
```

### Required Fields

| Field       | Type   | Description                          |
|-------------|--------|--------------------------------------|
| `name`      | string | Display name of the game             |
| `author`    | string | Creator name                         |
| `version`   | string | Semantic version (X.Y.Z)             |
| `entry`     | string | Main Lua file to execute             |

### Optional Fields

| Field                       | Type    | Default       | Description                              |
|-----------------------------|---------|---------------|------------------------------------------|
| `description`               | string  | ""            | Short game description                   |
| `display.width`             | int     | 480           | Canvas width in pixels                   |
| `display.height`            | int     | 640           | Canvas height in pixels                  |
| `display.orientation`       | string  | "portrait"    | "portrait" or "landscape"                |
| `display.scale_mode`        | string  | "integer"     | "integer", "stretch", or "fit"           |
| `sprites.file`              | string  | "sprites.png" | Sprite sheet filename                    |
| `sprites.grid_size`         | int     | 16            | Sprite cell size (8, 16, or 32)          |
| `sprites.transparent_color` | string  | "#000000"     | Color treated as transparent (hex)       |
| `tiles.file`                | string  | "tiles.png"   | Tilemap graphics filename                |
| `tiles.grid_size`           | int     | 16            | Tile cell size                           |
| `audio.sfx_channels`        | int     | 4             | Simultaneous sound effects               |
| `audio.music_channels`      | int     | 1             | Simultaneous music tracks                |
| `save_slots`                | int     | 4             | Number of save data slots (0-8)          |
| `thermo.min_runtime`        | string  | "1.0.0"       | Minimum runtime version required         |

## Sprite Sheet Format

- **Recommended size**: 256x256 pixels (16x16 grid of 16px sprites = 256 sprites)
- **Format**: PNG with transparency OR solid background with transparent_color
- **Sprite IDs**: Numbered left-to-right, top-to-bottom starting at 0

```
┌────┬────┬────┬────┐
│ 0  │ 1  │ 2  │ 3  │  ...
├────┼────┼────┼────┤
│ 16 │ 17 │ 18 │ 19 │  ...
├────┼────┼────┼────┤
│ 32 │ 33 │ 34 │ 35 │  ...
└────┴────┴────┴────┘
```

## Map Data Format (maps/*.json)

```json
{
  "width": 30,
  "height": 20,
  "layers": [
    {
      "name": "background",
      "data": [0, 0, 1, 1, 2, ...]
    },
    {
      "name": "collision",
      "data": [0, 0, 0, 1, 1, ...]
    }
  ],
  "flags": {
    "1": ["solid"],
    "2": ["solid", "hazard"],
    "5": ["platform"]
  }
}
```

- `data` array is row-major (left-to-right, top-to-bottom)
- Tile ID 0 is typically empty/transparent
- `flags` maps tile IDs to collision/behavior flags

## Audio Formats

### Sound Effects (sfx/)
- **Formats**: WAV (recommended), OGG
- **Sample rate**: 22050 Hz or 44100 Hz
- **Channels**: Mono recommended for smaller files
- **Named by filename**: `sfx("jump")` plays `sfx/jump.wav`

### Music (music/)
- **Formats**: OGG (recommended), WAV
- **Streaming**: Music is streamed, not loaded fully into memory
- **Named by filename**: `music("theme")` plays `music/theme.ogg`

## Additional Lua Scripts (scripts/)

Games can split code into multiple files:

```lua
-- main.lua
require("scripts/player")
require("scripts/enemies")

function _init()
  player_init()
  enemies_init()
end
```

## Save Data

- Each game gets isolated save storage
- Up to `save_slots` slots (default 4)
- Each slot stores a serialized Lua table
- Max 64KB per slot

## ROM Integrity

The runtime validates:
1. ZIP structure is valid
2. `manifest.json` exists and parses correctly
3. Entry Lua file exists
4. Referenced asset files exist

Invalid ROMs display an error screen rather than crashing.

## Example Minimal ROM

```
minimal.tcr
├── manifest.json
└── main.lua
```

**manifest.json:**
```json
{
  "name": "Hello ThermoConsole",
  "author": "Carson",
  "version": "1.0.0",
  "entry": "main.lua"
}
```

**main.lua:**
```lua
function _init()
  x = 240
  y = 320
end

function _update()
  if btn(0) then y = y - 2 end  -- up
  if btn(1) then y = y + 2 end  -- down
  if btn(2) then x = x - 2 end  -- left
  if btn(3) then x = x + 2 end  -- right
end

function _draw()
  cls(1)
  print("Hello ThermoConsole!", 140, 300, 7)
  spr(1, x, y)
end
```
