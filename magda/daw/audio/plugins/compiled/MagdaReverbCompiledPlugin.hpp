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
 * @brief Multi-engine compiled-Faust reverb.
 *
 * Engines:
 *  - Plate: Dattorro reverb (re.dattorro_rev). Dense allpass diffusion
 *    network, classic studio-plate sound. Backed by magda_reverb_plate.dsp.
 *  - Hall: Zita FDN reverb (re.zita_rev1_stereo). 8-tap feedback delay
 *    network with low/mid crossover decay; smooth large-space tails.
 *    Backed by magda_reverb_hall.dsp.
 *  - Room: Freeverb (re.stereo_freeverb). Schroeder/Moorer comb + allpass
 *    network, denser early reflections for small-space ambience. Backed
 *    by magda_reverb_room.dsp.
 *
 * All three engine DSPs are instantiated; only the active one runs
 * compute() per audio callback. Shared zones (Mix / Predelay / Decay /
 * Damping / Low Cut / High Cut / Width / Output) are written into every
 * engine every block so swapping engines preserves the user's settings.
 */
class MagdaReverbCompiledPlugin : public te::Plugin, public ICompiledFaustPlugin {
  public:
    static const char* xmlTypeName;

    explicit MagdaReverbCompiledPlugin(const te::PluginCreationInfo& info);
    ~MagdaReverbCompiledPlugin() override;

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
    int getNumOutputChannelsGivenInputs(int) override {
        return 2;
    }
    bool producesAudioWhenNoAudioInput() override {
        // True because the reverb has a tail that must keep playing after
        // dry input stops. TE uses this to schedule processing past the
        // last live audio sample.
        return true;
    }
    double getTailLength() const override {
        // Conservative cap covering Hall's worst case (~15s at Decay=100).
        // TE keeps the plugin processing for this long after audio stops.
        return 10.0;
    }

    // Slot ordering — Engine first (top-level mode switch), then the
    // shared reverb surface. DSP `[idx:N]` values mirror these (idx 0 is
    // wrapper-only).
    static constexpr int kEngineSlot = 0;
    static constexpr int kMixSlot = 1;
    static constexpr int kPredelaySlot = 2;
    static constexpr int kDecaySlot = 3;
    static constexpr int kDampingSlot = 4;
    static constexpr int kLowCutSlot = 5;
    static constexpr int kHighCutSlot = 6;
    static constexpr int kWidthSlot = 7;
    static constexpr int kOutputSlot = 8;
    static constexpr int kHostSlotCount = 9;

    enum class ReverbEngine { Plate = 0, Hall = 1, Room = 2 };
    static constexpr int kEngineCount = 3;

    te::AutomatableParameter* getSlotParameter(int slotIndex) const;

    float displayValueToNativeValue(int slotIndex, float displayValue) const;
    float nativeValueToDisplayValue(int slotIndex, float nativeValue) const;

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
    // engine's DSP for that host slot, or null if the engine doesn't
    // expose that slot (Engine slot 0 lives only in the wrapper).
    struct EngineState {
        std::unique_ptr<::dsp> dsp;
        std::array<FAUSTFLOAT*, kHostSlotCount> zones{};
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

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MagdaReverbCompiledPlugin)
};

}  // namespace magda::daw::audio::compiled
