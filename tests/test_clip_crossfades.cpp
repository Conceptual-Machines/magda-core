#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "AudioClipTestHelpers.hpp"
#include "magda/daw/core/ClipCommands.hpp"
#include "magda/daw/core/ClipManager.hpp"
#include "magda/daw/core/ClipOcclusion.hpp"
#include "magda/daw/core/Config.hpp"
#include "magda/daw/core/TrackManager.hpp"
#include "magda/daw/core/UndoManager.hpp"

namespace {
/// Source duration is a pooled file fact, so a test that wants one seeds the
/// pool rather than writing it onto the clip.
void seedSourceDuration(magda::ClipInfo* clip, double durationSeconds) {
    auto* event = magda::primaryEventOf(clip);
    if (event == nullptr)
        return;
    auto& pool = magda::SourcePool::getInstance();
    pool.seedFactsForTesting(event->sourceFilePath(), durationSeconds,
                             magda::test::kTestSourceSampleRate);
    pool.resolveFacts(event->sourceId);
}
}  // namespace

/**
 * Tests for explicit audio clip crossfades (#1499).
 *
 * A crossfade is an actual overlap between two adjacent audio arrangement
 * clips whose autoCrossfade flags are set. The geometry lives in the beat
 * placements; TE derives the playback fades from the overlap.
 *
 * At the default 120 BPM: 1 second = 2 beats.
 */

using namespace magda;

namespace {

void resetState() {
    UndoManager::getInstance().clearHistory();
    ClipManager::getInstance().clearAllClips();
    TrackManager::getInstance().clearAllTracks();
}

TrackId createTrack() {
    return TrackManager::getInstance().createTrack("Track", TrackType::Audio);
}

// Creates an audio clip with source headroom on both sides (offset 10 s into a
// 100 s file), so crossfade edge extensions aren't clamped by material limits.
// Extension clamping itself is covered by its own test case below.
// Sources are pooled per file (#1901), so a test that wants two clips with
// DIFFERENT source durations has to give them different files.
ClipId createAudioBeats(TrackId trackId, double startBeat, double lengthBeats,
                        const juce::String& filePath = "test.wav") {
    auto& cm = ClipManager::getInstance();
    const ClipId id =
        cm.createAudioClipBeats(trackId, startBeat, lengthBeats, filePath, ClipView::Arrangement);
    if (auto* clip = cm.getClip(id)) {
        magda::SourcePool::getInstance().seedFactsForTesting(
            primaryEventOf(clip)->sourceFilePath(), 100.0, magda::test::kTestSourceSampleRate);
        magda::SourcePool::getInstance().resolveFacts(primaryEventOf(clip)->sourceId);
        primaryEventOf(clip)->setAnchorSeconds(10.0);
        primaryEventOf(clip)->setLoopStartSeconds(10.0);
    }
    return id;
}

/// What a clip plays once the clips stacked over it are taken into account.
/// The model keeps every clip whole, so this is the only place the effect of a
/// cover shows up (#2003).
AudibleSpan audibleSpanFor(ClipId clipId) {
    auto& cm = ClipManager::getInstance();
    const auto* clip = cm.getClip(clipId);
    REQUIRE(clip != nullptr);

    std::vector<ClipInfo> lane;
    for (ClipId other : cm.getClipsOnTrack(clip->trackId, ClipView::Arrangement)) {
        if (const auto* c = cm.getClip(other))
            lane.push_back(*c);
    }
    return computeAudibleSpans(lane).at(clipId);
}

}  // namespace

TEST_CASE("new audio clips follow the auto-crossfade config default", "[crossfade]") {
    resetState();
    auto& cm = ClipManager::getInstance();
    const auto trackId = createTrack();

    const ClipId id = createAudioBeats(trackId, 0.0, 4.0);
    CHECK(cm.getClip(id)->autoCrossfade == Config::getInstance().getAutoCrossfadeByDefault());
}

// ============================================================================
// setCrossfadeBeats — creation, resize, removal
// ============================================================================

