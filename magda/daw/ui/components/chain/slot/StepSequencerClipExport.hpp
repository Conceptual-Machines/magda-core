#pragma once

#include <juce_core/juce_core.h>

namespace magda::daw::audio {
class StepSequencerPlugin;
}

namespace magda::daw::ui {

void copyStepSequencerPatternToClipboard(daw::audio::StepSequencerPlugin& plugin);
juce::File writeStepSequencerPatternToTempMidiFile(daw::audio::StepSequencerPlugin& plugin);

}  // namespace magda::daw::ui
