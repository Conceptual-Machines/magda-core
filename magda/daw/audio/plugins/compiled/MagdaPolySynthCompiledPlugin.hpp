#pragma once

#include <tracktion_engine/tracktion_engine.h>

#include <array>
#include <memory>
#include <vector>

#include "../FaustParamPool.hpp"
#include "CompiledFaustInterface.hpp"
#include "core/ParameterInfo.hpp"

// mydsp_poly (Faust's polyphonic voice allocator) is forward-declared via its
// base so the header doesn't pull in the Faust SDK; the .cpp owns it.
class dsp_poly;

namespace magda::daw::audio::compiled {

/**
 * @brief Compiled-Faust polyphonic instrument: sawtooth → resonant LPF → ADSR.
 *
 * The build-time-compiled single-voice MagdaPolySynthDsp is wrapped at runtime
 * in mydsp_poly (group=false), which allocates voices and drives the reserved
 * freq/gain/gate controls from MIDI note/velocity/gate via keyOn/keyOff. The
 * `[idx:N]` host macros (Cutoff/Resonance/Attack/Release) are shared controls:
 * each block their value is fanned out to every voice's own zone (RT-safe
 * pointer writes — group=false gives each voice independent zones, avoiding the
 * global GUI::updateAllGuis() the grouped path would otherwise require).
 *
 * First compiled instrument in MAGDA (all other compiled devices are effects).
 */
class MagdaPolySynthCompiledPlugin : public te::Plugin, public ICompiledFaustPlugin {
  public:
    static const char* xmlTypeName;

    explicit MagdaPolySynthCompiledPlugin(const te::PluginCreationInfo& info);
    ~MagdaPolySynthCompiledPlugin() override;

    juce::String getName() const override;
    juce::String getPluginType() override;
    juce::String getShortName(int) override;
    juce::String getSelectableDescription() override;

    void initialise(const te::PluginInitialisationInfo& info) override;
    void deinitialise() override;
    void reset() override;
    void applyToBuffer(const te::PluginRenderContext& fc) override;

    bool takesMidiInput() override {
        return true;
    }
    bool takesAudioInput() override {
        return false;
    }
    bool isSynth() override {
        return true;
    }
    bool producesAudioWhenNoAudioInput() override {
        return true;
    }
    double getTailLength() const override {
        return 0.0;
    }

    // Slot ordering matches the [idx:N] pins inside magda_polysynth.dsp.
    static constexpr int kCutoffSlot = 0;
    static constexpr int kResonanceSlot = 1;
    static constexpr int kAttackSlot = 2;
    static constexpr int kReleaseSlot = 3;
    static constexpr int kHostSlotCount = 4;

    static constexpr int kNumVoices = 16;

    te::AutomatableParameter* getSlotParameter(int slotIndex) const;
    float displayValueToNativeValue(int slotIndex, float displayValue) const;
    float nativeValueToDisplayValue(int slotIndex, float nativeValue) const;

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

    std::unique_ptr<::dsp_poly> poly_;
    int numOutputs_ = 0;

    // Per host slot: that control's zone in EVERY voice (group=false gives each
    // voice its own zones). Harvested by [idx:N], skipping the shared proxy box.
    std::array<std::vector<FAUSTFLOAT*>, kHostSlotCount> voiceZonesBySlot_;

    std::array<HostSlotInfo, kHostSlotCount> hostSlotInfo_;
    std::array<te::AutomatableParameter::Ptr, kHostSlotCount> hostParams_;
    std::array<juce::CachedValue<float>, kHostSlotCount> hostCached_;

    juce::AudioBuffer<float> scratchOut_;
    std::vector<float*> outPtrs_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MagdaPolySynthCompiledPlugin)
};

}  // namespace magda::daw::audio::compiled
