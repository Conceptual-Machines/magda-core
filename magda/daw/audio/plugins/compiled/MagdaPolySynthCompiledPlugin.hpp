#pragma once

#include <tracktion_engine/tracktion_engine.h>

#include <array>
#include <memory>
#include <vector>

#include "../FaustParamPool.hpp"
#include "CompiledFaustInterface.hpp"
#include "core/ParameterInfo.hpp"

// mydsp_poly (Faust's polyphonic voice allocator) and the single-voice dsp are
// forward-declared via their Faust base so the header doesn't pull in the Faust
// SDK; the .cpp owns them.
class dsp;
class dsp_poly;

namespace magda::daw::audio::compiled {

/**
 * @brief Compiled-Faust polyphonic instrument: 4 oscillators -> multimode SVF
 *        (with its own envelope) -> ADSR amp.
 *
 * The build-time-compiled single-voice MagdaPolySynthDsp is wrapped at runtime
 * in mydsp_poly (group=false), which allocates voices and drives the reserved
 * freq/gain/gate controls from MIDI note/velocity/gate via keyOn/keyOff. The
 * `[idx:N]` host macros (per-osc wave/level/coarse/fine, the filter section and
 * the amp envelope) are shared controls: each block their value is fanned out to
 * every voice's own zone (RT-safe pointer writes — group=false gives each voice
 * independent zones, avoiding the global GUI::updateAllGuis() the grouped path
 * would otherwise require).
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

    // Slot ordering matches the [idx:N] pins inside magda_polysynth.dsp:
    // four contiguous slots per oscillator (wave / level / coarse / fine),
    // then the filter section, then the amp envelope.
    static constexpr int kOscSlotCount = 4;  // slots per oscillator
    static constexpr int kNumOscillators = 4;
    static constexpr int kOscBaseSlot = 0;  // osc n -> kOscBaseSlot + 4*(n-1)

    static constexpr int kFilterTypeSlot = 16;
    static constexpr int kCutoffSlot = 17;
    static constexpr int kResonanceSlot = 18;
    static constexpr int kFilterEnvAmtSlot = 19;
    static constexpr int kFilterAttackSlot = 20;
    static constexpr int kFilterDecaySlot = 21;
    static constexpr int kFilterSustainSlot = 22;
    static constexpr int kFilterReleaseSlot = 23;

    static constexpr int kAmpAttackSlot = 24;
    static constexpr int kAmpDecaySlot = 25;
    static constexpr int kAmpSustainSlot = 26;
    static constexpr int kAmpReleaseSlot = 27;

    // Filter drive + slope (idx:28/29 in the dsp; appended after the amp
    // envelope so the existing slot indices stay stable).
    static constexpr int kFilterDriveSlot = 28;
    static constexpr int kFilterSlopeSlot = 29;

    // Performance controls (appended). Bend Range has a dsp [idx:30] zone (it
    // scales the MIDI-driven `bend` input); Voice Mode is wrapper-only (it
    // selects which engine renders, so it has no dsp zone - a decorative dsp
    // control would be dead-code-eliminated by Faust).
    static constexpr int kBendRangeSlot = 30;
    static constexpr int kVoiceModeSlot = 31;
    // Glide (portamento) has a dsp [idx:32] zone; the wrapper zeroes it on the
    // poly voices so only the Mono/Legato voice glides.
    static constexpr int kGlideSlot = 32;
    // Per-oscillator phase reset (restart that oscillator's phase on note-on),
    // discrete Off/On. osc n -> kOscResetBaseSlot + (n - 1), idx 33..36.
    static constexpr int kOscResetBaseSlot = 33;
    // Velocity routing: depth into amplitude, and octaves into the filter cutoff.
    static constexpr int kVelAmpSlot = 37;
    static constexpr int kVelFilterSlot = 38;

    static constexpr int kHostSlotCount = 39;

    enum VoiceMode { Poly = 0, Mono = 1, Legato = 2 };

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

    int readVoiceModeIndex() const;
    // Reset both engines and the held-note stack (used on a Poly/Mono switch).
    void resetAllVoices();
    // Mono/Legato note handling on the dedicated single voice. handleMonoNoteOn
    // returns true when a one-sample gate edge is needed to retrigger (Mono only).
    bool handleMonoNoteOn(int note, int velocity, int mode);
    void handleMonoNoteOff(int note);

    std::unique_ptr<::dsp_poly> poly_;
    // Dedicated single voice for Mono/Legato: the wrapper owns its freq/gate/gain
    // zones directly (the poly engine skips idle voices, so it cannot be driven
    // zone-only). Always allocated; only rendered when Voice Mode != Poly.
    std::unique_ptr<::dsp> monoVoice_;
    int numOutputs_ = 0;
    double sampleRate_ = 44100.0;

    // Per host slot: that control's zone in EVERY poly voice (group=false gives
    // each voice its own zones). Harvested by [idx:N], skipping the proxy box.
    std::array<std::vector<FAUSTFLOAT*>, kHostSlotCount> voiceZonesBySlot_;
    // Same control's single zone in the mono voice (nullptr if it has none, e.g.
    // the wrapper-only Voice Mode slot).
    std::array<FAUSTFLOAT*, kHostSlotCount> monoZonesBySlot_{};

    // MIDI-driven pitch-bend zones (no [idx]): per poly voice, and the mono voice.
    std::vector<FAUSTFLOAT*> voiceBendZones_;
    FAUSTFLOAT* monoBendZone_ = nullptr;
    // Mono voice's reserved freq/gain/gate zones, driven directly from MIDI.
    FAUSTFLOAT* monoFreqZone_ = nullptr;
    FAUSTFLOAT* monoGainZone_ = nullptr;
    FAUSTFLOAT* monoGateZone_ = nullptr;

    // Mono/Legato held-note stack (most-recent at the back) and live bend.
    struct HeldNote {
        int note = 0;
        float gain = 0.0f;
    };
    std::vector<HeldNote> heldNotes_;
    float currentBend_ = 0.0f;  // normalised [-1, 1]
    int lastVoiceMode_ = 0;

    std::array<HostSlotInfo, kHostSlotCount> hostSlotInfo_;
    std::array<te::AutomatableParameter::Ptr, kHostSlotCount> hostParams_;
    std::array<juce::CachedValue<float>, kHostSlotCount> hostCached_;

    juce::AudioBuffer<float> scratchOut_;
    std::vector<float*> outPtrs_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MagdaPolySynthCompiledPlugin)
};

}  // namespace magda::daw::audio::compiled
