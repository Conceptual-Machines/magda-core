#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "magda/daw/audio/AudioThumbnailManager.hpp"
#include "magda/daw/core/ClipInfo.hpp"
#include "magda/daw/core/ClipManager.hpp"

// Issue #1157: session/autoTempo audio clips have a single canonical update
// path (ClipManager::applyAudioClipBeats) that separates audio source facts,
// source interpretation, and user clip placement.
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
    clip.setAudioContent();
    clip.view = ClipView::Session;
    clip.audio().source.filePath = "fake.wav";
    clip.audio().source.durationSeconds = FILE_DURATION;
    clip.autoTempo = true;
    clip.loopEnabled = true;
    clip.speedRatio = 1.0;

    // Pretend detection has already populated source metadata.
    clip.audio().interpretation.bpm = DETECTED_BPM;
    clip.audio().interpretation.totalBeats = DETECTED_NUM_BEATS;

    // User intent: clip occupies 4 timeline beats, loop covers the full file.
    clip.lengthBeats = 4.0;
    clip.loopLengthBeats = 4.0;
    clip.loopStartBeats = 0.0;

    // Time-domain values consistent with the above (canonical setter would
    // recompute these; we seed them so reads-without-call still succeed).
    clip.length = clip.lengthBeats * 60.0 / PROJECT_BPM;
    clip.loopLength = clip.loopLengthBeats * 60.0 / clip.audio().interpretation.bpm;
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
        u.interpretationBpm = 240.0;
        u.interpretationTotalBeats = FILE_DURATION * 240.0 / 60.0;  // 8 beats at 240 BPM
        ClipManager::getInstance().applyAudioClipBeats(seed.id, u, PROJECT_BPM);

        const auto* c = ClipManager::getInstance().getClip(seed.id);
        REQUIRE(c != nullptr);
        // Source fact preserved; interpretation updated.
        REQUIRE(c->audio().source.durationSeconds == Approx(FILE_DURATION));
        REQUIRE(c->audio().interpretation.bpm == Approx(240.0));
        REQUIRE(c->audio().interpretation.totalBeats == Approx(8.0));
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
        u.interpretationBpm = 60.0;
        u.interpretationTotalBeats = FILE_DURATION * 60.0 / 60.0;  // 2 beats at 60 BPM
        ClipManager::getInstance().applyAudioClipBeats(seed.id, u, PROJECT_BPM);

        const auto* c = ClipManager::getInstance().getClip(seed.id);
        REQUIRE(c->audio().source.durationSeconds == Approx(FILE_DURATION));
        REQUIRE(c->audio().interpretation.bpm == Approx(60.0));
        REQUIRE(c->lengthBeats == Approx(4.0));
        REQUIRE(c->length == Approx(2.0));
    }
}

TEST_CASE("applyAudioClipBeats - beat-length edit preserves detected BPM",
          "[clip][bpm][issue-1157]") {
    ClipManager::getInstance().shutdown();

    auto seed = makeSessionAutoTempoClip();
    ClipManager::getInstance().restoreClip(seed);

    SECTION("Stretching to 8 beats does not change source interpretation BPM") {
        ClipManager::AudioClipBeatsUpdate u;
        u.lengthBeats = 8.0;
        ClipManager::getInstance().applyAudioClipBeats(seed.id, u, PROJECT_BPM);

        const auto* c = ClipManager::getInstance().getClip(seed.id);
        REQUIRE(c->lengthBeats == Approx(8.0));
        REQUIRE(c->length == Approx(4.0));  // 8 beats at 120 BPM
        // Detected metadata MUST NOT have changed.
        REQUIRE(c->audio().interpretation.bpm == Approx(DETECTED_BPM));
        REQUIRE(c->audio().interpretation.totalBeats == Approx(DETECTED_NUM_BEATS));
    }

    SECTION("Halving target length does not change source interpretation BPM") {
        ClipManager::AudioClipBeatsUpdate u;
        u.lengthBeats = 2.0;
        ClipManager::getInstance().applyAudioClipBeats(seed.id, u, PROJECT_BPM);

        const auto* c = ClipManager::getInstance().getClip(seed.id);
        REQUIRE(c->lengthBeats == Approx(2.0));
        REQUIRE(c->length == Approx(1.0));
        REQUIRE(c->audio().interpretation.bpm == Approx(DETECTED_BPM));
        REQUIRE(c->audio().interpretation.totalBeats == Approx(DETECTED_NUM_BEATS));
    }
}

