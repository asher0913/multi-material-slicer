#!/usr/bin/env bash
# Build the STEP assembly converter (step_to_stl_parts) with PyInstaller.
#
# This helper contains the OpenCascade/OCP runtime through the CadQuery wheel,
# so the GUI can import .step/.stp files without requiring a system Python
# environment at demo/customer runtime.
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PYTHON="${PYTHON:-python3}"
VENV_DIR="${STEP_VENV_DIR:-$ROOT_DIR/backend_build/step_venv_native}"

cd "$ROOT_DIR"

"$PYTHON" -m venv "$VENV_DIR"
VENV_PYTHON="$VENV_DIR/bin/python"
"$VENV_PYTHON" -m pip install --upgrade pip >/dev/null
"$VENV_PYTHON" -m pip install "pyinstaller>=6,<7" -r requirements-step.txt >/dev/null
"$VENV_PYTHON" -m PyInstaller --clean --onefile --name step_to_stl_parts \
    --hidden-import OCP.BRepMesh \
    --hidden-import OCP.IFSelect \
    --hidden-import OCP.STEPControl \
    --hidden-import OCP.StlAPI \
    --hidden-import OCP.TopAbs \
    --hidden-import OCP.TopExp \
    --collect-submodules OCP \
    --collect-binaries OCP \
    --distpath "$ROOT_DIR/backend_dist" \
    --workpath "$ROOT_DIR/backend_build/step_pyinstaller_work" \
    --specpath "$ROOT_DIR/backend_build/step_pyinstaller_spec" \
    tools/step_to_stl_parts.py

echo "STEP converter written to: $ROOT_DIR/backend_dist/step_to_stl_parts"
file "$ROOT_DIR/backend_dist/step_to_stl_parts"
