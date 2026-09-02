#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "core/ChainNodePath.hpp"

namespace magda::daw::ui {

/**
 * Export a step sequencer's pattern as MIDI - to the clip clipboard, or to a
 * temp file for a drag out of the app.
 *
 * Addressed by device path, not by a live device (#2313): the pattern and the
 * settings that shape it belong to the model, so an export reads what is saved
 * rather than what happens to be instantiated.
 */

void copyStepSequencerPatternToClipboard(const magda::ChainNodePath& devicePath);
juce::File writeStepSequencerPatternToTempMidiFile(const magda::ChainNodePath& devicePath);
bool handleStepSequencerPatternExternalDrag(const magda::ChainNodePath& devicePath,
                                            juce::Component* exportButton,
                                            juce::Component* dragOwner,
                                            const juce::MouseEvent& event, int dragThresholdPx = 5);

void copyPolyStepSequencerPatternToClipboard(const magda::ChainNodePath& devicePath);
juce::File writePolyStepSequencerPatternToTempMidiFile(const magda::ChainNodePath& devicePath);
bool handlePolyStepSequencerPatternExternalDrag(const magda::ChainNodePath& devicePath,
                                                juce::Component* exportButton,
                                                juce::Component* dragOwner,
                                                const juce::MouseEvent& event,
                                                int dragThresholdPx = 5);

}  // namespace magda::daw::ui
