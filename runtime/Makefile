# ThermoConsole Runtime Makefile
# For systems where CMake isn't available

CC = gcc
CFLAGS = -std=c11 -Wall -Wextra -O2 -I./include
LDFLAGS = -lm

# SDL2 and Lua flags (via pkg-config)
SDL2_CFLAGS := $(shell pkg-config --cflags sdl2 SDL2_image SDL2_mixer)
SDL2_LIBS := $(shell pkg-config --libs sdl2 SDL2_image SDL2_mixer)
LUA_CFLAGS := $(shell pkg-config --cflags lua5.4 2>/dev/null || pkg-config --cflags lua)
LUA_LIBS := $(shell pkg-config --libs lua5.4 2>/dev/null || pkg-config --libs lua)

CFLAGS += $(SDL2_CFLAGS) $(LUA_CFLAGS)
LDFLAGS += $(SDL2_LIBS) $(LUA_LIBS)

# Platform selection (default to PC)
PLATFORM ?= pc

# Core source files
CORE_SRCS = src/main.c \
            src/graphics.c \
            src/input.c \
            src/audio.c \
            src/rom.c \
            src/lua_api.c \
            src/script.c

# Platform-specific sources
ifeq ($(PLATFORM),pi)
    PLATFORM_SRCS = src/platform/platform_pi.c
    CFLAGS += -DTHERMO_PLATFORM_PI
else
    PLATFORM_SRCS = src/platform/platform_pc.c
    CFLAGS += -DTHERMO_PLATFORM_PC
endif

SRCS = $(CORE_SRCS) $(PLATFORM_SRCS)
OBJS = $(SRCS:.c=.o)
TARGET = thermoconsole

# Build targets
.PHONY: all clean debug release install pc pi help check-deps

all: release

release: CFLAGS += -O2 -DNDEBUG
release: $(TARGET)

debug: CFLAGS += -g -DTHERMO_DEBUG
debug: $(TARGET)

# Shorthand targets
pc:
	$(MAKE) PLATFORM=pc release

pi:
	$(MAKE) PLATFORM=pi release

# Cross-compile for Pi from PC
pi-cross: CC = arm-linux-gnueabihf-gcc
pi-cross:
	$(MAKE) PLATFORM=pi release

$(TARGET): $(OBJS)
	$(CC) -o $@ $^ $(LDFLAGS)
	@echo ""
	@echo "Build complete: $(TARGET)"
	@echo "Platform: $(PLATFORM)"
	@echo "Run with: ./$(TARGET) <game.tcr>"

src/%.o: src/%.c include/thermo.h include/platform.h
	$(CC) $(CFLAGS) -c -o $@ $<

src/platform/%.o: src/platform/%.c include/thermo.h include/platform.h
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(CORE_SRCS:.c=.o) src/platform/*.o $(TARGET)

install: $(TARGET)
	install -m 755 $(TARGET) /usr/local/bin/

# Dependencies check
check-deps:
	@echo "Checking dependencies..."
	@pkg-config --exists sdl2 && echo "  SDL2: OK" || echo "  SDL2: MISSING"
	@pkg-config --exists SDL2_image && echo "  SDL2_image: OK" || echo "  SDL2_image: MISSING"
	@pkg-config --exists SDL2_mixer && echo "  SDL2_mixer: OK" || echo "  SDL2_mixer: MISSING"
	@(pkg-config --exists lua5.4 || pkg-config --exists lua) && echo "  Lua: OK" || echo "  Lua: MISSING"

# Help
help:
	@echo "ThermoConsole Runtime Build System"
	@echo ""
	@echo "Targets:"
	@echo "  all       - Build release version for current platform (default)"
	@echo "  release   - Build optimized release"
	@echo "  debug     - Build with debug symbols"
	@echo "  pc        - Build for desktop PC"
	@echo "  pi        - Build for Raspberry Pi (native)"
	@echo "  pi-cross  - Cross-compile for Pi from PC"
	@echo "  clean     - Remove build artifacts"
	@echo "  install   - Install to /usr/local/bin"
	@echo "  check-deps - Check for required libraries"
	@echo ""
	@echo "Variables:"
	@echo "  PLATFORM=pc|pi   - Target platform (default: pc)"
	@echo ""
	@echo "Examples:"
	@echo "  make                    # Build for PC"
	@echo "  make PLATFORM=pi        # Build for Pi (on Pi)"
	@echo "  make pi-cross           # Cross-compile for Pi from PC"
	@echo ""
	@echo "Requirements:"
	@echo "  - SDL2, SDL2_image, SDL2_mixer"
	@echo "  - Lua 5.4"
	@echo ""
	@echo "Install dependencies (Ubuntu/Debian):"
	@echo "  sudo apt install libsdl2-dev libsdl2-image-dev libsdl2-mixer-dev liblua5.4-dev"
