#pragma once

#include <vector>

#include "MagdaCompiledPolyInstrument.hpp"

// The single-voice dsp is forward-declared via its Faust base; the .cpp owns it.
class dsp;

namespace magda::daw::audio::compiled {

/**
 * @brief Compiled-Faust four-operator FM synth with a full 4x4 modulation matrix.
 *
 * Four operators (selectable Sine / Triangle / Saw / Square / Noise) cross-modulate
 * through a 4x4 matrix - every operator can phase-modulate any operator, the
 * diagonal being self-feedback - so it spans every DX-style algorithm and the
 * unstable territory between them. Per-op ratio + output level and a shared amp
 * ADSR. Voice allocation and the output stage come from
 * MagdaCompiledPolyInstrument; this subclass only supplies the dsp and the slot
 * table.
 *
 * Slot layout (matches magda_fm.dsp [idx:N]):
 *   0..15  matrix M(src,dst), row-major idx = src*4 + dst
 *   16..19 Op ratio
 *   20..23 Op level
 *   24..27 Amp ADSR
 *   28..31 Op waveform
 *   42, 43 Gain, Voice Mode (wrapper)
 *   44..58 filter (#3120)
 *   59..68 filter as a matrix node (#3121): sends, returns, F Level, Position
 */
class MagdaFMCompiledPlugin : public MagdaCompiledPolyInstrument {
  public:
    static const char* xmlTypeName;

    static constexpr int kFilterEnableSlot = 44;
    static constexpr int kFilterEngineSlot = 45;
    static constexpr int kFilterTypeSlot = 46;
    static constexpr int kFilterCombModeSlot = 47;
    static constexpr int kFilterPolesSlot = 48;
    static constexpr int kFilterCutoffSlot = 49;
    static constexpr int kFilterResonanceSlot = 50;
    static constexpr int kFilterDriveSlot = 51;
    static constexpr int kFilterKeyTrackSlot = 52;
    static constexpr int kFilterVelSlot = 53;
    static constexpr int kFilterEnvSlot = 54;
    static constexpr int kFilterAdsrBaseSlot = 55;    // 55..58
    static constexpr int kFilterSendBaseSlot = 59;    // 59..62, Op n > F
    static constexpr int kFilterReturnBaseSlot = 63;  // 63..66, F > Op n
    static constexpr int kFilterLevelSlot = 67;
    static constexpr int kFilterPositionSlot = 68;
    static constexpr int kHostSlotCount = 69;

    MagdaFMCompiledPlugin();

    juce::String devicePluginId() const override {
        return xmlTypeName;
    }
    juce::String deviceName() const override {
        return "FM0";
    }

  protected:
    ::dsp* createVoiceDsp() const override;
    std::vector<HostSlotInfo> voiceSlotInfos() const override;
    // The base table (voice slots, Gain, Voice Mode) with the filter appended.
    std::vector<HostSlotInfo> slotInfos() const override;
    const char* slotIdPrefix() const override {
        return "magda_fm_";
    }
    int numVoices() const override {
        return 16;
    }
    bool hasVoiceModes() const override {
        return true;
    }
    int glideVoiceSlot() const override {
        return 32;  // magda_fm.dsp Glide [idx:32]
    }

  private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MagdaFMCompiledPlugin)
};

}  // namespace magda::daw::audio::compiled
