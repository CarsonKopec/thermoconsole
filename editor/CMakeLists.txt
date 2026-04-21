cmake_minimum_required(VERSION 3.16)
project(thermoconsole_editor VERSION 1.0.1 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Default to Release if the user didn't pass -DCMAKE_BUILD_TYPE
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS
        "Debug" "Release" "MinSizeRel" "RelWithDebInfo")
endif()

# ─── ImGui (vendored) ─────────────────────────────────────────────────────────

set(IMGUI_DIR ${CMAKE_SOURCE_DIR}/vendor/imgui)

if(NOT EXISTS ${IMGUI_DIR}/imgui.h)
    message(FATAL_ERROR
        "ImGui not found at ${IMGUI_DIR}\n"
        "Run:  git clone --depth 1 https://github.com/ocornut/imgui vendor/imgui")
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
#
# Windows (vcpkg):
#   cmake .. -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake
#
# Linux:
#   apt install libsdl2-dev libsdl2-image-dev
#
# Mac:
#   brew install sdl2 sdl2_image

# Try CONFIG mode first (vcpkg, Homebrew, or SDL2's own cmake install).
# If it fails we fall back to pkg-config (Linux / Mac distro packages).

find_package(SDL2       CONFIG QUIET)
find_package(SDL2_image CONFIG QUIET)

if(NOT SDL2_FOUND AND NOT TARGET SDL2::SDL2 AND NOT TARGET SDL2::SDL2-static)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(SDL2       REQUIRED sdl2)
    pkg_check_modules(SDL2_IMAGE REQUIRED SDL2_image)
    set(_USE_PKGCONFIG TRUE)
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

message(STATUS "ThermoConsole Editor ${PROJECT_VERSION}")
message(STATUS "  Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "  ImGui:      ${IMGUI_DIR}")
