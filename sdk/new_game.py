#!/usr/bin/env python3
"""
ThermoConsole New Game Generator
Creates a new game project with all the boilerplate set up.

Usage:
    python new_game.py <game_name> [--template TYPE]
    
Examples:
    python new_game.py mygame
    python new_game.py spaceshooter --template shooter
    python new_game.py rpg_quest --template rpg --author "Carson"
"""

import argparse
import json
import sys
from pathlib import Path
from datetime import datetime

# ANSI colors
class Colors:
    RED = '\033[91m'
    GREEN = '\033[92m'
    YELLOW = '\033[93m'
    BLUE = '\033[94m'
    CYAN = '\033[96m'
    MAGENTA = '\033[95m'
    RESET = '\033[0m'
    BOLD = '\033[1m'

def log_info(msg: str):
    print(f"{Colors.CYAN}[INFO]{Colors.RESET} {msg}")

def log_success(msg: str):
    print(f"{Colors.GREEN}[OK]{Colors.RESET} {msg}")

def log_error(msg: str):
    print(f"{Colors.RED}[ERROR]{Colors.RESET} {msg}")

# ─────────────────────────────────────────────────────────────────────────────
# TEMPLATES
# ─────────────────────────────────────────────────────────────────────────────

TEMPLATE_MINIMAL = '''-- {name}
-- A ThermoConsole game

function _init()
    -- Called once at game start
    x = 240
    y = 320
end

function _update()
    -- Called every frame (60fps) - game logic here
    if btn(0) then y = y - 2 end  -- up
    if btn(1) then y = y + 2 end  -- down
    if btn(2) then x = x - 2 end  -- left
    if btn(3) then x = x + 2 end  -- right
end

function _draw()
    -- Called every frame after update - rendering here
    cls(0)
    print("{name}", 180, 20, 7)
    print("Use arrow keys to move", 140, 600, 6)
    rectfill(x - 8, y - 8, 16, 16, 11)
end
'''

TEMPLATE_PLATFORMER = '''-- {name}
-- A ThermoConsole platformer

local player = {{}}
local gravity = 0.5
local jump_force = -10

function _init()
    player = {{
        x = 240,
        y = 500,
        vx = 0,
        vy = 0,
        w = 16,
        h = 24,
        grounded = false,
        facing = 1  -- 1 = right, -1 = left
    }}
end

function _update()
    -- Horizontal movement
    player.vx = 0
    if btn(2) then
        player.vx = -4
        player.facing = -1
    end
    if btn(3) then
        player.vx = 4
        player.facing = 1
    end
    
    -- Jump
    if btnp(4) and player.grounded then
        player.vy = jump_force
        player.grounded = false
        -- sfx("jump")
    end
    
    -- Apply gravity
    player.vy = player.vy + gravity
    if player.vy > 12 then
        player.vy = 12  -- terminal velocity
    end
    
    -- Apply velocity
    player.x = player.x + player.vx
    player.y = player.y + player.vy
    
    -- Floor collision (simple)
    local floor_y = 580
    if player.y + player.h > floor_y then
        player.y = floor_y - player.h
        player.vy = 0
        player.grounded = true
    end
    
    -- Screen bounds
    if player.x < 0 then player.x = 0 end
    if player.x > 480 - player.w then player.x = 480 - player.w end
end

function _draw()
    cls(1)  -- dark blue background
    
    -- Draw ground
    rectfill(0, 580, 480, 60, 3)
    
    -- Draw player (rectangle for now, replace with sprite)
    local px = player.x
    local py = player.y
    rectfill(px, py, player.w, player.h, 11)
    
    -- Eyes (show facing direction)
    local eye_x = px + (player.facing == 1 and 10 or 2)
    rectfill(eye_x, py + 4, 4, 4, 7)
    
    -- UI
    print("{name}", 10, 10, 7)
    print("Arrows: Move   Z: Jump", 10, 620, 6)
end
'''

