#!/usr/bin/env bash
# Regenerate C++ headers from the Faust DSP sources under
# magda/daw/audio/faust_dsp/*.dsp.
#
# Only needed after editing a .dsp file — the generated .hpp is checked in, so
# normal builds do not require `faust` on the PATH.
#
# Install faust: `brew install faust` (macOS) or see https://faust.grame.fr.
#
# Keep in sync with the class name used in FaustPlugin.cpp (MagdaDriveDsp).

set -euo pipefail

if ! command -v faust >/dev/null 2>&1; then
    echo "error: faust CLI not found on PATH. Install with: brew install faust" >&2
    exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
DSP_DIR="${REPO_ROOT}/magda/daw/audio/faust_dsp"

cd "${DSP_DIR}"

# -cn   : C++ class name
# -scn  : parent class (::dsp from our vendored runtime header)
# -lang : target language
faust -cn MagdaDriveDsp \
      -scn ::dsp \
      -lang cpp \
      -o magda_drive_generated.hpp \
      magda_drive.dsp

echo "Regenerated magda_drive_generated.hpp from magda_drive.dsp"
echo "Remember to prepend the AUTO-GENERATED banner (the faust CLI strips it on regen)."
