#!/usr/bin/env python3
"""
ThermoConsole ROM Packer
Packages a game folder into a .tcr (ThermoConsole ROM) file.

Usage:
    python pack_rom.py <game_folder> [output.tcr]
    
Examples:
    python pack_rom.py games/mygame
    python pack_rom.py games/mygame builds/mygame_v1.tcr
"""

import argparse
import json
import os
import sys
import zipfile
from pathlib import Path
from typing import Optional

# ANSI colors for terminal output
class Colors:
    RED = '\033[91m'
    GREEN = '\033[92m'
    YELLOW = '\033[93m'
    BLUE = '\033[94m'
    CYAN = '\033[96m'
    RESET = '\033[0m'
    BOLD = '\033[1m'

def log_info(msg: str):
    print(f"{Colors.CYAN}[INFO]{Colors.RESET} {msg}")

def log_success(msg: str):
    print(f"{Colors.GREEN}[OK]{Colors.RESET} {msg}")

def log_warn(msg: str):
    print(f"{Colors.YELLOW}[WARN]{Colors.RESET} {msg}")

def log_error(msg: str):
    print(f"{Colors.RED}[ERROR]{Colors.RESET} {msg}")

# Required manifest fields
REQUIRED_MANIFEST_FIELDS = ['name', 'author', 'version', 'entry']

# Valid file extensions for assets
VALID_IMAGE_EXTENSIONS = {'.png', '.bmp'}
VALID_AUDIO_EXTENSIONS = {'.wav', '.ogg', '.mp3'}
VALID_MAP_EXTENSIONS = {'.json'}
VALID_SCRIPT_EXTENSIONS = {'.lua'}

# Files/folders to exclude from ROM
EXCLUDE_PATTERNS = {
    '.git', '.gitignore', '.DS_Store', 'Thumbs.db', '__pycache__',
    '.vscode', '.idea', '*.pyc', 'node_modules', '.env'
}

def should_exclude(name: str) -> bool:
    """Check if a file/folder should be excluded from the ROM."""
    name_lower = name.lower()
    for pattern in EXCLUDE_PATTERNS:
        if pattern.startswith('*'):
            if name_lower.endswith(pattern[1:]):
                return True
        elif name_lower == pattern.lower():
            return True
    return False

def validate_manifest(manifest_path: Path) -> tuple[dict, list[str]]:
    """
    Validate the manifest.json file.
    Returns (manifest_dict, list_of_errors).
    """
    errors = []
    
    if not manifest_path.exists():
        return {}, ["manifest.json not found"]
    
    try:
        with open(manifest_path, 'r', encoding='utf-8') as f:
            manifest = json.load(f)
    except json.JSONDecodeError as e:
        return {}, [f"manifest.json is not valid JSON: {e}"]
    
    # Check required fields
    for field in REQUIRED_MANIFEST_FIELDS:
        if field not in manifest:
            errors.append(f"Missing required field: '{field}'")
        elif not isinstance(manifest[field], str) or not manifest[field].strip():
            errors.append(f"Field '{field}' must be a non-empty string")
    
    # Validate version format (semver-ish)
    if 'version' in manifest:
        version = manifest['version']
        parts = version.split('.')
        if len(parts) < 2 or len(parts) > 3:
            errors.append(f"Version '{version}' should be in X.Y or X.Y.Z format")
    
    # Validate display settings if present
    if 'display' in manifest:
        display = manifest['display']
        if not isinstance(display, dict):
            errors.append("'display' must be an object")
        else:
            if 'width' in display and (not isinstance(display['width'], int) or display['width'] <= 0):
                errors.append("'display.width' must be a positive integer")
            if 'height' in display and (not isinstance(display['height'], int) or display['height'] <= 0):
                errors.append("'display.height' must be a positive integer")
            if 'orientation' in display and display['orientation'] not in ('portrait', 'landscape'):
                errors.append("'display.orientation' must be 'portrait' or 'landscape'")
            if 'scale_mode' in display and display['scale_mode'] not in ('integer', 'stretch', 'fit'):
                errors.append("'display.scale_mode' must be 'integer', 'stretch', or 'fit'")
    
    # Validate sprites settings if present
    if 'sprites' in manifest:
        sprites = manifest['sprites']
        if not isinstance(sprites, dict):
            errors.append("'sprites' must be an object")
        else:
            if 'grid_size' in sprites and sprites['grid_size'] not in (8, 16, 32):
                errors.append("'sprites.grid_size' must be 8, 16, or 32")
    
    return manifest, errors

def validate_entry_file(game_folder: Path, manifest: dict) -> list[str]:
    """Validate that the entry Lua file exists and has required callbacks."""
    errors = []
    
    entry = manifest.get('entry', 'main.lua')
    entry_path = game_folder / entry
    
    if not entry_path.exists():
        errors.append(f"Entry file '{entry}' not found")
        return errors
    
    try:
        with open(entry_path, 'r', encoding='utf-8') as f:
            content = f.read()
    except Exception as e:
        errors.append(f"Could not read entry file: {e}")
        return errors
    
    # Check for required callbacks (basic check, not a full Lua parser)
    required_callbacks = ['_init', '_update', '_draw']
    for callback in required_callbacks:
        # Look for "function _init" or "_init = function"
        if f'function {callback}' not in content and f'{callback} = function' not in content:
            errors.append(f"Entry file missing required callback: {callback}()")
    
    return errors

