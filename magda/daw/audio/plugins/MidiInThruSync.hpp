#pragma once

#include "plugins/PolyStepSequencerPlugin.hpp"
#include "plugins/StepSequencerPlugin.hpp"

namespace magda::daw::audio {

inline void syncPluginMidiInThru(te::Plugin* plugin, bool enabled) {
    if (auto* stepSeq = dynamic_cast<StepSequencerPlugin*>(plugin)) {
        stepSeq->midiThru = enabled;
        return;
    }

    if (auto* polyStepSeq = dynamic_cast<PolyStepSequencerPlugin*>(plugin))
        polyStepSeq->midiThru = enabled;
}

}  // namespace magda::daw::audio
