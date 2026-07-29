#pragma once

#include <tracktion_engine/tracktion_engine.h>

#include <memory>
#include <vector>

#include "plugins/MagdaDevice.hpp"

namespace magda::daw::audio::tracktion_adapter {

namespace te = tracktion::engine;

class TracktionMagdaDevicePlugin final : public te::Plugin {
  public:
    TracktionMagdaDevicePlugin(const te::PluginCreationInfo& info,
                               std::unique_ptr<MagdaDevice> device);
    ~TracktionMagdaDevicePlugin() override;

    MagdaDevice& device() {
        return *device_;
    }
    const MagdaDevice& device() const {
        return *device_;
    }

    juce::String getName() const override;
    juce::String getPluginType() override;
    juce::String getShortName(int) override;
    juce::String getSelectableDescription() override;

    void initialise(const te::PluginInitialisationInfo& info) override;
    void deinitialise() override;
    void reset() override;
    void applyToBuffer(const te::PluginRenderContext& context) override;

    bool takesMidiInput() override;
    bool takesAudioInput() override;
    bool isSynth() override;
    bool producesAudioWhenNoAudioInput() override;
    bool canSidechain() override;
    double getLatencySeconds() override;
    double getTailLength() const override;

    void flushPluginStateToValueTree() override;
    void restorePluginStateFromValueTree(const juce::ValueTree& state) override;

  private:
    void buildParameters();
    void syncParametersToDevice();

    std::unique_ptr<MagdaDevice> device_;
    std::vector<std::unique_ptr<juce::CachedValue<float>>> parameterValues_;
    std::vector<te::AutomatableParameter::Ptr> parameters_;
};

template <typename DeviceType> DeviceType* deviceFromPlugin(te::Plugin* plugin) {
    if (auto* device = dynamic_cast<DeviceType*>(plugin))
        return device;

    auto* adapter = dynamic_cast<TracktionMagdaDevicePlugin*>(plugin);
    return adapter != nullptr ? dynamic_cast<DeviceType*>(&adapter->device()) : nullptr;
}

template <typename DeviceType> const DeviceType* deviceFromPlugin(const te::Plugin* plugin) {
    if (auto* device = dynamic_cast<const DeviceType*>(plugin))
        return device;

    auto* adapter = dynamic_cast<const TracktionMagdaDevicePlugin*>(plugin);
    return adapter != nullptr ? dynamic_cast<const DeviceType*>(&adapter->device()) : nullptr;
}

}  // namespace magda::daw::audio::tracktion_adapter
