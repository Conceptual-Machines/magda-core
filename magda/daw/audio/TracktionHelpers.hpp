#pragma once

#include <juce_data_structures/juce_data_structures.h>
#include <tracktion_engine/tracktion_engine.h>

namespace magda {

namespace te = tracktion;

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