TEST_CASE("loop-length edit drives source total beats until manual override",
          "[clip][bpm][issue-1157]") {
    ClipManager::getInstance().shutdown();

    auto seed = makeSessionAutoTempoClip();
    ClipManager::getInstance().restoreClip(seed);

    SECTION("Unlocked total beats follows loop length beats") {
        ClipManager::AudioClipBeatsUpdate u;
        u.loopLengthBeats = 8.0;
        ClipManager::getInstance().applyAudioClipBeats(seed.id, u, PROJECT_BPM);

        const auto* c = ClipManager::getInstance().getClip(seed.id);
        REQUIRE(c != nullptr);
        REQUIRE(c->loopLengthBeats == Approx(8.0));
        REQUIRE(c->audio().interpretation.totalBeats == Approx(8.0));
        REQUIRE_FALSE(c->audio().interpretation.totalBeatsLocked);
    }

    SECTION("Manual total beats override locks future loop edits out") {
        ClipManager::AudioClipBeatsUpdate manual;
        manual.interpretationTotalBeats = 13.0;
        manual.lockInterpretationTotalBeats = true;
        ClipManager::getInstance().applyAudioClipBeats(seed.id, manual, PROJECT_BPM);

        ClipManager::AudioClipBeatsUpdate loopEdit;
        loopEdit.loopLengthBeats = 8.0;
        ClipManager::getInstance().applyAudioClipBeats(seed.id, loopEdit, PROJECT_BPM);

        const auto* c = ClipManager::getInstance().getClip(seed.id);
        REQUIRE(c != nullptr);
        REQUIRE(c->loopLengthBeats == Approx(8.0));
        REQUIRE(c->audio().interpretation.totalBeats == Approx(13.0));
        REQUIRE(c->audio().interpretation.totalBeatsLocked);
    }
}

TEST_CASE("setLengthBeats extends placement without growing source loop or interpretation",
          "[clip][bpm][issue-1157]") {
    ClipManager::getInstance().shutdown();

    auto seed = makeSessionAutoTempoClip();
    ClipManager::getInstance().restoreClip(seed);

    ClipManager::getInstance().setLengthBeats(seed.id, 8.0, PROJECT_BPM);

    const auto* c = ClipManager::getInstance().getClip(seed.id);
    REQUIRE(c != nullptr);
    REQUIRE(c->lengthBeats == Approx(8.0));
    REQUIRE(c->placement.lengthBeats == Approx(8.0));
    REQUIRE(c->length == Approx(4.0));

    REQUIRE(c->loopLengthBeats == Approx(4.0));
    REQUIRE(c->loopLength == Approx(2.0));
    REQUIRE(c->audio().interpretation.bpm == Approx(DETECTED_BPM));
    REQUIRE(c->audio().interpretation.totalBeats == Approx(DETECTED_NUM_BEATS));
    REQUIRE(c->audio().source.durationSeconds == Approx(FILE_DURATION));
}

TEST_CASE("late source metadata does not overwrite extended clip placement",
          "[clip][bpm][issue-1157]") {
    ClipManager::getInstance().shutdown();

    auto clip = makeSessionAutoTempoClip();
    clip.audio().interpretation.bpm = 0.0;
    clip.audio().interpretation.totalBeats = 0.0;
    clip.audio().source.durationSeconds = 0.0;
    clip.setPlacementBeats(0.0, 356.0);  // 89 bars at 4/4
    clip.length = 356.0 * 60.0 / PROJECT_BPM;
    clip.loopLengthBeats = 8.0;
    clip.loopLength = 8.0 * 60.0 / 172.0;

    clip.setSourceMetadata(8.0, 172.0);

    REQUIRE(clip.placement.lengthBeats == Approx(356.0));
    REQUIRE(clip.lengthBeats == Approx(356.0));
    REQUIRE(clip.loopLengthBeats == Approx(8.0));
    REQUIRE(clip.audio().interpretation.bpm == Approx(172.0));
    REQUIRE(clip.audio().interpretation.totalBeats == Approx(8.0));
}

