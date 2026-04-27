cmake_minimum_required(VERSION 3.16)
project(ThermoConsole VERSION 1.0.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# ─────────────────────────────────────────────────────────────────────────────
# Options
# ─────────────────────────────────────────────────────────────────────────────

option(THERMO_BUILD_PI  "Build for Raspberry Pi"               OFF)
option(THERMO_PICO_AUDIO "Use Pico as sound chip via I2C"      OFF)
option(THERMO_PICO_USB   "Use Pico for sound+buttons via USB"  OFF)
option(THERMO_DEBUG      "Enable debug output"                 OFF)

if(THERMO_DEBUG)
    add_definitions(-DTHERMO_DEBUG)
    set(CMAKE_BUILD_TYPE Debug)
else()
    if(NOT CMAKE_BUILD_TYPE)
        set(CMAKE_BUILD_TYPE Release)
    endif()
endif()

# ─────────────────────────────────────────────────────────────────────────────
# Dependencies
# ─────────────────────────────────────────────────────────────────────────────
#
# Preferred path: every C/C++ dep is vendored under <repo>/vendor/ as a pinned
# git clone (see thermo.py setup). When vendored sources are present we build
# SDL / SDL_image / SDL_mixer / Lua from source — no vcpkg / brew / apt needed.
#
# If vendor/ is empty we fall back to find_package CONFIG → pkg-config, so
# distro-package installs still work.

set(_VENDOR_DIR ${CMAKE_SOURCE_DIR}/../vendor)

# ── SDL2 / SDL2_image / SDL2_mixer ────────────────────────────────────────────

set(_SDL_VENDORED FALSE)

if(EXISTS ${_VENDOR_DIR}/SDL/include/SDL.h AND
   EXISTS ${_VENDOR_DIR}/SDL_image/include/SDL_image.h AND
   EXISTS ${_VENDOR_DIR}/SDL_mixer/include/SDL_mixer.h)
    # Build all three SDL libs from vendored sources. Static link, no tests,
    # only format/codec loaders we actually use (PNG for SDL_image; OGG/WAV
    # for SDL_mixer) so we don't drag libjpeg/libwebp/libflac into the build.
    # BUILD_SHARED_LIBS=OFF is the global override SDL_image and SDL_mixer
    # key off to set INTERFACE_SDL2_SHARED=FALSE on their exported targets.
    set(BUILD_SHARED_LIBS  OFF CACHE BOOL "" FORCE)
    set(SDL_SHARED         OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC         ON  CACHE BOOL "" FORCE)
    set(SDL_TEST           OFF CACHE BOOL "" FORCE)

    set(SDL2IMAGE_INSTALL  OFF CACHE BOOL "" FORCE)
    set(SDL2IMAGE_SAMPLES  OFF CACHE BOOL "" FORCE)
    set(SDL2IMAGE_PNG      ON  CACHE BOOL "" FORCE)
    set(SDL2IMAGE_JPG      OFF CACHE BOOL "" FORCE)
    set(SDL2IMAGE_WEBP     OFF CACHE BOOL "" FORCE)
    set(SDL2IMAGE_AVIF     OFF CACHE BOOL "" FORCE)
    set(SDL2IMAGE_TIF      OFF CACHE BOOL "" FORCE)
    set(SDL2IMAGE_BMP      ON  CACHE BOOL "" FORCE)

    set(SDL2MIXER_INSTALL  OFF CACHE BOOL "" FORCE)
    set(SDL2MIXER_SAMPLES  OFF CACHE BOOL "" FORCE)
    set(SDL2MIXER_FLAC     OFF CACHE BOOL "" FORCE)
    set(SDL2MIXER_MOD      OFF CACHE BOOL "" FORCE)
    set(SDL2MIXER_MP3      OFF CACHE BOOL "" FORCE)
    set(SDL2MIXER_MIDI     OFF CACHE BOOL "" FORCE)
    set(SDL2MIXER_OPUS     OFF CACHE BOOL "" FORCE)
    set(SDL2MIXER_WAVPACK  OFF CACHE BOOL "" FORCE)
    set(SDL2MIXER_OGG      ON  CACHE BOOL "" FORCE)
    set(SDL2MIXER_WAV      ON  CACHE BOOL "" FORCE)

    add_subdirectory(${_VENDOR_DIR}/SDL
        ${CMAKE_BINARY_DIR}/_vendor_sdl       EXCLUDE_FROM_ALL)
    add_subdirectory(${_VENDOR_DIR}/SDL_image
        ${CMAKE_BINARY_DIR}/_vendor_sdl_image EXCLUDE_FROM_ALL)
    add_subdirectory(${_VENDOR_DIR}/SDL_mixer
        ${CMAKE_BINARY_DIR}/_vendor_sdl_mixer EXCLUDE_FROM_ALL)
    set(_SDL_VENDORED TRUE)
else()
    find_package(SDL2       CONFIG QUIET)
    find_package(SDL2_image CONFIG QUIET)
    find_package(SDL2_mixer CONFIG QUIET)

    if(NOT SDL2_FOUND AND NOT TARGET SDL2::SDL2 AND NOT TARGET SDL2::SDL2-static)
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(SDL2       REQUIRED sdl2)
        pkg_check_modules(SDL2_IMAGE REQUIRED SDL2_image)
        pkg_check_modules(SDL2_MIXER REQUIRED SDL2_mixer)
        set(_USE_PKGCONFIG TRUE)
    endif()
endif()

# ── Lua ───────────────────────────────────────────────────────────────────────
# Prefer the vendored Lua sources (cmake/lua.cmake builds them as a STATIC
# target called `lua` and exports FindLua-compatible variables). Otherwise
# fall back to CMake's built-in FindLua → pkg-config.

set(_LUA_VENDORED FALSE)

if(EXISTS ${_VENDOR_DIR}/lua/src/lua.h OR EXISTS ${_VENDOR_DIR}/lua/lua.h)
    include(${CMAKE_SOURCE_DIR}/../cmake/lua.cmake)
    set(_LUA_VENDORED TRUE)
else()
    find_package(Lua 5.4 QUIET)
    if(NOT LUA_FOUND)
        find_package(Lua QUIET)   # any version
    endif()

    if(NOT LUA_FOUND)
        if(_USE_PKGCONFIG OR (NOT WIN32))
            if(NOT COMMAND pkg_check_modules)
                find_package(PkgConfig REQUIRED)
            endif()
            pkg_check_modules(LUA REQUIRED lua5.4)
            set(LUA_INCLUDE_DIR  ${LUA_INCLUDE_DIRS})
            set(LUA_LIBRARIES    ${LUA_LIBRARIES})
            set(LUA_FOUND TRUE)
        else()
            message(FATAL_ERROR
"Lua 5.4 not found and vendored sources are missing.
  Run:  python thermo.py setup
  (or install liblua5.4-dev / brew install lua)")
        endif()
    endif()
endif()

# ── Math library ──────────────────────────────────────────────────────────────
# libm is a separate link on Linux/Mac; on Windows it's part of the CRT.

if(UNIX)
    find_library(M_LIB m REQUIRED)
endif()

# ─────────────────────────────────────────────────────────────────────────────
# Sources
# ─────────────────────────────────────────────────────────────────────────────

set(THERMO_SOURCES
    src/main.c
    src/graphics.c
    src/input.c
    src/rom.c
    src/lua_api.c
    src/script.c
)

# system_api.c is the hardware system menu — it uses dirent.h, unlink/symlink,
# /sys/class/thermal, /home/pi paths, and amixer. It only makes sense on the
# actual Pi console, so skip it on Windows/Mac desktop builds. Nothing else
# in the runtime currently calls into it, so excluding it doesn't break links.
if(UNIX)
    list(APPEND THERMO_SOURCES src/system_api.c)
endif()

if(THERMO_PICO_USB)
    message(STATUS "Audio: Pico via USB serial (sound + buttons)")
    list(APPEND THERMO_SOURCES src/pico_usb.c)
    add_definitions(-DTHERMO_PICO_USB)
elseif(THERMO_PICO_AUDIO)
    message(STATUS "Audio: Pico sound chip (I2C)")
    list(APPEND THERMO_SOURCES src/audio_pico.c)
    add_definitions(-DTHERMO_PICO_AUDIO)
else()
    message(STATUS "Audio: SDL_mixer")
    list(APPEND THERMO_SOURCES src/audio.c src/chiptune.c)
endif()

if(THERMO_BUILD_PI)
    message(STATUS "Platform: Raspberry Pi")
    list(APPEND THERMO_SOURCES src/platform/platform_pi.c)
    set(THERMO_PLATFORM_DEF THERMO_PLATFORM_PI)
else()
    message(STATUS "Platform: Desktop PC")
    list(APPEND THERMO_SOURCES src/platform/platform_pc.c)
    set(THERMO_PLATFORM_DEF THERMO_PLATFORM_PC)
endif()

# ─────────────────────────────────────────────────────────────────────────────
# Executable
# ─────────────────────────────────────────────────────────────────────────────

add_executable(thermoconsole ${THERMO_SOURCES})

target_include_directories(thermoconsole PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${LUA_INCLUDE_DIR}
)

target_compile_definitions(thermoconsole PRIVATE ${THERMO_PLATFORM_DEF})

# ─────────────────────────────────────────────────────────────────────────────
# Linking
# ─────────────────────────────────────────────────────────────────────────────

if(_USE_PKGCONFIG)
    # pkg-config variables (Linux / Mac distro packages)
    target_include_directories(thermoconsole PRIVATE
        ${SDL2_INCLUDE_DIRS}
        ${SDL2_IMAGE_INCLUDE_DIRS}
        ${SDL2_MIXER_INCLUDE_DIRS}
    )
    target_link_libraries(thermoconsole PRIVATE
        ${SDL2_LIBRARIES}
        ${SDL2_IMAGE_LIBRARIES}
        ${SDL2_MIXER_LIBRARIES}
    )
    target_compile_options(thermoconsole PRIVATE
        ${SDL2_CFLAGS_OTHER}
    )
else()
    # CONFIG / vcpkg import targets
    target_link_libraries(thermoconsole PRIVATE
        $<TARGET_NAME_IF_EXISTS:SDL2::SDL2main>
        $<IF:$<TARGET_EXISTS:SDL2::SDL2>,SDL2::SDL2,SDL2::SDL2-static>
    )
    if(TARGET SDL2_image::SDL2_image OR TARGET SDL2_image::SDL2_image-static)
        target_link_libraries(thermoconsole PRIVATE
            $<IF:$<TARGET_EXISTS:SDL2_image::SDL2_image>,SDL2_image::SDL2_image,SDL2_image::SDL2_image-static>
        )
    endif()
    if(TARGET SDL2_mixer::SDL2_mixer OR TARGET SDL2_mixer::SDL2_mixer-static)
        target_link_libraries(thermoconsole PRIVATE
            $<IF:$<TARGET_EXISTS:SDL2_mixer::SDL2_mixer>,SDL2_mixer::SDL2_mixer,SDL2_mixer::SDL2_mixer-static>
        )
    endif()
endif()

# Lua — FindLua.cmake always sets LUA_LIBRARIES / LUA_INCLUDE_DIR
target_link_libraries(thermoconsole PRIVATE ${LUA_LIBRARIES})

# libm (Linux / Mac only)
if(UNIX AND M_LIB)
    target_link_libraries(thermoconsole PRIVATE ${M_LIB})
endif()

# ─────────────────────────────────────────────────────────────────────────────
# Compiler flags
# ─────────────────────────────────────────────────────────────────────────────

if(MSVC)
    target_compile_options(thermoconsole PRIVATE /W3 /wd4244 /wd4267)
    target_compile_definitions(thermoconsole PRIVATE _CRT_SECURE_NO_WARNINGS)
    set_target_properties(thermoconsole PROPERTIES LINK_FLAGS "/SUBSYSTEM:CONSOLE")

    # Vendored builds are static — nothing to copy. For a non-vendored CONFIG
    # build (e.g. a pre-installed SDL2 with DLLs), copy the runtime DLLs next
    # to the exe so double-click launch works.
    if(NOT _SDL_VENDORED)
        foreach(_tgt SDL2::SDL2 SDL2_image::SDL2_image SDL2_mixer::SDL2_mixer)
            if(TARGET ${_tgt})
                add_custom_command(TARGET thermoconsole POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "$<TARGET_FILE:${_tgt}>" "$<TARGET_FILE_DIR:thermoconsole>"
                    COMMENT "Copying $<TARGET_FILE_NAME:${_tgt}>"
                )
            endif()
        endforeach()
    endif()
else()
    target_compile_options(thermoconsole PRIVATE
        -Wall -Wextra -Wpedantic -Wno-unused-parameter)
endif()

# ─────────────────────────────────────────────────────────────────────────────
# Install
# ─────────────────────────────────────────────────────────────────────────────

install(TARGETS thermoconsole RUNTIME DESTINATION bin)

# ─────────────────────────────────────────────────────────────────────────────
# Summary
# ─────────────────────────────────────────────────────────────────────────────

if(_SDL_VENDORED)
    set(_SDL_MODE "vendored (static)")
elseif(_USE_PKGCONFIG)
    set(_SDL_MODE "pkg-config")
else()
    set(_SDL_MODE "find_package CONFIG")
endif()

if(_LUA_VENDORED)
    set(_LUA_MODE "vendored (${LUA_VERSION_STRING})")
else()
    set(_LUA_MODE "system (${LUA_VERSION_STRING})")
endif()

message(STATUS "")
message(STATUS "ThermoConsole Runtime Configuration:")
message(STATUS "  Version:     ${PROJECT_VERSION}")
message(STATUS "  Build Type:  ${CMAKE_BUILD_TYPE}")
message(STATUS "  Platform:    ${CMAKE_SYSTEM_NAME}")
message(STATUS "  Compiler:    ${CMAKE_C_COMPILER_ID}")
message(STATUS "  SDL:         ${_SDL_MODE}")
message(STATUS "  Lua:         ${_LUA_MODE}")
message(STATUS "")