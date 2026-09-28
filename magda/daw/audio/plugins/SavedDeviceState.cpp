#include "plugins/SavedDeviceState.hpp"

#include "core/DeviceState.hpp"
#include "plugins/InternalPluginRegistry.hpp"

namespace magda::daw::audio {

juce::ValueTree savedDeviceStateTree(const juce::String& savedState) {
    juce::ValueTree tree;
    if (device_state::looksLikeLegacyEngineState(savedState)) {
        tree = device_state::legacyEngineStateTree(savedState);
    } else if (auto doc = device_state::decode(savedState)) {
        doc->root.type = "PLUGIN";
        tree = device_state::toValueTree(doc->root);
        // The type property is part of the restore-tree contract carried by older documents.
        if (!tree.hasProperty("type"))
            tree.setProperty("type", doc->deviceType, nullptr);
    }

    adoptCanonicalPluginType(tree);
    return tree;
}

}  // namespace magda::daw::audio
