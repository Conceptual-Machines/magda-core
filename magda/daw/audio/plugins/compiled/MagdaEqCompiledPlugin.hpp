#pragma once

#include <tracktion_engine/tracktion_engine.h>

#include <array>
#include <atomic>
#include <memory>
#include <vector>

#include "CompiledFaustInterface.hpp"
#include "analysis/AudioTapBuffer.hpp"
#include "core/ParameterInfo.hpp"

namespace magda::daw::audio::compiled {

/**
 * @brief Built-in 8-band parametric EQ.
 *
 * Each band carries Enabled plus its own filter Type (HP / LowShelf / Bell /
 * HighShelf / LP / Notch), Freq / Gain / Q. Audio runs through MAGDA-owned RBJ
 * biquads so the audible response and curve view share coefficient math.
 *
 * Slot layout (41 slots):
 *   5*band + 0 → Enabled (boolean)
 *   5*band + 1 → Type    (discrete menu, 0..5)
 *   5*band + 2 → Freq    (Hz, log)
 *   5*band + 3 → Gain    (dB, ±24)
 *   5*band + 4 → Q       (0.1..10)
 *   40         → Output  (dB, -24..+12)
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
    static constexpr int kSlotsPerBand = 5;                         // Enabled, Type, Freq, Gain, Q
    static constexpr int kOutputSlot = kBandCount * kSlotsPerBand;  // 40
    static constexpr int kHostSlotCount = kOutputSlot + 1;          // 41

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
    static constexpr int kBandEnabledOffset = 0;
    static constexpr int kBandTypeOffset = 1;
    static constexpr int kBandFreqOffset = 2;
    static constexpr int kBandGainOffset = 3;
    static constexpr int kBandQOffset = 4;

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
        bool enabled = false;
        BandType type = BandType::Bell;
        float freq = 1000.0f;
        float gainDb = 0.0f;
        float q = 1.0f;
    };
    BandSnapshot getBandSnapshot(int band) const;
    float getOutputDb() const;
    const magda::daw::audio::AudioTapBuffer& getPreSpectrumTapBuffer() const {
        return preSpectrumTap_;
    }
    const magda::daw::audio::AudioTapBuffer& getPostSpectrumTapBuffer() const {
        return postSpectrumTap_;
    }
    double getSampleRate() const {
        return sampleRate_.load(std::memory_order_relaxed);
    }

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

    struct BiquadState {
        float x1 = 0.0f;
        float x2 = 0.0f;
        float y1 = 0.0f;
        float y2 = 0.0f;
    };

  private:
    void buildHostParameters();
    void rebuildEngineState(int sampleRate);
    float readSlotDisplayValue(int slotIndex) const;
    BandSnapshot readBandSnapshot(int band) const;

    std::array<HostSlotInfo, kHostSlotCount> hostSlotInfo_;
    std::array<te::AutomatableParameter::Ptr, kHostSlotCount> hostParams_;
    std::array<juce::CachedValue<float>, kHostSlotCount> hostCached_;
    juce::CachedValue<bool> curveCollapsed_;

    std::vector<float> preTapScratch_;
    std::vector<float> postTapScratch_;
    magda::daw::audio::AudioTapBuffer preSpectrumTap_{8192};
    magda::daw::audio::AudioTapBuffer postSpectrumTap_{8192};
    std::atomic<double> sampleRate_{44100.0};
    std::array<std::vector<BiquadState>, kBandCount> biquadStates_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MagdaEqCompiledPlugin)
};

}  // namespace magda::daw::audio::compiled
