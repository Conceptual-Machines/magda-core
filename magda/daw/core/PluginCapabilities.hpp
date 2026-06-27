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
    // could never support it. This is backed by the shared chain MIDI routing
    // model and any runtime wrapper/rack graph that consumes it.
    bool supportsMidiInputThruToggle = false;

    // Current routing support for feeding MIDI from another track/device into
    // this plugin. This is narrower than hasMidiInput; instruments consume track
    // MIDI, but are not currently exposed as MIDI sidechain destinations.
    bool supportsExternalMidiInputRouting = false;
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
bool hasMidiInput(const DeviceInfo& device);
bool hasMidiOutput(const DeviceInfo& device);
bool supportsMidiSourceToggle(const DeviceInfo& device);
bool supportsMidiInputRouting(const DeviceInfo& device);
bool supportsMidiInputThruToggle(const DeviceInfo& device);
bool supportsExternalMidiInputRouting(const DeviceInfo& device);
bool supportsSidechainRoutingMenu(const DeviceInfo& device);
void applyCachedCapabilitiesToDevice(DeviceInfo& device);

}  // namespace magda
