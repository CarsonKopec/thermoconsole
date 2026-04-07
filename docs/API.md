# ThermoConsole Lua API Reference

## Overview

Games for ThermoConsole are written in Lua 5.4. The runtime provides a set of functions for graphics, input, audio, and data persistence. Your game must define three callback functions that the runtime calls each frame.

**Target Specs:**
- Resolution: 480×640 (portrait)
- Frame rate: 60 FPS
- Color depth: 24-bit (16.7M colors)
- Sprite sheet: 256×256 (256 sprites at 16×16)

---

## Lifecycle Callbacks

Your game **must** define these functions:

### `_init()`
Called once when the game starts. Use for initialization.

```lua
function _init()
  player = { x = 240, y = 320, speed = 3 }
  score = 0
end
```

### `_update()`
Called every frame (60 times per second). Use for game logic.

```lua
function _update()
  if btn(3) then
    player.x = player.x + player.speed
  end
end
```

### `_draw()`
Called every frame after `_update()`. Use for rendering.

```lua
function _draw()
  cls(0)
  spr(1, player.x, player.y)
  print("Score: " .. score, 10, 10, 7)
end
```

---

## Graphics Functions

### `cls([color])`
Clear the screen to a color.

| Param   | Type | Default | Description        |
|---------|------|---------|--------------------|
| `color` | int  | 0       | Color index (0-15) |

```lua
cls()       -- clear to black
cls(1)      -- clear to dark blue
```

### `spr(id, x, y, [w], [h], [flip_x], [flip_y])`
Draw a sprite from the sprite sheet.

| Param    | Type | Default | Description                      |
|----------|------|---------|----------------------------------|
| `id`     | int  | —       | Sprite index (0-255)             |
| `x`      | num  | —       | X position                       |
| `y`      | num  | —       | Y position                       |
| `w`      | num  | 1       | Width in sprite units            |
| `h`      | num  | 1       | Height in sprite units           |
| `flip_x` | bool | false   | Flip horizontally                |
| `flip_y` | bool | false   | Flip vertically                  |

```lua
spr(0, 100, 200)              -- draw sprite 0 at (100, 200)
spr(1, 50, 50, 2, 2)          -- draw 2x2 sprite block starting at sprite 1
spr(5, 100, 100, 1, 1, true)  -- draw sprite 5, flipped horizontally
```

### `sspr(sx, sy, sw, sh, dx, dy, [dw], [dh], [flip_x], [flip_y])`
Draw a region of the sprite sheet with optional scaling.

| Param    | Type | Default | Description                      |
|----------|------|---------|----------------------------------|
| `sx`     | num  | —       | Source X (pixels on sheet)       |
| `sy`     | num  | —       | Source Y (pixels on sheet)       |
| `sw`     | num  | —       | Source width                     |
| `sh`     | num  | —       | Source height                    |
| `dx`     | num  | —       | Destination X                    |
| `dy`     | num  | —       | Destination Y                    |
| `dw`     | num  | sw      | Destination width (for scaling)  |
| `dh`     | num  | sh      | Destination height               |
| `flip_x` | bool | false   | Flip horizontally                |
| `flip_y` | bool | false   | Flip vertically                  |

```lua
-- Draw 32x32 region from sheet position (0,0) scaled to 64x64
sspr(0, 0, 32, 32, 100, 100, 64, 64)
```

### `rect(x, y, w, h, color)`
Draw a rectangle outline.

```lua
rect(10, 10, 100, 50, 7)  -- white rectangle outline
```

### `rectfill(x, y, w, h, color)`
Draw a filled rectangle.

```lua
rectfill(10, 10, 100, 50, 8)  -- red filled rectangle
```

### `line(x1, y1, x2, y2, color)`
Draw a line between two points.

```lua
line(0, 0, 480, 640, 7)  -- diagonal line corner to corner
```

### `circ(x, y, r, color)`
Draw a circle outline.

```lua
circ(240, 320, 50, 11)  -- green circle at center
```

### `circfill(x, y, r, color)`
Draw a filled circle.

```lua
circfill(240, 320, 50, 12)  -- blue filled circle
```

### `pset(x, y, color)`
Set a single pixel.

```lua
pset(100, 100, 7)  -- white pixel
```

### `pget(x, y)`
Get the color of a pixel. Returns color index.

```lua
local c = pget(100, 100)
```

### `print(text, x, y, [color])`
Draw text at a position.

| Param   | Type   | Default | Description            |
|---------|--------|---------|------------------------|
| `text`  | string | —       | Text to display        |
| `x`     | num    | —       | X position             |
| `y`     | num    | —       | Y position             |
| `color` | int    | 7       | Text color (default white) |

```lua
print("Score: 100", 10, 10)
print("GAME OVER", 180, 300, 8)  -- red text
```

