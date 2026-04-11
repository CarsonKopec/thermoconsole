"""
ThermoConsole Pico Firmware
USB Serial Communication (buttons + sound)

Connect Pico USB to Pi USB data port
Pico appears as /dev/ttyACM0 on Pi

Protocol (binary):
  Pi sends:
    0x10 <sound_id>     - Play sound
    0x11                - Stop sound  
    0x12 <volume>       - Set volume (0-255)
    0x20                - Request button state
    0x01 <len_hi> <len_lo> <data...>  - Upload sound JSON chunk
    0x02                - Parse uploaded JSON

  Pico sends:
    0x20 <btn_lo> <btn_hi>  - Button state response
    0x01                    - ACK
    0xFF                    - Error
"""

import sys
import select
import json
import time
from machine import Pin, PWM

# ═══════════════════════════════════════════════════════════════════════════════
# Configuration
# ═══════════════════════════════════════════════════════════════════════════════

SPEAKER_PIN = 15

# Button GPIOs
BUTTON_PINS = [2, 3, 4, 5, 6, 7, 8, 9, 10, 11]
# 0=Up, 1=Down, 2=Left, 3=Right, 4=A, 5=B, 6=X, 7=Y, 8=Start, 9=Select

# ═══════════════════════════════════════════════════════════════════════════════
# USB Serial (uses stdin/stdout)
# ═══════════════════════════════════════════════════════════════════════════════

class USBSerial:
    def __init__(self):
        self.poll = select.poll()
        self.poll.register(sys.stdin, select.POLLIN)
    
    def available(self):
        """Check if data is available"""
        events = self.poll.poll(0)
        return len(events) > 0
    
    def read(self, n=1):
        """Read n bytes"""
        return sys.stdin.buffer.read(n)
    
    def write(self, data):
        """Write bytes"""
        sys.stdout.buffer.write(data)
        sys.stdout.buffer.flush()


# ═══════════════════════════════════════════════════════════════════════════════
# Sound Engine
# ═══════════════════════════════════════════════════════════════════════════════

