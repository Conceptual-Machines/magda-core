#pragma once

#include <tracktion_engine/tracktion_engine.h>

#include <array>
#include <memory>

namespace magda::daw::audio {

namespace te = tracktion::engine;

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
 */
class MutableElementsPlugin : public te::Plugin {
  public:
    explicit MutableElementsPlugin(const te::PluginCreationInfo&);
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

    juce::String getName() const override {
        return getPluginName();
    }
    juce::String getPluginType() override {
        return xmlTypeName;
    }
    juce::String getShortName(int) override {
        return "Materia";
    }
    juce::String getSelectableDescription() override {
        return getName();
    }

    //==============================================================================
    void initialise(const te::PluginInitialisationInfo&) override;
    void deinitialise() override;
    void reset() override;
    void applyToBuffer(const te::PluginRenderContext&) override;

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
        return 3.0;  // the space tail rings out well past note-off
    }

    void restorePluginStateFromValueTree(const juce::ValueTree&) override;

  private:
    //==============================================================================
    // Pimpl: keeps the Mutable DSP headers (and -DTEST) out of this header so the
    // rest of MAGDA does not transitively include the eurorack tree.
    struct Impl;
    std::unique_ptr<Impl> impl_;

    std::array<te::AutomatableParameter::Ptr, kNumParams> params_;
    std::array<juce::CachedValue<float>, kNumParams> values_;

    double sampleRate_ = 44100.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MutableElementsPlugin)
};

}  // namespace magda::daw::audio