TEST_CASE("setCrossfadeBeats creates an overlap at a butt joint and flags both clips",
          "[crossfade]") {
    resetState();
    auto& cm = ClipManager::getInstance();
    const auto trackId = createTrack();

    const ClipId a = createAudioBeats(trackId, 0.0, 8.0);
    const ClipId b = createAudioBeats(trackId, 8.0, 8.0);

    REQUIRE(cm.setCrossfadeBeats(a, b, 2.0));

    const auto* left = cm.getClip(a);
    const auto* right = cm.getClip(b);
    REQUIRE(left != nullptr);
    REQUIRE(right != nullptr);

    // Overlap centred on the old joint (beat 8): [7, 9]
    CHECK(left->placement.endBeat() == Catch::Approx(9.0));
    CHECK(right->placement.startBeat == Catch::Approx(7.0));
    CHECK(right->placement.endBeat() == Catch::Approx(16.0));
    CHECK(left->autoCrossfade);
    CHECK(right->autoCrossfade);

    auto xfB = cm.getCrossfadeAtStart(b);
    REQUIRE(xfB.has_value());
    CHECK(xfB->leftClipId == a);
    CHECK(xfB->rightClipId == b);
    CHECK(xfB->lengthBeats() == Catch::Approx(2.0));

    auto xfA = cm.getCrossfadeAtEnd(a);
    REQUIRE(xfA.has_value());
    CHECK(xfA->startBeat == Catch::Approx(7.0));
    CHECK(xfA->endBeat == Catch::Approx(9.0));
}

TEST_CASE("setCrossfadeBeats clamps so neither clip is swallowed", "[crossfade]") {
    resetState();
    auto& cm = ClipManager::getInstance();
    const auto trackId = createTrack();

    const ClipId a = createAudioBeats(trackId, 0.0, 4.0);
    const ClipId b = createAudioBeats(trackId, 4.0, 4.0);

    // Absurd duration: must clamp to a partial overlap, never containment
    REQUIRE(cm.setCrossfadeBeats(a, b, 100.0));

    const auto* left = cm.getClip(a);
    const auto* right = cm.getClip(b);
    CHECK(right->placement.startBeat > left->placement.startBeat);
    CHECK(left->placement.endBeat() < right->placement.endBeat());
    CHECK(cm.getCrossfadeAtStart(b).has_value());
}

TEST_CASE("setCrossfadeBeats with zero duration butts the joint (removal)", "[crossfade]") {
    resetState();
    auto& cm = ClipManager::getInstance();
    const auto trackId = createTrack();

    const ClipId a = createAudioBeats(trackId, 0.0, 8.0);
    const ClipId b = createAudioBeats(trackId, 8.0, 8.0);
    REQUIRE(cm.setCrossfadeBeats(a, b, 2.0));
    REQUIRE(cm.getCrossfadeAtStart(b).has_value());

    REQUIRE(cm.setCrossfadeBeats(a, b, 0.0));

    const auto* left = cm.getClip(a);
    const auto* right = cm.getClip(b);
    CHECK(left->placement.endBeat() == Catch::Approx(8.0));
    CHECK(right->placement.startBeat == Catch::Approx(8.0));
    CHECK_FALSE(cm.getCrossfadeAtStart(b).has_value());
    CHECK_FALSE(cm.getCrossfadeAtEnd(a).has_value());
}

TEST_CASE("setCrossfadeBeats rejects invalid pairs", "[crossfade]") {
    resetState();
    auto& cm = ClipManager::getInstance();
    const auto trackId = createTrack();
    const auto otherTrackId = createTrack();

    const ClipId a = createAudioBeats(trackId, 0.0, 8.0);
    const ClipId otherTrack = createAudioBeats(otherTrackId, 8.0, 8.0);
    const ClipId midi = cm.createMidiClipBeats(trackId, 8.0, 8.0, ClipView::Arrangement);

    CHECK_FALSE(cm.setCrossfadeBeats(a, otherTrack, 2.0));
    CHECK_FALSE(cm.setCrossfadeBeats(a, midi, 2.0));
    CHECK_FALSE(cm.setCrossfadeBeats(a, a, 2.0));
    CHECK_FALSE(cm.setCrossfadeBeats(a, INVALID_CLIP_ID, 2.0));
}

