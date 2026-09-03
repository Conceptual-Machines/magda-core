#pragma once

#include <atomic>

#include "plugins/PolyStepSequencerPlugin.hpp"
#include "plugins/StepSequencerPlugin.hpp"
#include "plugins/tracktion/TracktionMagdaDevicePlugin.hpp"

namespace magda::daw::audio {

/**
 * Push the model's "MIDI in thru" flag (DeviceInfo::midiInThru) onto whichever
 * device is behind @p plugin. Both step sequencers are MagdaDevices hosted by a
 * wrapper plugin (#2299), so the device is inside it rather than being it.
 */
inline void syncPluginMidiInThru(te::Plugin* plugin, bool enabled) {
    if (auto* stepSeq = tracktion_adapter::deviceFromPlugin<StepSequencerPlugin>(plugin)) {
        stepSeq->midiThru.store(enabled, std::memory_order_relaxed);
        return;
    }

    if (auto* polyStepSeq = tracktion_adapter::deviceFromPlugin<PolyStepSequencerPlugin>(plugin))
        polyStepSeq->midiThru.store(enabled, std::memory_order_relaxed);
}

}  // namespace magda::daw::audio