TEST_CASE("resizeClip extends looped beat-mode clip placement only", "[clip][bpm][issue-1157]") {
    ClipManager::getInstance().shutdown();

    auto seed = makeSessionAutoTempoClip();
    seed.audio().interpretation.bpm = 172.0;
    seed.audio().interpretation.totalBeats = 8.0;
    seed.audio().source.durationSeconds = 8.0 * 60.0 / 172.0;
    seed.setPlacementBeats(0.0, 8.0);
    seed.length = 8.0 * 60.0 / PROJECT_BPM;
    seed.loopLengthBeats = 8.0;
    seed.loopLength = 8.0 * 60.0 / 172.0;
    ClipManager::getInstance().restoreClip(seed);

    ClipManager::getInstance().resizeClip(seed.id, 356.0 * 60.0 / PROJECT_BPM, false, PROJECT_BPM);

    const auto* c = ClipManager::getInstance().getClip(seed.id);
    REQUIRE(c != nullptr);
    REQUIRE(c->placement.lengthBeats == Approx(356.0));
    REQUIRE(c->lengthBeats == Approx(356.0));
    REQUIRE(c->loopLengthBeats == Approx(8.0));
    REQUIRE(c->audio().interpretation.bpm == Approx(172.0));
    REQUIRE(c->audio().interpretation.totalBeats == Approx(8.0));
    REQUIRE(c->audio().source.durationSeconds == Approx(8.0 * 60.0 / 172.0));
}