TEST_CASE("setCrossfadeRegionBeats respects source material bounds", "[crossfade]") {
    resetState();
    auto& cm = ClipManager::getInstance();
    const auto trackId = createTrack();

    // 4 beats = 2 s at 120 BPM. Give both clips exactly 2 s of source with no
    // spare material: the left clip cannot extend right, the right clip cannot
    // extend left (offset 0), so no overlap can be created.
    const ClipId a = createAudioBeats(trackId, 0.0, 4.0, "xfade_a.wav");
    const ClipId b = createAudioBeats(trackId, 4.0, 4.0, "xfade_b.wav");
    seedSourceDuration(cm.getClip(a), 2.0);
    seedSourceDuration(cm.getClip(b), 2.0);
    magda::primaryEventOf(cm.getClip(a))->setAnchorSeconds(0.0);
    magda::primaryEventOf(cm.getClip(b))->setAnchorSeconds(0.0);

    REQUIRE(cm.setCrossfadeBeats(a, b, 2.0));

    CHECK(cm.getClip(a)->placement.endBeat() == Catch::Approx(4.0));
    CHECK(cm.getClip(b)->placement.startBeat == Catch::Approx(4.0));
    CHECK_FALSE(cm.getCrossfadeAtStart(b).has_value());

    // Give the right clip 1 s of pre-roll material (offset 1 s): its edge can
    // now extend up to 2 beats left, the left clip still cannot move.
    seedSourceDuration(cm.getClip(b), 3.0);
    magda::primaryEventOf(cm.getClip(b))->setAnchorSeconds(1.0);

    REQUIRE(cm.setCrossfadeBeats(a, b, 4.0));
    CHECK(cm.getClip(a)->placement.endBeat() == Catch::Approx(4.0));
    CHECK(cm.getClip(b)->placement.startBeat == Catch::Approx(2.0));

    auto xf = cm.getCrossfadeAtStart(b);
    REQUIRE(xf.has_value());
    CHECK(xf->lengthBeats() == Catch::Approx(2.0));
}

// ============================================================================
// resolveOverlaps — crossfade-aware overlap policy
// ============================================================================

TEST_CASE("moving an auto-crossfade clip onto a neighbour keeps the overlap", "[crossfade]") {
    resetState();
    auto& cm = ClipManager::getInstance();
    const auto trackId = createTrack();

    const ClipId a = createAudioBeats(trackId, 0.0, 8.0);
    const ClipId b = createAudioBeats(trackId, 10.0, 8.0);
    cm.setAutoCrossfade(b, true);

    cm.moveClipBeats(b, 6.0);

    // Overlap [6, 8] survives as a crossfade; the neighbour gets the flag too
    CHECK(cm.getClip(a)->placement.endBeat() == Catch::Approx(8.0));
    CHECK(cm.getClip(b)->placement.startBeat == Catch::Approx(6.0));
    CHECK(cm.getClip(a)->autoCrossfade);

    auto xf = cm.getCrossfadeAtStart(b);
    REQUIRE(xf.has_value());
    CHECK(xf->leftClipId == a);
    CHECK(xf->startBeat == Catch::Approx(6.0));
    CHECK(xf->endBeat == Catch::Approx(8.0));
}

TEST_CASE("moving a clip without auto-crossfade covers the neighbour's tail", "[crossfade]") {
    resetState();
    auto& cm = ClipManager::getInstance();
    const auto trackId = createTrack();

    // New audio clips default to AUTO-XFADE; pin it off so this is a cover
    const ClipId a = createAudioBeats(trackId, 0.0, 8.0);
    const ClipId b = createAudioBeats(trackId, 10.0, 8.0);
    cm.setAutoCrossfade(a, false);
    cm.setAutoCrossfade(b, false);

    cm.moveClipBeats(b, 6.0);

    // A is not cut: it keeps its placement and simply stops being heard where
    // B sits on top of it, so moving B away gives the tail back (#2003).
    CHECK(cm.getClip(a)->placement.endBeat() == Catch::Approx(8.0));
    CHECK(audibleSpanFor(a).endBeat() == Catch::Approx(6.0));
    CHECK_FALSE(cm.getCrossfadeAtStart(b).has_value());

    cm.moveClipBeats(b, 20.0);
    CHECK(audibleSpanFor(a).endBeat() == Catch::Approx(8.0));
}

TEST_CASE("auto-crossfade never applies to MIDI clips", "[crossfade]") {
    resetState();
    auto& cm = ClipManager::getInstance();
    const auto trackId = createTrack();

    const ClipId midi = cm.createMidiClipBeats(trackId, 0.0, 8.0, ClipView::Arrangement);
    const ClipId b = createAudioBeats(trackId, 10.0, 8.0);
    cm.setAutoCrossfade(b, true);

    cm.moveClipBeats(b, 6.0);

    // No fade to protect, so the audio clip covers the MIDI clip's tail — but
    // the MIDI clip itself is left alone.
    CHECK(cm.getClip(midi)->placement.endBeat() == Catch::Approx(8.0));
    CHECK(audibleSpanFor(midi).endBeat() == Catch::Approx(6.0));
    CHECK_FALSE(cm.getCrossfadeAtStart(b).has_value());
}

