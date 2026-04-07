# ThermoConsole Pico Controller

I2C slave firmware for Raspberry Pi Pico that reads button inputs and responds to polls from the Pi Zero.

## Communication

- **Protocol**: I2C Slave
- **Address**: 0x42
- **Speed**: 400kHz (Fast Mode)
- **Data**: 2 bytes (button state bitmask)

## Wiring

### I2C Connection to Pi Zero

| Pico Pin | Pi Zero Pin | Function |
|----------|-------------|----------|
| GPIO 14  | GPIO 2      | SDA (I2C1) |
| GPIO 15  | GPIO 3      | SCL (I2C1) |
| GND      | GND         | Ground |
| VSYS     | 5V (Pin 2)  | Power (or use 3V3) |

**Note**: The Waveshare 2.8" DPI LCD uses many GPIO pins. GPIO 2 and 3 are used for I2C, which is shared with the LCD's touch controller. The touch uses a different I2C address (Goodix 0x14 or 0x5D), so they can coexist.

### Button Wiring

Each button connects between its GPIO pin and GND (active LOW with internal pull-up):

| GPIO | Button | Physical Pin |
|------|--------|--------------|
| 2    | Up     | 4            |
| 3    | Down   | 5            |
| 4    | Left   | 6            |
| 5    | Right  | 7            |
| 6    | A      | 9            |
| 7    | B      | 10           |
| 8    | X      | 11           |
| 9    | Y      | 12           |
| 10   | Start  | 14           |
| 11   | Select | 15           |

```
Button wiring (active LOW):

    GPIO Pin ────┬──── Button ──── GND
                 │
           (internal pull-up)
```

### Wiring Diagram

```
                    ┌─────────────────┐
                    │   Pico (Top)    │
              ┌─────┤ GP0         VBUS├───── (USB 5V)
              │     │ GP1         VSYS├───── 5V from Pi
  GND ────────┼─────┤ GND          GND├───── GND to Pi
  Up ─────────┼─────┤ GP2         3V3 │
  Down ───────┼─────┤ GP3       GP28  │
  Left ───────┼─────┤ GP4       GP27  │
  Right ──────┼─────┤ GP5       GP26  │
  A ──────────┼─────┤ GP6       RUN   │
  B ──────────┼─────┤ GP7       GP22  │
  X ──────────┼─────┤ GP8       GND   │
  Y ──────────┼─────┤ GP9       GP21  │
  Start ──────┼─────┤ GP10      GP20  │
  Select ─────┼─────┤ GP11      GP19  │
              │     │ GP12      GP18  │
              │     │ GP13      GP17  │
              │     │ GND       GP16  │
  SDA ────────┼─────┤ GP14      GP15  ├───── SCL
              │     └─────────────────┘
              │           │
              └───────────┼──────────────── To Pi GPIO 2 (SDA)
                          └──────────────── To Pi GPIO 3 (SCL)
```

## Protocol

When the Pi Zero reads from I2C address 0x42, the Pico responds with 2 bytes:

| Byte | Bits | Content |
|------|------|---------|
| 0    | 0-7  | Up, Down, Left, Right, A, B, X, Y |
| 1    | 0-1  | Start, Select |
| 1    | 2-7  | Reserved (0) |

Bit = 1 means button is pressed.

## Building

### Prerequisites

```bash
# Clone Pico SDK
git clone https://github.com/raspberrypi/pico-sdk.git
cd pico-sdk
git submodule update --init
export PICO_SDK_PATH=$(pwd)

# Install build tools (Ubuntu/Debian)
sudo apt install cmake gcc-arm-none-eabi libnewlib-arm-none-eabi build-essential
```

### Compile

```bash
cd pico-controller
mkdir build && cd build
cmake ..
make

# Output: thermo_controller.uf2
```

## Flashing

1. Hold BOOTSEL button on Pico
2. Connect Pico to computer via USB
3. Release BOOTSEL - Pico appears as USB drive
4. Copy `thermo_controller.uf2` to the drive
5. Pico reboots automatically with new firmware

## Testing

### Enable I2C on Pi Zero

```bash
sudo raspi-config
# Interface Options -> I2C -> Enable
sudo reboot
```

### Detect Controller

```bash
i2cdetect -y 1
# Should show 42 at address 0x42:
#      0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f
# 40: -- -- 42 -- -- -- -- -- -- -- -- -- -- -- -- --
```

### Python Test Script

```python
#!/usr/bin/env python3
import smbus2
import time

bus = smbus2.SMBus(1)
ADDRESS = 0x42

BUTTONS = ['Up', 'Down', 'Left', 'Right', 'A', 'B', 'X', 'Y', 'Start', 'Select']

print("Reading buttons from Pico at 0x42...")
print("Press Ctrl+C to exit")

while True:
    try:
        data = bus.read_i2c_block_data(ADDRESS, 0, 2)
        state = data[0] | (data[1] << 8)
        
        pressed = [BUTTONS[i] for i in range(10) if state & (1 << i)]
        if pressed:
            print(f"Pressed: {', '.join(pressed)}")
        
        time.sleep(0.016)  # ~60Hz
    except IOError:
        print("I2C error - check connection")
        time.sleep(1)
    except KeyboardInterrupt:
        print("\nExiting")
        break
```

Install smbus2: `pip install smbus2`

## LED Indicators

- **3 blinks on startup**: Firmware loaded successfully
- **Solid on**: Ready and running

## Troubleshooting

### Controller not detected on I2C

1. Check wiring (SDA to SDA, SCL to SCL)
2. Ensure I2C is enabled on Pi: `sudo raspi-config`
3. Check for proper ground connection
4. Try `i2cdetect -y 1`

### Buttons not responding

1. Check button wiring (GPIO to GND when pressed)
2. Verify buttons are on correct GPIO pins (GP2-GP11)
3. Connect Pico via USB and check serial debug output:
   ```bash
   screen /dev/ttyACM0 115200
   ```

### I2C conflicts with LCD touch

The Waveshare DPI LCD touch controller uses I2C address 0x14 or 0x5D (Goodix). The Pico controller uses 0x42, so they coexist on the same I2C bus without conflict.

## Customizing Pin Assignments

Edit the `BUTTON_PINS` array in `controller.c`:

```c
static const uint8_t BUTTON_PINS[NUM_BUTTONS] = {
    2,   /* Up     - GPIO 2 */
    3,   /* Down   - GPIO 3 */
    // ... change to match your PCB
};
```

To change the I2C address:

```c
#define I2C_ADDRESS     0x42   /* Change this */
```

Then rebuild and reflash.