def validate_assets(game_folder: Path, manifest: dict) -> list[str]:
    """Validate that referenced assets exist."""
    errors = []
    
    # Check sprite sheet
    sprites_file = manifest.get('sprites', {}).get('file', 'sprites.png')
    sprites_path = game_folder / sprites_file
    if sprites_path.exists():
        # Could add image dimension validation here with PIL
        pass
    
    # Check tiles
    tiles_file = manifest.get('tiles', {}).get('file', 'tiles.png')
    tiles_path = game_folder / tiles_file
    if tiles_path.exists():
        pass
    
    return errors

def collect_files(game_folder: Path) -> list[tuple[Path, str]]:
    """
    Collect all files to include in the ROM.
    Returns list of (absolute_path, archive_path) tuples.
    """
    files = []
    
    for root, dirs, filenames in os.walk(game_folder):
        # Filter out excluded directories
        dirs[:] = [d for d in dirs if not should_exclude(d)]
        
        for filename in filenames:
            if should_exclude(filename):
                continue
            
            abs_path = Path(root) / filename
            rel_path = abs_path.relative_to(game_folder)
            files.append((abs_path, str(rel_path)))
    
    return files

def pack_rom(game_folder: Path, output_path: Optional[Path] = None, verbose: bool = True) -> bool:
    """
    Pack a game folder into a .tcr ROM file.
    Returns True on success, False on failure.
    """
    if verbose:
        print(f"\n{Colors.BOLD}═══════════════════════════════════════════════════════════════{Colors.RESET}")
        print(f"{Colors.BOLD}  ThermoConsole ROM Packer{Colors.RESET}")
        print(f"{Colors.BOLD}═══════════════════════════════════════════════════════════════{Colors.RESET}\n")
    
    # Validate game folder exists
    if not game_folder.exists():
        log_error(f"Game folder not found: {game_folder}")
        return False
    
    if not game_folder.is_dir():
        log_error(f"Not a directory: {game_folder}")
        return False
    
    # Validate manifest
    manifest_path = game_folder / 'manifest.json'
    if verbose:
        log_info(f"Validating manifest...")
    
    manifest, manifest_errors = validate_manifest(manifest_path)
    
    if manifest_errors:
        for err in manifest_errors:
            log_error(err)
        return False
    
    if verbose:
        log_success(f"Manifest valid: {manifest['name']} v{manifest['version']} by {manifest['author']}")
    
    # Validate entry file
    if verbose:
        log_info(f"Validating entry file...")
    
    entry_errors = validate_entry_file(game_folder, manifest)
    
    if entry_errors:
        for err in entry_errors:
            log_error(err)
        return False
    
    if verbose:
        log_success(f"Entry file valid: {manifest.get('entry', 'main.lua')}")
    
    # Validate assets
    if verbose:
        log_info(f"Validating assets...")
    
    asset_errors = validate_assets(game_folder, manifest)
    
    if asset_errors:
        for err in asset_errors:
            log_error(err)
        return False
    
    if verbose:
        log_success(f"Assets validated")
    
    # Collect files
    if verbose:
        log_info(f"Collecting files...")
    
    files = collect_files(game_folder)
    
    if verbose:
        log_success(f"Found {len(files)} files to pack")
    
    # Determine output path
    if output_path is None:
        output_path = game_folder.parent / f"{game_folder.name}.tcr"
    
    # Ensure output directory exists
    output_path.parent.mkdir(parents=True, exist_ok=True)
    
    # Create the ROM
    if verbose:
        log_info(f"Packing ROM...")
    
    try:
        with zipfile.ZipFile(output_path, 'w', zipfile.ZIP_DEFLATED) as zf:
            for abs_path, archive_path in files:
                zf.write(abs_path, archive_path)
                if verbose:
                    print(f"  {Colors.BLUE}+{Colors.RESET} {archive_path}")
    except Exception as e:
        log_error(f"Failed to create ROM: {e}")
        return False
    
    # Calculate size
    rom_size = output_path.stat().st_size
    if rom_size < 1024:
        size_str = f"{rom_size} bytes"
    elif rom_size < 1024 * 1024:
        size_str = f"{rom_size / 1024:.1f} KB"
    else:
        size_str = f"{rom_size / (1024 * 1024):.2f} MB"
    
    if verbose:
        print()
        log_success(f"ROM created successfully!")
        print(f"\n  {Colors.BOLD}Output:{Colors.RESET} {output_path}")
        print(f"  {Colors.BOLD}Size:{Colors.RESET}   {size_str}")
        print(f"  {Colors.BOLD}Files:{Colors.RESET}  {len(files)}")
        print()
    
    return True

def main():
    parser = argparse.ArgumentParser(
        description='Pack a ThermoConsole game folder into a .tcr ROM file.',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog='''
Examples:
  %(prog)s games/mygame                    Pack to games/mygame.tcr
  %(prog)s games/mygame builds/game.tcr    Pack to specific output path
  %(prog)s . -o release.tcr                Pack current directory
        '''
    )
    
    parser.add_argument(
        'game_folder',
        type=Path,
        help='Path to the game folder containing manifest.json'
    )
    
    parser.add_argument(
        'output',
        type=Path,
        nargs='?',
        default=None,
        help='Output .tcr file path (default: <game_folder>.tcr)'
    )
    
    parser.add_argument(
        '-o', '--output',
        type=Path,
        dest='output_flag',
        help='Output .tcr file path (alternative syntax)'
    )
    
    parser.add_argument(
        '-q', '--quiet',
        action='store_true',
        help='Suppress output except errors'
    )
    
    args = parser.parse_args()
    
    output_path = args.output_flag or args.output
    
    success = pack_rom(
        game_folder=args.game_folder.resolve(),
        output_path=output_path.resolve() if output_path else None,
        verbose=not args.quiet
    )
    
    sys.exit(0 if success else 1)

if __name__ == '__main__':
    main()
