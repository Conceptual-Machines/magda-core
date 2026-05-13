#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace magda::daw::audio {
class StepSequencerPlugin;
}

namespace magda::daw::ui {

void copyStepSequencerPatternToClipboard(daw::audio::StepSequencerPlugin& plugin);
juce::File writeStepSequencerPatternToTempMidiFile(daw::audio::StepSequencerPlugin& plugin);
bool handleStepSequencerPatternExternalDrag(daw::audio::StepSequencerPlugin* plugin,
                                            juce::Component* exportButton,
                                            juce::Component* dragOwner,
                                            const juce::MouseEvent& event, int dragThresholdPx = 5);

}  // namespace magda::daw::ui
