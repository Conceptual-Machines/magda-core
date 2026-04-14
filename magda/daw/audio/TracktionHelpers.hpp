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

}  // namespace magda
