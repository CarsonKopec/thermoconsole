#!/usr/bin/env python3
"""
ThermoConsole Launcher
Quick launcher for testing games during development.

Usage:
    python launch.py                    # Launch game selector
    python launch.py games/mygame       # Launch specific game folder
    python launch.py games/mygame.tcr   # Launch specific ROM
"""

import os
import sys
import subprocess
from pathlib import Path

# ANSI colors
class C:
    CYAN = '\033[96m'
    GREEN = '\033[92m'
    YELLOW = '\033[93m'
    RED = '\033[91m'
    BOLD = '\033[1m'
    END = '\033[0m'

def find_runtime():
    """Find the thermoconsole runtime executable."""
    script_dir = Path(__file__).parent
    
    # Check common locations
    candidates = [
        script_dir / 'runtime' / 'build' / 'thermoconsole',
        script_dir / 'runtime' / 'thermoconsole',
        script_dir / 'build' / 'thermoconsole',
        Path('thermoconsole'),
    ]
    
    # Windows variants
    if sys.platform == 'win32':
        candidates = [p.with_suffix('.exe') for p in candidates] + candidates
    
    for path in candidates:
        if path.exists() and path.is_file():
            return path
    
    return None

def find_games():
    """Find all available games."""
    script_dir = Path(__file__).parent
    games_dir = script_dir / 'games'
    
    games = []
    
    if games_dir.exists():
        # Find game folders (with manifest.json)
        for item in games_dir.iterdir():
            if item.is_dir():
                manifest = item / 'manifest.json'
                if manifest.exists():
                    games.append(('folder', item))
            elif item.suffix == '.tcr':
                games.append(('rom', item))
    
    return sorted(games, key=lambda x: x[1].name.lower())

def show_menu(games):
    """Show interactive game selection menu."""
    print(f"\n{C.BOLD}╔══════════════════════════════════════════════════════════╗{C.END}")
    print(f"{C.BOLD}║             ThermoConsole Game Launcher                  ║{C.END}")
    print(f"{C.BOLD}╚══════════════════════════════════════════════════════════╝{C.END}\n")
    
    if not games:
        print(f"{C.YELLOW}No games found in games/ directory.{C.END}")
        print(f"Create one with: {C.CYAN}python sdk/new_game.py mygame{C.END}\n")
        return None
    
    print(f"{C.BOLD}Available Games:{C.END}\n")
    
    for i, (game_type, path) in enumerate(games, 1):
        icon = '📦' if game_type == 'rom' else '📁'
        name = path.stem if game_type == 'rom' else path.name
        type_label = f"{C.CYAN}[ROM]{C.END}" if game_type == 'rom' else f"{C.GREEN}[DEV]{C.END}"
        print(f"  {C.BOLD}{i:2}.{C.END} {icon} {name:30} {type_label}")
    
    print(f"\n  {C.BOLD} 0.{C.END} Exit")
    print()
    
    while True:
        try:
            choice = input(f"{C.BOLD}Select game (1-{len(games)}): {C.END}").strip()
            
            if choice == '0' or choice.lower() == 'q':
                return None
            
            idx = int(choice) - 1
            if 0 <= idx < len(games):
                return games[idx][1]
            
            print(f"{C.RED}Invalid choice. Try again.{C.END}")
        except ValueError:
            print(f"{C.RED}Enter a number.{C.END}")
        except KeyboardInterrupt:
            print()
            return None

def launch_game(runtime_path, game_path):
    """Launch a game with the runtime."""
    print(f"\n{C.CYAN}Launching:{C.END} {game_path.name}")
    print(f"{C.CYAN}Runtime:{C.END} {runtime_path}")
    print()
    print(f"{C.YELLOW}Controls: Arrow keys/WASD to move, Z/X for A/B, ESC to quit{C.END}")
    print(f"{C.BOLD}{'─' * 60}{C.END}\n")
    
    try:
        result = subprocess.run(
            [str(runtime_path), str(game_path)],
            cwd=runtime_path.parent
        )
        return result.returncode
    except KeyboardInterrupt:
        print(f"\n{C.YELLOW}Interrupted.{C.END}")
        return 0

def main():
    script_dir = Path(__file__).parent
    os.chdir(script_dir)
    
    # Find runtime
    runtime = find_runtime()
    if not runtime:
        print(f"{C.RED}ERROR:{C.END} Runtime not found!")
        print(f"\nBuild it first:")
        print(f"  {C.CYAN}cd runtime && mkdir build && cd build && cmake .. && make{C.END}")
        sys.exit(1)
    
    # Check command line arguments
    if len(sys.argv) > 1:
        game_path = Path(sys.argv[1])
        
        # Handle relative paths
        if not game_path.is_absolute():
            game_path = script_dir / game_path
        
        if not game_path.exists():
            print(f"{C.RED}ERROR:{C.END} Game not found: {game_path}")
            sys.exit(1)
        
        sys.exit(launch_game(runtime, game_path))
    
    # Interactive mode
    games = find_games()
    selected = show_menu(games)
    
    if selected:
        sys.exit(launch_game(runtime, selected))

if __name__ == '__main__':
    main()
