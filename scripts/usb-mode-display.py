#!/usr/bin/env python3
"""
ThermoConsole USB Mode Display

Shows a friendly screen when the console is in USB storage mode,
letting the user know they can copy games.
"""

import os
import sys
import time
import subprocess

# Try to use pygame for display, fall back to framebuffer
try:
    os.environ['SDL_VIDEODRIVER'] = 'kmsdrm'
    import pygame
    HAS_PYGAME = True
except ImportError:
    HAS_PYGAME = False

# Display settings
SCREEN_WIDTH = 640
SCREEN_HEIGHT = 480

# Colors (PICO-8 palette)
BLACK = (0, 0, 0)
DARK_BLUE = (29, 43, 83)
DARK_PURPLE = (126, 37, 83)
DARK_GREEN = (0, 135, 81)
BROWN = (171, 82, 54)
DARK_GRAY = (95, 87, 79)
LIGHT_GRAY = (194, 195, 199)
WHITE = (255, 241, 232)
RED = (255, 0, 77)
ORANGE = (255, 163, 0)
YELLOW = (255, 236, 39)
GREEN = (0, 228, 54)
BLUE = (41, 173, 255)
INDIGO = (131, 118, 156)
PINK = (255, 119, 168)
PEACH = (255, 204, 170)


def draw_usb_icon(surface, x, y, size, color):
    """Draw a simple USB icon."""
    pygame.draw.rect(surface, color, (x, y, size * 0.6, size * 0.3))
    pygame.draw.rect(surface, color, (x + size * 0.1, y - size * 0.15, size * 0.1, size * 0.15))
    pygame.draw.rect(surface, color, (x + size * 0.4, y - size * 0.15, size * 0.1, size * 0.15))
    pygame.draw.rect(surface, color, (x + size * 0.6, y + size * 0.05, size * 0.2, size * 0.2))


def draw_computer_icon(surface, x, y, size, color):
    """Draw a simple computer/monitor icon."""
    # Monitor
    pygame.draw.rect(surface, color, (x, y, size, size * 0.7), 2)
    # Stand
    pygame.draw.rect(surface, color, (x + size * 0.35, y + size * 0.7, size * 0.3, size * 0.15))
    pygame.draw.rect(surface, color, (x + size * 0.2, y + size * 0.85, size * 0.6, size * 0.1))