TEST_CASE("applyAudioClipBeats - all derived fields agree after edit", "[clip][bpm][issue-1157]") {
    ClipManager::getInstance().shutdown();

    auto seed = makeSessionAutoTempoClip();
    ClipManager::getInstance().restoreClip(seed);

    // Combine a BPM correction with a beat-length stretch in a single call —
    // the inspector and waveform display read length, lengthBeats, loopLength,
    // and loopLengthBeats. They must be consistent after one call.
    ClipManager::AudioClipBeatsUpdate u;
    u.interpretationBpm = 100.0;
    u.interpretationTotalBeats = FILE_DURATION * 100.0 / 60.0;
    u.lengthBeats = 6.0;
    u.loopLengthBeats = 6.0;
    ClipManager::getInstance().applyAudioClipBeats(seed.id, u, PROJECT_BPM);

    const auto* c = ClipManager::getInstance().getClip(seed.id);
    // Timeline-domain pair: length must equal lengthBeats * 60 / projectBPM.
    REQUIRE(c->length == Approx(c->lengthBeats * 60.0 / PROJECT_BPM));
    // Source-domain pair: loopLength must equal loopLengthBeats * 60 / source interpretation BPM.
    REQUIRE(c->loopLength == Approx(c->loopLengthBeats * 60.0 / c->audio().interpretation.bpm));
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

TEST_CASE("applyAudioClipBeats - source interpretation BPM unknown leaves source-seconds intact",
          "[clip][bpm][issue-1157]") {
    ClipManager::getInstance().shutdown();

    auto seed = makeSessionAutoTempoClip();
    seed.audio().interpretation.bpm = 0.0;         // detection has not yet completed
    seed.audio().interpretation.totalBeats = 0.0;  // ditto
    seed.loopLength = 0.0;
    seed.loopStart = 0.0;
    ClipManager::getInstance().restoreClip(seed);

    ClipManager::AudioClipBeatsUpdate u;
    u.lengthBeats = 8.0;  // user resizes before detection lands
    ClipManager::getInstance().applyAudioClipBeats(seed.id, u, PROJECT_BPM);

    const auto* c = ClipManager::getInstance().getClip(seed.id);
    REQUIRE(c->lengthBeats == Approx(8.0));
    REQUIRE(c->length == Approx(4.0));  // 8 beats at 120 BPM
    // loopLength stays 0 — we only touch source-domain seconds when source interpretation BPM
    // is known. ClipDisplayInfo and TE have fallback paths for the pre-detection
    // window.
    REQUIRE(c->loopLength == Approx(0.0));
}

TEST_CASE("setAutoTempo adopts cached detected BPM when clip BPM is project default",
          "[clip][bpm][auto-tempo][issue-1157]") {
    ClipManager::getInstance().shutdown();
    AudioThumbnailManager::getInstance().clearCache();

    constexpr double detectedBPM = 135.0;
    constexpr double sourceDuration = 6.0;
    constexpr double expectedSourceBeats = sourceDuration * detectedBPM / 60.0;
    const juce::String path = "/tmp/cached-detection-defaulted-source.wav";

    ClipInfo seed;
    seed.id = 77;
    seed.trackId = 1;
    seed.setAudioContent();
    seed.view = ClipView::Arrangement;
    seed.audio().source.filePath = path;
    seed.loopEnabled = false;
    seed.autoTempo = false;
    seed.speedRatio = 1.0;
    seed.startTime = 0.0;
    seed.length = sourceDuration;
    seed.loopStart = 0.0;
    seed.loopLength = sourceDuration;
    seed.audio().interpretation.bpm = PROJECT_BPM;  // defaulted placeholder, not trusted metadata
    seed.audio().interpretation.totalBeats = 0.0;
    seed.setPlacementBeats(0.0, sourceDuration * PROJECT_BPM / 60.0);

    ClipManager::getInstance().restoreClip(seed);
    AudioThumbnailManager::getInstance().cacheBPM(path, detectedBPM);

    ClipManager::getInstance().setAutoTempo(seed.id, true, PROJECT_BPM);

    const auto* c = ClipManager::getInstance().getClip(seed.id);
    REQUIRE(c != nullptr);
    REQUIRE(c->autoTempo);
    REQUIRE(c->audio().interpretation.bpm == Approx(detectedBPM));
    REQUIRE(c->audio().interpretation.totalBeats == Approx(expectedSourceBeats));
    REQUIRE(c->lengthBeats == Approx(expectedSourceBeats));
    REQUIRE(c->placement.lengthBeats == Approx(expectedSourceBeats));
    REQUIRE(c->length == Approx(expectedSourceBeats * 60.0 / PROJECT_BPM));

    AudioThumbnailManager::getInstance().clearCache();
}

// ============================================================================
// BPM-change invariants — the core regression suite for issue #1157.
//
// Project BPM and source BPM live in different domains. The visible drift
// reported in the issue was the seconds cache going stale after a project
// tempo change because session clips were skipped by the tempo-change
// handler. These tests pin every direction:
//   - project BPM up → autoTempo clip's lengthBeats unchanged, length
//     contracts proportionally.
//   - project BPM down → length expands, beats unchanged.
//   - source interpretation BPM correction → lengthBeats AND length unchanged on the
//     timeline (BPM is just metadata); only the source-domain loop seconds
//     change because the source's beat-to-seconds ratio shifted.
//   - accessor consistency: getTimelineLength matches the cached length
//     after every operation.
// ============================================================================

TEST_CASE("Project-BPM change preserves autoTempo lengthBeats", "[clip][bpm][issue-1157]") {
    ClipManager::getInstance().shutdown();

    auto seed = makeSessionAutoTempoClip();
    ClipManager::getInstance().restoreClip(seed);

    // Apply with PROJECT_BPM = 120 first to populate the seconds cache.
    ClipManager::AudioClipBeatsUpdate u;
    u.lengthBeats = 24.0;
    u.loopLengthBeats = 4.0;
    ClipManager::getInstance().applyAudioClipBeats(seed.id, u, PROJECT_BPM);

    const auto* c = ClipManager::getInstance().getClip(seed.id);
    REQUIRE(c->lengthBeats == Approx(24.0));
    REQUIRE(c->length == Approx(12.0));  // 24 × 60/120

    SECTION("Project BPM up: length contracts, lengthBeats unchanged") {
        ClipManager::getInstance().refreshDerivedSeconds(seed.id, 240.0);
        c = ClipManager::getInstance().getClip(seed.id);
        REQUIRE(c->lengthBeats == Approx(24.0));
        REQUIRE(c->length == Approx(6.0));
        REQUIRE(c->getTimelineLength(240.0) == Approx(c->length));
    }

    SECTION("Project BPM down: length expands, lengthBeats unchanged") {
        ClipManager::getInstance().refreshDerivedSeconds(seed.id, 60.0);
        c = ClipManager::getInstance().getClip(seed.id);
        REQUIRE(c->lengthBeats == Approx(24.0));
        REQUIRE(c->length == Approx(24.0));  // 24 × 60/60
        REQUIRE(c->getTimelineLength(60.0) == Approx(c->length));
    }

    SECTION("Accessor stays correct between cache writes") {
        // After applyAudioClipBeats with old BPM, ask the accessor at a NEW
        // BPM — it must return the live value, not the cached one. This is
        // what protects renderers when the tempo-change listener hasn't run
        // yet but the project state is already at the new BPM.
        REQUIRE(c->getTimelineLength(240.0) == Approx(6.0));
        REQUIRE(c->getTimelineLength(60.0) == Approx(24.0));
        REQUIRE(c->getTimelineLength(PROJECT_BPM) == Approx(12.0));
    }
}

TEST_CASE("source interpretation BPM correction does not move the clip on the timeline",
          "[clip][bpm][issue-1157]") {
    ClipManager::getInstance().shutdown();

    auto seed = makeSessionAutoTempoClip();
    ClipManager::getInstance().restoreClip(seed);

    // Initial state: clip is 24 timeline beats long, file detected at 120 BPM,
    // loop covers all 16 source beats (loopLengthBeats matches source interpretation total beats).
    seed.lengthBeats = 24.0;
    seed.loopLengthBeats = 16.0;
    seed.audio().interpretation.bpm = 120.0;
    seed.audio().interpretation.totalBeats = 16.0;
    ClipManager::getInstance().restoreClip(
        seed);  // no-op (already there) — values applied directly
    ClipManager::AudioClipBeatsUpdate prime;
    prime.lengthBeats = 24.0;
    prime.loopLengthBeats = 16.0;
    prime.interpretationBpm = 120.0;
    prime.interpretationTotalBeats = 16.0;
    ClipManager::getInstance().applyAudioClipBeats(seed.id, prime, PROJECT_BPM);

    const auto* c = ClipManager::getInstance().getClip(seed.id);
    REQUIRE(c->length == Approx(12.0));  // 24 × 60 / 120

    SECTION("Doubling source interpretation BPM: timeline length unchanged, source seconds halve") {
        ClipManager::AudioClipBeatsUpdate u;
        u.interpretationBpm = 240.0;
        u.interpretationTotalBeats = 32.0;
        ClipManager::getInstance().applyAudioClipBeats(seed.id, u, PROJECT_BPM);

        c = ClipManager::getInstance().getClip(seed.id);
        REQUIRE(c->lengthBeats == Approx(24.0));
        REQUIRE(c->length == Approx(12.0));
        REQUIRE(c->loopLengthBeats == Approx(16.0));
        REQUIRE(c->loopLength == Approx(4.0));  // 16 × 60/240
    }

    SECTION("Halving source interpretation BPM: timeline length unchanged, source seconds double") {
        ClipManager::AudioClipBeatsUpdate u;
        u.interpretationBpm = 60.0;
        u.interpretationTotalBeats = 8.0;
        ClipManager::getInstance().applyAudioClipBeats(seed.id, u, PROJECT_BPM);

        c = ClipManager::getInstance().getClip(seed.id);
        REQUIRE(c->lengthBeats == Approx(24.0));
        REQUIRE(c->length == Approx(12.0));
        REQUIRE(c->loopLength == Approx(16.0));  // 16 × 60/60
    }
}

TEST_CASE("Beats-only edits never drift through float round-trips", "[clip][bpm][issue-1157]") {
    ClipManager::getInstance().shutdown();

    auto seed = makeSessionAutoTempoClip();
    ClipManager::getInstance().restoreClip(seed);

    // Stress: bounce the project BPM around and confirm lengthBeats is bit-stable.
    ClipManager::AudioClipBeatsUpdate u;
    u.lengthBeats = 24.0;
    ClipManager::getInstance().applyAudioClipBeats(seed.id, u, 120.0);

    const auto* c = ClipManager::getInstance().getClip(seed.id);
    const double originalBeats = c->lengthBeats;

    for (double bpm : {88.0, 99.0, 137.5, 60.0, 240.0, 120.0}) {
        ClipManager::getInstance().refreshDerivedSeconds(seed.id, bpm);
        c = ClipManager::getInstance().getClip(seed.id);
        REQUIRE(c->lengthBeats == originalBeats);  // exact equality
        REQUIRE(c->getTimelineLength(bpm) == Approx(originalBeats * 60.0 / bpm));
    }
}
