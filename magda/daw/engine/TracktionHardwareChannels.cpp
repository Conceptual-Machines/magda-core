#include "TracktionHardwareChannels.hpp"

namespace magda {

namespace {

/** @brief The channels of @p waveDevices that are enabled, each under its device's name. */
HardwareChannels::Direction enabledOf(const auto& waveDevices, juce::StringArray channelNames) {
    HardwareChannels::Direction direction{.channelNames = std::move(channelNames)};
    for (auto* device : waveDevices) {
        if (device == nullptr || !device->isEnabled())
            continue;
        for (const auto& channel : device->getChannels()) {
            direction.open.setBit(channel.indexInDevice);
            direction.routeNames[channel.indexInDevice] = device->getName();
        }
    }
    return direction;
}

}  // namespace

TracktionHardwareChannels::TracktionHardwareChannels(tracktion::DeviceManager& devices)
    : devices_(devices) {
    // Wave devices are rebuilt, and their enablement saved, before this is told.
    devices_.addChangeListener(this);
}

TracktionHardwareChannels::~TracktionHardwareChannels() {
    devices_.removeChangeListener(this);
}

bool TracktionHardwareChannels::isOpen() const {
    return devices_.deviceManager.getCurrentAudioDevice() != nullptr;
}

HardwareChannels::Direction TracktionHardwareChannels::inputs() const {
    auto* device = devices_.deviceManager.getCurrentAudioDevice();
    return enabledOf(devices_.getWaveInputDevices(),
                     device != nullptr ? device->getInputChannelNames() : juce::StringArray{});
}

HardwareChannels::Direction TracktionHardwareChannels::outputs() const {
    auto* device = devices_.deviceManager.getCurrentAudioDevice();
    return enabledOf(devices_.getWaveOutputDevices(),
                     device != nullptr ? device->getOutputChannelNames() : juce::StringArray{});
}

void TracktionHardwareChannels::changeListenerCallback(juce::ChangeBroadcaster*) {
    notifyChanged();
}

}  // namespace magda
