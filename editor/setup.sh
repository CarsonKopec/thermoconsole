#!/usr/bin/env bash
# Thin shim — the real orchestrator is thermo.py at the repo root.
# This script exists so long-time muscle memory (./editor/setup.sh) still works.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "${REPO_ROOT}"

PY="${PYTHON:-python3}"
command -v "${PY}" >/dev/null 2>&1 || PY=python

echo "Delegating to thermo.py (vendors deps, configures, builds the editor)..."
"${PY}" thermo.py setup --only imgui,SDL,SDL_image
"${PY}" thermo.py build editor
