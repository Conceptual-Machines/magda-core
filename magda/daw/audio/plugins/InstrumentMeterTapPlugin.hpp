#pragma once

#include <tracktion_engine/tracktion_engine.h>

#include <atomic>
#include <memory>
#include <vector>

#include "../../core/ChainNodePath.hpp"
#include "../DeviceMeteringManager.hpp"

namespace magda::daw::audio {

namespace te = tracktion::engine;

/**
 * Transparent plugin used inside instrument wrapper racks as an explicit meter tap.
 *
 * The wrapper rack has two audio paths: upstream audio passthrough and synth output.
 * Device meters should show only the synth output, so InstrumentRackManager inserts this
 * tap on the synth-output branch. The tap owns metering/gain for wrapped instruments
 * so Tracktion's rack graph does not need MAGDA-specific device lookup.
 */
class InstrumentMeterTapPlugin : public te::Plugin {
  public:
    explicit InstrumentMeterTapPlugin(const te::PluginCreationInfo& info);
    ~InstrumentMeterTapPlugin() override;

    static const char* getPluginName() {
        return "Instrument Meter Tap";
    }
    static const char* xmlTypeName;

    juce::String getName() const override {
        return getPluginName();
    }
    juce::String getPluginType() override {
        return xmlTypeName;
    }
    juce::String getShortName(int) override {
        return "MeterTap";
    }
    juce::String getSelectableDescription() override {
        return getName();
    }

    void initialise(const te::PluginInitialisationInfo&) override {}
    void deinitialise() override {}
    void reset() override {}
    void applyToBuffer(const te::PluginRenderContext&) override;

    void setDeviceId(DeviceId deviceId);
    void setDevicePath(const ChainNodePath& devicePath);
    DeviceId getDeviceId() const {
        return deviceId_.load(std::memory_order_relaxed);
    }

    bool takesMidiInput() override {
        return false;
    }
    bool takesAudioInput() override {
        return true;
    }
    bool isSynth() override {
        return false;
    }
    bool producesAudioWhenNoAudioInput() override {
        return false;
    }
    double getTailLength() const override {
        return 0.0;
    }
    void getChannelNames(juce::StringArray* ins, juce::StringArray* outs) override;

  private:
    void bindRealtimeTap();

    std::atomic<DeviceId> deviceId_{INVALID_DEVICE_ID};
    ChainNodePath devicePath_;
    std::atomic<std::atomic<float>*> peakL_{nullptr};
    std::atomic<std::atomic<float>*> peakR_{nullptr};
    std::atomic<std::atomic<float>*> gainLinear_{nullptr};
    std::shared_ptr<DeviceMeteringManager::RealtimeTapStorage> tapStorage_;
    std::vector<std::shared_ptr<DeviceMeteringManager::RealtimeTapStorage>> retiredTapStorage_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(InstrumentMeterTapPlugin)
};

}  // namespace magda::daw::audio