def main_pygame():
    """Main loop using pygame."""
    pygame.init()
    
    # Try to set up display
    try:
        screen = pygame.display.set_mode((SCREEN_WIDTH, SCREEN_HEIGHT), pygame.FULLSCREEN)
    except:
        screen = pygame.display.set_mode((SCREEN_WIDTH, SCREEN_HEIGHT))
    
    pygame.display.set_caption("ThermoConsole USB Mode")
    pygame.mouse.set_visible(False)
    
    # Load or create font
    try:
        font_large = pygame.font.Font(None, 48)
        font_medium = pygame.font.Font(None, 32)
        font_small = pygame.font.Font(None, 24)
    except:
        font_large = pygame.font.SysFont('monospace', 48)
        font_medium = pygame.font.SysFont('monospace', 32)
        font_small = pygame.font.SysFont('monospace', 24)
    
    clock = pygame.time.Clock()
    frame = 0
    
    running = True
    while running:
        for event in pygame.event.get():
            if event.type == pygame.QUIT:
                running = False
            elif event.type == pygame.KEYDOWN:
                if event.key == pygame.K_ESCAPE:
                    running = False
        
        # Check if we're still in USB mode
        usb_mode_active = os.path.exists("/sys/kernel/config/usb_gadget/thermoconsole/UDC")
        if usb_mode_active:
            with open("/sys/kernel/config/usb_gadget/thermoconsole/UDC", "r") as f:
                usb_mode_active = len(f.read().strip()) > 0
        
        if not usb_mode_active:
            running = False
            continue
        
        # Clear screen
        screen.fill(DARK_BLUE)
        
        # Animated border
        border_color = [BLUE, GREEN, YELLOW, ORANGE][(frame // 30) % 4]
        pygame.draw.rect(screen, border_color, (10, 10, SCREEN_WIDTH - 20, SCREEN_HEIGHT - 20), 3)
        
        # Title
        title = font_large.render("USB MODE", True, WHITE)
        title_rect = title.get_rect(center=(SCREEN_WIDTH // 2, 60))
        screen.blit(title, title_rect)
        
        # USB Icon (animated)
        icon_y = 120 + int(5 * ((frame % 60) / 60.0 * 2 - 1) ** 2 * (-1 if (frame // 60) % 2 else 1))
        draw_usb_icon(screen, SCREEN_WIDTH // 2 - 40, icon_y, 80, WHITE)
        
        # Arrow animation
        arrow_x = SCREEN_WIDTH // 2
        for i in range(3):
            alpha = 255 - (frame + i * 20) % 60 * 4
            if alpha > 0:
                arrow_color = (*GREEN[:3], alpha) if alpha < 255 else GREEN
                y = 200 + i * 25 + (frame % 30)
                # Draw arrow pointing down
                pygame.draw.polygon(screen, GREEN, [
                    (arrow_x - 15, y),
                    (arrow_x + 15, y),
                    (arrow_x, y + 20)
                ])
        
        # Computer icon
        draw_computer_icon(screen, SCREEN_WIDTH // 2 - 50, 280, 100, LIGHT_GRAY)
        
        # Instructions
        instructions = [
            "Console connected to PC",
            "",
            "1. Open the TCGAMES drive",
            "2. Copy .tcr game files",
            "3. Safely eject the drive",
            "",
            "Hold START+SELECT to exit USB mode"
        ]
        
        y = 340
        for line in instructions:
            if line:
                color = YELLOW if "eject" in line.lower() else WHITE
                text = font_small.render(line, True, color)
                text_rect = text.get_rect(center=(SCREEN_WIDTH // 2, y))
                screen.blit(text, text_rect)
            y += 22
        
        # Status indicator (pulsing)
        pulse = abs((frame % 60) - 30) / 30.0
        status_color = (
            int(GREEN[0] * pulse + DARK_GREEN[0] * (1 - pulse)),
            int(GREEN[1] * pulse + DARK_GREEN[1] * (1 - pulse)),
            int(GREEN[2] * pulse + DARK_GREEN[2] * (1 - pulse))
        )
        pygame.draw.circle(screen, status_color, (30, SCREEN_HEIGHT - 30), 10)
        
        status_text = font_small.render("Connected", True, GREEN)
        screen.blit(status_text, (50, SCREEN_HEIGHT - 38))
        
        pygame.display.flip()
        clock.tick(30)
        frame += 1
    
    pygame.quit()


def main_framebuffer():
    """Fallback: write directly to framebuffer."""
    fb_path = "/dev/fb0"
    
    if not os.path.exists(fb_path):
        print("No framebuffer available")
        return
    
    # Simple text display using fbset
    message = """
    ╔═══════════════════════════════════════════════════════╗
    ║                     USB MODE                          ║
    ║                                                       ║
    ║           Console connected to PC                     ║
    ║                                                       ║
    ║        1. Open the TCGAMES drive                      ║
    ║        2. Copy .tcr game files                        ║
    ║        3. Safely eject the drive                      ║
    ║                                                       ║
    ║        Hold START+SELECT to exit USB mode             ║
    ╚═══════════════════════════════════════════════════════╝
    """
    
    # Try to display using fbi or similar
    try:
        # Create a simple image
        from PIL import Image, ImageDraw, ImageFont
        
        img = Image.new('RGB', (SCREEN_WIDTH, SCREEN_HEIGHT), DARK_BLUE)
        draw = ImageDraw.Draw(img)
        
        try:
            font = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf", 20)
        except:
            font = ImageFont.load_default()
        
        y = 100
        for line in message.strip().split('\n'):
            draw.text((50, y), line, fill=WHITE, font=font)
            y += 25
        
        # Write to framebuffer
        with open(fb_path, 'wb') as fb:
            fb.write(img.tobytes())
        
        # Keep running while in USB mode
        while True:
            usb_mode_active = os.path.exists("/sys/kernel/config/usb_gadget/thermoconsole/UDC")
            if not usb_mode_active:
                break
            time.sleep(1)
            
    except ImportError:
        print("No display method available (need pygame or PIL)")
        # Just wait
        while os.path.exists("/sys/kernel/config/usb_gadget/thermoconsole/UDC"):
            time.sleep(1)


def main():
    """Main entry point."""
    if HAS_PYGAME:
        main_pygame()
    else:
        main_framebuffer()


if __name__ == "__main__":
    main()
