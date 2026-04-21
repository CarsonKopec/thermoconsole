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
# Windows (vcpkg):
#   vcpkg install sdl2 sdl2-image sdl2-mixer lua
#   cmake .. -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake
#
# Linux:
#   apt install libsdl2-dev libsdl2-image-dev libsdl2-mixer-dev liblua5.4-dev
#
# Mac:
#   brew install sdl2 sdl2_image sdl2_mixer lua

# ── SDL2 ──────────────────────────────────────────────────────────────────────

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

# ── Lua ───────────────────────────────────────────────────────────────────────
# CMake's built-in FindLua works on all platforms (Windows, Linux, Mac).
# We ask for 5.4; it also finds 5.3 as a fallback if 5.4 isn't available.

find_package(Lua 5.4 QUIET)
if(NOT LUA_FOUND)
    find_package(Lua QUIET)   # any version
endif()

if(NOT LUA_FOUND)
    if(_USE_PKGCONFIG OR (NOT WIN32))
        # Already have PkgConfig, or on a Unix system — try it
        if(NOT COMMAND pkg_check_modules)
            find_package(PkgConfig REQUIRED)
        endif()
        pkg_check_modules(LUA REQUIRED lua5.4)
        set(LUA_INCLUDE_DIR  ${LUA_INCLUDE_DIRS})
        set(LUA_LIBRARIES    ${LUA_LIBRARIES})
        set(LUA_FOUND TRUE)
    else()
        message(FATAL_ERROR
"Lua 5.4 not found.
  Windows:  vcpkg install lua
            then add -DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake
  Linux:    apt install liblua5.4-dev
  Mac:      brew install lua")
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
    src/system_api.c
)

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
    list(APPEND THERMO_SOURCES src/audio.c)
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

    # Copy SDL2 / SDL2_image / SDL2_mixer / Lua DLLs next to exe after build
    set(_VCPKG_BIN "${CMAKE_SOURCE_DIR}/../../vcpkg/installed/x64-windows/bin")
    if(EXISTS "${_VCPKG_BIN}")
        foreach(_dll SDL2 SDL2_image SDL2_mixer lua54)
            set(_src "${_VCPKG_BIN}/${_dll}.dll")
            if(EXISTS "${_src}")
                add_custom_command(TARGET thermoconsole POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "${_src}" "$<TARGET_FILE_DIR:thermoconsole>"
                    COMMENT "Copying ${_dll}.dll"
                )
            endif()
        endforeach()
    else()
        message(STATUS
            "DLL auto-copy skipped — copy SDL2.dll, SDL2_image.dll, "
            "SDL2_mixer.dll, lua54.dll next to thermoconsole.exe manually.")
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

message(STATUS "")
message(STATUS "ThermoConsole Runtime Configuration:")
message(STATUS "  Version:     ${PROJECT_VERSION}")
message(STATUS "  Build Type:  ${CMAKE_BUILD_TYPE}")
message(STATUS "  Platform:    ${CMAKE_SYSTEM_NAME}")
message(STATUS "  Compiler:    ${CMAKE_C_COMPILER_ID}")
message(STATUS "  Lua:         ${LUA_VERSION_STRING}")
message(STATUS "")