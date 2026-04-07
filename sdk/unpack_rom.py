#!/usr/bin/env python3
"""
ThermoConsole ROM Unpacker
Extracts a .tcr ROM file into a game folder for editing.

Usage:
    python unpack_rom.py <game.tcr> [output_folder]
    
Examples:
    python unpack_rom.py mygame.tcr
    python unpack_rom.py mygame.tcr games/mygame_edit
"""

import argparse
import json
import sys
import zipfile
from pathlib import Path

# ANSI colors
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

def unpack_rom(rom_path: Path, output_folder: Path = None, verbose: bool = True) -> bool:
    """
    Unpack a .tcr ROM file into a folder.
    Returns True on success, False on failure.
    """
    if verbose:
        print(f"\n{Colors.BOLD}═══════════════════════════════════════════════════════════════{Colors.RESET}")
        print(f"{Colors.BOLD}  ThermoConsole ROM Unpacker{Colors.RESET}")
        print(f"{Colors.BOLD}═══════════════════════════════════════════════════════════════{Colors.RESET}\n")
    
    if not rom_path.exists():
        log_error(f"ROM file not found: {rom_path}")
        return False
    
    if not rom_path.is_file():
        log_error(f"Not a file: {rom_path}")
        return False
    
    if output_folder is None:
        output_folder = rom_path.parent / rom_path.stem
    
    if output_folder.exists():
        if verbose:
            log_warn(f"Output folder already exists: {output_folder}")
            response = input(f"  Overwrite? [y/N]: ").strip().lower()
            if response != 'y':
                log_info("Aborted.")
                return False
    
    if verbose:
        log_info(f"Validating ROM...")
    
    if not zipfile.is_zipfile(rom_path):
        log_error(f"Not a valid ROM file (not a ZIP archive)")
        return False
    
    if verbose:
        log_info(f"Extracting to {output_folder}...")
    
    try:
        with zipfile.ZipFile(rom_path, 'r') as zf:
            if 'manifest.json' not in zf.namelist():
                log_error("Invalid ROM: missing manifest.json")
                return False
            
            output_folder.mkdir(parents=True, exist_ok=True)
            
            for member in zf.namelist():
                zf.extract(member, output_folder)
                if verbose:
                    print(f"  {Colors.BLUE}+{Colors.RESET} {member}")
            
            file_count = len(zf.namelist())
    except zipfile.BadZipFile:
        log_error("Corrupted ROM file")
        return False
    except Exception as e:
        log_error(f"Extraction failed: {e}")
        return False
    
    manifest_path = output_folder / 'manifest.json'
    try:
        with open(manifest_path, 'r', encoding='utf-8') as f:
            manifest = json.load(f)
        game_name = manifest.get('name', 'Unknown')
        game_version = manifest.get('version', '?')
        game_author = manifest.get('author', 'Unknown')
    except:
        game_name = "Unknown"
        game_version = "?"
        game_author = "Unknown"
    
    if verbose:
        print()
        log_success(f"ROM unpacked successfully!")
        print(f"\n  {Colors.BOLD}Game:{Colors.RESET}    {game_name} v{game_version}")
        print(f"  {Colors.BOLD}Author:{Colors.RESET}  {game_author}")
        print(f"  {Colors.BOLD}Output:{Colors.RESET}  {output_folder}")
        print(f"  {Colors.BOLD}Files:{Colors.RESET}   {file_count}")
        print()
    
    return True

def main():
    parser = argparse.ArgumentParser(
        description='Unpack a ThermoConsole .tcr ROM file for editing.',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog='''
Examples:
  %(prog)s mygame.tcr                  Unpack to ./mygame/
  %(prog)s mygame.tcr games/edit       Unpack to specific folder
        '''
    )
    
    parser.add_argument(
        'rom_file',
        type=Path,
        help='Path to the .tcr ROM file'
    )
    
    parser.add_argument(
        'output',
        type=Path,
        nargs='?',
        default=None,
        help='Output folder (default: <rom_name>/)'
    )
    
    parser.add_argument(
        '-q', '--quiet',
        action='store_true',
        help='Suppress output except errors'
    )
    
    args = parser.parse_args()
    
    success = unpack_rom(
        rom_path=args.rom_file.resolve(),
        output_folder=args.output.resolve() if args.output else None,
        verbose=not args.quiet
    )
    
    sys.exit(0 if success else 1)

if __name__ == '__main__':
    main()
