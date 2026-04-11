cmake_minimum_required(VERSION 3.16)
project(ThermoConsole VERSION 1.0.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# ─────────────────────────────────────────────────────────────────────────────
# Options
# ─────────────────────────────────────────────────────────────────────────────

option(THERMO_BUILD_PI "Build for Raspberry Pi" OFF)
option(THERMO_PICO_AUDIO "Use Pico as sound chip via I2C" OFF)
option(THERMO_PICO_USB "Use Pico for sound+buttons via USB serial" OFF)
option(THERMO_DEBUG "Enable debug output" OFF)

if(THERMO_DEBUG)
    add_definitions(-DTHERMO_DEBUG)
    set(CMAKE_BUILD_TYPE Debug)
else()
    set(CMAKE_BUILD_TYPE Release)
endif()

# ─────────────────────────────────────────────────────────────────────────────
# Find Dependencies
# ─────────────────────────────────────────────────────────────────────────────

# SDL2
find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 REQUIRED sdl2)
pkg_check_modules(SDL2_IMAGE REQUIRED SDL2_image)
pkg_check_modules(SDL2_MIXER REQUIRED SDL2_mixer)

# Lua 5.4
pkg_check_modules(LUA REQUIRED lua5.4)

# Math library
find_library(M_LIB m)

# ─────────────────────────────────────────────────────────────────────────────
# Source Files
# ─────────────────────────────────────────────────────────────────────────────

set(THERMO_SOURCES
    src/main.c
    src/graphics.c
    src/input.c
    src/rom.c
    src/lua_api.c
    src/script.c
    src/system_api.c
)

# Audio source selection
if(THERMO_PICO_USB)
    message(STATUS "Using Pico via USB serial (sound + buttons)")
    list(APPEND THERMO_SOURCES src/pico_usb.c)
    add_definitions(-DTHERMO_PICO_USB)
elseif(THERMO_PICO_AUDIO)
    message(STATUS "Using Pico sound chip (I2C)")
    list(APPEND THERMO_SOURCES src/audio_pico.c)
    add_definitions(-DTHERMO_PICO_AUDIO)
else()
    message(STATUS "Using SDL_mixer audio")
    list(APPEND THERMO_SOURCES src/audio.c)
endif()

# Platform-specific sources
set(THERMO_PLATFORM_PC_SOURCES
    src/platform/platform_pc.c
)

set(THERMO_PLATFORM_PI_SOURCES
    src/platform/platform_pi.c
)

# ─────────────────────────────────────────────────────────────────────────────
# Platform Selection
# ─────────────────────────────────────────────────────────────────────────────

if(THERMO_BUILD_PI)
    message(STATUS "Building for Raspberry Pi")
    list(APPEND THERMO_SOURCES ${THERMO_PLATFORM_PI_SOURCES})
    set(THERMO_PLATFORM_DEF THERMO_PLATFORM_PI)
else()
    message(STATUS "Building for desktop (PC)")
    list(APPEND THERMO_SOURCES ${THERMO_PLATFORM_PC_SOURCES})
    set(THERMO_PLATFORM_DEF THERMO_PLATFORM_PC)
endif()

# ─────────────────────────────────────────────────────────────────────────────
# Executable
# ─────────────────────────────────────────────────────────────────────────────

add_executable(thermoconsole ${THERMO_SOURCES})

target_include_directories(thermoconsole PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${SDL2_INCLUDE_DIRS}
    ${SDL2_IMAGE_INCLUDE_DIRS}
    ${SDL2_MIXER_INCLUDE_DIRS}
    ${LUA_INCLUDE_DIRS}
)

target_link_libraries(thermoconsole
    ${SDL2_LIBRARIES}
    ${SDL2_IMAGE_LIBRARIES}
    ${SDL2_MIXER_LIBRARIES}
    ${LUA_LIBRARIES}
    ${M_LIB}
)

target_compile_options(thermoconsole PRIVATE
    ${SDL2_CFLAGS_OTHER}
    ${LUA_CFLAGS_OTHER}
)

target_compile_definitions(thermoconsole PRIVATE ${THERMO_PLATFORM_DEF})

# ─────────────────────────────────────────────────────────────────────────────
# Platform-specific Configuration
# ─────────────────────────────────────────────────────────────────────────────

if(THERMO_BUILD_PI)
    # Pi may need additional libraries for serial I/O
    # target_link_libraries(thermoconsole pthread)
endif()

# Compiler warnings
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(thermoconsole PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Wno-unused-parameter
    )
endif()

# ─────────────────────────────────────────────────────────────────────────────
# Install
# ─────────────────────────────────────────────────────────────────────────────

install(TARGETS thermoconsole
    RUNTIME DESTINATION bin
)

# ─────────────────────────────────────────────────────────────────────────────
# Print Config
# ─────────────────────────────────────────────────────────────────────────────

message(STATUS "")
message(STATUS "ThermoConsole Runtime Configuration:")
message(STATUS "  Version:     ${PROJECT_VERSION}")
message(STATUS "  Build Type:  ${CMAKE_BUILD_TYPE}")
message(STATUS "  Platform:    ${CMAKE_SYSTEM_NAME}")
message(STATUS "  Compiler:    ${CMAKE_C_COMPILER_ID}")
message(STATUS "  SDL2:        ${SDL2_VERSION}")
message(STATUS "  Lua:         ${LUA_VERSION}")
message(STATUS "")
