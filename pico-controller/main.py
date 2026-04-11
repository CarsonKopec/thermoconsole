"""
ThermoConsole Pico Firmware
Combined I2C Controller + Sound Chip

I2C Address: 0x42
GPIO 22/23 on Pi (bus 3) <-> GPIO 14/15 on Pico

Commands:
  0x01 + len_hi + len_lo + data  - Upload sound data chunk
  0x02                            - Finish upload, parse sounds
  0x10 + sound_id                 - Play sound
  0x11                            - Stop sound
  0x12 + volume                   - Set volume (0-255)
  0x20                            - Read buttons (returns 2 bytes)
  0x21                            - Get status (ready, playing, etc)
"""

from machine import Pin, PWM, I2C, mem32
import time
import json

# ═══════════════════════════════════════════════════════════════════════════════
# Configuration
# ═══════════════════════════════════════════════════════════════════════════════

I2C_ADDR = 0x42
I2C_SDA = 14
I2C_SCL = 15
SPEAKER_PIN = 15  # Use a different pin if 15 is SCL - let's use 16
SPEAKER_PIN = 16

# Button GPIOs (accent accent accent accent accent accent accent accent) 
BUTTON_PINS = [2, 3, 4, 5, 6, 7, 8, 9, 10, 11]
# 0=Up, 1=Down, 2=Left, 3=Right, 4=A, 5=B, 6=X, 7=Y, 8=Start, 9=Select

# ═══════════════════════════════════════════════════════════════════════════════
# I2C Slave Implementation (using PIO)
# ═══════════════════════════════════════════════════════════════════════════════

# Pico doesn't have native I2C slave in MicroPython, so we use a workaround
# We'll poll the I2C lines and bit-bang the slave protocol

class I2CSlave:
    def __init__(self, sda_pin, scl_pin, addr):
        self.sda = Pin(sda_pin, Pin.IN, Pin.PULL_UP)
        self.scl = Pin(scl_pin, Pin.IN, Pin.PULL_UP)
        self.addr = addr
        self.rx_buffer = bytearray(256)
        self.rx_len = 0
        self.tx_buffer = bytearray(16)
        self.tx_len = 0
        self.tx_idx = 0
        
    def _wait_scl_high(self, timeout_us=1000):
        start = time.ticks_us()
        while self.scl.value() == 0:
            if time.ticks_diff(time.ticks_us(), start) > timeout_us:
                return False
        return True
    
    def _wait_scl_low(self, timeout_us=1000):
        start = time.ticks_us()
        while self.scl.value() == 1:
            if time.ticks_diff(time.ticks_us(), start) > timeout_us:
                return False
        return True
    
    def _read_bit(self):
        self._wait_scl_high()
        bit = self.sda.value()
        self._wait_scl_low()
        return bit
    
    def _write_bit(self, bit):
        if bit:
            self.sda.init(Pin.IN, Pin.PULL_UP)
        else:
            self.sda.init(Pin.OUT)
            self.sda.value(0)
        self._wait_scl_high()
        self._wait_scl_low()
        self.sda.init(Pin.IN, Pin.PULL_UP)
    
    def _read_byte(self):
        byte = 0
        for i in range(8):
            byte = (byte << 1) | self._read_bit()
        return byte
    
    def _write_byte(self, byte):
        for i in range(8):
            self._write_bit((byte >> (7 - i)) & 1)
    
    def _send_ack(self):
        self._write_bit(0)
    
    def _send_nack(self):
        self._write_bit(1)
    
    def _detect_start(self):
        # Start condition: SDA goes low while SCL is high
        if self.scl.value() == 1 and self.sda.value() == 0:
            return True
        return False
    
    def poll(self):
        """Check for I2C activity, return (command, data) or None"""
        if not self._detect_start():
            return None
        
        # Wait for SCL to go low
        self._wait_scl_low()
        
        # Read address byte
        addr_byte = self._read_byte()
        addr = addr_byte >> 1
        is_read = addr_byte & 1
        
        if addr != self.addr:
            return None
        
        self._send_ack()
        
        if is_read:
            # Master wants to read from us
            for i in range(self.tx_len):
                self._write_byte(self.tx_buffer[i])
                # Check for ACK/NACK
                if self._read_bit() == 1:  # NACK
                    break
            return ('READ', None)
        else:
            # Master is writing to us
            self.rx_len = 0
            while True:
                byte = self._read_byte()
                self.rx_buffer[self.rx_len] = byte
                self.rx_len += 1
                self._send_ack()
                # Simple timeout check for stop condition
                time.sleep_us(10)
                if self.scl.value() == 1 and self.sda.value() == 1:
                    break
                if self.rx_len >= 255:
                    break
            
            return ('WRITE', bytes(self.rx_buffer[:self.rx_len]))
    
    def set_response(self, data):
        """Set data to send on next read"""
        self.tx_buffer[:len(data)] = data
        self.tx_len = len(data)


