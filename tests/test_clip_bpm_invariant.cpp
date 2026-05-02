#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "magda/daw/core/ClipInfo.hpp"
#include "magda/daw/core/ClipManager.hpp"

// Issue #1157: session/autoTempo audio clips have a single canonical update
// path (ClipManager::applyAudioClipBeats) that separates DETECTED metadata
// (sourceBPM, sourceNumBeats) from USER INTENT (lengthBeats, loop region).
// These tests pin the contract:
//   - BPM corrections never resize the clip on the timeline.
//   - Beat-length edits never touch detected BPM.
//   - lengthBeats / length / loopLengthBeats / loopLength are atomically
//     consistent after every call.

using namespace magda;
using Catch::Approx;

namespace {
constexpr double PROJECT_BPM = 120.0;
constexpr double FILE_DURATION = 2.0;       // seconds
constexpr double DETECTED_BPM = 120.0;      // file plays at 120 BPM
constexpr double DETECTED_NUM_BEATS = 4.0;  // 2s × 120/60

ClipInfo makeSessionAutoTempoClip(ClipId id = 1) {
    ClipInfo clip;
    clip.id = id;
    clip.trackId = 1;
    clip.type = ClipType::Audio;
    clip.view = ClipView::Session;
    clip.audioFilePath = "fake.wav";
    clip.autoTempo = true;
    clip.loopEnabled = true;
    clip.speedRatio = 1.0;

    // Pretend detection has already populated source metadata.
    clip.sourceBPM = DETECTED_BPM;
    clip.sourceNumBeats = DETECTED_NUM_BEATS;

    // User intent: clip occupies 4 timeline beats, loop covers the full file.
    clip.lengthBeats = 4.0;
    clip.loopLengthBeats = 4.0;
    clip.loopStartBeats = 0.0;

    // Time-domain values consistent with the above (canonical setter would
    // recompute these; we seed them so reads-without-call still succeed).
    clip.length = clip.lengthBeats * 60.0 / PROJECT_BPM;
    clip.loopLength = clip.loopLengthBeats * 60.0 / clip.sourceBPM;
    clip.loopStart = 0.0;
    clip.startTime = 0.0;
    return clip;
}
}  // namespace

TEST_CASE("applyAudioClipBeats - BPM correction is metadata-only", "[clip][bpm][issue-1157]") {
    ClipManager::getInstance().shutdown();

    auto seed = makeSessionAutoTempoClip();
    ClipManager::getInstance().restoreClip(seed);

    SECTION("Doubling BPM keeps timeline length and lengthBeats unchanged") {
        ClipManager::AudioClipBeatsUpdate u;
        u.sourceBPM = 240.0;
        u.sourceNumBeats = FILE_DURATION * 240.0 / 60.0;  // 8 beats at 240 BPM
        ClipManager::getInstance().applyAudioClipBeats(seed.id, u, PROJECT_BPM);

        const auto* c = ClipManager::getInstance().getClip(seed.id);
        REQUIRE(c != nullptr);
        // Detected metadata updated.
        REQUIRE(c->sourceBPM == Approx(240.0));
        REQUIRE(c->sourceNumBeats == Approx(8.0));
        // User intent untouched.
        REQUIRE(c->lengthBeats == Approx(4.0));
        REQUIRE(c->loopLengthBeats == Approx(4.0));
        // Timeline length untouched (still 4 project beats = 2.0 s at 120 BPM).
        REQUIRE(c->length == Approx(2.0));
        // Source-domain loop length DID update (4 source beats at 240 BPM = 1.0 s
        // of source audio, half what it was). This is correct: TE will play the
        // shorter region twice across the same timeline span.
        REQUIRE(c->loopLength == Approx(1.0));
    }

    SECTION("Halving BPM keeps timeline length unchanged") {
        ClipManager::AudioClipBeatsUpdate u;
        u.sourceBPM = 60.0;
        u.sourceNumBeats = FILE_DURATION * 60.0 / 60.0;  // 2 beats at 60 BPM
        ClipManager::getInstance().applyAudioClipBeats(seed.id, u, PROJECT_BPM);

        const auto* c = ClipManager::getInstance().getClip(seed.id);
        REQUIRE(c->sourceBPM == Approx(60.0));
        REQUIRE(c->lengthBeats == Approx(4.0));
        REQUIRE(c->length == Approx(2.0));
    }
}

