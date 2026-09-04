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
    int blockSize = kBlockSize;

    explicit CloudsRig(double rate, int bs = kBlockSize) : blockSize(bs) {
        device.prepare({.sampleRate = rate, .maximumBlockSize = bs});
    }

    /// Slots take normalised values; the tests speak in display units.
    void set(int slot, float displayValue) {
        device.setParameterValue(slot, magda::ParameterUtils::realToNormalized(
                                           displayValue, device.parameterInfo(slot)));
    }

    void render(juce::AudioBuffer<float>& buffer) {
        for (int pos = 0; pos < buffer.getNumSamples(); pos += blockSize) {
            audio::DeviceProcessContext context;
            context.audio = &buffer;
            context.startSample = pos;
            context.numSamples = std::min(blockSize, buffer.getNumSamples() - pos);
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

namespace {

/// Energy at `hz` over the whole buffer (Goertzel).
double toneEnergy(const juce::AudioBuffer<float>& buffer, double rate, double hz) {
    const double w = 2.0 * juce::MathConstants<double>::pi * hz / rate;
    const double coeff = 2.0 * std::cos(w);
    double s1 = 0.0, s2 = 0.0;
    for (int i = 0; i < buffer.getNumSamples(); ++i) {
        const double s = coeff * s1 - s2 + buffer.getSample(0, i);
        s2 = s1;
        s1 = s;
    }
    return s1 * s1 + s2 * s2 - coeff * s1 * s2;
}

juce::AudioBuffer<float> steadyTone(double rate, double hz, double seconds) {
    juce::AudioBuffer<float> buffer(2, secondsToSamples(seconds, rate));
    for (int i = 0; i < buffer.getNumSamples(); ++i) {
        const auto s = static_cast<float>(
            0.5 * std::sin(2.0 * juce::MathConstants<double>::pi * hz * i / rate));
        buffer.setSample(0, i, s);
        buffer.setSample(1, i, s);
    }
    return buffer;
}

/// Dry only, in looping delay with the reverb and feedback out: a clean delayed
/// copy, so what comes back is the plumbing rather than the granulator.
void makeDryPath(CloudsRig& rig) {
    rig.set(MutableCloudsPlugin::kMode, 2.0f);
    rig.set(MutableCloudsPlugin::kDryWet, 0.0f);
    rig.set(MutableCloudsPlugin::kReverb, 0.0f);
    rig.set(MutableCloudsPlugin::kFeedback, 0.0f);
}

void warmUp(CloudsRig& rig, double rate) {
    // The DSP holds its output at zero until it has cleared its buffers, and
    // the resampler rings need their window filled.
    juce::AudioBuffer<float> warmup(2, secondsToSamples(0.2, rate));
    warmup.clear();
    rig.render(warmup);
}

/// Output energy at `probeHz` for a `inHz` tone in. Absolute, because a spur
/// has to be read against a passband reference and not against a source the
/// band limiting has already removed.
double spurEnergy(double rate, double inHz, double probeHz) {
    CloudsRig rig(rate);
    makeDryPath(rig);
    warmUp(rig, rate);

    auto buffer = steadyTone(rate, inHz, 0.25);
    rig.render(buffer);
    return toneEnergy(buffer, rate, probeHz);
}

/// Bulk delay through the device in samples, by correlating the output against
/// the input. Not an energy centroid: the band-limiting filters ring, and a
/// centroid counts that ringing as delay.
int bulkDelaySamples(double rate, int blockSize) {
    CloudsRig rig(rate, blockSize);
    makeDryPath(rig);
    warmUp(rig, rate);

    // A 4 kHz half-cycle, inside every passband here, so this reads the bulk
    // delay rather than a group delay at the filter's corner.
    const int impulseAt = 512;
    const int width = static_cast<int>(rate / 8000.0);
    juce::AudioBuffer<float> buffer(2, 4096);
    buffer.clear();
    for (int i = 0; i < width; ++i) {
        const auto s =
            static_cast<float>(0.5 * std::sin(juce::MathConstants<double>::pi * i / width));
        buffer.setSample(0, impulseAt + i, s);
        buffer.setSample(1, impulseAt + i, s);
    }

    juce::AudioBuffer<float> reference(2, buffer.getNumSamples());
    reference.makeCopyOf(buffer);
    rig.render(buffer);

    double best = -1.0e30;
    int bestLag = -1;
    for (int lag = 0; lag < 512; ++lag) {
        double acc = 0.0;
        for (int i = 0; i < width * 4; ++i)
            acc += reference.getSample(0, impulseAt + i) * buffer.getSample(0, impulseAt + i + lag);
        if (acc > best) {
            best = acc;
            bestLag = lag;
        }
    }
    return bestLag;
}

/// Seconds after `burstEnd` until the output last exceeds -60 dBFS.
double decayToSilenceSeconds(const juce::AudioBuffer<float>& buffer, double rate, int burstEnd) {
    int last = burstEnd;
    for (int i = burstEnd; i < buffer.getNumSamples(); ++i)
        if (std::abs(buffer.getSample(0, i)) > 0.001)
            last = i;
    return static_cast<double>(last - burstEnd) / rate;
}

}  // namespace

TEST_CASE("Nimbus declares the priming delay it imposes on the dry path",
          "[nimbus][clouds][latency]") {
    // Every rate a host runs at, and block sizes either side of the internal
    // chunk: the delay is a property of the plumbing, not of how the host
    // happens to slice its buffer (#2365 review).
    for (double rate : {44100.0, 48000.0, 88200.0, 96000.0}) {
        const double declared = MutableCloudsPlugin::kLatencySeconds * rate;
        for (int blockSize : {16, 128, 512, 2048}) {
            const int measured = bulkDelaySamples(rate, blockSize);
            INFO("rate " << rate << " block " << blockSize << ": declared " << declared
                         << " samples, measured " << measured);
            CHECK(std::abs(measured - declared) <= 3.0);
        }
    }
}

TEST_CASE("A 32 kHz host resamples nothing and skips the band limiting",
          "[nimbus][clouds][latency]") {
    // The one rate where both steps are 1. Nothing aliases, so both filters are
    // bypassed and the output arrives their group delay early against a
    // constant that has to assume they are in.
    const double bypassed = 2.0 * MutableCloudsPlugin::kBandLimitDelaySeconds * 32000.0;
    const double declared = MutableCloudsPlugin::kLatencySeconds * 32000.0;
    const int measured = bulkDelaySamples(32000.0, kBlockSize);

    INFO("declared " << declared << " samples, measured " << measured << ", filters worth "
                     << bypassed);
    CHECK(std::abs(measured - (declared - bypassed)) <= 1.0);
}

TEST_CASE("Nimbus band-limits both crossings of its 32 kHz rate", "[nimbus][clouds][resampler]") {
    constexpr double rate = 48000.0;
    // Everything is read against a passband tone at the same input level.
    const double reference = spurEnergy(rate, 1000.0, 1000.0);
    REQUIRE(reference > 0.0);

    const auto belowReference = [reference](double energy) {
        return -10.0 * std::log10(energy / reference + 1.0e-30);
    };

    SECTION("what folds coming down to 32 kHz") {
        // Undertaken, an 18 kHz tone arrives at 14 kHz louder than it left, and
        // 20 kHz lands at 12 kHz only 5 dB below a passband reference.
        const double fold18 = belowReference(spurEnergy(rate, 18000.0, 14000.0));
        const double fold20 = belowReference(spurEnergy(rate, 20000.0, 12000.0));
        INFO("18k->14k " << fold18 << " dB down, 20k->12k " << fold20 << " dB down");
        CHECK(fold18 > 30.0);
        CHECK(fold20 > 50.0);
    }

    SECTION("what images going back up") {
        // The cubic alone leaves a 10 kHz tone's image 17 dB down.
        const double image = belowReference(spurEnergy(rate, 10000.0, 22000.0));
        INFO("10k image at 22k, " << image << " dB down");
        CHECK(image > 60.0);
    }

    SECTION("and the passband survives it") {
        // The rolloff below the corner is the cubic interpolators', which the
        // filters are not allowed to add meaningfully to.
        for (double hz : {1000.0, 5000.0, 10000.0}) {
            const double loss = belowReference(spurEnergy(rate, hz, hz));
            INFO(hz << " Hz down " << loss << " dB");
            CHECK(loss < 2.0);
        }
    }
}

TEST_CASE("Nimbus takes a host buffer larger than its own ring", "[nimbus][clouds][resampler]") {
    // Pushing a whole call before draining any of it made StereoRing::kCap a
    // limit on the host's buffer: at 8192 it overran and dropped input
    // continuously, which is a render that quietly loses level (#2365 review).
    constexpr double rate = 48000.0;
    const auto throughputAt = [rate](int blockSize) {
        CloudsRig rig(rate, blockSize);
        makeDryPath(rig);
        warmUp(rig, rate);
        auto buffer = steadyTone(rate, 1000.0, 0.5);
        rig.render(buffer);
        return toneEnergy(buffer, rate, 1000.0);
    };

    const double reference = throughputAt(kBlockSize);
    REQUIRE(reference > 0.0);

    for (int blockSize : {2048, 4096, 8192, 16384}) {
        INFO("block " << blockSize);
        CHECK(throughputAt(blockSize) == Catch::Approx(reference).epsilon(0.01));
    }
}

TEST_CASE("Nimbus declares the tail its reverb is actually running", "[nimbus][clouds][latency]") {
    // A constant cannot: the decay runs from 60 ms to past 18 s across the one
    // parameter, and a bounce trusting 2 s was cut at the last note (#2365
    // review). getTailLength() is read per render, so this can follow.
    constexpr double rate = 48000.0;

    const auto measureDecay = [rate](float reverb) {
        CloudsRig rig(rate);
        rig.set(MutableCloudsPlugin::kMode, 2.0f);
        rig.set(MutableCloudsPlugin::kPosition, 0.0f);
        rig.set(MutableCloudsPlugin::kDensity, 0.0f);
        rig.set(MutableCloudsPlugin::kTexture, 0.5f);
        rig.set(MutableCloudsPlugin::kFeedback, 0.0f);
        rig.set(MutableCloudsPlugin::kDryWet, 1.0f);
        rig.set(MutableCloudsPlugin::kReverb, reverb);

        auto buffer = burstThenSilence(rate, kBurstSeconds, 40.0);
        rig.render(buffer);
        return decayToSilenceSeconds(buffer, rate, secondsToSamples(kBurstSeconds, rate));
    };

    SECTION("the declaration covers the decay at every reverb setting") {
        for (float reverb : {0.0f, 0.25f, 0.5f, 0.75f, 1.0f}) {
            CloudsRig rig(rate);
            rig.set(MutableCloudsPlugin::kReverb, reverb);
            const double declared = rig.device.properties().tailLengthSeconds;
            const double measured = measureDecay(reverb);

            INFO("reverb " << reverb << ": declared " << declared << "s, decays in " << measured
                           << "s");
            CHECK(declared >= measured);
            CHECK(declared <= measured + 3.0);
        }
    }

    SECTION("a reverb that is off does not extend the render") {
        CloudsRig rig(rate);
        rig.set(MutableCloudsPlugin::kReverb, 0.0f);
        CHECK(rig.device.properties().tailLengthSeconds < 1.0);
    }

    SECTION("feedback that never decays declares the ceiling") {
        // From 0.85 the buffer recirculates indefinitely. No finite tail covers
        // that, so the most useful thing to say is the ceiling.
        CloudsRig rig(rate);
        rig.set(MutableCloudsPlugin::kFeedback, 1.0f);
        CHECK(rig.device.properties().tailLengthSeconds ==
              Catch::Approx(MutableCloudsPlugin::kMaxTailSeconds));
    }
}
