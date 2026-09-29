#pragma once

#include <juce_data_structures/juce_data_structures.h>

#include <optional>
#include <utility>

#include "plugins/DevicePluginDefaults.hpp"
#include "plugins/DeviceSessionKey.hpp"

namespace magda::daw::audio {

struct DevicePluginCreationContext {
    DeviceSessionKey sessionKey;
    juce::ValueTree state;
    bool isNewPlugin = false;

    /// What the registry would have answered, for a creation with no session
    /// behind it: the native engine builds its devices detached, and the
    /// preferences a fresh analyser starts from are the host's to supply
    /// (#2663).
    std::optional<DevicePluginDefaults> defaults;
};

}  // namespace magda::daw::audio
