// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "core/ClipInfo.hpp"

namespace magda::daw::ui {

/**
 * @brief Draw a MIDI clip's notes (loop-tiled, brightness by velocity) into noteArea, whose
 *        width represents clipLengthInBeats of the clip from midiOffset.
 *
 * Shared by the arrangement ClipComponent and the panel's drag ghosts (#3342).
 */
void paintClipMidiNotes(juce::Graphics& g, const ClipInfo& clip, juce::Rectangle<int> noteArea,
                        juce::Colour noteColour, double clipLengthInBeats, double midiOffset);

}  // namespace magda::daw::ui
