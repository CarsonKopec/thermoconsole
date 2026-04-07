# USB Game Loading

ThermoConsole supports drag-and-drop game loading via USB. When connected to a PC, the console appears as a removable drive where you can copy game files.

## How It Works

1. **Connect** the ThermoConsole to your PC via the Pi Zero's USB data port
2. **Toggle USB mode** by holding **Start + Select** for 3 seconds
3. A drive called **TCGAMES** appears on your PC
4. **Copy** your `.tcr` files or game folders to the drive
5. **Safely eject** the drive
6. ThermoConsole automatically detects new games and returns to game mode

## USB Mode Indicator

When USB mode is active:
- The screen shows "USB MODE" with connection instructions
- The LED blinks to indicate file transfer activity
- The game runtime is paused

## Toggling USB Mode

### Hardware Method (Recommended)
Hold **Start + Select** buttons simultaneously for 3 seconds.

### SSH Method
```bash
# Enable USB mode
tc-usb-on

# Disable USB mode (return to games)  
tc-usb-off

# Toggle
tc-usb
```

## Drive Contents

When you open the TCGAMES drive, you'll see:

```
TCGAMES/
├── README.txt       # Instructions
├── CURRENT.txt      # Current game name (edit to switch)
├── space_dodge/     # Example game folder
├── hello/           # Example game folder
└── my_game.tcr      # Your games go here!
```

## Supported Game Formats

### .tcr Files (Recommended)
ThermoConsole ROM archives - just copy and play:
```
TCGAMES/
└── awesome_game.tcr
```

### Game Folders
Unpackaged games with source:
```
TCGAMES/
└── my_game/
    ├── manifest.json
    ├── main.lua
    └── sprites.png
```

## Switching Games

### Method 1: Edit CURRENT.txt
Open `CURRENT.txt` on the USB drive and type the game name:
```
my_awesome_game
```

### Method 2: Console Command
Via SSH:
```bash
tc-switch my_awesome_game
```

### Method 3: In-Game Menu (if implemented)
Press Start + Select briefly to open the game switcher menu.

## Troubleshooting

### Drive doesn't appear on PC

1. Make sure you're using the **data USB port** on the Pi Zero (not the power-only port)
2. Use a data-capable USB cable (some cables are charge-only)
3. Check USB mode is enabled: `tc-usb-on`

### Games aren't detected

1. Ensure the `.tcr` file or game folder is in the root of the drive
2. Game folders must contain `main.lua` and `manifest.json`
3. Try running `tc-switch` to see detected games

### USB mode won't activate

1. Check the USB gadget service: `systemctl status thermoconsole-usb`
2. Verify dwc2 is loaded: `lsmod | grep dwc2`
3. Check logs: `journalctl -u thermoconsole-usb-monitor -f`

### Drive is read-only

The drive should be writable. If not:
1. Check the storage image isn't corrupted
2. Recreate it: `sudo /opt/thermoconsole/scripts/usb-gadget.sh setup`

## Technical Details

### How USB Gadget Mode Works

The Pi Zero's USB port can act as either a **host** (connecting peripherals) or a **device** (appearing as a peripheral to another computer). ThermoConsole uses **USB Gadget Mode** to make the Pi appear as a mass storage device.

### Storage Architecture

```
┌─────────────────────────────────────────┐
│            /opt/thermoconsole/          │
│              games.img (256MB)          │
│         (FAT32 filesystem image)        │
└─────────────────────────────────────────┘
                    │
        ┌───────────┴───────────┐
        │                       │
   USB Mode OFF            USB Mode ON
        │                       │
        ▼                       ▼
┌───────────────┐      ┌───────────────┐
│ Loop mounted  │      │ Exposed via   │
│ to /home/pi/  │      │ USB mass      │
│ games/        │      │ storage       │
└───────────────┘      └───────────────┘
```

### Files

| Path | Purpose |
|------|---------|
| `/opt/thermoconsole/games.img` | FAT32 storage image |
| `/home/pi/games/` | Mount point when in game mode |
| `/opt/thermoconsole/scripts/usb-gadget.sh` | USB mode manager |
| `/opt/thermoconsole/scripts/usb-mode-monitor.sh` | Button combo detector |

### Services

| Service | Purpose |
|---------|---------|
| `thermoconsole-usb.service` | Initial storage setup |
| `thermoconsole-usb-monitor.service` | Watches for Start+Select combo |

## Customization

### Change Storage Size

Edit `/opt/thermoconsole/scripts/usb-gadget.sh`:
```bash
STORAGE_SIZE_MB=512  # Default is 256MB
```

Then recreate the storage:
```bash
sudo rm /opt/thermoconsole/games.img
sudo /opt/thermoconsole/scripts/usb-gadget.sh setup
```

### Change Button Combo

Edit `/opt/thermoconsole/scripts/usb-mode-monitor.sh`:
```bash
BTN_START=8
BTN_SELECT=9
HOLD_TIME=3  # Seconds
```

### Disable USB Feature

```bash
sudo systemctl disable thermoconsole-usb.service
sudo systemctl disable thermoconsole-usb-monitor.service
```