# ═══════════════════════════════════════════════════════════════════════════════
# Sound Engine
# ═══════════════════════════════════════════════════════════════════════════════

class SoundEngine:
    def __init__(self, pin):
        self.speaker = PWM(Pin(pin))
        self.speaker.duty_u16(0)
        self.volume = 128  # 0-255
        self.sounds = {}   # Sound definitions from JSON
        self.sound_list = []  # Ordered list for index access
        self.playing = False
        
    def set_volume(self, vol):
        self.volume = max(0, min(255, vol))
    
    def _duty(self):
        """Get duty cycle based on volume"""
        return int((self.volume / 255) * 50000)
    
    def load_sounds_json(self, json_str):
        """Parse JSON and load sound definitions"""
        try:
            data = json.loads(json_str)
            self.sounds = data.get('sounds', {})
            self.sound_list = list(self.sounds.keys())
            print(f"Loaded {len(self.sound_list)} sounds: {self.sound_list}")
            return True
        except Exception as e:
            print(f"JSON parse error: {e}")
            return False
    
    def play(self, sound_id):
        """Play sound by index"""
        if sound_id >= len(self.sound_list):
            print(f"Invalid sound ID: {sound_id}")
            return
        
        name = self.sound_list[sound_id]
        sound = self.sounds[name]
        
        print(f"Playing sound {sound_id}: {name}")
        
        stype = sound.get('type', 'notes')
        
        if stype == 'notes':
            self._play_notes(sound.get('data', []))
        elif stype == 'sweep':
            self._play_sweep(sound.get('start', 400), sound.get('end', 800), sound.get('duration', 100))
        elif stype == 'noise':
            self._play_noise(sound.get('duration', 100))
        elif stype == 'arpeggio':
            self._play_arpeggio(sound.get('notes', []), sound.get('speed', 50))
        elif stype == 'melody':
            self._play_melody(sound.get('data', ''), sound.get('bpm', 120))
    
    def _play_notes(self, notes):
        """Play list of [freq, duration_ms] pairs"""
        for note in notes:
            if len(note) >= 2:
                freq, dur = note[0], note[1]
                if freq > 0:
                    self.speaker.freq(freq)
                    self.speaker.duty_u16(self._duty())
                else:
                    self.speaker.duty_u16(0)
                time.sleep_ms(dur)
        self.speaker.duty_u16(0)
    
    def _play_sweep(self, start, end, duration):
        """Play frequency sweep"""
        steps = 20
        step_time = duration // steps
        freq_step = (end - start) / steps
        
        for i in range(steps):
            freq = int(start + i * freq_step)
            self.speaker.freq(freq)
            self.speaker.duty_u16(self._duty())
            time.sleep_ms(step_time)
        self.speaker.duty_u16(0)
    
    def _play_noise(self, duration):
        """Play noise burst (rapid random frequencies)"""
        import random
        end_time = time.ticks_ms() + duration
        while time.ticks_ms() < end_time:
            self.speaker.freq(random.randint(100, 2000))
            self.speaker.duty_u16(self._duty())
            time.sleep_ms(5)
        self.speaker.duty_u16(0)
    
    def _play_arpeggio(self, notes, speed):
        """Play arpeggio (rapid note sequence)"""
        for freq in notes:
            self.speaker.freq(freq)
            self.speaker.duty_u16(self._duty())
            time.sleep_ms(speed)
        self.speaker.duty_u16(0)
    
    def _play_melody(self, data, bpm):
        """Play melody string like 'C5:4,D5:4,E5:2'"""
        # Note name to frequency
        NOTE_FREQ = {
            'C4': 262, 'D4': 294, 'E4': 330, 'F4': 349, 'G4': 392, 'A4': 440, 'B4': 494,
            'C5': 523, 'D5': 587, 'E5': 659, 'F5': 698, 'G5': 784, 'A5': 880, 'B5': 988,
            'C6': 1047, 'D6': 1175, 'E6': 1319, 'F6': 1397, 'G6': 1568, 'A6': 1760, 'B6': 1976,
            'R': 0
        }
        
        beat_ms = int(60000 / bpm)
        
        for part in data.split(','):
            part = part.strip()
            if ':' in part:
                note, length = part.split(':')
                freq = NOTE_FREQ.get(note.upper(), 0)
                dur = beat_ms // int(length)
                
                if freq > 0:
                    self.speaker.freq(freq)
                    self.speaker.duty_u16(self._duty())
                else:
                    self.speaker.duty_u16(0)
                time.sleep_ms(dur)
        
        self.speaker.duty_u16(0)
    
    def stop(self):
        self.speaker.duty_u16(0)


