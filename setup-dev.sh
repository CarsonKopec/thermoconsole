#!/bin/bash
#
# ThermoConsole Development Environment Setup
#
# This script sets up options for testing ThermoConsole without Pi hardware:
# 1. Native PC build (recommended for game development)
# 2. Docker ARM emulation (for testing Pi builds)
# 3. QEMU full system emulation (closest to real hardware)
#

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

echo ""
echo -e "${BLUE}╔═══════════════════════════════════════════════════════════════╗${NC}"
echo -e "${BLUE}║         ThermoConsole Development Environment                 ║${NC}"
echo -e "${BLUE}╚═══════════════════════════════════════════════════════════════╝${NC}"
echo ""

# ═══════════════════════════════════════════════════════════════════════════════
# Option 1: Native PC Build (Recommended)
# ═══════════════════════════════════════════════════════════════════════════════

setup_native() {
    echo -e "${GREEN}Setting up Native PC Build...${NC}"
    echo ""
    echo "This builds ThermoConsole to run directly on your PC."
    echo "Best for: Game development, testing Lua scripts, UI work"
    echo ""
    
    # Detect OS
    if [[ "$OSTYPE" == "linux-gnu"* ]]; then
        echo "Detected: Linux"
        echo "Installing dependencies..."
        
        if command -v apt-get &>/dev/null; then
            sudo apt-get update
            sudo apt-get install -y \
                build-essential \
                cmake \
                libsdl2-dev \
                libsdl2-image-dev \
                libsdl2-mixer-dev \
                liblua5.4-dev \
                pkg-config
        elif command -v dnf &>/dev/null; then
            sudo dnf install -y \
                gcc gcc-c++ cmake \
                SDL2-devel SDL2_image-devel SDL2_mixer-devel \
                lua-devel
        elif command -v pacman &>/dev/null; then
            sudo pacman -S --noconfirm \
                base-devel cmake \
                sdl2 sdl2_image sdl2_mixer \
                lua
        fi
        
    elif [[ "$OSTYPE" == "darwin"* ]]; then
        echo "Detected: macOS"
        echo "Installing dependencies via Homebrew..."
        
        if ! command -v brew &>/dev/null; then
            echo "Homebrew not found. Install from https://brew.sh"
            exit 1
        fi
        
        brew install cmake sdl2 sdl2_image sdl2_mixer lua
        
    elif [[ "$OSTYPE" == "msys" ]] || [[ "$OSTYPE" == "mingw"* ]]; then
        echo "Detected: Windows (MSYS2/MinGW)"
        echo "Installing dependencies..."
        
        pacman -S --noconfirm \
            mingw-w64-x86_64-gcc \
            mingw-w64-x86_64-cmake \
            mingw-w64-x86_64-SDL2 \
            mingw-w64-x86_64-SDL2_image \
            mingw-w64-x86_64-SDL2_mixer \
            mingw-w64-x86_64-lua
    fi
    
    echo ""
    echo -e "${GREEN}Building ThermoConsole...${NC}"
    
    cd "$SCRIPT_DIR/runtime"
    mkdir -p build && cd build
    cmake ..
    make -j$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 2)
    
    echo ""
    echo -e "${GREEN}✓ Native build complete!${NC}"
    echo ""
    echo "Run games with:"
    echo "  ./runtime/build/thermoconsole games/space_dodge"
    echo ""
    echo "Or launch the system menu:"
    echo "  ./runtime/build/thermoconsole system/menu"
}

# ═══════════════════════════════════════════════════════════════════════════════
# Option 2: Docker ARM Emulation
# ═══════════════════════════════════════════════════════════════════════════════

setup_docker_arm() {
    echo -e "${GREEN}Setting up Docker ARM Emulation...${NC}"
    echo ""
    echo "This uses Docker + QEMU to run ARM binaries on x86."
    echo "Best for: Testing Pi-specific code, cross-compilation"
    echo ""
    
    # Check Docker
    if ! command -v docker &>/dev/null; then
        echo -e "${RED}Docker not found. Please install Docker first.${NC}"
        echo "https://docs.docker.com/get-docker/"
        exit 1
    fi
    
    # Enable ARM emulation
    echo "Enabling ARM emulation..."
    docker run --rm --privileged multiarch/qemu-user-static --reset -p yes
    
    # Create Dockerfile
    cat > "$SCRIPT_DIR/Dockerfile.arm" << 'DOCKERFILE'
FROM arm32v7/debian:bookworm

# Install dependencies
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    libsdl2-dev \
    libsdl2-image-dev \
    libsdl2-mixer-dev \
    liblua5.4-dev \
    i2c-tools \
    git \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /thermoconsole

# Copy source
COPY runtime/ /thermoconsole/runtime/
COPY system/ /thermoconsole/system/
COPY games/ /thermoconsole/games/

# Build
RUN cd runtime && mkdir -p build && cd build && \
    cmake .. -DTHERMO_BUILD_PI=ON && \
    make -j$(nproc)

CMD ["/bin/bash"]
DOCKERFILE

    echo "Building ARM Docker image (this takes a while)..."
    docker build -f Dockerfile.arm -t thermoconsole-arm .
    
    echo ""
    echo -e "${GREEN}✓ Docker ARM environment ready!${NC}"
    echo ""
    echo "Run ARM shell with:"
    echo "  docker run -it thermoconsole-arm bash"
    echo ""
    echo "Test the build:"
    echo "  docker run -it thermoconsole-arm ./runtime/build/thermoconsole --help"
}

