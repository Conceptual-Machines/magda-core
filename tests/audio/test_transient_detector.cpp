#include <juce_audio_basics/juce_audio_basics.h>

#include <algorithm>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
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
 * The detector is the incumbent's, reproduced coefficient for coefficient, so
 * what is asserted here is what that algorithm does rather than what a better
 * one might: transients land on the attacks, quiet ones need sensitivity to be
 * found, and nothing survives closer together than the spacing rule allows.
 */

using magda::engine::detectTransients;
using magda::engine::TransientDetectionSettings;

namespace {

constexpr double kSampleRate = 44100.0;

/// Half a millisecond, which is how far back a trigger is placed from where the
/// differentiated envelope crossed the threshold. Rounded up, because the
/// incumbent truncates after subtracting it rather than before: at 44100 the
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

    ClickReader(std::vector<Click> clicks, std::int64_t length)
        : clicks_(std::move(clicks)), length_(length) {}

    std::int64_t lengthInSamples() const override {
        return length_;
    }
    double sampleRate() const override {
        return kSampleRate;
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
};

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
        // what survives, and the incumbent has this property too.
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