# ═══════════════════════════════════════════════════════════════════════════════
# Button Handler
# ═══════════════════════════════════════════════════════════════════════════════

class ButtonHandler:
    def __init__(self, pins):
        self.buttons = [Pin(p, Pin.IN, Pin.PULL_UP) for p in pins]
    
    def read(self):
        """Return 2 bytes of button state"""
        state = 0
        for i, btn in enumerate(self.buttons):
            if btn.value() == 0:  # Active low
                state |= (1 << i)
        return bytes([state & 0xFF, (state >> 8) & 0xFF])


# ═══════════════════════════════════════════════════════════════════════════════
# Main
# ═══════════════════════════════════════════════════════════════════════════════

def main():
    print("ThermoConsole Pico - Controller + Sound")
    print(f"I2C Address: 0x{I2C_ADDR:02X}")
    print("Waiting for commands...")
    
    # Initialize
    i2c = I2CSlave(I2C_SDA, I2C_SCL, I2C_ADDR)
    sound = SoundEngine(SPEAKER_PIN)
    buttons = ButtonHandler(BUTTON_PINS)
    
    # Buffer for receiving JSON chunks
    json_buffer = bytearray(4096)
    json_len = 0
    
    # Default button response
    i2c.set_response(buttons.read())
    
    # Load default sounds
    default_sounds = '''{
        "sounds": {
            "jump": {"type": "sweep", "start": 400, "end": 800, "duration": 100},
            "coin": {"type": "notes", "data": [[988, 80], [1319, 200]]},
            "hit": {"type": "sweep", "start": 600, "end": 200, "duration": 80},
            "powerup": {"type": "arpeggio", "notes": [523, 659, 784, 1047], "speed": 80},
            "death": {"type": "notes", "data": [[494, 150], [440, 150], [392, 150], [349, 300]]},
            "select": {"type": "notes", "data": [[1200, 50]]},
            "start": {"type": "arpeggio", "notes": [523, 659, 784, 1047], "speed": 100},
            "explosion": {"type": "noise", "duration": 200}
        }
    }'''
    sound.load_sounds_json(default_sounds)
    
    while True:
        # Update button state for next read
        i2c.set_response(buttons.read())
        
        # Check for I2C commands
        result = i2c.poll()
        
        if result:
            cmd_type, data = result
            
            if cmd_type == 'WRITE' and data:
                cmd = data[0]
                
                if cmd == 0x01 and len(data) >= 4:
                    # Upload sound data chunk
                    chunk_len = (data[1] << 8) | data[2]
                    chunk = data[3:3+chunk_len]
                    json_buffer[json_len:json_len+len(chunk)] = chunk
                    json_len += len(chunk)
                    print(f"Received chunk: {len(chunk)} bytes, total: {json_len}")
                
                elif cmd == 0x02:
                    # Parse uploaded JSON
                    if json_len > 0:
                        json_str = json_buffer[:json_len].decode('utf-8')
                        sound.load_sounds_json(json_str)
                        json_len = 0
                
                elif cmd == 0x10 and len(data) >= 2:
                    # Play sound
                    sound_id = data[1]
                    sound.play(sound_id)
                
                elif cmd == 0x11:
                    # Stop sound
                    sound.stop()
                
                elif cmd == 0x12 and len(data) >= 2:
                    # Set volume
                    sound.set_volume(data[1])
                
                elif cmd == 0x20:
                    # Read buttons (response already set)
                    pass
                
                elif cmd == 0x21:
                    # Status
                    i2c.set_response(bytes([0x01, len(sound.sound_list)]))
        
        time.sleep_us(100)


if __name__ == '__main__':
    main()
