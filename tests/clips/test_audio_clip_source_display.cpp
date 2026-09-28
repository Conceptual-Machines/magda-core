#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "AudioClipTestHelpers.hpp"
#include "core/AudioClipSourceDisplay.hpp"
#include "core/ClipInfo.hpp"
#include "core/SourcePool.hpp"

using namespace magda;
using Catch::Approx;

namespace {

struct SourceDisplayFixture {
    SourceDisplayFixture() {
        SourcePool::getInstance().clear();
        SourcePool::getInstance().clearSeededFactsForTesting();
    }
    ~SourceDisplayFixture() {
        SourcePool::getInstance().clear();
        SourcePool::getInstance().clearSeededFactsForTesting();
    }
};

constexpr double kFileSeconds = 5.517;
constexpr double kCachedBpm = 174.0;

/// A time-mode audio clip with one event and no tempo interpretation.
ClipInfo makeAudioClip() {
    ClipInfo clip;
    magda::test::giveAudioEvent(clip, "loop.wav", kFileSeconds);
    clip.setPlacementBeats(0.0, 8.0);
    return clip;
}

}  // namespace

TEST_CASE("Source display shows the adopted interpretation, not a substitute",
          "[clip][inspector][interpretation]") {
    SourceDisplayFixture fixture;
    auto clip = makeAudioClip();
    auto& event = *clip.primaryEvent();

    SECTION("A User-owned BPM equal to the project BPM is still the user's") {
        REQUIRE(event.adoptBpm(120.0, Provenance::User));
        REQUIRE(event.adoptTotalBeats(11.034, Provenance::User));

        const auto d = computeAudioClipSourceDisplay(clip, 120.0, kFileSeconds, kCachedBpm);

        REQUIRE(d.bpm == Approx(120.0));
        REQUIRE(d.totalBeats == Approx(11.034));
        REQUIRE(d.sourceFieldsActive);
    }

    SECTION("An Analysis-owned BPM is shown as adopted") {
        REQUIRE(event.adoptBpm(174.0, Provenance::Analysis));
        REQUIRE(event.adoptTotalBeats(16.0, Provenance::Analysis));

        const auto d = computeAudioClipSourceDisplay(clip, 120.0, kFileSeconds, kCachedBpm);

        REQUIRE(d.bpm == Approx(174.0));
        REQUIRE(d.totalBeats == Approx(16.0));
    }
}

TEST_CASE("Source display hints the cached analysis only when the event has no tempo",
          "[clip][inspector][interpretation]") {
    SourceDisplayFixture fixture;
    auto clip = makeAudioClip();
    REQUIRE_FALSE(clip.primaryEvent()->hasInterpretedBpm());

    SECTION("The cached BPM fills the gap with beats derived from the file duration") {
        const auto d = computeAudioClipSourceDisplay(clip, 120.0, kFileSeconds, kCachedBpm);

        REQUIRE(d.bpm == Approx(174.0));
        REQUIRE(d.totalBeats == Approx(16.0));
        REQUIRE(d.sourceFieldsActive);
    }

    SECTION("No cache leaves the BPM unknown") {
        const auto d = computeAudioClipSourceDisplay(clip, 120.0, kFileSeconds, 0.0);

        REQUIRE(d.bpm <= 0.0);
        REQUIRE(d.totalBeats <= 0.0);
    }
}
