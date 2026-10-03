#include <juce_audio_basics/juce_audio_basics.h>

#include <algorithm>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstdint>
#include <vector>

#include "analysis/TransientDetector.hpp"

/**
 * Where a source's beats are, when the user has not said (#2038).
 *
 * Against a computed reader rather than a fixture, which is what the reader
 * interface is narrow for: a click train whose impulses are at sample positions
 * this file chose is a file whose right answer is known exactly, and no
 * filesystem is involved in asking.
 *
 * What is asserted here is what the algorithm does rather than what a better
 * one might: transients land on the attacks, quiet ones need sensitivity to be
 * found, and nothing survives closer together than the spacing rule allows.
 */

using magda::engine::detectTransients;
using magda::engine::TransientDetectionSettings;

namespace {

constexpr double kSampleRate = 44100.0;

/// Half a millisecond, which is how far back a trigger is placed from where the
/// differentiated envelope crossed the threshold. Rounded up, because the
/// detector truncates after subtracting it rather than before: at 44100 the
/// effective rewind is 23 samples, not 22.
constexpr int kRewindSamples = 23;

Catch::Approx approx(double value, double margin = 1e-4) {
    return Catch::Approx(value).margin(margin);
}

/// Impulses at chosen positions and silence everywhere else. An attack with no
/// body is the hardest thing to place and the easiest to check.
class ClickReader final : public magda::engine::AudioFileReader {
  public:
    struct Click {
        std::int64_t at = 0;
        float amplitude = 1.0f;
    };

    ClickReader(std::vector<Click> clicks, std::int64_t length, double sampleRate = kSampleRate)
        : clicks_(std::move(clicks)), length_(length), sampleRate_(sampleRate) {}

    std::int64_t lengthInSamples() const override {
        return length_;
    }
    double sampleRate() const override {
        return sampleRate_;
    }
    int numChannels() const override {
        return 1;
    }

    int read(juce::AudioBuffer<float>& destination, int destinationOffset, std::int64_t startSample,
             int numSamples) override {
        destination.clear(destinationOffset, numSamples);

        for (const auto& click : clicks_) {
            const auto offset = click.at - startSample;

            if (offset >= 0 && offset < numSamples)
                for (auto channel = 0; channel < destination.getNumChannels(); ++channel)
                    destination.setSample(channel, destinationOffset + static_cast<int>(offset),
                                          click.amplitude);
        }

        return numSamples;
    }

  private:
    std::vector<Click> clicks_;
    std::int64_t length_;
    double sampleRate_;
};

/// Decaying noise bursts over a quiet noise floor, so the envelope followers see
/// float arithmetic that a bare impulse never exercises.
class BurstReader final : public magda::engine::AudioFileReader {
  public:
    BurstReader(std::vector<std::int64_t> starts, std::int64_t length, double sampleRate)
        : starts_(std::move(starts)), length_(length), sampleRate_(sampleRate) {}

    std::int64_t lengthInSamples() const override {
        return length_;
    }
    double sampleRate() const override {
        return sampleRate_;
    }
    int numChannels() const override {
        return 1;
    }

    int read(juce::AudioBuffer<float>& destination, int destinationOffset, std::int64_t startSample,
             int numSamples) override {
        for (int i = 0; i < numSamples; ++i)
            destination.setSample(0, destinationOffset + i, sampleAt(startSample + i));
        return numSamples;
    }

  private:
    /// Stateless, so a read at any offset gives the same samples.
    float sampleAt(std::int64_t at) const {
        auto hash = static_cast<std::uint32_t>(at) * 2654435761u + 12345u;
        hash ^= hash >> 15;
        hash *= 2246822519u;
        hash ^= hash >> 13;
        const float noise = static_cast<float>(hash >> 8) / 8388608.0f - 1.0f;

        float level = 0.01f;
        for (const auto start : starts_) {
            const auto age = at - start;
            if (age >= 0 && age < 4000)
                level += 0.8f * std::exp(-static_cast<float>(age) / 600.0f);
        }
        return noise * level;
    }

    std::vector<std::int64_t> starts_;
    std::int64_t length_;
    double sampleRate_;
};

/// Reported positions as whole samples, which is exact: a position is a sample
/// index over the rate.
std::vector<std::int64_t> inSamples(const std::vector<double>& seconds, double sampleRate) {
    std::vector<std::int64_t> samples;
    for (const auto time : seconds)
        samples.push_back(std::llround(time * sampleRate));
    return samples;
}

/// Where a click at @p sample is expected to be reported.
double expected(std::int64_t sample) {
    return static_cast<double>(std::max<std::int64_t>(0, sample - kRewindSamples)) / kSampleRate;
}

}  // namespace

TEST_CASE("A click train is found where its clicks are", "[engine][analysis]") {
    ClickReader reader({{44100}, {88200}, {132300}}, 200000);

    const auto transients = detectTransients(reader, {});

    REQUIRE(transients.size() == 3);
    REQUIRE(transients[0] == approx(expected(44100)));
    REQUIRE(transients[1] == approx(expected(88200)));
    REQUIRE(transients[2] == approx(expected(132300)));
}

TEST_CASE("A file with nothing in it has no transients", "[engine][analysis]") {
    SECTION("silence") {
        ClickReader reader({}, 200000);
        REQUIRE(detectTransients(reader, {}).empty());
    }

    SECTION("no samples at all") {
        ClickReader reader({{44100}}, 0);
        REQUIRE(detectTransients(reader, {}).empty());
    }
}

