#include <algorithm>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>

#include "magda/daw/audio/plugins/mutable/MutableCloudsPlugin.hpp"
#include "magda/daw/core/ParameterUtils.hpp"

// Nimbus (Mutable Clouds) runs its DSP at a fixed 32 kHz, with the host audio
// resampled down into it and the result resampled back up. Two things ride on
// that plumbing and neither announces itself as a fault, so they are pinned
// here.
//
// The rate: every time constant inside Clouds - grain size and density, the
// reverb and diffuser decay, the looping-delay length, the feedback filter
// corner - is written in 32 kHz samples. Hand the engine more than 32000 of
// them per second and all of it speeds up together, which sounds like a
// different patch rather than like a bug (#2365).
//
// The latency: the output lags the input by the grain block plus the resampler
// windows, and the dry path lags with it, because Clouds mixes dry in itself. A
// host can only compensate for what the device declares.

namespace {

namespace audio = magda::daw::audio;
using audio::MutableCloudsPlugin;

constexpr int kBlockSize = 128;

struct CloudsRig {
    MutableCloudsPlugin device;

    explicit CloudsRig(double rate) {
        device.prepare({.sampleRate = rate, .maximumBlockSize = kBlockSize});
    }

    /// Slots take normalised values; the tests speak in display units.
    void set(int slot, float displayValue) {
        device.setParameterValue(slot, magda::ParameterUtils::realToNormalized(
                                           displayValue, device.parameterInfo(slot)));
    }

    void render(juce::AudioBuffer<float>& buffer) {
        for (int pos = 0; pos < buffer.getNumSamples(); pos += kBlockSize) {
            audio::DeviceProcessContext context;
            context.audio = &buffer;
            context.startSample = pos;
            context.numSamples = std::min(kBlockSize, buffer.getNumSamples() - pos);
            device.process(context);
        }
    }
};

int secondsToSamples(double seconds, double rate) {
    return static_cast<int>(std::lround(seconds * rate));
}

double energyAt(const juce::AudioBuffer<float>& buffer, int index) {
    const double s = buffer.getSample(0, index);
    return s * s;
}

/// How long the tail after `burstEnd` takes to deliver `fraction` of its own
/// energy, in seconds. A running integral rather than a level crossing: the
/// reverb's envelope ripples by several dB, which walks a threshold across
/// whole buckets, and a cumulative measure cannot be moved that way.
double tailEnergySpanSeconds(const juce::AudioBuffer<float>& buffer, double rate, int burstEnd,
                             double fraction) {
    double total = 0.0;
    for (int i = burstEnd; i < buffer.getNumSamples(); ++i)
        total += energyAt(buffer, i);
    if (total <= 0.0)
        return -1.0;

    double running = 0.0;
    for (int i = burstEnd; i < buffer.getNumSamples(); ++i) {
        running += energyAt(buffer, i);
        if (running >= fraction * total)
            return static_cast<double>(i - burstEnd) / rate;
    }
    return -1.0;
}

/// A 220 Hz burst followed by silence: the same signal whatever the host rate,
/// so two runs compare the DSP rather than their own input.
juce::AudioBuffer<float> burstThenSilence(double rate, double burstSeconds, double tailSeconds) {
    const int burst = secondsToSamples(burstSeconds, rate);
    juce::AudioBuffer<float> buffer(2, burst + secondsToSamples(tailSeconds, rate));
    buffer.clear();
    for (int i = 0; i < burst; ++i) {
        const auto s = static_cast<float>(
            0.5 * std::sin(2.0 * juce::MathConstants<double>::pi * 220.0 * i / rate));
        buffer.setSample(0, i, s);
        buffer.setSample(1, i, s);
    }
    return buffer;
}

constexpr double kBurstSeconds = 0.25;
constexpr double kTailSeconds = 3.0;

/// The reverb tail's 90%-energy span in seconds, rendered at `rate`. Wet only,
/// in looping delay mode with the diffuser and the feedback path out, so the
/// tail is the reverb's own decay and nothing in the path is random.
double reverbTailSpanAt(double rate) {
    CloudsRig rig(rate);
    rig.set(MutableCloudsPlugin::kMode, 2.0f);  // Looping Delay
    rig.set(MutableCloudsPlugin::kPosition, 0.0f);
    rig.set(MutableCloudsPlugin::kDensity, 0.0f);  // diffusion off in this mode
    rig.set(MutableCloudsPlugin::kTexture, 0.5f);  // filters wide open
    rig.set(MutableCloudsPlugin::kFeedback, 0.0f);
    rig.set(MutableCloudsPlugin::kDryWet, 1.0f);
    rig.set(MutableCloudsPlugin::kReverb, 0.5f);

    auto buffer = burstThenSilence(rate, kBurstSeconds, kTailSeconds);
    rig.render(buffer);
    return tailEnergySpanSeconds(buffer, rate, secondsToSamples(kBurstSeconds, rate), 0.9);
}

}  // namespace