### `camera([x], [y])`
Set the camera offset for all subsequent draw calls.

```lua
camera(player.x - 240, player.y - 320)  -- center on player
-- ... draw calls are now offset ...
camera()  -- reset to (0,0)
```

### `clip([x], [y], [w], [h])`
Set a clipping rectangle. Drawing outside is ignored.

```lua
clip(100, 100, 200, 200)  -- only draw within this region
-- ... draw calls ...
clip()  -- reset to full screen
```

### `pal(c1, c2)`
Swap color c1 with c2 for subsequent draws. Call with no args to reset.

```lua
pal(7, 8)   -- white becomes red
spr(1, 100, 100)
pal()       -- reset palette
```

---

## Color Palette

ThermoConsole uses a 16-color palette (expandable to 256):

| Index | Name         | Hex       |
|-------|--------------|-----------|
| 0     | Black        | `#000000` |
| 1     | Dark Blue    | `#1D2B53` |
| 2     | Dark Purple  | `#7E2553` |
| 3     | Dark Green   | `#008751` |
| 4     | Brown        | `#AB5236` |
| 5     | Dark Gray    | `#5F574F` |
| 6     | Light Gray   | `#C2C3C7` |
| 7     | White        | `#FFF1E8` |
| 8     | Red          | `#FF004D` |
| 9     | Orange       | `#FFA300` |
| 10    | Yellow       | `#FFEC27` |
| 11    | Green        | `#00E436` |
| 12    | Blue         | `#29ADFF` |
| 13    | Indigo       | `#83769C` |
| 14    | Pink         | `#FF77A8` |
| 15    | Peach        | `#FFCCAA` |

You can also use 24-bit colors directly:
```lua
rectfill(0, 0, 100, 100, 0xFF5500)  -- orange via hex
```

---

## Input Functions

### `btn(id)`
Check if a button is currently held. Returns `true`/`false`.

| ID | Button   | PC Key          |
|----|----------|-----------------|
| 0  | Up       | Arrow Up / W    |
| 1  | Down     | Arrow Down / S  |
| 2  | Left     | Arrow Left / A  |
| 3  | Right    | Arrow Right / D |
| 4  | A        | Z / J           |
| 5  | B        | X / K           |
| 6  | X        | C / L           |
| 7  | Y        | V / ;           |
| 8  | Start    | Enter           |
| 9  | Select   | Shift           |

```lua
if btn(4) then
  -- A button is held
  player_jump()
end
```

### `btnp(id)`
Check if a button was **just pressed** this frame (rising edge).

```lua
if btnp(4) then
  -- A button was just pressed (not held)
  fire_bullet()
end
```

---

## Audio Functions

### `sfx(name, [channel], [loop])`
Play a sound effect from `sfx/` folder.

| Param     | Type   | Default | Description                 |
|-----------|--------|---------|-----------------------------|
| `name`    | string | —       | Sound filename without ext  |
| `channel` | int    | -1      | Channel (0-3), -1 = auto    |
| `loop`    | bool   | false   | Loop the sound              |

```lua
sfx("jump")                  -- play sfx/jump.wav on any channel
sfx("engine", 0, true)       -- loop engine sound on channel 0
```

### `music(name, [loop])`
Play background music from `music/` folder.

```lua
music("level1")              -- play music/level1.ogg
music("boss", true)          -- loop boss music
```

### `stop([channel])`
Stop audio playback.

```lua
stop()       -- stop all audio
stop(0)      -- stop channel 0 only
stop(-1)     -- stop music only
```

### `volume(level, [channel])`
Set volume (0.0 to 1.0).

```lua
volume(0.5)      -- set master volume to 50%
volume(0.8, 0)   -- set channel 0 to 80%
```

---

## Map Functions

### `map(mx, my, dx, dy, mw, mh, [layer])`
Draw a region of the loaded map.

| Param   | Type   | Default      | Description                      |
|---------|--------|--------------|----------------------------------|
| `mx`    | int    | —            | Map X (in tiles)                 |
| `my`    | int    | —            | Map Y (in tiles)                 |
| `dx`    | num    | —            | Destination X (screen pixels)    |
| `dy`    | num    | —            | Destination Y (screen pixels)    |
| `mw`    | int    | —            | Width in tiles to draw           |
| `mh`    | int    | —            | Height in tiles to draw          |
| `layer` | string | "background" | Layer name from map JSON         |

```lua
map(0, 0, 0, 0, 30, 40)  -- draw 30x40 tile region at top-left
```

### `mget(mx, my, [layer])`
Get tile ID at map position.

```lua
local tile = mget(10, 5)
if tile == 1 then
  -- solid wall
end
```

### `mset(mx, my, tile_id, [layer])`
Set tile ID at map position (runtime only, not saved).

```lua
mset(10, 5, 0)  -- clear the tile
```