TEMPLATE_SHOOTER = '''-- {name}
-- A ThermoConsole shooter

local player = {{}}
local bullets = {{}}
local enemies = {{}}
local score = 0
local spawn_timer = 0

function _init()
    player = {{
        x = 240,
        y = 550,
        w = 24,
        h = 24,
        speed = 5
    }}
    bullets = {{}}
    enemies = {{}}
    score = 0
end

function _update()
    -- Player movement
    if btn(2) then player.x = player.x - player.speed end
    if btn(3) then player.x = player.x + player.speed end
    
    -- Clamp to screen
    player.x = mid(0, player.x, 480 - player.w)
    
    -- Shooting
    if btnp(4) then
        table.insert(bullets, {{
            x = player.x + player.w / 2 - 2,
            y = player.y,
            vy = -10
        }})
        -- sfx("shoot")
    end
    
    -- Update bullets
    for i = #bullets, 1, -1 do
        local b = bullets[i]
        b.y = b.y + b.vy
        if b.y < -10 then
            table.remove(bullets, i)
        end
    end
    
    -- Spawn enemies
    spawn_timer = spawn_timer + 1
    if spawn_timer > 60 then
        spawn_timer = 0
        table.insert(enemies, {{
            x = rnd(440),
            y = -20,
            w = 20,
            h = 20,
            vy = 2 + rnd(2)
        }})
    end
    
    -- Update enemies
    for i = #enemies, 1, -1 do
        local e = enemies[i]
        e.y = e.y + e.vy
        
        -- Remove if off screen
        if e.y > 660 then
            table.remove(enemies, i)
        end
    end
    
    -- Collision: bullets vs enemies
    for bi = #bullets, 1, -1 do
        local b = bullets[bi]
        for ei = #enemies, 1, -1 do
            local e = enemies[ei]
            if overlap(b.x, b.y, 4, 8, e.x, e.y, e.w, e.h) then
                table.remove(bullets, bi)
                table.remove(enemies, ei)
                score = score + 100
                -- sfx("explode")
                break
            end
        end
    end
    
    -- Collision: player vs enemies
    for i = #enemies, 1, -1 do
        local e = enemies[i]
        if overlap(player.x, player.y, player.w, player.h, e.x, e.y, e.w, e.h) then
            -- Game over! For now just reset
            _init()
            return
        end
    end
end

function _draw()
    cls(0)
    
    -- Stars background
    for i = 1, 50 do
        local sx = (i * 73) % 480
        local sy = ((i * 97) + time() * 50) % 640
        pset(sx, sy, 6)
    end
    
    -- Draw player
    rectfill(player.x, player.y, player.w, player.h, 12)
    -- Cockpit
    rectfill(player.x + 8, player.y + 4, 8, 8, 7)
    
    -- Draw bullets
    for _, b in ipairs(bullets) do
        rectfill(b.x, b.y, 4, 8, 10)
    end
    
    -- Draw enemies
    for _, e in ipairs(enemies) do
        rectfill(e.x, e.y, e.w, e.h, 8)
    end
    
    -- UI
    print("SCORE: " .. score, 10, 10, 7)
    print("Arrows: Move   Z: Shoot", 10, 620, 6)
end
'''

