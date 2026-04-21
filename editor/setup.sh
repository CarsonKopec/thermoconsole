#!/usr/bin/env bash
# ThermoConsole Editor — quick-setup script
set -euo pipefail
cd "$(dirname "$0")"

echo "=== ThermoConsole Editor Setup ==="

# 1. Clone ImGui (master — no docking branch needed)
if [ ! -f vendor/imgui/imgui.h ]; then
    echo "Cloning Dear ImGui..."
    mkdir -p vendor
    git clone --depth 1 https://github.com/ocornut/imgui vendor/imgui
else
    echo "ImGui already present."
fi

# 2. Configure + build
mkdir -p build
cd build

# -DCMAKE_BUILD_TYPE is picked up if not already set
cmake -DCMAKE_BUILD_TYPE=Release ..

# Portable parallel build (works with Make, Ninja, MSBuild on Windows)
cmake --build . --config Release --parallel

echo ""
echo "Build complete: build/thermo_editor"
echo "Run with:  ./build/thermo_editor ../../games/space_dodge"