# ═══════════════════════════════════════════════════════════════════════════════
# Option 3: QEMU Full System Emulation
# ═══════════════════════════════════════════════════════════════════════════════

setup_qemu() {
    echo -e "${GREEN}Setting up QEMU Full System Emulation...${NC}"
    echo ""
    echo "This emulates a complete Raspberry Pi system."
    echo "Best for: Testing boot process, systemd services, full OS"
    echo -e "${YELLOW}Warning: Very slow, no GPU acceleration${NC}"
    echo ""
    
    QEMU_DIR="$SCRIPT_DIR/qemu-pi"
    mkdir -p "$QEMU_DIR"
    cd "$QEMU_DIR"
    
    # Install QEMU
    if command -v apt-get &>/dev/null; then
        sudo apt-get install -y qemu-system-arm qemu-utils
    elif command -v brew &>/dev/null; then
        brew install qemu
    fi
    
    # Download kernel
    if [ ! -f kernel-qemu-5.10.63-bullseye ]; then
        echo "Downloading QEMU kernel..."
        wget -q https://github.com/dhruvvyas90/qemu-rpi-kernel/raw/master/kernel-qemu-5.10.63-bullseye
        wget -q https://github.com/dhruvvyas90/qemu-rpi-kernel/raw/master/versatile-pb-bullseye-5.10.63.dtb
    fi
    
    # Download Pi OS image
    if [ ! -f raspios.img ]; then
        echo "Downloading Raspberry Pi OS Lite..."
        wget -q -O raspios.img.xz https://downloads.raspberrypi.org/raspios_lite_armhf/images/raspios_lite_armhf-2024-03-15/2024-03-15-raspios-bookworm-armhf-lite.img.xz
        echo "Extracting..."
        xz -d raspios.img.xz
        mv 2024-03-15-raspios-bookworm-armhf-lite.img raspios.img 2>/dev/null || true
        
        # Resize for more space
        qemu-img resize raspios.img 8G
    fi
    
    # Create launch script
    cat > run-qemu.sh << 'QEMU_SCRIPT'
#!/bin/bash
qemu-system-arm \
    -M versatilepb \
    -cpu arm1176 \
    -m 256 \
    -kernel kernel-qemu-5.10.63-bullseye \
    -dtb versatile-pb-bullseye-5.10.63.dtb \
    -drive file=raspios.img,format=raw \
    -append "root=/dev/sda2 panic=1 rootfstype=ext4 rw" \
    -net nic -net user,hostfwd=tcp::2222-:22,hostfwd=tcp::5900-:5900 \
    -no-reboot \
    -serial stdio

# After boot, SSH with: ssh -p 2222 pi@localhost
# Default password: raspberry
QEMU_SCRIPT
    chmod +x run-qemu.sh
    
    echo ""
    echo -e "${GREEN}✓ QEMU environment ready!${NC}"
    echo ""
    echo "Start emulator with:"
    echo "  cd qemu-pi && ./run-qemu.sh"
    echo ""
    echo "SSH into it with:"
    echo "  ssh -p 2222 pi@localhost"
    echo "  (password: raspberry)"
    echo ""
    echo "Then install ThermoConsole inside the emulator."
}

# ═══════════════════════════════════════════════════════════════════════════════
# Option 4: Mock Hardware (For testing without display/controller)
# ═══════════════════════════════════════════════════════════════════════════════