class SoundEngine:
    def __init__(self, pin):
        self.speaker = PWM(Pin(pin))
        self.speaker.duty_u16(0)
        self.volume = 200  # 0-255
        self.sounds = {}
        self.sound_list = []
        self.playing = False
        self._load_defaults()
    
    def _load_defaults(self):
        """Load default sound effects"""
        self.sounds = {
            "jump": {"type": "sweep", "start": 400, "end": 800, "duration": 100},
            "coin": {"type": "notes", "data": [[988, 80], [1319, 200]]},
            "hit": {"type": "sweep", "start": 600, "end": 200, "duration": 80},
            "powerup": {"type": "arpeggio", "notes": [523, 659, 784, 1047], "speed": 80},
            "death": {"type": "notes", "data": [[494, 150], [440, 150], [392, 150], [349, 300]]},
            "select": {"type": "notes", "data": [[1200, 50]]},
            "start": {"type": "arpeggio", "notes": [523, 659, 784, 1047], "speed": 100},
            "explosion": {"type": "noise", "duration": 200},
            "laser": {"type": "sweep", "start": 1500, "end": 300, "duration": 80},
            "pickup": {"type": "notes", "data": [[880, 50], [1100, 50], [1320, 100]]},
            "menu_move": {"type": "notes", "data": [[800, 30]]},
            "menu_select": {"type": "notes", "data": [[600, 50], [900, 100]]},
            "game_over": {"type": "notes", "data": [[494, 200], [440, 200], [392, 200], [349, 400]]},
            "victory": {"type": "arpeggio", "notes": [523, 659, 784, 1047, 1319], "speed": 80},
            "level_start": {"type": "arpeggio", "notes": [262, 330, 392, 523, 659, 784], "speed": 60}
        }
        self.sound_list = list(self.sounds.keys())
    
    def set_volume(self, vol):
        self.volume = max(0, min(255, vol))
    
    def _duty(self):
        return int((self.volume / 255) * 60000)
    
    def load_json(self, json_str):
        """Parse JSON and load sound definitions"""
        try:
            data = json.loads(json_str)
            if "sounds" in data:
                self.sounds = data["sounds"]
                self.sound_list = list(self.sounds.keys())
            return True
        except Exception as e:
            print(f"JSON error: {e}")
            return False
    
    def play(self, sound_id):
        """Play sound by index"""
        if sound_id >= len(self.sound_list):
            return
        
        name = self.sound_list[sound_id]
        sound = self.sounds[name]
        stype = sound.get("type", "notes")
        
        if stype == "notes":
            self._play_notes(sound.get("data", []))
        elif stype == "sweep":
            self._play_sweep(sound.get("start", 400), sound.get("end", 800), sound.get("duration", 100))
        elif stype == "noise":
            self._play_noise(sound.get("duration", 100))
        elif stype == "arpeggio":
            self._play_arpeggio(sound.get("notes", []), sound.get("speed", 50))
        elif stype == "melody":
            self._play_melody(sound.get("data", ""), sound.get("bpm", 120))
    
    def _play_notes(self, notes):
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
        steps = 20
        step_time = duration // steps
        freq_step = (end - start) / steps
        
        for i in range(steps):
            freq = int(start + i * freq_step)
            if freq > 0:
                self.speaker.freq(freq)
                self.speaker.duty_u16(self._duty())
            time.sleep_ms(step_time)
        self.speaker.duty_u16(0)
    
    def _play_noise(self, duration):
        import random
        end_time = time.ticks_ms() + duration
        while time.ticks_ms() < end_time:
            self.speaker.freq(random.randint(100, 2000))
            self.speaker.duty_u16(self._duty())
            time.sleep_ms(5)
        self.speaker.duty_u16(0)
    
    def _play_arpeggio(self, notes, speed):
        for freq in notes:
            if freq > 0:
                self.speaker.freq(freq)
                self.speaker.duty_u16(self._duty())
            time.sleep_ms(speed)
        self.speaker.duty_u16(0)
    
    def _play_melody(self, data, bpm):
        NOTE_FREQ = {
            'C4': 262, 'D4': 294, 'E4': 330, 'F4': 349, 'G4': 392, 'A4': 440, 'B4': 494,
            'C5': 523, 'D5': 587, 'E5': 659, 'F5': 698, 'G5': 784, 'A5': 880, 'B5': 988,
            'C6': 1047, 'D6': 1175, 'E6': 1319, 'F6': 1397, 'G6': 1568, 'R': 0
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
    print("ThermoConsole Pico - USB Serial Mode")
    
    serial = USBSerial()
    sound = SoundEngine(SPEAKER_PIN)
    buttons = ButtonHandler(BUTTON_PINS)
    
    # JSON upload buffer
    json_buffer = bytearray(4096)
    json_len = 0
    
    # Play startup sound
    sound.play(6)  # "start" sound
    
    while True:
        if serial.available():
            cmd = serial.read(1)
            
            if not cmd:
                continue
            
            cmd = cmd[0]
            
            if cmd == 0x10:  # Play sound
                sound_id = serial.read(1)
                if sound_id:
                    sound.play(sound_id[0])
                    serial.write(b'\x01')  # ACK
            
            elif cmd == 0x11:  # Stop sound
                sound.stop()
                serial.write(b'\x01')
            
            elif cmd == 0x12:  # Set volume
                vol = serial.read(1)
                if vol:
                    sound.set_volume(vol[0])
                    serial.write(b'\x01')
            
            elif cmd == 0x20:  # Read buttons
                btn_state = buttons.read()
                serial.write(bytes([0x20]) + btn_state)
            
            elif cmd == 0x01:  # Upload JSON chunk
                header = serial.read(2)
                if header and len(header) == 2:
                    chunk_len = (header[0] << 8) | header[1]
                    chunk = serial.read(chunk_len)
                    if chunk:
                        json_buffer[json_len:json_len+len(chunk)] = chunk
                        json_len += len(chunk)
                        serial.write(b'\x01')
            
            elif cmd == 0x02:  # Parse JSON
                if json_len > 0:
                    json_str = json_buffer[:json_len].decode('utf-8')
                    if sound.load_json(json_str):
                        serial.write(b'\x01')
                    else:
                        serial.write(b'\xFF')
                    json_len = 0
                else:
                    serial.write(b'\xFF')
            
            elif cmd == 0x30:  # Get sound count
                serial.write(bytes([0x30, len(sound.sound_list)]))
            
            elif cmd == 0x31:  # Get sound name
                idx = serial.read(1)
                if idx and idx[0] < len(sound.sound_list):
                    name = sound.sound_list[idx[0]].encode()
                    serial.write(bytes([0x31, len(name)]) + name)
                else:
                    serial.write(b'\xFF')
        
        # Small delay to prevent busy-waiting
        time.sleep_ms(1)


if __name__ == '__main__':
    main()