### `fget(tile_id, [flag])`
Get tile flags. Returns table of flags or bool if flag specified.

```lua
if fget(tile, "solid") then
  -- collision
end

local flags = fget(tile)  -- { "solid", "hazard" }
```

### `mapload(name)`
Load a map from `maps/` folder.

```lua
mapload("level2")  -- load maps/level2.json
```

---

## Save/Load Functions

### `save(slot, data)`
Save a Lua table to persistent storage.

| Param  | Type  | Description              |
|--------|-------|--------------------------|
| `slot` | int   | Slot index (0-3)         |
| `data` | table | Data to save             |

```lua
save(0, { score = 1000, level = 3, name = "Carson" })
```

### `load(slot)`
Load data from a slot. Returns `nil` if empty.

```lua
local data = load(0)
if data then
  score = data.score
  level = data.level
end
```

### `delete(slot)`
Delete save data from a slot.

```lua
delete(0)  -- clear slot 0
```

---

## Math Functions

All standard Lua math is available, plus these shortcuts:

| Function     | Description                           |
|--------------|---------------------------------------|
| `flr(n)`     | Floor (round down)                    |
| `ceil(n)`    | Ceiling (round up)                    |
| `abs(n)`     | Absolute value                        |
| `sgn(n)`     | Sign (-1, 0, or 1)                    |
| `min(a, b)`  | Minimum of two values                 |
| `max(a, b)`  | Maximum of two values                 |
| `mid(a,b,c)` | Middle value of three                 |
| `sin(n)`     | Sine (0-1 range, not 0-2π)            |
| `cos(n)`     | Cosine (0-1 range)                    |
| `atan2(x,y)` | Arctangent                            |
| `sqrt(n)`    | Square root                           |
| `rnd(max)`   | Random float from 0 to max            |
| `irnd(max)`  | Random integer from 0 to max-1        |
| `srand(seed)`| Seed the random number generator      |

```lua
local angle = rnd(1)           -- random angle 0-1
local x = cos(angle) * 100
local y = sin(angle) * 100
local damage = irnd(10) + 5    -- random 5-14
```

---

## System Functions

### `time()`
Seconds elapsed since game start (float).

```lua
local t = time()
local pulse = sin(t * 2)  -- pulsing effect
```

### `dt()`
Delta time since last frame (typically ~0.0167 at 60fps).

```lua
player.x = player.x + speed * dt() * 60
```

### `fps()`
Current frames per second.

```lua
print("FPS: " .. fps(), 10, 10, 7)
```

### `stat(id)`
Get system stats.

| ID | Returns                    |
|----|----------------------------|
| 0  | Memory usage (KB)          |
| 1  | CPU usage (% of frame)     |
| 2  | Current FPS                |
| 3  | Runtime version string     |

```lua
print("MEM: " .. stat(0) .. "KB", 10, 10, 7)
```

### `exit()`
Quit the game and return to launcher.

```lua
if btnp(8) and btnp(9) then  -- Start + Select
  exit()
end
```

### `trace(message)`
Print debug message to console (PC only).

```lua
trace("Player position: " .. player.x .. ", " .. player.y)
```

---

## Collision Helpers

### `overlap(x1,y1,w1,h1, x2,y2,w2,h2)`
Check if two rectangles overlap. Returns `true`/`false`.

```lua
if overlap(player.x, player.y, 16, 16, enemy.x, enemy.y, 16, 16) then
  player_hit()
end
```

### `distance(x1, y1, x2, y2)`
Get distance between two points.

```lua
local d = distance(player.x, player.y, goal.x, goal.y)
if d < 20 then
  level_complete()
end
```

---

## Example Game

```lua
-- Simple platformer character
local player = {}

function _init()
  player = {
    x = 240,
    y = 500,
    vx = 0,
    vy = 0,
    grounded = false
  }
end

function _update()
  -- horizontal movement
  if btn(2) then player.vx = -3 end
  if btn(3) then player.vx = 3 end
  if not btn(2) and not btn(3) then
    player.vx = player.vx * 0.8
  end
  
  -- jump
  if btnp(4) and player.grounded then
    player.vy = -8
    sfx("jump")
  end
  
  -- gravity
  player.vy = player.vy + 0.4
  
  -- apply velocity
  player.x = player.x + player.vx
  player.y = player.y + player.vy
  
  -- floor collision
  if player.y > 580 then
    player.y = 580
    player.vy = 0
    player.grounded = true
  else
    player.grounded = false
  end
  
  -- screen bounds
  player.x = mid(0, player.x, 464)
end

function _draw()
  cls(1)
  
  -- ground
  rectfill(0, 596, 480, 44, 3)
  
  -- player
  spr(1, player.x, player.y)
  
  -- instructions
  print("Arrow keys to move, Z to jump", 100, 20, 7)
end
```
