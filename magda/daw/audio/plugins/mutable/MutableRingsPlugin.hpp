#pragma once

#include <array>
#include <memory>

#include "core/ParameterUtils.hpp"
#include "plugins/MagdaDevice.hpp"

namespace magda::daw::audio {

//==============================================================================
/**
 * @brief Native port of Mutable Instruments Rings (Emilie Gillet, MIT).
 *
 * A resonator: switchable modal / sympathetic-string / inharmonic-string / FM
 * voice, with internal polyphony (Rings rotates voices on each strum, so held
 * notes ring on while new ones are played). The DSP is the unmodified upstream
 * code (third_party/eurorack, magda::mutable), run at its native 48 kHz and
 * resampled to the host rate.
 *
 * Driven from MIDI via the internal exciter: each note-on sets the pitch and
 * fires a one-block strum. There is no note-off gate - resonators decay
 * naturally per the Damping control, exactly like the hardware.
 *
 * A MagdaDevice since #2299: one DSP hosted by whichever engine is running it.
 * The slot ids, order and display ranges are the ones the retired host-native
 * plugin used, because projects address the parameters by index and store
 * their values in display units.
 */
class MutableRingsPlugin : public MagdaDevice {
  public:
    MutableRingsPlugin();
    ~MutableRingsPlugin() override;

    //==============================================================================
    enum ParamIndex {
        kStructure = 0,
        kBrightness,
        kDamping,
        kPosition,
        kModel,      // 0..5 resonator model
        kPolyphony,  // 0..2 -> 1 / 2 / 4 voices
        kChord,      // 0..10 (used by the quantized/sympathetic models)
        kPitch,      // semitone transpose
        kFine,       // cents
        kLevel,      // output dB
        kNumParams
    };

    static const char* getPluginName() {
        return "Halo";
    }
    static const char* xmlTypeName;

    //==============================================================================
    DeviceProperties properties() const override {
        return {
            .pluginId = xmlTypeName,
            .name = getPluginName(),
            .shortName = "Halo",
            .takesMidiInput = true,
            .takesAudioInput = false,
            .isSynth = true,
            .producesAudioWithoutInput = true,
            .tailLengthSeconds = 4.0,  // long resonator/reverb tail
        };
    }

    void prepare(const DevicePrepareContext& context) override;
    void reset() override;
    void process(DeviceProcessContext& context) override;

    int parameterCount() const override {
        return kNumParams;
    }
    ParameterInfo parameterInfo(int index) const override;
    float parameterValue(int index) const override;
    void setParameterValue(int index, float value) override;

  private:
    /// The parameter's display-domain value, converted through the cached
    /// domain rather than a freshly built ParameterInfo: this runs per block on
    /// the audio thread, and a ParameterInfo carries strings that allocate.
    float displayValue(int index) const;

    struct Impl;
    std::unique_ptr<Impl> impl_;

    std::array<float, kNumParams> values_{};
    std::array<ParameterUtils::ParameterDomain, kNumParams> domains_{};

    double sampleRate_ = 44100.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MutableRingsPlugin)
};

}  // namespace magda::daw::audio
