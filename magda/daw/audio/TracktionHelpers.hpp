#pragma once

#include <juce_data_structures/juce_data_structures.h>
#include <tracktion_engine/tracktion_engine.h>

#include "../core/DeviceInfo.hpp"

namespace magda {

namespace te = tracktion;

/** Point a device's channel counts at what the live plugin reports.

    The widths the chain model compiles against, asked the way the incumbent's
    chain wiring asks them. The model is told once and keeps the answer while
    the current engine asks again on every rewire, so a wrong answer here lasts
    the rest of the session.

    What we distrust is a missing instance, not an empty answer: a
    te::ExternalPlugin fills neither array while it has no AudioPluginInstance,
    and storing 0 and 0 would leave a working device connected to no audio. An
    empty answer from a plugin that HAS its instance is correct, and belongs to
    MIDI-only plugins such as te::MidiPatchBay, which the current engine gives
    no audio for exactly that reason. */
inline void applyLiveChannelCounts(DeviceInfo& device, te::Plugin& plugin) {
    const auto* external = dynamic_cast<const te::ExternalPlugin*>(&plugin);
    if (external != nullptr && external->getAudioPluginInstance() == nullptr)
        return;

    juce::StringArray inputs, outputs;
    plugin.getChannelNames(&inputs, &outputs);
    device.audioInputChannels = inputs.size();
    device.audioOutputChannels = outputs.size();
}

/** Recursively strip TE-internal `id` properties from a ValueTree.
    Used when duplicating plugin state to prevent copied objects from
    sharing Tracktion object IDs with the originals. */
inline void stripTracktionIdsRecursive(juce::ValueTree state) {
    if (!state.isValid())
        return;

    state.removeProperty(te::IDs::id, nullptr);
    for (int i = 0; i < state.getNumChildren(); ++i)
        stripTracktionIdsRecursive(state.getChild(i));
}

/** Recursively remove all MODIFIERASSIGNMENTS child trees from a plugin
    ValueTree.

    Called before a rack-internal plugin's state is restored after a
    structural rebuild: RackSyncManager::syncModifiers re-binds modifiers
    fresh via param->addModifier, so leaving the previously-captured
    assignments inside the plugin state would result in TWO assignments
    per param after restore — modulating each param twice, sweeping it
    well past the user's intended range (e.g. 4OSC filterFreq driven up
    to ~22 kHz when an LFO link was reattached on top of the restored one). */
inline void stripModifierAssignmentsRecursive(juce::ValueTree state) {
    if (!state.isValid())
        return;

    for (int i = state.getNumChildren(); --i >= 0;) {
        auto child = state.getChild(i);
        if (child.hasType(te::IDs::MODIFIERASSIGNMENTS))
            state.removeChild(i, nullptr);
        else
            stripModifierAssignmentsRecursive(child);
    }
}

}  // namespace magda
