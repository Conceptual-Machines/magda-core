#pragma once

#include <tracktion_engine/tracktion_engine.h>

#include <array>
#include <memory>

#include "audio/analysis/AudioTapBuffer.hpp"

namespace magda::daw::audio {

namespace te = tracktion::engine;

//==============================================================================
/**
 * @brief Native port of Mutable Instruments Clouds (Emilie Gillet, MIT).
 *
 * A stereo granular texture processor: granular / pitch-time-stretch /
 * looping-delay / spectral modes, with its own diffuser/reverb and a freeze.
 * The DSP is the unmodified upstream code (third_party/eurorack,
 * magda::mutable), run at its native 32 kHz; the host audio is resampled down
 * into it and the result resampled back up, around the fixed 32-sample grain
 * block.
 *
 * This is an audio-in effect (not a synth) - it processes the track signal in
 * place.
 */
class MutableCloudsPlugin : public te::Plugin {
  public:
    explicit MutableCloudsPlugin(const te::PluginCreationInfo&);
    ~MutableCloudsPlugin() override;

    //==============================================================================
    enum ParamIndex {
        kPosition = 0,
        kSize,
        kPitch,
        kDensity,
        kTexture,
        kDryWet,
        kSpread,
        kFeedback,
        kReverb,
        kMode,    // 0..3 granular / stretch / looping-delay / spectral
        kFreeze,  // 0/1
        kNumParams
    };

    static const char* getPluginName() {
        return "Nimbus";
    }
    static const char* xmlTypeName;

    juce::String getName() const override {
        return getPluginName();
    }
    juce::String getPluginType() override {
        return xmlTypeName;
    }
    juce::String getShortName(int) override {
        return "Nimbus";
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
        return false;
    }
    bool takesAudioInput() override {
        return true;
    }
    bool isSynth() override {
        return false;
    }
    double getTailLength() const override {
        return 2.0;  // diffuser/reverb + grain tail
    }

    void restorePluginStateFromValueTree(const juce::ValueTree&) override;

    // Live input-envelope tap for the faceplate's grain-buffer view: one decimated
    // peak per bucket, ~8s of history across kEnvelopeBuckets buckets.
    static constexpr int kEnvelopeBuckets = 480;
    static constexpr double kBufferSeconds = 8.0;
    const AudioTapBuffer& inputEnvelopeTap() const {
        return inputEnvelope_;
    }

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;

    std::array<te::AutomatableParameter::Ptr, kNumParams> params_;
    std::array<juce::CachedValue<float>, kNumParams> values_;

    double sampleRate_ = 44100.0;

    // Input-envelope decimation state (audio thread).
    AudioTapBuffer inputEnvelope_{1024};
    float envPeak_ = 0.0f;
    int envCount_ = 0;
    int envBucketLen_ = 256;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MutableCloudsPlugin)
};

}  // namespace magda::daw::audio
