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
 * @brief Multi-engine compiled-Faust stereo widener.
 *
 * Engines:
 *  - Dimension: Roland Dimension D-style anti-phase modulated delays
 *    with cross-channel mixing. Subtle, breathing widening that lifts
 *    mono content into a wide stereo image. Backed by
 *    magda_dimension_dim.dsp.
 *  - Haas: short fixed delay on one channel — classic psychoacoustic
 *    cue. Mono-leaning but very wide. magda_dimension_haas.dsp.
 *  - M/S: pure mid-side side-channel gain — surgical stereo width
 *    without any time smear. magda_dimension_ms.dsp.
 *
 * Same Pattern B layout the Reverb wrapper uses: all three engine DSPs
 * are instantiated, only the active one's compute() runs per audio
 * callback. Shared zones (Amount / Rate / Width / Mix / Output) are
 * written into every engine every block so swapping engines preserves
 * the user's settings.
 */
class MagdaDimensionCompiledPlugin : public te::Plugin, public ICompiledFaustPlugin {
  public:
    static const char* xmlTypeName;

    explicit MagdaDimensionCompiledPlugin(const te::PluginCreationInfo& info);
    ~MagdaDimensionCompiledPlugin() override;

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
        // Modulated-delay engines have a tail in the modulator state and
        // delay lines; keep TE pumping briefly after dry input stops so
        // the trail doesn't truncate.
        return true;
    }
    double getTailLength() const override {
        return 0.1;  // Haas at 30 ms × safety margin.
    }

    // Slot ordering — Engine first, then the shared widener surface. DSP
    // `[idx:N]` values mirror these (idx 0 is wrapper-only).
    static constexpr int kEngineSlot = 0;
    static constexpr int kAmountSlot = 1;
    static constexpr int kRateSlot = 2;
    static constexpr int kWidthSlot = 3;
    static constexpr int kMixSlot = 4;
    static constexpr int kOutputSlot = 5;
    static constexpr int kHostSlotCount = 6;

    enum class DimensionEngine { Dimension = 0, Haas = 1, MidSide = 2 };
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
    bool isSlotHiddenForActiveEngine(int slotIndex) const override;

  private:
    void buildHostParameters();
    void rebuildEngineState(int sampleRate);

    // Per-engine state. zones_[slotIndex] is the FAUSTFLOAT* into this
    // engine's DSP for that host slot, or null if the engine doesn't expose
    // that slot (Engine slot 0 lives only in the wrapper; Rate is inert in
    // Haas / M/S so Faust strips its zone).
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

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MagdaDimensionCompiledPlugin)
};

}  // namespace magda::daw::audio::compiled
