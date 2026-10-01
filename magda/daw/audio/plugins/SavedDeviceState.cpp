#include "plugins/SavedDeviceState.hpp"

#include "plugins/DeviceStateDocument.hpp"

namespace magda::daw::audio {

juce::ValueTree savedDeviceStateTree(const juce::String& savedState) {
    const auto state = normaliseDeviceState(savedState);
    return state ? toLegacyTree(state->document) : juce::ValueTree{};
}

}  // namespace magda::daw::audio