TEST_CASE("applyAudioClipBeats - beat-length edit preserves detected BPM",
          "[clip][bpm][issue-1157]") {
    ClipManager::getInstance().shutdown();

    auto seed = makeSessionAutoTempoClip();
    ClipManager::getInstance().restoreClip(seed);

    SECTION("Stretching to 8 beats does not change sourceBPM") {
        ClipManager::AudioClipBeatsUpdate u;
        u.lengthBeats = 8.0;
        u.loopLengthBeats = 8.0;
        ClipManager::getInstance().applyAudioClipBeats(seed.id, u, PROJECT_BPM);

        const auto* c = ClipManager::getInstance().getClip(seed.id);
        REQUIRE(c->lengthBeats == Approx(8.0));
        REQUIRE(c->length == Approx(4.0));  // 8 beats at 120 BPM
        // Detected metadata MUST NOT have changed.
        REQUIRE(c->sourceBPM == Approx(DETECTED_BPM));
        REQUIRE(c->sourceNumBeats == Approx(DETECTED_NUM_BEATS));
    }

    SECTION("Halving target length does not change sourceBPM") {
        ClipManager::AudioClipBeatsUpdate u;
        u.lengthBeats = 2.0;
        u.loopLengthBeats = 2.0;
        ClipManager::getInstance().applyAudioClipBeats(seed.id, u, PROJECT_BPM);

        const auto* c = ClipManager::getInstance().getClip(seed.id);
        REQUIRE(c->lengthBeats == Approx(2.0));
        REQUIRE(c->length == Approx(1.0));
        REQUIRE(c->sourceBPM == Approx(DETECTED_BPM));
        REQUIRE(c->sourceNumBeats == Approx(DETECTED_NUM_BEATS));
    }
}

TEST_CASE("applyAudioClipBeats - all derived fields agree after edit", "[clip][bpm][issue-1157]") {
    ClipManager::getInstance().shutdown();

    auto seed = makeSessionAutoTempoClip();
    ClipManager::getInstance().restoreClip(seed);

    // Combine a BPM correction with a beat-length stretch in a single call —
    // the inspector and waveform display read length, lengthBeats, loopLength,
    // and loopLengthBeats. They must be consistent after one call.
    ClipManager::AudioClipBeatsUpdate u;
    u.sourceBPM = 100.0;
    u.sourceNumBeats = FILE_DURATION * 100.0 / 60.0;
    u.lengthBeats = 6.0;
    u.loopLengthBeats = 6.0;
    ClipManager::getInstance().applyAudioClipBeats(seed.id, u, PROJECT_BPM);

    const auto* c = ClipManager::getInstance().getClip(seed.id);
    // Timeline-domain pair: length must equal lengthBeats * 60 / projectBPM.
    REQUIRE(c->length == Approx(c->lengthBeats * 60.0 / PROJECT_BPM));
    // Source-domain pair: loopLength must equal loopLengthBeats * 60 / sourceBPM.
    REQUIRE(c->loopLength == Approx(c->loopLengthBeats * 60.0 / c->sourceBPM));
    // speedRatio is forced to 1.0.
    REQUIRE(c->speedRatio == Approx(1.0));
}

TEST_CASE("applyAudioClipBeats - no-op for non-autoTempo clips", "[clip][bpm][issue-1157]") {
    ClipManager::getInstance().shutdown();

    auto seed = makeSessionAutoTempoClip();
    seed.autoTempo = false;
    seed.length = 1.0;
    seed.lengthBeats = 0.0;  // arrangement-style: time-authoritative
    ClipManager::getInstance().restoreClip(seed);

    ClipManager::AudioClipBeatsUpdate u;
    u.lengthBeats = 8.0;
    ClipManager::getInstance().applyAudioClipBeats(seed.id, u, PROJECT_BPM);

    const auto* c = ClipManager::getInstance().getClip(seed.id);
    // Update was rejected — non-autoTempo clips don't go through this path.
    REQUIRE(c->lengthBeats == Approx(0.0));
    REQUIRE(c->length == Approx(1.0));
}

TEST_CASE("applyAudioClipBeats - sourceBPM unknown leaves source-seconds intact",
          "[clip][bpm][issue-1157]") {
    ClipManager::getInstance().shutdown();

    auto seed = makeSessionAutoTempoClip();
    seed.sourceBPM = 0.0;       // detection has not yet completed
    seed.sourceNumBeats = 0.0;  // ditto
    seed.loopLength = 0.0;
    seed.loopStart = 0.0;
    ClipManager::getInstance().restoreClip(seed);

    ClipManager::AudioClipBeatsUpdate u;
    u.lengthBeats = 8.0;  // user resizes before detection lands
    ClipManager::getInstance().applyAudioClipBeats(seed.id, u, PROJECT_BPM);

    const auto* c = ClipManager::getInstance().getClip(seed.id);
    REQUIRE(c->lengthBeats == Approx(8.0));
    REQUIRE(c->length == Approx(4.0));  // 8 beats at 120 BPM
    // loopLength stays 0 — we only touch source-domain seconds when sourceBPM
    // is known. ClipDisplayInfo and TE have fallback paths for the pre-detection
    // window.
    REQUIRE(c->loopLength == Approx(0.0));
}
