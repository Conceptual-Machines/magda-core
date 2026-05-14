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
 * @brief Multi-engine compiled-Faust pitch shifter.
 *
 * Engines:
 *  - Shifter: single-voice transpose, full +/-24 st range. The workhorse.
 *  - Detuner: two voices anti-symmetrically detuned, hard-panned L/R for
 *    chorus-without-modulation widening.
 *  - Harmonizer: single shifted voice summed with dry - default Pitch is
 *    a perfect fifth, default Mix 0.5 so the harmony reads immediately.
 *
 * All three are built on ef.transpose (two-delay-line crossfade shifter),
 * which is a granular method - transients smear, the crossfade audibly
 * pumps on long windows, and small windows go grainy. Those artefacts are
 * the character; if you want clean PSOLA you want a different device.
 *
 * Pattern B layout (per the Dimension / Reverb wrappers): every engine
 * DSP is instantiated, only the active one's compute() runs each block.
 * Shared zones (Pitch / Fine / Texture / Mix / Output) are written into
 * every engine every block so swapping engines preserves the user's
 * settings.
 */
class MagdaPitchCompiledPlugin : public te::Plugin, public ICompiledFaustPlugin {
  public:
    static const char* xmlTypeName;

    explicit MagdaPitchCompiledPlugin(const te::PluginCreationInfo& info);
    ~MagdaPitchCompiledPlugin() override;

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
        // The internal delay lines carry a short tail after dry input
        // stops; let TE keep pumping so the trail doesn't clip.
        return true;
    }
    double getTailLength() const override {
        return 0.25;  // 200 ms max window + safety margin.
    }

    static constexpr int kEngineSlot = 0;
    static constexpr int kPitchSlot = 1;
    static constexpr int kFineSlot = 2;
    static constexpr int kTextureSlot = 3;
    static constexpr int kMixSlot = 4;
    static constexpr int kOutputSlot = 5;
    static constexpr int kHostSlotCount = 6;

    enum class PitchEngine { Shifter = 0, Detuner = 1, Harmonizer = 2 };
    static constexpr int kEngineCount = 3;

    te::AutomatableParameter* getSlotParameter(int slotIndex) const;

    float displayValueToNativeValue(int slotIndex, float displayValue) const;
    float nativeValueToDisplayValue(int slotIndex, float nativeValue) const;

    int activeEngineIndex() const;

    using HostSlotInfo = CompiledHostSlotInfo;
    const HostSlotInfo& getSlotInfo(int slotIndex) const;

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
    bool isSlotHiddenForActiveEngine(int) const override {
        return false;
    }

  private:
    void buildHostParameters();
    void rebuildEngineState(int sampleRate);

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

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MagdaPitchCompiledPlugin)
};

}  // namespace magda::daw::audio::compiled
