cmake_minimum_required(VERSION 3.16)
project(thermoconsole_editor VERSION 1.1.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Default to Release if the user didn't pass -DCMAKE_BUILD_TYPE
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS
        "Debug" "Release" "MinSizeRel" "RelWithDebInfo")
endif()

# ─── Vendored dependencies ────────────────────────────────────────────────────
#
# Preferred path: every C/C++ dep lives under <repo>/vendor/ as a pinned git
# clone (see thermo.py setup). When that layout is present we build SDL and
# SDL_image from source via add_subdirectory — no vcpkg / brew / apt needed.
#
# If vendor/ is empty (e.g. a dev that bypassed the bootstrap), we fall back
# to find_package CONFIG → pkg-config, matching the old behavior.

set(_VENDOR_DIR ${CMAKE_SOURCE_DIR}/../vendor)

# ─── ImGui (vendored) ─────────────────────────────────────────────────────────

set(IMGUI_DIR ${_VENDOR_DIR}/imgui)

if(NOT EXISTS ${IMGUI_DIR}/imgui.h)
    message(FATAL_ERROR
        "ImGui not found at ${IMGUI_DIR}\n"
        "Run:  python thermo.py setup")
endif()

set(IMGUI_SOURCES
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/imgui_demo.cpp
    ${IMGUI_DIR}/backends/imgui_impl_sdl2.cpp
    ${IMGUI_DIR}/backends/imgui_impl_sdlrenderer2.cpp
)

# ─── SDL2 + SDL2_image ────────────────────────────────────────────────────────

set(_SDL_VENDORED FALSE)

if(EXISTS ${_VENDOR_DIR}/SDL/include/SDL.h AND
   EXISTS ${_VENDOR_DIR}/SDL_image/SDL_image.h)
    # Build the SDL deps from the vendored sources. Static link, no tests, and
    # only PNG loader in SDL_image so we don't need libjpeg/libwebp/libavif.
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

    add_subdirectory(${_VENDOR_DIR}/SDL
        ${CMAKE_BINARY_DIR}/_vendor_sdl       EXCLUDE_FROM_ALL)
    add_subdirectory(${_VENDOR_DIR}/SDL_image
        ${CMAKE_BINARY_DIR}/_vendor_sdl_image EXCLUDE_FROM_ALL)
    set(_SDL_VENDORED TRUE)
else()
    find_package(SDL2       CONFIG QUIET)
    find_package(SDL2_image CONFIG QUIET)

    if(NOT SDL2_FOUND AND NOT TARGET SDL2::SDL2 AND NOT TARGET SDL2::SDL2-static)
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(SDL2       REQUIRED sdl2)
        pkg_check_modules(SDL2_IMAGE REQUIRED SDL2_image)
        set(_USE_PKGCONFIG TRUE)
    endif()
endif()

# ─── Editor sources ───────────────────────────────────────────────────────────

set(EDITOR_SOURCES
    main.cpp
    ThermoEditor.cpp
    panels/CodeEditor.cpp
    panels/SpriteEditor.cpp
    panels/FileBrowser.cpp
    panels/GamePreview.cpp
    panels/Console.cpp
    panels/ManifestEditor.cpp
)

set(EDITOR_HEADERS
    ThermoEditor.h
    panels/CodeEditor.h
    panels/SpriteEditor.h
    panels/FileBrowser.h
    panels/GamePreview.h
    panels/Console.h
    panels/ManifestEditor.h
)

add_executable(thermo_editor ${EDITOR_SOURCES} ${EDITOR_HEADERS} ${IMGUI_SOURCES})

target_include_directories(thermo_editor PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
)

# ─── Link SDL2 ────────────────────────────────────────────────────────────────

if(_USE_PKGCONFIG)
    target_include_directories(thermo_editor PRIVATE
        ${SDL2_INCLUDE_DIRS} ${SDL2_IMAGE_INCLUDE_DIRS})
    target_link_libraries(thermo_editor PRIVATE
        ${SDL2_LIBRARIES} ${SDL2_IMAGE_LIBRARIES})
else()
    target_link_libraries(thermo_editor PRIVATE
        $<TARGET_NAME_IF_EXISTS:SDL2::SDL2main>
        $<IF:$<TARGET_EXISTS:SDL2::SDL2>,SDL2::SDL2,SDL2::SDL2-static>
    )
    if(TARGET SDL2_image::SDL2_image OR TARGET SDL2_image::SDL2_image-static)
        target_link_libraries(thermo_editor PRIVATE
            $<IF:$<TARGET_EXISTS:SDL2_image::SDL2_image>,SDL2_image::SDL2_image,SDL2_image::SDL2_image-static>
        )
    endif()
endif()

# ─── Platform flags ───────────────────────────────────────────────────────────

if(UNIX AND NOT APPLE)
    target_link_libraries(thermo_editor PRIVATE pthread dl)
endif()

if(MSVC)
    target_compile_options(thermo_editor PRIVATE /W3 /wd4244 /wd4267 /utf-8)
    set_target_properties(thermo_editor PROPERTIES LINK_FLAGS "/SUBSYSTEM:CONSOLE")
    target_compile_definitions(thermo_editor PRIVATE
        _CRT_SECURE_NO_WARNINGS
        NOMINMAX
        WIN32_LEAN_AND_MEAN)
else()
    target_compile_options(thermo_editor PRIVATE
        -Wall -Wextra
        -Wno-unused-parameter
        -Wno-missing-field-initializers)
endif()

# ─── Install ──────────────────────────────────────────────────────────────────

install(TARGETS thermo_editor DESTINATION bin)

if(_SDL_VENDORED)
    set(_SDL_MODE "vendored (static)")
elseif(_USE_PKGCONFIG)
    set(_SDL_MODE "pkg-config")
else()
    set(_SDL_MODE "find_package CONFIG")
endif()

message(STATUS "ThermoConsole Editor ${PROJECT_VERSION}")
message(STATUS "  Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "  ImGui:      ${IMGUI_DIR}")
message(STATUS "  SDL:        ${_SDL_MODE}")