TEST_CASE("a clip covered end to end goes silent and stays put", "[crossfade]") {
    resetState();
    auto& cm = ClipManager::getInstance();
    const auto trackId = createTrack();

    const ClipId a = createAudioBeats(trackId, 2.0, 2.0);
    const ClipId b = createAudioBeats(trackId, 10.0, 8.0);
    cm.setAutoCrossfade(b, true);

    cm.moveClipBeats(b, 0.0);

    // Not deleted, not disabled, not moved — just covered.
    const auto* covered = cm.getClip(a);
    REQUIRE(covered != nullptr);
    CHECK(covered->enabled);
    CHECK(covered->placement.startBeat == Catch::Approx(2.0));
    CHECK(covered->placement.lengthBeats == Catch::Approx(2.0));
    CHECK_FALSE(audibleSpanFor(a).audible);

    // Pull B off it and it plays again, with no restore step.
    cm.moveClipBeats(b, 20.0);
    const auto uncovered = audibleSpanFor(a);
    CHECK(uncovered.audible);
    CHECK(uncovered.startBeat == Catch::Approx(2.0));
    CHECK(uncovered.lengthBeats == Catch::Approx(2.0));
}

TEST_CASE("an existing crossfade survives edits to other clips on the track", "[crossfade]") {
    resetState();
    auto& cm = ClipManager::getInstance();
    const auto trackId = createTrack();

    const ClipId a = createAudioBeats(trackId, 0.0, 8.0);
    const ClipId b = createAudioBeats(trackId, 8.0, 8.0);
    REQUIRE(cm.setCrossfadeBeats(a, b, 2.0));

    // A third clip lands elsewhere on the track — the A/B crossfade is untouched
    const ClipId c = createAudioBeats(trackId, 30.0, 4.0);
    cm.moveClipBeats(c, 20.0);

    auto xf = cm.getCrossfadeAtStart(b);
    REQUIRE(xf.has_value());
    CHECK(xf->lengthBeats() == Catch::Approx(2.0));
}

// ============================================================================
// SetCrossfadeCommand — undo/redo
// ============================================================================

TEST_CASE("SetCrossfadeCommand undo restores both clips exactly", "[crossfade]") {
    resetState();
    auto& cm = ClipManager::getInstance();
    auto& um = UndoManager::getInstance();
    const auto trackId = createTrack();

    // Pin AUTO-XFADE off so the command's flag-enabling side effect (and its
    // undo) is observable regardless of the creation default.
    const ClipId a = createAudioBeats(trackId, 0.0, 8.0);
    const ClipId b = createAudioBeats(trackId, 8.0, 8.0);
    cm.setAutoCrossfade(a, false);
    cm.setAutoCrossfade(b, false);
    const double aEndBefore = cm.getClip(a)->placement.endBeat();
    const double bStartBefore = cm.getClip(b)->placement.startBeat;
    const double bOffsetBefore = magda::audioEventRef(*cm.getClip(b)).anchorSeconds();

    um.executeCommand(std::make_unique<SetCrossfadeCommand>(a, b, 7.0, 9.0, 120.0));

    REQUIRE(cm.getCrossfadeAtStart(b).has_value());

    um.undo();
    CHECK(cm.getClip(a)->placement.endBeat() == Catch::Approx(aEndBefore));
    CHECK(cm.getClip(b)->placement.startBeat == Catch::Approx(bStartBefore));
    CHECK(magda::audioEventRef(*cm.getClip(b)).anchorSeconds() == Catch::Approx(bOffsetBefore));
    CHECK_FALSE(cm.getClip(a)->autoCrossfade);
    CHECK_FALSE(cm.getClip(b)->autoCrossfade);
    CHECK_FALSE(cm.getCrossfadeAtStart(b).has_value());

    um.redo();
    auto xf = cm.getCrossfadeAtStart(b);
    REQUIRE(xf.has_value());
    CHECK(xf->startBeat == Catch::Approx(7.0));
    CHECK(xf->endBeat == Catch::Approx(9.0));
    CHECK(cm.getClip(a)->autoCrossfade);
    CHECK(cm.getClip(b)->autoCrossfade);
}

TEST_CASE("crossfade region edits keep the untouched edge fixed", "[crossfade]") {
    resetState();
    auto& cm = ClipManager::getInstance();
    const auto trackId = createTrack();

    const ClipId a = createAudioBeats(trackId, 0.0, 8.0);
    const ClipId b = createAudioBeats(trackId, 8.0, 8.0);
    REQUIRE(cm.setCrossfadeBeats(a, b, 2.0));  // region [7, 9]

    // Drag the overlap end (the fade-in handle gesture): start stays at 7
    REQUIRE(cm.setCrossfadeRegionBeats(a, b, 7.0, 10.0));
    auto xf = cm.getCrossfadeAtStart(b);
    REQUIRE(xf.has_value());
    CHECK(xf->startBeat == Catch::Approx(7.0));
    CHECK(xf->endBeat == Catch::Approx(10.0));

    // Drag the overlap start (the fade-out handle gesture): end stays at 10
    REQUIRE(cm.setCrossfadeRegionBeats(a, b, 6.0, 10.0));
    xf = cm.getCrossfadeAtStart(b);
    REQUIRE(xf.has_value());
    CHECK(xf->startBeat == Catch::Approx(6.0));
    CHECK(xf->endBeat == Catch::Approx(10.0));
}

