#pragma once

#include <array>
#include <atomic>
#include <memory>

#include "audio/analysis/AudioTapBuffer.hpp"
#include "core/ParameterUtils.hpp"
#include "plugins/MagdaDevice.hpp"

namespace magda::daw::audio {

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
 *
 * A MagdaDevice since #2299: one DSP hosted by whichever engine is running it.
 * The slot ids, order and display ranges are the ones the retired host-native
 * plugin used, because projects address the parameters by index and store
 * their values in display units.
 */
class MutableCloudsPlugin : public MagdaDevice {
  public:
    MutableCloudsPlugin();
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

    /// The lowpass either side of the 32 kHz DSP. Its corner drops to stay
    /// under Nyquist at low rates, so the .cpp designs it per rate.
    static constexpr double kBandLimitHz = 15000.0;

    /// Output lag: the grain block, the placed output cushion and both filters.
    /// Measured, not derived: the filters' group delay is not the analog
    /// prototype's sum(1/Q)/w0, since the bilinear transform collapses it as
    /// the corner nears Nyquist, and it is not one number across the band
    /// anyway. In seconds because properties() is a construction-time
    /// snapshot; the dry path carries it too, since Clouds mixes dry itself.
    static constexpr double kLatencySeconds = 1145.9e-6;

    /// How far the delay moves across 22.05 kHz to 192 kHz, which is the
    /// measured quantity rather than a tolerance picked to pass a test. What is
    /// left is the input guard holding a fixed sample count, a shrinking
    /// duration as the rate climbs; closing it needs a rate-aware declaration,
    /// which construction-time properties() has no room for.
    static constexpr double kLatencySpreadSeconds = 55.0e-6;

    /// Past this the device sustains rather than decays: feedback from 0.75
    /// recirculates indefinitely, and granular at full reverb rings for 32 s.
    static constexpr double kMaxTailSeconds = 40.0;

    //==============================================================================
    DeviceProperties properties() const override {
        return {
            .pluginId = xmlTypeName,
            .name = getPluginName(),
            .shortName = "Nimbus",
            .latencySeconds = kLatencySeconds,
            // Off the reverb, not a constant: the decay runs 60 ms to past 18 s
            // across that parameter, and getTailLength() is read per render.
            .tailLengthSeconds = tailSeconds_.load(std::memory_order_relaxed),
        };
    }

    /// @brief The tail those parameters imply, in seconds. `position` because
    ///        every mode replays the buffer from there, `mode` because spectral
    ///        rings unpredictably.
    static double tailSecondsFor(float reverb, float feedback, bool freeze, float position,
                                 int mode);

    void prepare(const DevicePrepareContext& context) override;
    void reset() override;
    void process(DeviceProcessContext& context) override;

    int parameterCount() const override {
        return kNumParams;
    }
    ParameterInfo parameterInfo(int index) const override;
    float parameterValue(int index) const override;
    void setParameterValue(int index, float value) override;

    // Live input-envelope tap for the faceplate's grain-buffer view: one decimated
    // peak per bucket, ~8s of history across kEnvelopeBuckets buckets.
    static constexpr int kEnvelopeBuckets = 480;
    static constexpr double kBufferSeconds = 8.0;
    const AudioTapBuffer& inputEnvelopeTap() const {
        return inputEnvelope_;
    }

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

    /// setParameterValue writes it, getTailLength() reads it off the message
    /// thread.
    std::atomic<double> tailSeconds_{tailSecondsFor(0.0f, 0.0f, false, 0.5f, 0)};

    // Input-envelope decimation state (audio thread).
    AudioTapBuffer inputEnvelope_{1024};
    float envPeak_ = 0.0f;
    int envCount_ = 0;
    int envBucketLen_ = 256;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MutableCloudsPlugin)
};

}  // namespace magda::daw::audio
