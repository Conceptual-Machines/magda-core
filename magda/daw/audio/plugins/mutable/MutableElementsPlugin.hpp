#pragma once

#include <array>
#include <memory>

#include "core/ParameterUtils.hpp"
#include "plugins/MagdaDevice.hpp"

namespace magda::daw::audio {

//==============================================================================
/**
 * @brief Native port of Mutable Instruments Elements (Emilie Gillet, MIT).
 *
 * A modal-synthesis voice: an exciter (bow / blow / strike) drives a
 * modal + string resonator, followed by a stereo space (reverb). The DSP is
 * the unmodified upstream code (third_party/eurorack, magda::mutable), run at
 * its native 32 kHz and resampled to the host rate.
 *
 * Monophonic, like the hardware (one heavy modal bank per voice). MIDI note ->
 * resonator pitch + gate; the internal exciters generate the excitation, so it
 * sounds from MIDI alone (no audio input needed).
 *
 * A MagdaDevice since #2299: one DSP hosted by whichever engine is running it.
 * The slot ids, order and display ranges are the ones the retired host-native
 * plugin used, because projects address the parameters by index and store
 * their values in display units.
 */
class MutableElementsPlugin : public MagdaDevice {
  public:
    MutableElementsPlugin();
    ~MutableElementsPlugin() override;

    //==============================================================================
    // Front-panel parameters, in host-slot order. Values are the normalised
    // 0..1 Elements pot positions, except level (dB) and pitch/fine (semitones).
    enum ParamIndex {
        kContour = 0,
        kBow,
        kBowTimbre,
        kBlow,
        kBlowFlow,
        kBlowTimbre,
        kStrike,
        kStrikeMallet,
        kStrikeTimbre,
        kSignature,
        kGeometry,
        kBrightness,
        kDamping,
        kPosition,
        kSpace,
        kPitch,
        kFine,
        kLevel,
        kVelAmp,  // velocity -> amplitude depth (0 = fixed level, 1 = full velocity)
        kNumParams
    };

    static const char* getPluginName() {
        return "Materia";
    }
    static const char* xmlTypeName;

    //==============================================================================
    DeviceProperties properties() const override {
        return {
            .pluginId = xmlTypeName,
            .name = getPluginName(),
            .shortName = "Materia",
            .takesMidiInput = true,
            .takesAudioInput = false,
            .isSynth = true,
            .producesAudioWithoutInput = true,
            .tailLengthSeconds = 3.0,  // the space tail rings out well past note-off
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

    //==============================================================================
    // Pimpl: keeps the Mutable DSP headers (and -DTEST) out of this header so the
    // rest of MAGDA does not transitively include the eurorack tree.
    struct Impl;
    std::unique_ptr<Impl> impl_;

    std::array<float, kNumParams> values_{};
    std::array<ParameterUtils::ParameterDomain, kNumParams> domains_{};

    double sampleRate_ = 44100.0;

    /// Rendered here, then added into the host buffer (#2370): the DSP writes
    /// straight into these pointers, so it can't itself sum onto whatever the
    /// host handed us.
    juce::AudioBuffer<float> scratch_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MutableElementsPlugin)
};

}  // namespace magda::daw::audio
