#pragma once

#include <tracktion_engine/tracktion_engine.h>

#include <array>
#include <memory>
#include <vector>

#include "../FaustParamPool.hpp"
#include "CompiledFaustInterface.hpp"
#include "core/ParameterInfo.hpp"

class dsp;

namespace magda::daw::audio::compiled {

/**
 * @brief Compiled-Faust 8-band parametric EQ.
 *
 * Each band carries its own filter Type (HP / LowShelf / Bell / HighShelf /
 * LP / Notch) plus Freq / Gain / Q. The DSP runs all six filter shapes per
 * band in parallel and picks one with `ba.selectn`, so Type can be swapped
 * at audio rate without re-init.
 *
 * Slot layout (33 slots):
 *   4*band + 0 → Type   (discrete menu, 0..5)
 *   4*band + 1 → Freq   (Hz, log)
 *   4*band + 2 → Gain   (dB, ±24)
 *   4*band + 3 → Q      (0.1..10)
 *   32         → Output (dB, -24..+12)
 */
class MagdaEqCompiledPlugin : public te::Plugin, public ICompiledFaustPlugin {
  public:
    static const char* xmlTypeName;

    explicit MagdaEqCompiledPlugin(const te::PluginCreationInfo& info);
    ~MagdaEqCompiledPlugin() override;

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
        return false;
    }
    double getTailLength() const override {
        return 0.0;
    }

    static constexpr int kBandCount = 8;
    static constexpr int kSlotsPerBand = 4;                         // Type, Freq, Gain, Q
    static constexpr int kOutputSlot = kBandCount * kSlotsPerBand;  // 32
    static constexpr int kHostSlotCount = kOutputSlot + 1;          // 33

    enum class BandType {
        Highpass = 0,
        LowShelf = 1,
        Bell = 2,
        HighShelf = 3,
        Lowpass = 4,
        Notch = 5
    };
    static constexpr int kBandTypeCount = 6;

    // Sub-slot offsets within a band.
    static constexpr int kBandTypeOffset = 0;
    static constexpr int kBandFreqOffset = 1;
    static constexpr int kBandGainOffset = 2;
    static constexpr int kBandQOffset = 3;

    static int bandSlot(int band, int offset) {
        return band * kSlotsPerBand + offset;
    }

    te::AutomatableParameter* getSlotParameter(int slotIndex) const;

    float displayValueToNativeValue(int slotIndex, float displayValue) const;
    float nativeValueToDisplayValue(int slotIndex, float nativeValue) const;

    using HostSlotInfo = CompiledHostSlotInfo;
    const HostSlotInfo& getSlotInfo(int slotIndex) const;

    // Live per-band state for the curve view — these are the smoothed
    // values currently driving the audio thread.
    struct BandSnapshot {
        BandType type = BandType::Bell;
        float freq = 1000.0f;
        float gainDb = 0.0f;
        float q = 1.0f;
    };
    BandSnapshot getBandSnapshot(int band) const;
    float getOutputDb() const;

    /// "Collapse knobs" toggle persisted on the plugin's state ValueTree
    /// so the user's preferred slot layout survives project reload.
    /// Defaults to true (curve takes the full slot body) since the curve
    /// is the EQ's primary surface.
    bool isCurveCollapsed() const {
        return curveCollapsed_.get();
    }
    void setCurveCollapsed(bool collapsed) {
        curveCollapsed_ = collapsed;
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

    std::array<FAUSTFLOAT*, kHostSlotCount> zones_{};

    std::array<HostSlotInfo, kHostSlotCount> hostSlotInfo_;
    std::array<te::AutomatableParameter::Ptr, kHostSlotCount> hostParams_;
    std::array<juce::CachedValue<float>, kHostSlotCount> hostCached_;
    juce::CachedValue<bool> curveCollapsed_;

    juce::AudioBuffer<float> scratchIn_;
    juce::AudioBuffer<float> scratchOut_;
    std::vector<float*> inPtrs_;
    std::vector<float*> outPtrs_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MagdaEqCompiledPlugin)
};

}  // namespace magda::daw::audio::compiled