TEST_CASE("Nimbus clocks its DSP at 32 kHz whatever the host rate", "[nimbus][clouds][resampler]") {
    // 32 kHz is the one host rate at which both resampler steps are 1, so it is
    // the rate the engine has always been fed correctly at. Every other rate
    // has to reproduce it in real seconds.
    const double at32k = reverbTailSpanAt(32000.0);
    const double at44k = reverbTailSpanAt(44100.0);
    const double at48k = reverbTailSpanAt(48000.0);

    INFO("reverb 90% tail span: 32k=" << at32k << "s 44.1k=" << at44k << "s 48k=" << at48k << "s");
    REQUIRE(at32k > 0.0);
    REQUIRE(at44k > 0.0);
    REQUIRE(at48k > 0.0);

    // Steps swapped, the engine runs (host/32000)^2 fast - 1.9x at 44.1 kHz,
    // 2.25x at 48 kHz. 5% is far inside that and far outside the difference
    // between two correctly clocked runs.
    CHECK(at44k == Catch::Approx(at32k).epsilon(0.05));
    CHECK(at48k == Catch::Approx(at32k).epsilon(0.05));
}

TEST_CASE("Nimbus declares the priming delay it imposes on the dry path",
          "[nimbus][clouds][latency]") {
    constexpr double kRate = 48000.0;

    CloudsRig rig(kRate);
    // What the hosts compensate by: both take it off properties(), in seconds,
    // once at construction.
    const auto declared =
        static_cast<int>(std::lround(rig.device.properties().latencySeconds * kRate));
    rig.set(MutableCloudsPlugin::kMode, 2.0f);    // Looping Delay
    rig.set(MutableCloudsPlugin::kDryWet, 0.0f);  // dry only: a clean delayed copy
    rig.set(MutableCloudsPlugin::kReverb, 0.0f);
    rig.set(MutableCloudsPlugin::kFeedback, 0.0f);

    // The DSP holds its output at zero until it has cleared its buffers, and
    // the resampler rings need their window filled.
    juce::AudioBuffer<float> warmup(2, secondsToSamples(0.2, kRate));
    warmup.clear();
    rig.render(warmup);

    const int impulseAt = 512;
    juce::AudioBuffer<float> buffer(2, 2048);
    buffer.clear();
    buffer.setSample(0, impulseAt, 0.5f);
    buffer.setSample(1, impulseAt, 0.5f);
    rig.render(buffer);

    // Energy centroid rather than the peak: the cubic interpolators spread the
    // impulse over a few samples, and which one is highest moves with the phase
    // the impulse lands on.
    double weighted = 0.0;
    double energy = 0.0;
    for (int i = impulseAt; i < buffer.getNumSamples(); ++i) {
        weighted += energyAt(buffer, i) * (i - impulseAt);
        energy += energyAt(buffer, i);
    }
    REQUIRE(energy > 0.0);

    const double measured = weighted / energy;
    INFO("declared " << declared << " samples, measured " << measured);
    CHECK(std::abs(measured - declared) <= 2.0);
}
