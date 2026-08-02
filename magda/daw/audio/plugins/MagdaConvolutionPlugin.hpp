#pragma once

#include <juce_dsp/juce_dsp.h>
#include <tracktion_engine/tracktion_engine.h>

#include <atomic>

namespace magda::daw::audio {

namespace te = tracktion::engine;

//==============================================================================
/**
 * @brief MAGDA's native convolution device (IR Reverb).
 *
 * Loads a user-supplied impulse response and convolves the track signal with
 * it, followed by a high pass / low pass pair and an output trim, mixed back
 * against the dry signal. It replaces `te::ImpulseResponsePlugin`, the last
 * browser-visible stock Tracktion effect (#1980).
 *
 * Faust is not an option for this device: `fi.conv` takes a compile-time
 * constant kernel, so a user-loadable IR cannot be expressed as a compiled
 * Faust patch. The DSP is `juce::dsp::Convolution` - the same class the retired
 * device wrapped, so the replacement is a like-for-like swap.
 *
 * Latency: the convolution runs in JUCE's default configuration, uniform
 * partitioned and ZERO latency, which is what the retired device used. The
 * device therefore reports no latency and nothing about delay compensation
 * changes across the migration. (JUCE also offers `NonUniform`, cheaper for
 * long IRs and also zero latency; it is a one-line change if CPU ever asks for
 * it, and produces the same output.)
 *
 * The impulse response lives in the device's own state as `irFileData`, a FLAC
 * encoding of the loaded audio, so a project stays self-contained. MAGDA's v2
 * device state base64-encodes binary properties, so the blob round-trips
 * through a `.mgd` unchanged - and the retired device wrote the same property
 * under the same name, which is how an existing IR carries over.
 *
 * Parameter identity with the retired device is deliberate and complete: five
 * parameters in the same order, each with the same normalised curve, so saved
 * automation, macro links and mod links survive the migration untouched. The
 * one change is that the two cutoffs now STORE Hz where the retired device
 * stored a MIDI note number and merely displayed Hz. The normalised mapping is
 * unchanged (a MIDI note number is affine in log2 of frequency, and the range
 * ends are the same 10 Hz and 20 kHz), so only the unit a host write carries is
 * different - which is what the custom UI, whose sliders were already in Hz,
 * had assumed all along.
 */
class MagdaConvolutionPlugin : public te::Plugin {
  public:
    explicit MagdaConvolutionPlugin(const te::PluginCreationInfo&);
    ~MagdaConvolutionPlugin() override;

    //==============================================================================
    /// FROZEN parameter order - the compatibility surface saved links address.
    enum ParamIndex {
        kGain = 0,  // dB
        kLowCut,    // Hz (high pass)
        kHighCut,   // Hz (low pass)
        kMix,       // 0..1, 0 = dry
        kFilterQ,   // shared Q of both cutoffs
        kNumParams,
    };

    static const char* getPluginName() {
        return "IR Reverb";
    }
    static const char* xmlTypeName;

    //==============================================================================
    /** Loads an impulse response from any audio format the engine can read, and
        stores it in the device state. Message thread only.
        @return false when the file cannot be read or encoded.
    */
    bool loadImpulseResponse(const juce::File&);

    /** Loads an impulse response from an encoded audio file held in memory. */
    bool loadImpulseResponse(const void* sourceData, size_t sourceDataSize);

    /// The loaded IR's display name. Not used by the DSP; the custom UI shows it.
    juce::CachedValue<juce::String> irName;
    /// Normalise the IR's amplitude on load. True, as on the retired device.
    juce::CachedValue<bool> normalise;
    /// Trim leading and trailing silence on load. False, as on the retired device.
    juce::CachedValue<bool> trimSilence;

    te::AutomatableParameter::Ptr gainParam;
    te::AutomatableParameter::Ptr lowCutParam;
    te::AutomatableParameter::Ptr highCutParam;
    te::AutomatableParameter::Ptr mixParam;
    te::AutomatableParameter::Ptr filterQParam;

    //==============================================================================
    juce::String getName() const override {
        return getPluginName();
    }
    juce::String getPluginType() override {
        return xmlTypeName;
    }
    juce::String getShortName(int) override {
        return "IR";
    }
    juce::String getSelectableDescription() override {
        return getName();
    }

    double getLatencySeconds() override;
    double getTailLength() const override;

    void initialise(const te::PluginInitialisationInfo&) override;
    void deinitialise() override;
    void reset() override;
    void applyToBuffer(const te::PluginRenderContext&) override;

    bool takesAudioInput() override {
        return true;
    }
    bool takesMidiInput() override {
        return false;
    }
    bool isSynth() override {
        return false;
    }

    void restorePluginStateFromValueTree(const juce::ValueTree&) override;

  private:
    //==============================================================================
    enum ChainIndex { kConvolution = 0, kHighPass, kLowPass, kOutputGain };

    using Duplicator = juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>,
                                                      juce::dsp::IIR::Coefficients<float>>;

    void loadImpulseResponseFromState();
    void valueTreePropertyChanged(juce::ValueTree&, const juce::Identifier&) override;

    juce::CachedValue<float> gainValue, lowCutValue, highCutValue, mixValue, filterQValue;

    juce::dsp::ProcessorChain<juce::dsp::Convolution, Duplicator, Duplicator,
                              juce::dsp::Gain<float>>
        chain_;

    juce::SmoothedValue<float> lowCutSmoother_, highCutSmoother_, gainSmoother_, filterQSmoother_,
        wetSmoother_, drySmoother_;

    // Written when an IR is loaded, read by getTailLength() off the message
    // thread. The convolution's own IR size lives behind a pointer the audio
    // thread swaps, so it is not a safe read from here.
    std::atomic<double> irLengthSeconds_{0.0};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MagdaConvolutionPlugin)
};

}  // namespace magda::daw::audio
