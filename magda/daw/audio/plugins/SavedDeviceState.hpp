#pragma once

#include <juce_data_structures/juce_data_structures.h>

namespace magda::daw::audio {

/// Restore current JSON or legacy XML as a device's state tree, including load aliases.
/// Empty, malformed and unsupported future documents return an invalid tree.
juce::ValueTree savedDeviceStateTree(const juce::String& savedState);

}  // namespace magda::daw::audio
