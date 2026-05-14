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
 * @brief Multi-engine compiled-Faust compressor.
 *
 * Engines:
 *  - Clean: hand-rolled FF compressor (peak/RMS detector, soft knee, stereo
 *    link, sidechain HPF, external audio sidechain, parallel mix, soft-limit
 *    output stage). Backed by magda_compressor.dsp.
 *  - Glue: Brouns FBFF compressor with user-exposed Peak/RMS detector,
 *    Pre/Post style, and FF↔FB blend; no sidechain HPF, no external
 *    sidechain. Backed by magda_compressor_glue.dsp.
 *
 * Both engine DSPs are instantiated; only the active one runs compute() per
 * audio callback. Shared zones (threshold/ratio/attack/release/etc.) are
 * written into both engines every block so swapping engines preserves the
 * user's settings. Engine-specific zones (SC HPF on Clean, FBFF/Style on
 * Glue) are written only when present on each engine.
 */
class MagdaCompressorCompiledPlugin : public te::Plugin, public ICompiledFaustPlugin {
  public:
    static const char* xmlTypeName;

    explicit MagdaCompressorCompiledPlugin(const te::PluginCreationInfo& info);
    ~MagdaCompressorCompiledPlugin() override;

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
    bool canSidechain() override {
        return true;
    }
    int getNumOutputChannelsGivenInputs(int) override {
        return 2;
    }
    void getChannelNames(juce::StringArray* ins, juce::StringArray* outs) override;
    bool producesAudioWhenNoAudioInput() override {
        return false;
    }
    double getTailLength() const override {
        return 0.0;
    }

    // Slot ordering — engine first (top-level mode switch), then the
    // compressor surface, then engine-specific tail. DSP `[idx:N]` values
    // mirror these (with idx 0 reserved for Engine, which lives only in the
    // wrapper).
    static constexpr int kEngineSlot = 0;
    static constexpr int kThresholdSlot = 1;
    static constexpr int kRatioSlot = 2;
    static constexpr int kAttackSlot = 3;
    static constexpr int kReleaseSlot = 4;
    static constexpr int kKneeSlot = 5;
    static constexpr int kMakeupSlot = 6;
    static constexpr int kMixSlot = 7;
    static constexpr int kOutputSlot = 8;
    static constexpr int kDetectorSlot = 9;
    static constexpr int kLinkSlot = 10;
    static constexpr int kSidechainHpfSlot = 11;  // Clean only
    static constexpr int kFbffSlot = 12;          // Glue only
    static constexpr int kStyleSlot = 13;         // Glue only — Pre / Post
    static constexpr int kAutogainSlot = 14;
    static constexpr int kHostSlotCount = 15;
    static constexpr int kUseSidechainHiddenSlot = 63;

    enum class CompressorEngine { Clean = 0, Glue = 1 };
    static constexpr int kEngineCount = 2;

    te::AutomatableParameter* getSlotParameter(int slotIndex) const;

    float displayValueToNativeValue(int slotIndex, float displayValue) const;
    float nativeValueToDisplayValue(int slotIndex, float nativeValue) const;

    float getInputPeakDb() const {
        return inputPeakDb_.load(std::memory_order_relaxed);
    }
    float getKeyPeakDb() const {
        return keyPeakDb_.load(std::memory_order_relaxed);
    }
    float getOutputPeakDb() const {
        return outputPeakDb_.load(std::memory_order_relaxed);
    }
    float getGainReductionDb() const {
        return gainReductionDb_.load(std::memory_order_relaxed);
    }
    bool isUsingExternalSidechain() const {
        return usingExternalSidechain_.load(std::memory_order_relaxed);
    }

    int activeEngineIndex() const;

    using HostSlotInfo = CompiledHostSlotInfo;
    const HostSlotInfo& getSlotInfo(int slotIndex) const;

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

    // Per-engine state. zones_[slotIndex] is the FAUSTFLOAT* into this
    // engine's DSP for that host slot, or null if the engine doesn't expose
    // that slot (Glue has no SC HPF; Clean has no FBFF/Style; neither exposes
    // Engine).
    struct EngineState {
        std::unique_ptr<::dsp> dsp;
        std::array<FAUSTFLOAT*, kHostSlotCount> zones{};
        FAUSTFLOAT* useSidechainZone = nullptr;
        int numInputs = 0;
        int numOutputs = 0;
    };
    std::array<EngineState, kEngineCount> engines_;
    std::atomic<int> activeEngine_{0};

    std::array<HostSlotInfo, kHostSlotCount> hostSlotInfo_;
    std::array<te::AutomatableParameter::Ptr, kHostSlotCount> hostParams_;
    std::array<juce::CachedValue<float>, kHostSlotCount> hostCached_;

    juce::AudioBuffer<float> scratchIn_;
    juce::AudioBuffer<float> scratchOut_;
    std::vector<float*> inPtrs_;
    std::vector<float*> outPtrs_;

    std::atomic<float> inputPeakDb_{-120.0f};
    std::atomic<float> keyPeakDb_{-120.0f};
    std::atomic<float> outputPeakDb_{-120.0f};
    std::atomic<float> gainReductionDb_{0.0f};
    std::atomic<bool> usingExternalSidechain_{false};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MagdaCompressorCompiledPlugin)
};

}  // namespace magda::daw::audio::compiled