TEST_CASE("Sensitivity decides how quiet a transient may be", "[engine][analysis]") {
    // Threshold runs from -10 dB at zero to -40 dB at one, against the file's
    // own peak, so each of these needs more sensitivity than the last.
    ClickReader reader({{44100, 1.0f}, {88200, 0.1f}, {132300, 0.02f}}, 200000);

    TransientDetectionSettings settings;

    settings.sensitivity = 0.0f;
    const auto few = detectTransients(reader, settings);

    settings.sensitivity = 0.5f;
    const auto some = detectTransients(reader, settings);

    settings.sensitivity = 1.0f;
    const auto many = detectTransients(reader, settings);

    REQUIRE(few.size() == 1);
    REQUIRE(some.size() == 2);
    REQUIRE(many.size() == 3);
}

TEST_CASE("A quiet recording has the same transients as a loud one", "[engine][analysis]") {
    // The threshold is relative to the file's own peak, which is the whole
    // reason there is a pass over it before anything is judged.
    ClickReader loud({{44100, 1.0f}, {88200, 1.0f}}, 200000);
    ClickReader quiet({{44100, 0.02f}, {88200, 0.02f}}, 200000);

    REQUIRE(detectTransients(loud, {}).size() == detectTransients(quiet, {}).size());
}

TEST_CASE("Transients closer together than the spacing rule are thinned", "[engine][analysis]") {
    SECTION("two inside the retrigger lockout only fire once") {
        // 30 ms apart, and the detector will not fire again for 50.
        //
        // In fact for rather longer than 50: the lockout is re-armed by every
        // sample over the threshold, and the last follower's release keeps an
        // impulse over it for about 1400 samples, so an attack shuts the
        // detector for something closer to 80 ms. Worth knowing rather than
        // worth changing -- the spacing rule below is what actually decides
        // what survives.
        ClickReader reader({{44100}, {44100 + 1323}}, 200000);

        const auto transients = detectTransients(reader, {});

        REQUIRE(transients.size() == 1);
        REQUIRE(transients[0] == approx(expected(44100)));
    }

    SECTION("two past the lockout but inside the spacing keep the later") {
        // 113 ms apart, which clears the lockout above, against a spacing rule
        // of 150: both fire, and the thinning pass drops the first.
        ClickReader reader({{44100}, {44100 + 5000}}, 200000);

        TransientDetectionSettings settings;
        settings.minimumSpacingSeconds = 0.15;

        const auto transients = detectTransients(reader, settings);

        REQUIRE(transients.size() == 1);
        REQUIRE(transients[0] == approx(expected(44100 + 5000)));
    }

    SECTION("two beyond the spacing both survive") {
        ClickReader reader({{44100}, {44100 + 8820}}, 200000);

        REQUIRE(detectTransients(reader, {}).size() == 2);
    }
}

TEST_CASE("Detection is deterministic", "[engine][analysis]") {
    // Cached per source and per sensitivity by whoever calls this, which is only
    // sound if two runs over one file agree.
    ClickReader reader({{44100}, {88200}, {132300}}, 200000);

    REQUIRE(detectTransients(reader, {}) == detectTransients(reader, {}));
}

TEST_CASE("Detected positions are pinned across the SDK split", "[engine][analysis]") {
    using Samples = std::vector<std::int64_t>;

    // Captured from the detector before it moved to the SDK. The bursts and the
    // clicks near a 32768-sample block edge pin the block-relative rewind.
    struct Golden {
        float sensitivity;
        Samples levels;
        Samples boundary;
        Samples bursts44100;
        Samples bursts48000;
        Samples bursts96000;
    };
    const Golden goldens[] = {
        {0.0f,
         {44077},
         {32768, 65542},
         {19978, 32768, 69977, 131072, 179977},
         {19977, 32768, 69976, 131072, 179976},
         {19953, 32768, 69952, 131072, 179952}},
        {0.5f,
         {44077, 88177},
         {32768, 65542, 99976},
         {19978, 32768, 69977, 98977, 131072, 179977},
         {19977, 32768, 69976, 98976, 131072, 179976},
         {19953, 32768, 69952, 98952, 131072, 179952}},
        {1.0f,
         {44077, 88177, 132277},
         {32768, 65542, 99976},
         {0, 19977, 32768, 69977, 98977, 131072, 179977},
         {0, 19976, 32768, 69976, 98976, 131072, 179976},
         {0, 19952, 32768, 69952, 98952, 131072, 179952}},
    };

    const std::vector<std::int64_t> burstStarts{20000, 32768 + 2,   70000,
                                                99000, 131072 + 10, 180000};

    for (const auto& golden : goldens) {
        TransientDetectionSettings settings;
        settings.sensitivity = golden.sensitivity;
        INFO("sensitivity " << golden.sensitivity);

        ClickReader levels({{44100, 1.0f}, {88200, 0.1f}, {132300, 0.02f}}, 200000);
        CHECK(inSamples(detectTransients(levels, settings), kSampleRate) == golden.levels);

        ClickReader boundary({{32768 + 5, 1.0f}, {65536 + 30, 0.5f}, {100000, 0.3f}}, 150000,
                             48000.0);
        CHECK(inSamples(detectTransients(boundary, settings), 48000.0) == golden.boundary);

        const std::pair<double, const Samples*> bursts[] = {{44100.0, &golden.bursts44100},
                                                            {48000.0, &golden.bursts48000},
                                                            {96000.0, &golden.bursts96000}};
        for (const auto& [rate, expectedSamples] : bursts) {
            BurstReader reader(burstStarts, 260000, rate);
            INFO("rate " << rate);
            CHECK(inSamples(detectTransients(reader, settings), rate) == *expectedSamples);
        }
    }
}
