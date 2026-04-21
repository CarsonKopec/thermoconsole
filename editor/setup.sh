#!/usr/bin/env bash
# ThermoConsole Editor — quick-setup script
set -euo pipefail
cd "$(dirname "$0")"

echo "=== ThermoConsole Editor Setup ==="

# 1. Clone ImGui (docking branch — required for dockable/tear-off panels).
# If a stale master-branch clone is present, rewire it to docking so we don't
# quietly build against the wrong branch.
IMGUI_URL="https://github.com/ocornut/imgui"
IMGUI_BRANCH="docking"

if [ ! -f vendor/imgui/imgui.h ]; then
    echo "Cloning Dear ImGui (branch: ${IMGUI_BRANCH})..."
    mkdir -p vendor
    git clone --depth 1 -b "${IMGUI_BRANCH}" "${IMGUI_URL}" vendor/imgui
else
    current_branch="$(git -C vendor/imgui rev-parse --abbrev-ref HEAD 2>/dev/null || echo unknown)"
    if [ "${current_branch}" != "${IMGUI_BRANCH}" ]; then
        echo "ImGui clone is on '${current_branch}', switching to '${IMGUI_BRANCH}'..."
        # --depth 1 clones don't carry the other branch; refetch just the branch we need.
        git -C vendor/imgui fetch --depth 1 origin "${IMGUI_BRANCH}:${IMGUI_BRANCH}" \
            || { echo "Reclone required — removing stale vendor/imgui."; \
                 rm -rf vendor/imgui; \
                 git clone --depth 1 -b "${IMGUI_BRANCH}" "${IMGUI_URL}" vendor/imgui; }
        git -C vendor/imgui checkout "${IMGUI_BRANCH}" 2>/dev/null || true
    else
        echo "ImGui (${IMGUI_BRANCH}) already present."
    fi
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