// Dropping an audio clip INSIDE another splits the lower clip, which leaves
// butt joints on both sides. A joint is not an overlap, so no crossfade may
// come out of it — an automatic fade there is a bug, not a feature (#2003).
TEST_CASE("dropping a clip inside another creates no crossfade", "[crossfade][overlap]") {
    resetState();
    auto& cm = ClipManager::getInstance();
    const auto trackId = createTrack();

    const ClipId longClip = createAudioBeats(trackId, 0.0, 16.0);
    const ClipId dropped = createAudioBeats(trackId, 40.0, 4.0);
    cm.setAutoCrossfade(longClip, true);
    cm.setAutoCrossfade(dropped, true);

    cm.moveClipBeats(dropped, 6.0);  // -> [6,10], strictly inside [0,16]

    CHECK_FALSE(cm.getCrossfadeAtStart(dropped).has_value());
    CHECK_FALSE(cm.getCrossfadeAtEnd(dropped).has_value());

    // Nor on the pieces the split produced.
    for (ClipId id : cm.getClipsOnTrack(trackId)) {
        if (id == dropped)
            continue;
        CHECK_FALSE(cm.getCrossfadeAtStart(id).has_value());
        CHECK_FALSE(cm.getCrossfadeAtEnd(id).has_value());
    }
}

// While a clip is being dragged the model still holds where it started, so the
// overlay has to be asked against the previewed lane instead — otherwise the
// crossfade a clip is dragging itself out of stays drawn until the mouse comes
// up, and one it is dragging itself into does not appear at all (#2003).
TEST_CASE("crossfades can be resolved against a previewed lane", "[crossfade][overlap]") {
    resetState();
    auto& cm = ClipManager::getInstance();
    const auto trackId = createTrack();

    const ClipId a = createAudioBeats(trackId, 0.0, 8.0);
    // Creation shifts a clip clear of what is already there, so the overlap is
    // made by moving it, the way the user would.
    const ClipId b = createAudioBeats(trackId, 20.0, 8.0);
    cm.setAutoCrossfade(a, true);
    cm.setAutoCrossfade(b, true);
    cm.moveClipBeats(b, 6.0);

    auto laneFromModel = [&]() {
        std::vector<ClipInfo> lane;
        for (ClipId id : cm.getClipsOnTrack(trackId, ClipView::Arrangement)) {
            if (const auto* clip = cm.getClip(id))
                lane.push_back(*clip);
        }
        return lane;
    };

    // Committed state: A and B overlap over [6,8].
    REQUIRE(cm.getCrossfadeAtStart(b).has_value());

    SECTION("dragging clear of the overlap drops the crossfade immediately") {
        auto lane = laneFromModel();
        for (auto& clip : lane) {
            if (clip.id == b)
                clip.setPlacementBeats(20.0, clip.placement.lengthBeats);
        }

        CHECK_FALSE(ClipManager::crossfadeAtStartIn(lane, b).has_value());
        CHECK_FALSE(ClipManager::crossfadeAtEndIn(lane, a).has_value());
        // The model is untouched until the mouse comes up.
        CHECK(cm.getCrossfadeAtStart(b).has_value());
    }

    SECTION("dragging into an overlap shows the crossfade before release") {
        auto lane = laneFromModel();
        for (auto& clip : lane) {
            if (clip.id == b)
                clip.setPlacementBeats(4.0, clip.placement.lengthBeats);
        }

        auto xf = ClipManager::crossfadeAtStartIn(lane, b);
        REQUIRE(xf.has_value());
        CHECK(xf->leftClipId == a);
        CHECK(xf->startBeat == Catch::Approx(4.0));
        CHECK(xf->endBeat == Catch::Approx(8.0));
    }

    SECTION("a clip dragged fully inside another is a cover, not a joint") {
        auto lane = laneFromModel();
        for (auto& clip : lane) {
            if (clip.id == b)
                clip.setPlacementBeats(2.0, 4.0);
        }

        CHECK_FALSE(ClipManager::crossfadeAtStartIn(lane, b).has_value());
        CHECK_FALSE(ClipManager::crossfadeAtEndIn(lane, b).has_value());
    }
}
