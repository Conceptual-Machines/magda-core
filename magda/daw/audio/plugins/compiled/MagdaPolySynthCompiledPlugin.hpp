#pragma once

#include <vector>

#include "plugins/compiled/MagdaCompiledPolyInstrument.hpp"

// The single-voice dsp is forward-declared via its Faust base; the .cpp owns it.
class dsp;

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
class MagdaPolySynthCompiledPlugin : public MagdaCompiledPolyInstrument {
  public:
    static const char* xmlTypeName;

    MagdaPolySynthCompiledPlugin();

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
    static constexpr int kFilterDriveSlot = 28;
    static constexpr int kFilterSlopeSlot = 29;
    static constexpr int kBendRangeSlot = 30;
    static constexpr int kVoiceModeSlot = 31;
    static constexpr int kGlideSlot = 32;
    static constexpr int kOscResetBaseSlot = 33;
    static constexpr int kVelAmpSlot = 37;
    static constexpr int kVelFilterSlot = 38;
    static constexpr int kOscEnableBaseSlot = 39;
    static constexpr int kOutputGainSlot = 43;
    static constexpr int kHostSlotCount = 44;
    static constexpr int kNumVoices = 16;

    juce::String devicePluginId() const override {
        return xmlTypeName;
    }
    juce::String deviceName() const override {
        return "Poly Synth";
    }

    // Where this synth's panel puts the controls the base would otherwise
    // append. Gain is -1 because the dsp trims and soft-clips per voice
    // (`outGain`, [idx:43]), so a second output stage in the wrapper would be
    // limiting a signal that has already been limited.
    int gainSlot() const override {
        return -1;
    }
    int voiceModeSlot() const override {
        return kVoiceModeSlot;
    }

  protected:
    ::dsp* createVoiceDsp() const override;
    std::vector<HostSlotInfo> slotInfos() const override;
    const char* slotIdPrefix() const override {
        return "magda_polysynth_";
    }
    int numVoices() const override {
        return kNumVoices;
    }
    bool hasVoiceModes() const override {
        return true;
    }
    int glideVoiceSlot() const override {
        return kGlideSlot;
    }

  private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MagdaPolySynthCompiledPlugin)
};

}  // namespace magda::daw::audio::compiled
