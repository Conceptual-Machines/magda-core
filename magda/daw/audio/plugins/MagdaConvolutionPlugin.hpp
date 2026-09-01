#pragma once

#include <juce_dsp/juce_dsp.h>

#include <atomic>

#include "core/ParameterUtils.hpp"
#include "plugins/MagdaDevice.hpp"

namespace magda::daw::audio {

//==============================================================================
/**
 * @brief MAGDA's native convolution device (IR Reverb).
 *
 * Loads a user-supplied impulse response and convolves the track signal with
 * it, followed by a high pass / low pass pair and an output trim, mixed back
 * against the dry signal. It replaced the fork's ImpulseResponsePlugin, the last
 * browser-visible stock Tracktion effect (#1980), and is a MagdaDevice since
 * #2299: one DSP hosted by whichever engine is running it.
 *
 * Faust is not an option for this device: `fi.conv` takes a compile-time
 * constant kernel, so a user-loadable IR cannot be expressed as a compiled
 * Faust patch. The DSP is `juce::dsp::Convolution` - the same class the retired
 * device wrapped, so the replacement is a like-for-like swap.
 *
 * Latency: the convolution runs in JUCE's default configuration, uniform
 * partitioned and ZERO latency, which is what the retired device used. The
 * device therefore reports no latency and nothing about delay compensation
 * changes across the migration.
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
 * cutoffs STORE Hz (the retired Tracktion device stored a MIDI note number and
 * merely displayed Hz); the normalised mapping is unchanged.
 */
class MagdaConvolutionPlugin : public MagdaDevice {
  public:
    MagdaConvolutionPlugin();
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
    /** Loads an impulse response from any audio format JUCE's basic formats
        read, and stores it in the device state. Message thread only.
        @return false when the file cannot be read or encoded.
    */
    bool loadImpulseResponse(const juce::File&);

    /** Loads an impulse response from an encoded audio file held in memory. */
    bool loadImpulseResponse(const void* sourceData, size_t sourceDataSize);

    /// The loaded IR's display name. Not used by the DSP; the custom UI shows it.
    const juce::String& irName() const {
        return irName_;
    }
    void setIrName(const juce::String& name) {
        irName_ = name;
    }

    /// Normalise the IR's amplitude on load. True, as on the retired device.
    /// Read when a blob is decoded, so set it before loading.
    bool normalise() const {
        return normalise_;
    }
    void setNormalise(bool shouldNormalise) {
        normalise_ = shouldNormalise;
    }

    /// Trim leading and trailing silence on load. False, as on the retired device.
    bool trimSilence() const {
        return trimSilence_;
    }
    void setTrimSilence(bool shouldTrim) {
        trimSilence_ = shouldTrim;
    }

    //==============================================================================
    DeviceProperties properties() const override {
        return {
            .pluginId = xmlTypeName,
            .name = getPluginName(),
            .shortName = "IR",
            // A convolution rings for exactly the length of its impulse
            // response, plus the filters' own short decay. Declaring it is what
            // lets an offline render or a freeze keep the reverb's tail instead
            // of cutting it at the last note.
            .tailLengthSeconds = irLengthSeconds_.load(std::memory_order_relaxed),
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

    void flushState(juce::ValueTree& state) override;
    void restoreState(const juce::ValueTree& state) override;

  private:
    //==============================================================================
    enum ChainIndex { kConvolution = 0, kHighPass, kLowPass, kOutputGain };

    using Duplicator = juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>,
                                                      juce::dsp::IIR::Coefficients<float>>;

    /// The parameter's display-domain value, converted through the cached
    /// domain rather than a freshly built ParameterInfo: this runs per block on
    /// the audio thread, and a ParameterInfo carries strings that allocate.
    float displayValue(int index) const;

    void loadImpulseResponseFromData();

    std::array<float, kNumParams> values_{};
    std::array<ParameterUtils::ParameterDomain, kNumParams> domains_{};

    // The device's own non-parameter state, flushed to and restored from the
    // host's state tree under the retired device's property names.
    juce::MemoryBlock irData_;
    juce::String irName_;
    bool normalise_ = true;     // normalise the IR's amplitude on load
    bool trimSilence_ = false;  // trim leading/trailing silence on load

    juce::dsp::ProcessorChain<juce::dsp::Convolution, Duplicator, Duplicator,
                              juce::dsp::Gain<float>>
        chain_;

    juce::SmoothedValue<float> lowCutSmoother_, highCutSmoother_, gainSmoother_, filterQSmoother_,
        wetSmoother_, drySmoother_;

    /// Dry copy for the mix, sized in prepare() so process() never allocates.
    /// (The fork's pooled AudioScratchBuffer is host code.)
    juce::AudioBuffer<float> dryBuffer_;

    double sampleRate_ = 44100.0;

    // Written when an IR is loaded, read by properties() off the message
    // thread. The convolution's own IR size lives behind a pointer the audio
    // thread swaps, so it is not a safe read from here.
    std::atomic<double> irLengthSeconds_{0.0};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MagdaConvolutionPlugin)
};

}  // namespace magda::daw::audio
