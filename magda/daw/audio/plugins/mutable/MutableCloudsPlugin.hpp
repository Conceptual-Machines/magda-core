// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <atomic>
#include <cmath>
#include <memory>

#include "audio/analysis/AudioTapBuffer.hpp"
#include "core/ParameterUtils.hpp"
#include "plugins/AnalysisTelemetry.hpp"
#include "plugins/MagdaDevice.hpp"

namespace magda::daw::audio {

//==============================================================================
/**
 * @brief Native port of Mutable Instruments Clouds (Emilie Gillet, MIT).
 *
 * A stereo granular texture processor: granular / pitch-time-stretch /
 * looping-delay / spectral modes, with its own diffuser/reverb and a freeze.
 * The DSP is the unmodified upstream code (third_party/eurorack,
 * magda::mutable), run at its native 32 kHz inside a MutableRateHarness, around
 * the fixed 32-sample grain block.
 *
 * This is an audio-in effect (not a synth) - it processes the track signal in
 * place.
 *
 * A MagdaDevice since #2299: one DSP hosted by whichever engine is running it.
 * The slot ids, order and display ranges are the ones the retired host-native
 * plugin used, because projects address the parameters by index and store
 * their values in display units.
 */
class MutableCloudsPlugin : public MagdaDevice, public GrainEnvelopeTelemetry {
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

    /// Past this the device sustains rather than decays: feedback from 0.75
    /// recirculates indefinitely, and granular at full reverb rings for 32 s.
    static constexpr double kMaxTailSeconds = 40.0;

    //==============================================================================
    DeviceProperties properties() const override {
        return {
            .pluginId = xmlTypeName,
            .name = getPluginName(),
            .shortName = "Nimbus",
        };
    }

    /// The rate crossing and the grain block, placed at reset; the dry path carries it too,
    /// since Clouds mixes dry itself.
    int latencySamples() const override;

    /// Off the reverb, not a constant: the decay runs 60 ms to past 18 s across that parameter,
    /// and the tail is read per render.
    std::int64_t tailSamples() const override {
        return static_cast<std::int64_t>(
            std::ceil(tailSeconds_.load(std::memory_order_relaxed) * sampleRate_));
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
    sdk::ParameterDescriptor parameterDescriptor(int index) const override;
    float parameterValue(int index) const override;
    void setParameterValue(int index, float value) override;

    // Live input-envelope tap for the faceplate's grain-buffer view: one decimated
    // peak per bucket, ~8s of history across kEnvelopeBuckets buckets.
    static constexpr int kEnvelopeBuckets = 480;
    static constexpr double kBufferSeconds = 8.0;
    const AudioTapBuffer& inputEnvelopeTap() const {
        return inputEnvelope_;
    }

    // The envelope, as whatever draws it asks for it (#2585).
    std::string_view telemetryKey() const override {
        return kKey;
    }

    DeviceTelemetry* telemetry(std::string_view key) override {
        return key == kKey ? this : nullptr;
    }

    const DeviceTelemetry* telemetry(std::string_view key) const override {
        return key == kKey ? this : nullptr;
    }

    std::size_t writePosition() const override {
        return inputEnvelope_.writePosition();
    }

    std::size_t readLatest(float* dest, int numSamples) const override {
        return inputEnvelope_.readLatest(dest, numSamples);
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
