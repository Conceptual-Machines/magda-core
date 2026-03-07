#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <cmath>

#include "../../themes/DarkTheme.hpp"
#include "core/ClipInfo.hpp"

namespace magda {

static constexpr int PHASE_HIT_TOLERANCE = 8;

/** Paint the loop phase marker (yellow vertical line) on a MIDI grid.
 *  Returns true if something was drawn. */
inline bool paintPhaseMarker(juce::Graphics& g, const ClipInfo* clip, int phaseX, int height,
                             bool nearPhaseMarker) {
    if (!clip || !clip->loopEnabled) {
        return false;
    }

    if (clip->midiOffset > 0.0) {
        g.setColour(DarkTheme::getColour(DarkTheme::OFFSET_MARKER));
        g.fillRect(phaseX - 1, 0, 2, height);
        return true;
    }

    if (nearPhaseMarker) {
        g.setColour(DarkTheme::getColour(DarkTheme::OFFSET_MARKER).withAlpha(0.4f));
        g.fillRect(phaseX - 1, 0, 2, height);
        return true;
    }

    return false;
}

/** Check if the mouse is near the phase marker position (when phase is at 0).
 *  Returns true if within tolerance. */
inline bool isNearPhaseMarker(int mouseX, int phaseX, const ClipInfo* clip) {
    if (!clip || !clip->loopEnabled || clip->midiOffset != 0.0) {
        return false;
    }
    return std::abs(mouseX - phaseX) <= PHASE_HIT_TOLERANCE;
}

}  // namespace magda
