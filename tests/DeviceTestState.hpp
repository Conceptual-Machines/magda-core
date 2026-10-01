#pragma once

#include "audio/plugins/DeviceStateDocument.hpp"

namespace magda::test {

/// A juce tree as the SDK node a device is restored from, for cases that author state as a tree.
inline device_state::Node legacyNodeOf(const juce::ValueTree& tree, bool isRoot = true) {
    device_state::Node node;
    node.type = isRoot ? juce::String() : tree.getType().toString();
    for (int i = 0; i < tree.getNumProperties(); ++i) {
        const auto name = tree.getPropertyName(i);
        node.props.set(name, tree.getProperty(name));
    }
    for (int i = 0; i < tree.getNumChildren(); ++i)
        node.children.push_back(legacyNodeOf(tree.getChild(i), false));
    return node;
}

inline sdk::StateNode stateOf(const juce::ValueTree& tree) {
    return daw::audio::toSdkNode(legacyNodeOf(tree));
}

}  // namespace magda::test
