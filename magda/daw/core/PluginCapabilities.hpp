#pragma once

#include <juce_core/juce_core.h>

#include <mutex>
#include <optional>
#include <unordered_map>

#include "DeviceInfo.hpp"

namespace magda {

struct PluginCapabilitySnapshot {
    juce::String pluginIdentifier;
    juce::String name;
    juce::String manufacturer;
    juce::String format;

    bool hasMidiInput = false;
    bool hasMidiOutput = false;
    bool hasAudioInput = false;
    bool hasAudioOutput = false;

    int audioInputChannels = 0;
    int audioOutputChannels = 0;
    int inputBusCount = 0;
    int outputBusCount = 0;

    bool processorAcceptsMidi = false;
    bool processorProducesMidi = false;
    bool processorIsMidiEffect = false;
    bool tracktionTakesMidiInput = false;
    bool tracktionTakesAudioInput = false;
    bool tracktionProducesAudioWhenNoAudioInput = false;
};

struct DeviceMidiCapabilities {
    bool hasMidiInput = false;
    bool hasMidiOutput = false;
    bool hasAudioInput = false;
    bool hasAudioOutput = false;

    // Current implementation support, not a statement that the plugin itself
    // could never support it. Today this is backed by InstrumentRackManager.
    bool supportsMidiInputThruToggle = false;
};

class PluginCapabilityCache {
  public:
    static PluginCapabilityCache& getInstance();

    static juce::String identifierForDevice(const DeviceInfo& device);

    std::optional<PluginCapabilitySnapshot> find(const juce::String& pluginIdentifier) const;
    void update(const PluginCapabilitySnapshot& snapshot);

    DeviceMidiCapabilities capabilitiesForDevice(const DeviceInfo& device) const;

  private:
    PluginCapabilityCache();
    void loadUnlocked();
    void saveUnlocked() const;

    mutable std::mutex mutex_;
    std::unordered_map<juce::String, PluginCapabilitySnapshot> snapshots_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginCapabilityCache)
};

DeviceMidiCapabilities midiCapabilitiesForDevice(const DeviceInfo& device);
bool supportsMidiInputThruToggle(const DeviceInfo& device);

}  // namespace magda