TEMPLATE_RPG = '''-- {name}
-- A ThermoConsole RPG

local player = {{}}
local npcs = {{}}
local dialog = nil
local game_state = "explore"  -- "explore" or "dialog"

function _init()
    player = {{
        x = 240,
        y = 320,
        speed = 3,
        dir = "down",  -- for sprite facing
        hp = 100,
        max_hp = 100,
        gold = 50
    }}
    
    -- Create some NPCs
    npcs = {{
        {{
            x = 350,
            y = 200,
            name = "Elder",
            dialog = {{
                "Welcome, traveler!",
                "Our village has been peaceful...",
                "But lately, strange things happen at night."
            }}
        }},
        {{
            x = 150,
            y = 450,
            name = "Merchant",
            dialog = {{
                "Care to see my wares?",
                "I have the finest potions in the land!"
            }}
        }}
    }}
end

function _update()
    if game_state == "explore" then
        update_explore()
    elseif game_state == "dialog" then
        update_dialog()
    end
end

function update_explore()
    local dx, dy = 0, 0
    
    if btn(0) then dy = -1; player.dir = "up" end
    if btn(1) then dy = 1; player.dir = "down" end
    if btn(2) then dx = -1; player.dir = "left" end
    if btn(3) then dx = 1; player.dir = "right" end
    
    player.x = player.x + dx * player.speed
    player.y = player.y + dy * player.speed
    
    -- Screen bounds
    player.x = mid(16, player.x, 464)
    player.y = mid(16, player.y, 600)
    
    -- Interact with NPCs
    if btnp(4) then
        for _, npc in ipairs(npcs) do
            if distance(player.x, player.y, npc.x, npc.y) < 40 then
                dialog = {{
                    npc = npc,
                    line = 1
                }}
                game_state = "dialog"
                break
            end
        end
    end
end

function update_dialog()
    if btnp(4) or btnp(5) then
        dialog.line = dialog.line + 1
        if dialog.line > #dialog.npc.dialog then
            dialog = nil
            game_state = "explore"
        end
    end
end

function _draw()
    cls(3)  -- green grass
    
    -- Draw some decoration
    for i = 1, 20 do
        local tx = (i * 123) % 480
        local ty = (i * 67) % 640
        rectfill(tx, ty, 8, 8, 2)  -- trees/bushes
    end
    
    -- Draw NPCs
    for _, npc in ipairs(npcs) do
        rectfill(npc.x - 8, npc.y - 12, 16, 24, 4)  -- body
        circfill(npc.x, npc.y - 16, 8, 15)  -- head
    end
    
    -- Draw player
    local px, py = player.x, player.y
    rectfill(px - 8, py - 12, 16, 24, 12)  -- body
    circfill(px, py - 16, 8, 15)  -- head
    
    -- UI - Health bar
    rectfill(10, 10, 102, 14, 5)
    rectfill(11, 11, player.hp, 12, 8)
    print("HP", 14, 12, 7)
    
    -- Gold
    print("Gold: " .. player.gold, 380, 12, 10)
    
    -- Dialog box
    if dialog then
        -- Box background
        rectfill(20, 480, 440, 140, 1)
        rect(20, 480, 440, 140, 7)
        
        -- NPC name
        print(dialog.npc.name, 35, 495, 10)
        
        -- Dialog text
        local text = dialog.npc.dialog[dialog.line]
        print(text, 35, 530, 7)
        
        -- Continue prompt
        print("[Z] Continue", 350, 590, 6)
    else
        print("Arrows: Move   Z: Talk", 10, 620, 7)
    end
end
'''

TEMPLATES = {
    'minimal': TEMPLATE_MINIMAL,
    'platformer': TEMPLATE_PLATFORMER,
    'shooter': TEMPLATE_SHOOTER,
    'rpg': TEMPLATE_RPG,
}

