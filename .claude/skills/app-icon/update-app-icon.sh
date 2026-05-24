#!/usr/bin/env bash
#
# Update the MAGDA application icon from a source PNG.
#
# assets/app_icon.png is the single source JUCE turns into the embedded
# icon (ICON_BIG / ICON_SMALL in magda/daw/CMakeLists.txt): Icon.icns on
# macOS and the .ico on Windows, both generated at configure time. The
# Linux release (release.yml) copies app_icon.png directly. Updating the
# icon therefore means replacing app_icon.png and rebuilding.
#
# Usage:
#   update-app-icon.sh <source.png>
#
# <source.png> should be a square PNG (1024x1024 by convention). Keep the
# mark in assets/ as an iteration source (Bold-M, Bold-M2, ... Bold-M5)
# and pass that path, e.g.:
#   update-app-icon.sh assets/Bold-M6.png

set -euo pipefail

usage() {
    echo "Usage: $(basename "$0") <source-png>" >&2
    echo "  Replaces assets/app_icon.png with <source-png> (a 1024x1024 square PNG)." >&2
}

[ $# -eq 1 ] || { usage; exit 1; }
SRC="$1"

ROOT="$(git rev-parse --show-toplevel 2>/dev/null)" \
    || { echo "ERROR: not inside the magda-core git repo." >&2; exit 1; }
DEST="$ROOT/assets/app_icon.png"

[ -f "$SRC" ] || { echo "ERROR: source not found: $SRC" >&2; exit 1; }

# Best-effort dimension check (sips is macOS-only; skip elsewhere).
if command -v sips >/dev/null 2>&1; then
    W="$(sips -g pixelWidth  "$SRC" 2>/dev/null | awk '/pixelWidth/{print $2}')"
    H="$(sips -g pixelHeight "$SRC" 2>/dev/null | awk '/pixelHeight/{print $2}')"
    if [ -n "${W:-}" ] && [ -n "${H:-}" ]; then
        if [ "$W" != "$H" ]; then
            echo "ERROR: source is ${W}x${H}, not square. App icons must be square." >&2
            exit 1
        fi
        [ "$W" = "1024" ] || echo "WARNING: source is ${W}x${H}; convention is 1024x1024." >&2
    fi
fi

cp "$SRC" "$DEST"
echo "Updated assets/app_icon.png <- $SRC"
command -v md5 >/dev/null 2>&1 && echo "md5: $(md5 -q "$DEST")"

cat <<'EOF'

Next steps:
  1. Reconfigure and rebuild so JUCE regenerates the embedded icon:
       make configure && make debug
     CMake regenerates Icon.icns (macOS) and the .ico (Windows) from this PNG
     at configure time, and the build copies the fresh icns into the app
     bundle. A plain `make debug` will NOT pick up the change -- ninja does not
     track app_icon.png, so you must reconfigure. The Linux release copies
     app_icon.png directly.
  2. Commit app_icon.png together with the new source mark, e.g.:
       git add assets/app_icon.png assets/Bold-M6.png
EOF
