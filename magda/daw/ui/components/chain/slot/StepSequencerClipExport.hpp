#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace magda::daw::audio {
class PolyStepSequencerPlugin;
class StepSequencerPlugin;
}  // namespace magda::daw::audio

namespace magda::daw::ui {

void copyStepSequencerPatternToClipboard(daw::audio::StepSequencerPlugin& plugin);
juce::File writeStepSequencerPatternToTempMidiFile(daw::audio::StepSequencerPlugin& plugin);
bool handleStepSequencerPatternExternalDrag(daw::audio::StepSequencerPlugin* plugin,
                                            juce::Component* exportButton,
                                            juce::Component* dragOwner,
                                            const juce::MouseEvent& event, int dragThresholdPx = 5);
void copyPolyStepSequencerPatternToClipboard(daw::audio::PolyStepSequencerPlugin& plugin);
juce::File writePolyStepSequencerPatternToTempMidiFile(daw::audio::PolyStepSequencerPlugin& plugin);
bool handlePolyStepSequencerPatternExternalDrag(daw::audio::PolyStepSequencerPlugin* plugin,
                                                juce::Component* exportButton,
                                                juce::Component* dragOwner,
                                                const juce::MouseEvent& event,
                                                int dragThresholdPx = 5);

}  // namespace magda::daw::ui
