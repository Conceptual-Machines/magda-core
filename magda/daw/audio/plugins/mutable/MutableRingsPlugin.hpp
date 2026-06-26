#pragma once

#include <tracktion_engine/tracktion_engine.h>

#include <array>
#include <memory>

namespace magda::daw::audio {

namespace te = tracktion::engine;

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
 */
class MutableRingsPlugin : public te::Plugin {
  public:
    explicit MutableRingsPlugin(const te::PluginCreationInfo&);
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

    juce::String getName() const override {
        return getPluginName();
    }
    juce::String getPluginType() override {
        return xmlTypeName;
    }
    juce::String getShortName(int) override {
        return "Halo";
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
        return 4.0;  // long resonator/reverb tail
    }

    void restorePluginStateFromValueTree(const juce::ValueTree&) override;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;

    std::array<te::AutomatableParameter::Ptr, kNumParams> params_;
    std::array<juce::CachedValue<float>, kNumParams> values_;

    double sampleRate_ = 44100.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MutableRingsPlugin)
};

}  // namespace magda::daw::audio