setup_mock() {
    echo -e "${GREEN}Setting up Mock Hardware Mode...${NC}"
    echo ""
    echo "This runs ThermoConsole with simulated I2C and display."
    echo "Best for: CI/CD, automated testing, headless development"
    echo ""
    
    # Create mock I2C device
    cat > "$SCRIPT_DIR/scripts/mock-i2c.py" << 'MOCK_I2C'
#!/usr/bin/env python3
"""
Mock I2C device for testing Pico controller communication.
Creates a virtual I2C device that responds like the Pico.
"""

import socket
import struct
import threading
import time

class MockPicoController:
    def __init__(self, port=4242):
        self.port = port
        self.buttons = 0
        self.running = True
        
    def set_button(self, btn, pressed):
        if pressed:
            self.buttons |= (1 << btn)
        else:
            self.buttons &= ~(1 << btn)
    
    def get_state(self):
        return struct.pack('<H', self.buttons)
    
    def run_server(self):
        """Run TCP server that mimics I2C reads."""
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        sock.bind(('127.0.0.1', self.port))
        sock.listen(1)
        sock.settimeout(1.0)
        
        print(f"Mock Pico controller on port {self.port}")
        print("Keyboard controls: WASD=D-pad, JK=AB, Enter=Start, Shift=Select")
        
        while self.running:
            try:
                conn, addr = sock.accept()
                conn.settimeout(0.1)
                
                while self.running:
                    try:
                        data = conn.recv(1)
                        if not data:
                            break
                        # Any read request returns button state
                        conn.send(self.get_state())
                    except socket.timeout:
                        continue
                    except:
                        break
                        
                conn.close()
            except socket.timeout:
                continue
            except:
                break
        
        sock.close()

if __name__ == '__main__':
    import sys
    
    try:
        import keyboard  # pip install keyboard
        HAS_KEYBOARD = True
    except ImportError:
        HAS_KEYBOARD = False
        print("Install 'keyboard' package for input: pip install keyboard")
    
    controller = MockPicoController()
    
    # Start server thread
    server_thread = threading.Thread(target=controller.run_server)
    server_thread.start()
    
    # Keyboard input loop
    if HAS_KEYBOARD:
        KEY_MAP = {
            'w': 0, 'up': 0,
            's': 1, 'down': 1,
            'a': 2, 'left': 2,
            'd': 3, 'right': 3,
            'j': 4, 'z': 4,
            'k': 5, 'x': 5,
            'l': 6, 'c': 6,
            ';': 7, 'v': 7,
            'enter': 8,
            'shift': 9,
        }
        
        for key, btn in KEY_MAP.items():
            keyboard.on_press_key(key, lambda e, b=btn: controller.set_button(b, True))
            keyboard.on_release_key(key, lambda e, b=btn: controller.set_button(b, False))
        
        print("Press Ctrl+C to exit")
        try:
            while True:
                time.sleep(0.1)
        except KeyboardInterrupt:
            pass
    else:
        input("Press Enter to stop...")
    
    controller.running = False
    server_thread.join()
MOCK_I2C
    chmod +x "$SCRIPT_DIR/scripts/mock-i2c.py"
    
    echo ""
    echo -e "${GREEN}✓ Mock hardware ready!${NC}"
    echo ""
    echo "Run the mock controller:"
    echo "  python3 scripts/mock-i2c.py"
    echo ""
    echo "Then run ThermoConsole with THERMO_MOCK_I2C=1"
}

# ═══════════════════════════════════════════════════════════════════════════════
# Main Menu
# ═══════════════════════════════════════════════════════════════════════════════

echo "Select development environment:"
echo ""
echo "  1) Native PC Build (recommended)"
echo "     - Fastest, runs directly on your machine"
echo "     - Perfect for game development"
echo ""
echo "  2) Docker ARM Emulation"
echo "     - Cross-compile for Pi on x86"
echo "     - Good for testing Pi-specific code"
echo ""
echo "  3) QEMU Full System"
echo "     - Complete Pi OS emulation"
echo "     - Very slow, but most accurate"
echo ""
echo "  4) Mock Hardware"
echo "     - Simulated I2C controller"
echo "     - For headless/CI testing"
echo ""
echo "  5) All of the above"
echo ""
read -p "Choice [1-5]: " choice

case $choice in
    1) setup_native ;;
    2) setup_docker_arm ;;
    3) setup_qemu ;;
    4) setup_mock ;;
    5)
        setup_native
        setup_docker_arm
        setup_qemu
        setup_mock
        ;;
    *)
        echo "Invalid choice"
        exit 1
        ;;
esac

echo ""
echo -e "${GREEN}═══════════════════════════════════════════════════════════════${NC}"
echo -e "${GREEN}  Setup complete! Happy developing!${NC}"
echo -e "${GREEN}═══════════════════════════════════════════════════════════════${NC}"
echo ""
