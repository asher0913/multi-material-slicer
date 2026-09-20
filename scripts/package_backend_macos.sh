#!/usr/bin/env bash
# Build the backend command-line tool (slice_merge_tool) from slice_1080p.py
# using PyInstaller, so the App does not depend on a Python install at runtime.
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BACKEND_ARCH="${BACKEND_ARCH:-$(uname -m)}"
if [ "$BACKEND_ARCH" = "x86_64" ]; then
    PYTHON="${PYTHON:-/usr/bin/python3}"
    ARCH_CMD=(arch -x86_64)
else
    PYTHON="${PYTHON:-python3}"
    ARCH_CMD=()
fi
VENV_DIR="${VENV_DIR:-$ROOT_DIR/backend_build/venv_$BACKEND_ARCH}"

cd "$ROOT_DIR"

"${ARCH_CMD[@]}" "$PYTHON" -m venv "$VENV_DIR"
VENV_PYTHON="$VENV_DIR/bin/python"
if ! "${ARCH_CMD[@]}" "$VENV_PYTHON" -m pip --version >/dev/null 2>&1; then
    echo "Existing backend venv has no working pip; recreating $VENV_DIR"
    rm -rf "$VENV_DIR"
    "${ARCH_CMD[@]}" "$PYTHON" -m venv "$VENV_DIR"
    VENV_PYTHON="$VENV_DIR/bin/python"
fi
"${ARCH_CMD[@]}" "$VENV_PYTHON" -m pip install --upgrade pip >/dev/null
"${ARCH_CMD[@]}" "$VENV_PYTHON" -m pip install "pyinstaller>=6,<7" >/dev/null
"${ARCH_CMD[@]}" "$VENV_PYTHON" -m pip install -r requirements.txt >/dev/null
"${ARCH_CMD[@]}" "$VENV_PYTHON" -m PyInstaller --clean --onefile --target-architecture "$BACKEND_ARCH" --name slice_merge_tool \
    --hidden-import yaml \
    --collect-submodules yaml \
    --distpath "$ROOT_DIR/backend_dist" \
    --workpath "$ROOT_DIR/backend_build" \
    --specpath "$ROOT_DIR/backend_build" \
    slice_1080p.py

echo "Backend tool written to: $ROOT_DIR/backend_dist/slice_merge_tool"
file "$ROOT_DIR/backend_dist/slice_merge_tool"
echo "It still supports:  slice_merge_tool --config <config.yaml> --output <merged_dir>"