def create_game(
    name: str,
    output_dir: Path,
    template: str = 'minimal',
    author: str = 'Unknown',
    verbose: bool = True
) -> bool:
    """Create a new game project."""
    
    if verbose:
        print(f"\n{Colors.BOLD}═══════════════════════════════════════════════════════════════{Colors.RESET}")
        print(f"{Colors.BOLD}  ThermoConsole New Game{Colors.RESET}")
        print(f"{Colors.BOLD}═══════════════════════════════════════════════════════════════{Colors.RESET}\n")
    
    # Validate template
    if template not in TEMPLATES:
        log_error(f"Unknown template: {template}")
        print(f"  Available: {', '.join(TEMPLATES.keys())}")
        return False
    
    # Create game folder
    game_folder = output_dir / name
    
    if game_folder.exists():
        log_error(f"Folder already exists: {game_folder}")
        return False
    
    if verbose:
        log_info(f"Creating game: {name}")
        log_info(f"Template: {template}")
    
    try:
        # Create directories
        game_folder.mkdir(parents=True)
        (game_folder / 'sfx').mkdir()
        (game_folder / 'music').mkdir()
        (game_folder / 'maps').mkdir()
        (game_folder / 'scripts').mkdir()
        
        # Create manifest
        manifest = {
            "name": name.replace('_', ' ').title(),
            "author": author,
            "version": "0.1.0",
            "description": f"A ThermoConsole game created from {template} template",
            "entry": "main.lua",
            "display": {
                "width": 480,
                "height": 640,
                "orientation": "portrait"
            },
            "sprites": {
                "file": "sprites.png",
                "grid_size": 16
            }
        }
        
        manifest_path = game_folder / 'manifest.json'
        with open(manifest_path, 'w', encoding='utf-8') as f:
            json.dump(manifest, f, indent=2)
        
        if verbose:
            print(f"  {Colors.BLUE}+{Colors.RESET} manifest.json")
        
        # Create main.lua from template
        main_lua = TEMPLATES[template].format(name=name.replace('_', ' ').title())
        main_path = game_folder / 'main.lua'
        with open(main_path, 'w', encoding='utf-8') as f:
            f.write(main_lua)
        
        if verbose:
            print(f"  {Colors.BLUE}+{Colors.RESET} main.lua")
        
        # Create placeholder sprite sheet (1x1 transparent PNG header)
        # In a real scenario, you'd include an actual starter sprite sheet
        readme_path = game_folder / 'README.md'
        with open(readme_path, 'w', encoding='utf-8') as f:
            f.write(f'''# {name.replace('_', ' ').title()}

A game for ThermoConsole.

## Development

1. Edit `main.lua` to add your game logic
2. Add sprites to `sprites.png` (256x256, 16x16 grid)
3. Add sound effects to `sfx/` folder (.wav or .ogg)
4. Add music to `music/` folder (.ogg recommended)

## Building

```bash
python sdk/pack_rom.py games/{name}
```

## Controls

| Button | PC Key        | Action |
|--------|---------------|--------|
| D-Pad  | Arrow keys    | Move   |
| A      | Z             | Action |
| B      | X             | Back   |
| Start  | Enter         | Pause  |

## Created

{datetime.now().strftime('%Y-%m-%d')}
''')
        
        if verbose:
            print(f"  {Colors.BLUE}+{Colors.RESET} README.md")
            print(f"  {Colors.BLUE}+{Colors.RESET} sfx/")
            print(f"  {Colors.BLUE}+{Colors.RESET} music/")
            print(f"  {Colors.BLUE}+{Colors.RESET} maps/")
            print(f"  {Colors.BLUE}+{Colors.RESET} scripts/")
        
    except Exception as e:
        log_error(f"Failed to create game: {e}")
        return False
    
    if verbose:
        print()
        log_success("Game created successfully!")
        print(f"\n  {Colors.BOLD}Location:{Colors.RESET} {game_folder}")
        print(f"\n  {Colors.MAGENTA}Next steps:{Colors.RESET}")
        print(f"    1. Add your sprites.png (256x256, 16x16 grid)")
        print(f"    2. Edit main.lua to build your game")
        print(f"    3. Run: python sdk/pack_rom.py games/{name}")
        print()
    
    return True

def main():
    parser = argparse.ArgumentParser(
        description='Create a new ThermoConsole game project.',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=f'''
Templates:
  minimal     - Bare bones starter (default)
  platformer  - Side-scrolling platformer with physics
  shooter     - Top-down space shooter
  rpg         - Simple RPG with NPCs and dialog

Examples:
  %(prog)s mygame
  %(prog)s spaceshooter --template shooter
  %(prog)s my_rpg -t rpg --author "Carson"
        '''
    )
    
    parser.add_argument(
        'name',
        help='Name of the game (used for folder and title)'
    )
    
    parser.add_argument(
        '-t', '--template',
        default='minimal',
        choices=list(TEMPLATES.keys()),
        help='Game template to use (default: minimal)'
    )
    
    parser.add_argument(
        '-a', '--author',
        default='Unknown',
        help='Author name for manifest'
    )
    
    parser.add_argument(
        '-o', '--output',
        type=Path,
        default=Path('games'),
        help='Output directory (default: ./games/)'
    )
    
    parser.add_argument(
        '-q', '--quiet',
        action='store_true',
        help='Suppress output except errors'
    )
    
    args = parser.parse_args()
    
    # Sanitize game name
    name = args.name.lower().replace(' ', '_').replace('-', '_')
    name = ''.join(c for c in name if c.isalnum() or c == '_')
    
    if not name:
        log_error("Invalid game name")
        sys.exit(1)
    
    success = create_game(
        name=name,
        output_dir=args.output.resolve(),
        template=args.template,
        author=args.author,
        verbose=not args.quiet
    )
    
    sys.exit(0 if success else 1)

if __name__ == '__main__':
    main()
