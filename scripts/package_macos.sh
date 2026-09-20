#!/usr/bin/env bash
set -euo pipefail

# Build the macOS app.
#
# Default mode produces a self-contained app bundle suitable for demo packaging:
# Qt frameworks are copied into the app, the Python backend is packaged as a
# command-line executable, and both are ad-hoc signed.
#
# For quick local development builds that load Qt from QT_PREFIX, run with
# BUNDLE_QT=0. That mode intentionally leaves the app unsigned; signing an app
# that loads an external Qt install can make macOS kill it because
# the external Qt frameworks are not signed.

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
if [ -z "${QT_PREFIX:-}" ]; then
    if ! command -v qmake >/dev/null 2>&1; then
        echo "ERROR: Qt qmake was not found. Install Qt 5 or set QT_PREFIX." >&2
        exit 1
    fi
    QT_PREFIX="$(qmake -query QT_INSTALL_PREFIX)"
fi
APP_ARCH="${APP_ARCH:-$(uname -m)}"
BUILD_DIR="${BUILD_DIR:-$ROOT_DIR/build-$APP_ARCH}"
APP_PATH="$BUILD_DIR/MultiMaterialSlicer.app"
DIST_DIR="$ROOT_DIR/dist"
BUNDLE_QT="${BUNDLE_QT:-1}"
BUILD_BACKEND="${BUILD_BACKEND:-1}"
BUILD_STEP_TOOL="${BUILD_STEP_TOOL:-1}"
BACKEND_TOOL="$ROOT_DIR/backend_dist/slice_merge_tool"
STEP_TOOL="$ROOT_DIR/backend_dist/step_to_stl_parts"
export BACKEND_ARCH="${BACKEND_ARCH:-$APP_ARCH}"

if [ "$BUILD_BACKEND" = "1" ]; then
    bash "$ROOT_DIR/scripts/package_backend_macos.sh"
fi
if [ "$BUILD_STEP_TOOL" = "1" ] && [ ! -x "$STEP_TOOL" ]; then
    bash "$ROOT_DIR/scripts/package_step_macos.sh"
fi

cmake -S "$ROOT_DIR" -B "$BUILD_DIR" \
  -DCMAKE_PREFIX_PATH="$QT_PREFIX" \
  -DCMAKE_OSX_ARCHITECTURES="$APP_ARCH"
cmake --build "$BUILD_DIR" --parallel "$(sysctl -n hw.ncpu)"

# Defensive: remove stale loose non-code files that older builds left under
# MacOS/. Executable helpers are allowed there; YAML and Python source stay in
# Resources so code signing stays clean.
rm -f "$APP_PATH/Contents/MacOS/"*.yaml "$APP_PATH/Contents/MacOS/"*.py 2>/dev/null || true

if [ -x "$BACKEND_TOOL" ]; then
    mkdir -p "$APP_PATH/Contents/MacOS"
    cp "$BACKEND_TOOL" "$APP_PATH/Contents/MacOS/slice_merge_tool"
    chmod +x "$APP_PATH/Contents/MacOS/slice_merge_tool"
else
    echo "WARNING: backend tool not found at $BACKEND_TOOL"
    echo "         The app will fall back to Resources/slice_1080p.py in development mode."
fi
if [ -x "$STEP_TOOL" ]; then
    mkdir -p "$APP_PATH/Contents/MacOS"
    cp "$STEP_TOOL" "$APP_PATH/Contents/MacOS/step_to_stl_parts"
    chmod +x "$APP_PATH/Contents/MacOS/step_to_stl_parts"
else
    echo "WARNING: STEP converter not found at $STEP_TOOL"
    echo "         STEP import will require Python + OCP/CadQuery at runtime."
fi

if [ "$BUNDLE_QT" != "1" ]; then
    echo "== Local build (loads system Qt from $QT_PREFIX) =="
    # Do not sign this mode: the executable loads Qt from QT_PREFIX, and the
    # stock Qt 5.12.12 frameworks are unsigned. A signed app can be killed by
    # macOS library validation before main() starts.
    codesign --remove-signature "$APP_PATH/Contents/MacOS/MultiMaterialSlicer" >/dev/null 2>&1 || true
    rm -rf "$APP_PATH/Contents/_CodeSignature"
    echo "Done. Run it with:  open \"$APP_PATH\""
    echo "(For a self-contained bundle to share with machines without Qt, run: BUNDLE_QT=1 $0)"
    exit 0
fi

echo "== Self-contained build (bundling Qt frameworks) =="
STAGE="${STAGE:-$(mktemp -d "${TMPDIR:-/tmp}/mms_pkg.XXXXXX")}"
DAPP="$STAGE/MultiMaterialSlicer.app"
rm -rf "$STAGE"; mkdir -p "$STAGE"
# Use ditto without extended attributes/resource forks. The source tree lives in
# iCloud Drive, which adds file-provider metadata that makes codesign reject the
# bundle with "resource fork, Finder information, or similar detritus not allowed".
ditto --noextattr --norsrc "$APP_PATH" "$DAPP"

"$QT_PREFIX/bin/macdeployqt" "$DAPP" -verbose=1 || true

if [ -x "$BACKEND_TOOL" ]; then
    rm -f "$DAPP/Contents/Resources/slice_merge_tool"
    cp "$BACKEND_TOOL" "$DAPP/Contents/MacOS/slice_merge_tool"
    chmod +x "$DAPP/Contents/MacOS/slice_merge_tool"
fi
if [ -x "$STEP_TOOL" ]; then
    rm -f "$DAPP/Contents/Resources/step_to_stl_parts"
    cp "$STEP_TOOL" "$DAPP/Contents/MacOS/step_to_stl_parts"
    chmod +x "$DAPP/Contents/MacOS/step_to_stl_parts"
fi

# Sign inside-out so the bundled frameworks pass library validation.
xattr -cr "$DAPP" 2>/dev/null || true
find "$DAPP" -name ".DS_Store" -delete 2>/dev/null || true
find "$DAPP" -name "._*" -delete 2>/dev/null || true
find "$DAPP" -exec xattr -d com.apple.FinderInfo {} \; 2>/dev/null || true
find "$DAPP" -exec xattr -d 'com.apple.fileprovider.fpfs#P' {} \; 2>/dev/null || true
find "$DAPP/Contents/Frameworks" -maxdepth 1 -name "*.framework" -print0 2>/dev/null \
    | while IFS= read -r -d '' fw; do codesign --force --sign - "$fw" >/dev/null 2>&1 || true; done
find "$DAPP/Contents/PlugIns" -name "*.dylib" -print0 2>/dev/null \
    | while IFS= read -r -d '' p; do codesign --force --sign - "$p" >/dev/null 2>&1 || true; done
find "$DAPP/Contents/MacOS" -type f -perm -111 -print0 2>/dev/null \
    | while IFS= read -r -d '' exe; do codesign --force --sign - "$exe" >/dev/null 2>&1 || true; done
codesign --force --sign - "$DAPP/Contents/MacOS/MultiMaterialSlicer" >/dev/null 2>&1 || true
codesign --force --sign - "$DAPP" >/dev/null 2>&1 || echo "  (bundle signing incomplete)"

mkdir -p "$DIST_DIR"
ditto -c -k --norsrc --keepParent "$DAPP" "$DIST_DIR/MultiMaterialSlicer-mac-$APP_ARCH.zip"
echo "Self-contained package written to: $DIST_DIR/MultiMaterialSlicer-mac-$APP_ARCH.zip"
