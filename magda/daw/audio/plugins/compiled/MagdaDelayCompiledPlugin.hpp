#pragma once

#include <tracktion_engine/tracktion_engine.h>

#include <array>
#include <atomic>
#include <memory>
#include <vector>

#include "../FaustParamPool.hpp"
#include "CompiledFaustInterface.hpp"
#include "core/ParameterInfo.hpp"

class dsp;

namespace magda::daw::audio::compiled {

/**
 * @brief Compiled-Faust standard digital delay.
 *
 * Stereo delay line with tempo-sync, feedback tone tilt, and ping-pong
 * cross-feedback. Single-engine compiled plugin — every user control
 * maps 1:1 to a Faust slot pinned by [idx:N].
 *
 * The hidden BPM slot ([idx:63]) is populated each block from TE's
 * transport so musical-division mode tracks the live tempo.
 */
class MagdaDelayCompiledPlugin : public te::Plugin, public ICompiledFaustPlugin {
  public:
    static const char* xmlTypeName;

    explicit MagdaDelayCompiledPlugin(const te::PluginCreationInfo& info);
    ~MagdaDelayCompiledPlugin() override;

    juce::String getName() const override;
    juce::String getPluginType() override;
    juce::String getShortName(int) override;
    juce::String getSelectableDescription() override;

    void initialise(const te::PluginInitialisationInfo& info) override;
    void deinitialise() override;
    void reset() override;
    void applyToBuffer(const te::PluginRenderContext& fc) override;

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
        return true;  // delay buffer keeps producing tails after dry input stops
    }
    double getTailLength() const override {
        return 4.0;  // matches MAX_DELAY_SAMPLES / SR worst case
    }

    // Slot ordering matches [idx:N] pins inside magda_delay.dsp.
    static constexpr int kTimeSlot = 0;
    static constexpr int kDivisionSlot = 1;
    static constexpr int kSyncSlot = 2;
    static constexpr int kFeedbackSlot = 3;
    static constexpr int kMixSlot = 4;
    static constexpr int kToneSlot = 5;
    static constexpr int kCrossSlot = 6;
    static constexpr int kHostSlotCount = 7;
    static constexpr int kBpmSlot = 63;  // hidden, populated from TE transport

    te::AutomatableParameter* getSlotParameter(int slotIndex) const;

    float displayValueToNativeValue(int slotIndex, float displayValue) const;
    float nativeValueToDisplayValue(int slotIndex, float nativeValue) const;

    using HostSlotInfo = CompiledHostSlotInfo;
    const HostSlotInfo& getSlotInfo(int slotIndex) const;

    // Underlying Faust value (quarter-note multiplier) for a given Division
    // dropdown index. Curve views use this to compute exact echo times when
    // Sync is on — the dropdown labels ("1/4", "1/8.") aren't parseable so
    // we can't recover the value via String::getFloatValue.
    float divisionFaustValueForIndex(int index) const;

    // Last BPM read from the edit's tempo sequence in applyToBuffer. Updated
    // every audio block; safe to read from any thread. Lets the UI curve
    // view draw the sync-mode beat grid against the live tempo without
    // touching te::TempoSequence itself from the message thread.
    float currentBpm() const {
        return currentBpm_.load(std::memory_order_relaxed);
    }

    // ICompiledFaustPlugin
    int hostSlotCount() const override {
        return kHostSlotCount;
    }
    const CompiledHostSlotInfo& hostSlotInfo(int slotIndex) const override {
        return getSlotInfo(slotIndex);
    }
    te::AutomatableParameter* hostSlotParameter(int slotIndex) const override {
        return getSlotParameter(slotIndex);
    }
    float displayToNormalized(int slotIndex, float displayValue) const override {
        return displayValueToNativeValue(slotIndex, displayValue);
    }
    float normalizedToDisplay(int slotIndex, float normalizedValue) const override {
        return nativeValueToDisplayValue(slotIndex, normalizedValue);
    }

  private:
    void buildHostParameters();
    void rebuildEngineState(int sampleRate);

    std::unique_ptr<::dsp> dsp_;
    int numInputs_ = 0;
    int numOutputs_ = 0;

    // Faust zones harvested by [idx:N]. Mode + Division use an underlying
    // numeric value the user picks via a menu, but here both Sync (bool)
    // and Division (continuous menu) work via the same direct-write path.
    std::array<FAUSTFLOAT*, kHostSlotCount> zones_{};
    FAUSTFLOAT* bpmZone_ = nullptr;                   // [idx:63], hidden, host-driven
    std::vector<float> divisionChoiceValues_;         // Faust-side values, used for audio writes
    std::vector<juce::String> divisionChoiceLabels_;  // Display labels (e.g. "1/4", "1/8.")

    // Gate annotations harvested from the DSP, kept aside so
    // buildHostParameters' designated-initializer assignments don't wipe
    // them. Indexed by host slot.
    struct GateSpec {
        int slotIndex = -1;
        bool negated = false;
    };
    std::array<GateSpec, kHostSlotCount> harvestedGates_{};

    std::array<HostSlotInfo, kHostSlotCount> hostSlotInfo_;
    std::array<te::AutomatableParameter::Ptr, kHostSlotCount> hostParams_;
    std::array<juce::CachedValue<float>, kHostSlotCount> hostCached_;

    juce::AudioBuffer<float> scratchIn_;
    juce::AudioBuffer<float> scratchOut_;
    std::vector<float*> inPtrs_;
    std::vector<float*> outPtrs_;

    std::atomic<float> currentBpm_{120.0f};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MagdaDelayCompiledPlugin)
};

}  // namespace magda::daw::audio::compiled
