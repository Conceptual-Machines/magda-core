#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "../magda/daw/core/AutomationCommands.hpp"
#include "../magda/daw/core/AutomationManager.hpp"
#include "../magda/daw/core/ClipLaneFlattener.hpp"
#include "../magda/daw/core/TrackManager.hpp"
#include "../magda/daw/core/UndoManager.hpp"

using namespace magda;
using Catch::Approx;

// Model + flattener coverage for clip-based automation lanes (issue #1087):
// gap-hold value semantics and the bake-time loop unroll.

namespace {

struct ClipLaneFixture {
    AutomationLaneId laneId = INVALID_AUTOMATION_LANE_ID;

    ClipLaneFixture() {
        AutomationManager::getInstance().clearAll();
        TrackManager::getInstance().clearAllTracks();
        auto trackId = TrackManager::getInstance().createTrack("T", TrackType::Audio);
        laneId = AutomationManager::getInstance().getOrCreateLane(
            ControlTarget::trackVolume(trackId), AutomationLaneType::ClipBased);
    }

    const AutomationLaneInfo& lane() const {
        return *AutomationManager::getInstance().getLane(laneId);
    }

    /** Clip with a 0 -> 1 rising ramp across rampLenBeats. */
    AutomationClipId addRampClip(double startBeats, double lengthBeats, double rampLenBeats) {
        auto& mgr = AutomationManager::getInstance();
        auto clipId = mgr.createClip(laneId, startBeats, lengthBeats);
        mgr.addPointToClip(clipId, 0.0, 0.0);
        mgr.addPointToClip(clipId, rampLenBeats, 1.0);
        return clipId;
    }

    std::vector<AutomationPoint> flatten() const {
        auto& mgr = AutomationManager::getInstance();
        auto id = laneId;
        return flattenClipLane(
            lane(), [&mgr](AutomationClipId clipId) { return mgr.getClip(clipId); },
            [&mgr, id](double beat) { return mgr.getValueAtBeat(id, beat); });
    }
};

double flattenedValueAt(const std::vector<AutomationPoint>& pts, double beat) {
    REQUIRE(pts.size() >= 2);
    if (beat <= pts.front().beatPosition)
        return pts.front().value;
    for (size_t i = 1; i < pts.size(); ++i) {
        if (pts[i].beatPosition >= beat) {
            const auto& a = pts[i - 1];
            const auto& b = pts[i];
            const double span = b.beatPosition - a.beatPosition;
            const double t = span > 0.0 ? (beat - a.beatPosition) / span : 0.0;
            return a.value + t * (b.value - a.value);
        }
    }
    return pts.back().value;
}

}  // namespace

TEST_CASE("AutomationClipInfo::getEndLocalBeat", "[automation][cliplane]") {
    AutomationClipInfo clip;
    clip.lengthBeats = 6.0;

    SECTION("non-looping: the clip end is the local end") {
        REQUIRE(clip.getEndLocalBeat() == Approx(6.0));
    }
    SECTION("looping with a truncated final cycle") {
        clip.looping = true;
        clip.loopLengthBeats = 4.0;
        REQUIRE(clip.getEndLocalBeat() == Approx(2.0));
    }
    SECTION("looping with the end exactly on a cycle boundary") {
        clip.looping = true;
        clip.loopLengthBeats = 3.0;
        REQUIRE(clip.getEndLocalBeat() == Approx(3.0));
    }
    SECTION("non-representable length/loop ratio still lands on the boundary") {
        // fmod(0.9, 0.3) is ~5.6e-17, not 0: an exact compare would hold the
        // cycle-START value across the following gap.
        clip.looping = true;
        clip.lengthBeats = 0.9;
        clip.loopLengthBeats = 0.3;
        REQUIRE(clip.getEndLocalBeat() == Approx(0.3));
    }
}

TEST_CASE("getValueAtBeat - clip lane loop wrap and gap hold", "[automation][cliplane]") {
    ClipLaneFixture fx;
    auto& mgr = AutomationManager::getInstance();

    // Clip at [4, 12): 0 -> 1 ramp over 4 beats, looping every 4 beats.
    auto clipId = fx.addRampClip(4.0, 8.0, 4.0);
    mgr.setClipLooping(clipId, true);
    mgr.setClipLoopLength(clipId, 4.0);

    SECTION("inside the first iteration") {
        REQUIRE(mgr.getValueAtBeat(fx.laneId, 4.0) == Approx(0.0));
        REQUIRE(mgr.getValueAtBeat(fx.laneId, 6.0) == Approx(0.5));
    }
    SECTION("loop wraps back to the ramp start") {
        REQUIRE(mgr.getValueAtBeat(fx.laneId, 8.0) == Approx(0.0));
        REQUIRE(mgr.getValueAtBeat(fx.laneId, 11.0) == Approx(0.75));
    }
    SECTION("before the first clip holds its initial value") {
        REQUIRE(mgr.getValueAtBeat(fx.laneId, 0.0) == Approx(0.0));
        REQUIRE(mgr.getValueAtBeat(fx.laneId, 3.9) == Approx(0.0));
    }
    SECTION("after the last clip holds its outgoing value") {
        // End local beat = full cycle end -> ramp value 1.0.
        REQUIRE(mgr.getValueAtBeat(fx.laneId, 12.0) == Approx(1.0));
        REQUIRE(mgr.getValueAtBeat(fx.laneId, 100.0) == Approx(1.0));
    }
    SECTION("gap between two clips holds the earlier clip's end value") {
        // Second clip at [20, 24), constant-ish: single ramp over its length.
        fx.addRampClip(20.0, 4.0, 4.0);
        REQUIRE(mgr.getValueAtBeat(fx.laneId, 16.0) == Approx(1.0));
        REQUIRE(mgr.getValueAtBeat(fx.laneId, 19.9) == Approx(1.0));
    }
    SECTION("lane with no clips falls back to 0.5") {
        mgr.deleteClip(clipId);
        REQUIRE(mgr.getValueAtBeat(fx.laneId, 4.0) == Approx(0.5));
    }
}

TEST_CASE("flattenClipLane - single non-looping clip", "[automation][cliplane]") {
    ClipLaneFixture fx;
    fx.addRampClip(4.0, 4.0, 4.0);

    auto pts = fx.flatten();
    REQUIRE(pts.size() >= 3);

    // Monotonic positions.
    for (size_t i = 1; i < pts.size(); ++i)
        REQUIRE(pts[i].beatPosition > pts[i - 1].beatPosition);

    // The ramp is reproduced on the timeline.
    REQUIRE(flattenedValueAt(pts, 4.0) == Approx(0.0).margin(0.001));
    REQUIRE(flattenedValueAt(pts, 6.0) == Approx(0.5).margin(0.001));
    REQUIRE(flattenedValueAt(pts, 8.0) == Approx(1.0).margin(0.001));
    // Hold before the clip.
    REQUIRE(flattenedValueAt(pts, 1.0) == Approx(0.0).margin(0.001));
}

TEST_CASE("flattenClipLane - looping clip unrolls with wrap jumps", "[automation][cliplane]") {
    ClipLaneFixture fx;
    auto& mgr = AutomationManager::getInstance();
    auto clipId = fx.addRampClip(0.0, 8.0, 4.0);
    mgr.setClipLooping(clipId, true);
    mgr.setClipLoopLength(clipId, 4.0);

    auto pts = fx.flatten();

    // Two iterations of the ramp: high just before the wrap, low right after.
    REQUIRE(flattenedValueAt(pts, 3.99) == Approx(1.0).margin(0.02));
    REQUIRE(flattenedValueAt(pts, 4.001) == Approx(0.0).margin(0.02));
    REQUIRE(flattenedValueAt(pts, 6.0) == Approx(0.5).margin(0.001));
    REQUIRE(flattenedValueAt(pts, 7.9) == Approx(0.97).margin(0.03));
}

TEST_CASE("flattenClipLane - gap between clips holds the outgoing value",
          "[automation][cliplane]") {
    ClipLaneFixture fx;
    fx.addRampClip(0.0, 4.0, 4.0);
    fx.addRampClip(8.0, 4.0, 4.0);

    auto pts = fx.flatten();

    // Gap [4, 8) holds the first clip's end value (1.0), then jumps to the
    // second clip's start (0.0).
    REQUIRE(flattenedValueAt(pts, 5.0) == Approx(1.0).margin(0.001));
    REQUIRE(flattenedValueAt(pts, 7.9) == Approx(1.0).margin(0.01));
    REQUIRE(flattenedValueAt(pts, 8.0) == Approx(0.0).margin(0.01));
    REQUIRE(flattenedValueAt(pts, 10.0) == Approx(0.5).margin(0.001));
}

TEST_CASE("flattenClipLane - truncated final loop iteration stops at the clip end",
          "[automation][cliplane]") {
    ClipLaneFixture fx;
    auto& mgr = AutomationManager::getInstance();
    // Length 6 with a 4-beat loop: second iteration is cut at local 2.
    auto clipId = fx.addRampClip(0.0, 6.0, 4.0);
    mgr.setClipLooping(clipId, true);
    mgr.setClipLoopLength(clipId, 4.0);

    auto pts = fx.flatten();

    REQUIRE(flattenedValueAt(pts, 5.0) == Approx(0.25).margin(0.01));
    // After the clip: hold the truncated iteration's outgoing value (0.5).
    REQUIRE(pts.back().beatPosition <= Approx(6.0));
    REQUIRE(flattenedValueAt(pts, 6.0) == Approx(0.5).margin(0.01));
}

TEST_CASE("flattenClipLane - front-priority clip wins inside its range", "[automation][cliplane]") {
    ClipLaneFixture fx;
    auto& mgr = AutomationManager::getInstance();

    // Underlying ramp [0, 8): 0 -> 1 over 8 beats.
    fx.addRampClip(0.0, 8.0, 8.0);
    // Front clip [2, 6): constant 0.25, moved to the front of clipIds (the
    // model's overlap precedence, same shape a bake produces).
    const auto frontId = mgr.createClip(fx.laneId, 2.0, 4.0);
    mgr.addPointToClip(frontId, 0.0, 0.25);
    mgr.addPointToClip(frontId, 4.0, 0.25);
    mgr.moveClipToFront(frontId);

    auto pts = fx.flatten();
    for (size_t i = 1; i < pts.size(); ++i)
        REQUIRE(pts[i].beatPosition > pts[i - 1].beatPosition);

    // Underlying ramp before the overlap.
    REQUIRE(flattenedValueAt(pts, 1.0) == Approx(0.125).margin(0.001));
    // Front-priority value throughout the overlap.
    REQUIRE(flattenedValueAt(pts, 2.5) == Approx(0.25).margin(0.001));
    REQUIRE(flattenedValueAt(pts, 4.0) == Approx(0.25).margin(0.001));
    REQUIRE(flattenedValueAt(pts, 5.9) == Approx(0.25).margin(0.01));
    // The underlying curve resumes mid-ramp at the overlap's far edge.
    REQUIRE(flattenedValueAt(pts, 6.0) == Approx(0.75).margin(0.01));
    REQUIRE(flattenedValueAt(pts, 7.0) == Approx(0.875).margin(0.01));
}

TEST_CASE("flattenClipLane - front clip overlapping the underlying clip's edges",
          "[automation][cliplane]") {
    ClipLaneFixture fx;
    auto& mgr = AutomationManager::getInstance();

    SECTION("overlap at the underlying clip's tail") {
        fx.addRampClip(0.0, 4.0, 4.0);                             // ramp [0, 4)
        const auto frontId = mgr.createClip(fx.laneId, 3.0, 2.0);  // [3, 5)
        mgr.addPointToClip(frontId, 0.0, 0.1);
        mgr.addPointToClip(frontId, 2.0, 0.1);
        mgr.moveClipToFront(frontId);

        auto pts = fx.flatten();
        REQUIRE(flattenedValueAt(pts, 1.0) == Approx(0.25).margin(0.001));
        REQUIRE(flattenedValueAt(pts, 2.9) == Approx(0.725).margin(0.01));
        REQUIRE(flattenedValueAt(pts, 3.5) == Approx(0.1).margin(0.01));
        REQUIRE(flattenedValueAt(pts, 4.5) == Approx(0.1).margin(0.01));
        // After the front clip: the gap holds its outgoing value.
        REQUIRE(flattenedValueAt(pts, 6.0) == Approx(0.1).margin(0.01));
    }

    SECTION("overlap at the underlying clip's head") {
        fx.addRampClip(4.0, 4.0, 4.0);                             // ramp [4, 8)
        const auto frontId = mgr.createClip(fx.laneId, 3.0, 2.0);  // [3, 5)
        mgr.addPointToClip(frontId, 0.0, 0.9);
        mgr.addPointToClip(frontId, 2.0, 0.9);
        mgr.moveClipToFront(frontId);

        auto pts = fx.flatten();
        REQUIRE(flattenedValueAt(pts, 3.5) == Approx(0.9).margin(0.01));
        REQUIRE(flattenedValueAt(pts, 4.9) == Approx(0.9).margin(0.01));
        // The underlying ramp resumes mid-curve (local beat 1 of 4 -> 0.25).
        REQUIRE(flattenedValueAt(pts, 5.0) == Approx(0.25).margin(0.01));
        REQUIRE(flattenedValueAt(pts, 7.0) == Approx(0.75).margin(0.01));
    }
}

TEST_CASE("flattenClipLane - baked front clip is audible in the flattened curve",
          "[automation][cliplane]") {
    ClipLaneFixture fx;
    auto& mgr = AutomationManager::getInstance();

    // The BakeModulationToClipCommand shape: a baked clip at the front of
    // clipIds inside a longer existing clip. The flattened TE curve used to
    // get ZERO breakpoints from it (monotonic guard), making the bake
    // inaudible even though the UI showed the baked curve.
    fx.addRampClip(0.0, 8.0, 8.0);
    std::vector<AutomationPoint> baked;
    for (int i = 0; i <= 4; ++i) {
        AutomationPoint p;
        p.beatPosition = 2.0 + i;
        p.value = 0.25;
        baked.push_back(p);
    }
    BakeModulationToClipCommand cmd(fx.laneId, 2.0, 6.0, std::move(baked), {});
    cmd.execute();
    REQUIRE(fx.lane().clipIds.front() == cmd.getCreatedClipId());

    auto pts = fx.flatten();
    int inBakedRange = 0;
    for (const auto& p : pts)
        if (p.beatPosition >= 2.0 && p.beatPosition < 6.0 - 0.001)
            ++inBakedRange;
    REQUIRE(inBakedRange > 0);
    REQUIRE(flattenedValueAt(pts, 4.0) == Approx(0.25).margin(0.001));
    // The underlying ramp still plays before and after the baked range.
    REQUIRE(flattenedValueAt(pts, 1.0) == Approx(0.125).margin(0.01));
    REQUIRE(flattenedValueAt(pts, 7.0) == Approx(0.875).margin(0.01));
}

TEST_CASE("flattenClipLane - absolute lanes and empty clip lanes return empty",
          "[automation][cliplane]") {
    ClipLaneFixture fx;
    REQUIRE(fx.flatten().empty());  // no clips yet

    AutomationLaneInfo absolute;
    absolute.type = AutomationLaneType::Absolute;
    REQUIRE(flattenClipLane(
                absolute, [](AutomationClipId) -> const AutomationClipInfo* { return nullptr; },
                [](double) { return 0.5; })
                .empty());
}

// ============================================================================
// Lane type conversion + clip commands (issue #1087)
// ============================================================================

TEST_CASE("convertLaneToClipBased wraps points into one clip", "[automation][cliplane]") {
    AutomationManager::getInstance().clearAll();
    TrackManager::getInstance().clearAllTracks();
    auto& mgr = AutomationManager::getInstance();
    auto trackId = TrackManager::getInstance().createTrack("T", TrackType::Audio);
    auto laneId =
        mgr.getOrCreateLane(ControlTarget::trackVolume(trackId), AutomationLaneType::Absolute);
    mgr.clearLanePoints(laneId);
    mgr.addPoint(laneId, 4.0, 0.2);
    mgr.addPoint(laneId, 8.0, 0.9);

    auto clipId = mgr.convertLaneToClipBased(laneId);
    REQUIRE(clipId != INVALID_AUTOMATION_CLIP_ID);

    const auto* lane = mgr.getLane(laneId);
    REQUIRE(lane->isClipBased());
    REQUIRE(lane->absolutePoints.empty());
    REQUIRE(lane->clipIds.size() == 1);

    const auto* clip = mgr.getClip(clipId);
    REQUIRE(clip->startBeats == Approx(4.0));
    REQUIRE(clip->lengthBeats == Approx(4.0));
    REQUIRE(clip->points.size() == 2);
    REQUIRE(clip->points[0].beatPosition == Approx(0.0));  // clip-local
    REQUIRE(clip->points[1].beatPosition == Approx(4.0));

    // The lane still evaluates identically on the timeline.
    REQUIRE(mgr.getValueAtBeat(laneId, 6.0) == Approx(0.55));
}

TEST_CASE("convertLaneToAbsolute flattens clips back to points", "[automation][cliplane]") {
    ClipLaneFixture fx;
    auto& mgr = AutomationManager::getInstance();
    auto clipId = fx.addRampClip(4.0, 8.0, 4.0);
    mgr.setClipLooping(clipId, true);
    mgr.setClipLoopLength(clipId, 4.0);

    const double before6 = mgr.getValueAtBeat(fx.laneId, 6.0);
    const double before11 = mgr.getValueAtBeat(fx.laneId, 11.0);

    mgr.convertLaneToAbsolute(fx.laneId);

    const auto* lane = mgr.getLane(fx.laneId);
    REQUIRE(lane->isAbsolute());
    REQUIRE(lane->clipIds.empty());
    REQUIRE_FALSE(lane->absolutePoints.empty());
    REQUIRE(mgr.getClip(clipId) == nullptr);

    REQUIRE(mgr.getValueAtBeat(fx.laneId, 6.0) == Approx(before6).margin(0.001));
    REQUIRE(mgr.getValueAtBeat(fx.laneId, 11.0) == Approx(before11).margin(0.001));
}

TEST_CASE("ConvertAutomationLaneTypeCommand - undo restores exact state",
          "[automation][cliplane]") {
    AutomationManager::getInstance().clearAll();
    TrackManager::getInstance().clearAllTracks();
    UndoManager::getInstance().clearHistory();
    auto& mgr = AutomationManager::getInstance();
    auto& undoMgr = UndoManager::getInstance();
    auto trackId = TrackManager::getInstance().createTrack("T", TrackType::Audio);
    auto laneId =
        mgr.getOrCreateLane(ControlTarget::trackVolume(trackId), AutomationLaneType::Absolute);
    mgr.clearLanePoints(laneId);
    mgr.addPoint(laneId, 0.0, 0.1);
    mgr.addPoint(laneId, 4.0, 0.7);
    const auto beforePoints = mgr.getLane(laneId)->absolutePoints;

    undoMgr.executeCommand(std::make_unique<ConvertAutomationLaneTypeCommand>(laneId));
    REQUIRE(mgr.getLane(laneId)->isClipBased());

    REQUIRE(undoMgr.undo());
    const auto* lane = mgr.getLane(laneId);
    REQUIRE(lane->isAbsolute());
    REQUIRE(lane->absolutePoints.size() == beforePoints.size());
    for (size_t i = 0; i < beforePoints.size(); ++i) {
        REQUIRE(lane->absolutePoints[i].id == beforePoints[i].id);
        REQUIRE(lane->absolutePoints[i].beatPosition == Approx(beforePoints[i].beatPosition));
        REQUIRE(lane->absolutePoints[i].value == Approx(beforePoints[i].value));
    }

    // And the round trip back: clip lane converts to absolute, undo restores
    // the clip (same id, same points).
    undoMgr.executeCommand(std::make_unique<ConvertAutomationLaneTypeCommand>(laneId));
    const auto clipIds = mgr.getLane(laneId)->clipIds;
    REQUIRE(clipIds.size() == 1);
    undoMgr.executeCommand(std::make_unique<ConvertAutomationLaneTypeCommand>(laneId));
    REQUIRE(mgr.getLane(laneId)->isAbsolute());
    REQUIRE(undoMgr.undo());
    REQUIRE(mgr.getLane(laneId)->isClipBased());
    REQUIRE(mgr.getLane(laneId)->clipIds == clipIds);
    REQUIRE(mgr.getClip(clipIds.front()) != nullptr);
}

TEST_CASE("BakeModulationCommand no-ops on a clip-based lane", "[automation][cliplane]") {
    // getOrCreateLane returns the existing lane regardless of the requested
    // type, so a bake can be pointed at a clip-based lane. It must not touch
    // the lane: no absolute points written, clip content intact. (The bake
    // menu entry is disabled for these targets; this covers the guard.)
    ClipLaneFixture fx;
    auto& mgr = AutomationManager::getInstance();
    const auto clipId = fx.addRampClip(0.0, 4.0, 4.0);
    const auto clipPointsBefore = mgr.getClip(clipId)->points.size();

    std::vector<AutomationPoint> baked;
    AutomationPoint p;
    p.beatPosition = 0.0;
    p.value = 0.25;
    baked.push_back(p);
    p.beatPosition = 4.0;
    baked.push_back(p);

    BakeModulationCommand cmd(fx.laneId, 0.0, 4.0, std::move(baked), {});
    cmd.execute();

    REQUIRE(fx.lane().absolutePoints.empty());
    REQUIRE(mgr.getClip(clipId)->points.size() == clipPointsBefore);

    // Undo must also be a no-op (nothing was captured or replaced).
    cmd.undo();
    REQUIRE(fx.lane().absolutePoints.empty());
    REQUIRE(mgr.getClip(clipId)->points.size() == clipPointsBefore);
}

TEST_CASE("retypeEmptyLane flips only data-less lanes", "[automation][cliplane]") {
    ClipLaneFixture fx;
    auto& mgr = AutomationManager::getInstance();

    SECTION("empty lane flips both ways") {
        REQUIRE(mgr.retypeEmptyLane(fx.laneId, AutomationLaneType::Absolute));
        REQUIRE(fx.lane().isAbsolute());
        REQUIRE(mgr.retypeEmptyLane(fx.laneId, AutomationLaneType::ClipBased));
        REQUIRE(fx.lane().isClipBased());
    }
    SECTION("matching type is a no-op success") {
        REQUIRE(mgr.retypeEmptyLane(fx.laneId, AutomationLaneType::ClipBased));
        REQUIRE(fx.lane().isClipBased());
    }
    SECTION("a lane with clips refuses") {
        fx.addRampClip(0.0, 4.0, 4.0);
        REQUIRE_FALSE(mgr.retypeEmptyLane(fx.laneId, AutomationLaneType::Absolute));
        REQUIRE(fx.lane().isClipBased());
    }
    SECTION("a lane with absolute points refuses") {
        REQUIRE(mgr.retypeEmptyLane(fx.laneId, AutomationLaneType::Absolute));
        mgr.addPoint(fx.laneId, 0.0, 0.5);
        REQUIRE_FALSE(mgr.retypeEmptyLane(fx.laneId, AutomationLaneType::ClipBased));
        REQUIRE(fx.lane().isAbsolute());
    }
}

TEST_CASE("Rename and colour clip commands with undo", "[automation][cliplane]") {
    ClipLaneFixture fx;
    auto& mgr = AutomationManager::getInstance();
    const auto clipId = fx.addRampClip(0.0, 4.0, 4.0);
    const auto originalName = mgr.getClip(clipId)->name;
    const auto originalColour = mgr.getClip(clipId)->colour;

    RenameAutomationClipCommand rename(clipId, "Sweep");
    rename.execute();
    REQUIRE(mgr.getClip(clipId)->name == "Sweep");
    rename.undo();
    REQUIRE(mgr.getClip(clipId)->name == originalName);

    SetAutomationClipColourCommand colour(clipId, juce::Colour(0xFF123456));
    colour.execute();
    REQUIRE(mgr.getClip(clipId)->colour == juce::Colour(0xFF123456));
    colour.undo();
    REQUIRE(mgr.getClip(clipId)->colour == originalColour);
}

TEST_CASE("New clips inherit the track colour", "[automation][cliplane]") {
    auto& mgr = AutomationManager::getInstance();
    auto& trackMgr = TrackManager::getInstance();
    mgr.clearAll();
    trackMgr.clearAllTracks();

    const auto trackId = trackMgr.createTrack("T", TrackType::Audio);
    trackMgr.setTrackColour(trackId, juce::Colour(0xFFAB4321));
    const auto laneId =
        mgr.getOrCreateLane(ControlTarget::trackVolume(trackId), AutomationLaneType::ClipBased);

    const auto clipId = mgr.createClip(laneId, 0.0, 4.0);
    REQUIRE(mgr.getClip(clipId)->colour == juce::Colour(0xFFAB4321));
}

TEST_CASE("BakeModulationToClipCommand bakes into a front-priority clip",
          "[automation][cliplane]") {
    ClipLaneFixture fx;
    auto& mgr = AutomationManager::getInstance();

    // Existing clip overlapping the bake range; the baked clip must win
    // playback over it (first-in-clipIds rule).
    const auto existingId = fx.addRampClip(0.0, 8.0, 8.0);

    std::vector<AutomationPoint> baked;
    for (int i = 0; i <= 4; ++i) {
        AutomationPoint p;
        p.beatPosition = 2.0 + i;  // timeline beats [2, 6]
        p.value = 0.25;
        baked.push_back(p);
    }

    BakeModulationToClipCommand cmd(fx.laneId, 2.0, 6.0, std::move(baked), {});
    cmd.execute();

    const auto bakedClipId = cmd.getCreatedClipId();
    REQUIRE(bakedClipId != INVALID_AUTOMATION_CLIP_ID);
    const auto* bakedClip = mgr.getClip(bakedClipId);
    REQUIRE(bakedClip != nullptr);
    REQUIRE(bakedClip->startBeats == Approx(2.0));
    REQUIRE(bakedClip->lengthBeats == Approx(4.0));
    // Points converted to clip-local beats.
    REQUIRE(bakedClip->points.front().beatPosition == Approx(0.0));
    REQUIRE(bakedClip->points.back().beatPosition == Approx(4.0));
    // Front of clipIds -> the baked value wins over the overlapping ramp.
    REQUIRE(fx.lane().clipIds.front() == bakedClipId);
    REQUIRE(mgr.getValueAtBeat(fx.laneId, 4.0) == Approx(0.25));

    // Undo removes the clip and restores the ramp's playback.
    cmd.undo();
    REQUIRE(mgr.getClip(bakedClipId) == nullptr);
    REQUIRE(fx.lane().clipIds == std::vector<AutomationClipId>{existingId});
    REQUIRE(mgr.getValueAtBeat(fx.laneId, 4.0) == Approx(0.5));
}

TEST_CASE("BakeModulationToClipCommand loops one baked cycle across the range",
          "[automation][cliplane]") {
    ClipLaneFixture fx;
    auto& mgr = AutomationManager::getInstance();

    // One cycle of a 1-beat "triangle": 0 -> 1 -> 0, timeline beats [0, 1].
    std::vector<AutomationPoint> cycle;
    AutomationPoint p;
    p.beatPosition = 0.0;
    p.value = 0.0;
    cycle.push_back(p);
    p.beatPosition = 0.5;
    p.value = 1.0;
    cycle.push_back(p);
    p.beatPosition = 1.0;
    p.value = 0.0;
    cycle.push_back(p);

    BakeModulationToClipCommand cmd(fx.laneId, 0.0, 8.0, std::move(cycle), {},
                                    /*loopLengthBeats=*/1.0);
    cmd.execute();

    const auto* clip = mgr.getClip(cmd.getCreatedClipId());
    REQUIRE(clip != nullptr);
    REQUIRE(clip->lengthBeats == Approx(8.0));
    REQUIRE(clip->looping);
    REQUIRE(clip->loopLengthBeats == Approx(1.0));
    REQUIRE(clip->points.size() == 3);

    // The cycle repeats across the clip: beat 2.5 wraps to local 0.5.
    REQUIRE(mgr.getValueAtBeat(fx.laneId, 0.5) == Approx(1.0));
    REQUIRE(mgr.getValueAtBeat(fx.laneId, 2.5) == Approx(1.0));
    REQUIRE(mgr.getValueAtBeat(fx.laneId, 7.25) == Approx(0.5));
}

TEST_CASE("Clip commands - create/move/resize/delete/duplicate with undo",
          "[automation][cliplane]") {
    ClipLaneFixture fx;
    auto& mgr = AutomationManager::getInstance();
    auto& undoMgr = UndoManager::getInstance();
    undoMgr.clearHistory();

    // Create
    auto create = std::make_unique<CreateAutomationClipCommand>(fx.laneId, 4.0, 4.0);
    auto* createPtr = create.get();
    undoMgr.executeCommand(std::move(create));
    const auto clipId = createPtr->getCreatedClipId();
    REQUIRE(mgr.getClip(clipId) != nullptr);
    REQUIRE(fx.lane().clipIds.size() == 1);
    // The command seeds a straight hold line across the clip (two points at
    // the target's current value); bare AutomationManager::createClip stays
    // an empty primitive.
    REQUIRE(mgr.getClip(clipId)->points.size() == 2);
    REQUIRE(mgr.getClip(clipId)->points.front().beatPosition == Approx(0.0));
    REQUIRE(mgr.getClip(clipId)->points.back().beatPosition == Approx(4.0));
    REQUIRE(mgr.getClip(clipId)->points.front().value ==
            Approx(mgr.getClip(clipId)->points.back().value));

    // Move (mergeable)
    undoMgr.executeCommand(std::make_unique<MoveAutomationClipCommand>(clipId, 8.0));
    REQUIRE(mgr.getClip(clipId)->startBeats == Approx(8.0));
    REQUIRE(undoMgr.undo());
    REQUIRE(mgr.getClip(clipId)->startBeats == Approx(4.0));

    // Resize from start: start shifts, undo restores both bounds.
    undoMgr.executeCommand(std::make_unique<ResizeAutomationClipCommand>(clipId, 2.0, true));
    REQUIRE(mgr.getClip(clipId)->startBeats == Approx(6.0));
    REQUIRE(mgr.getClip(clipId)->lengthBeats == Approx(2.0));
    REQUIRE(undoMgr.undo());
    REQUIRE(mgr.getClip(clipId)->startBeats == Approx(4.0));
    REQUIRE(mgr.getClip(clipId)->lengthBeats == Approx(4.0));

    // Duplicate lands at the source's end; undo removes it.
    auto dup = std::make_unique<DuplicateAutomationClipCommand>(clipId);
    auto* dupPtr = dup.get();
    undoMgr.executeCommand(std::move(dup));
    const auto dupId = dupPtr->getCreatedClipId();
    REQUIRE(mgr.getClip(dupId)->startBeats == Approx(8.0));
    // Same lane as its source, and registered on it. The Ctrl+D handler selects
    // the copy using the lane id it already had for the source rather than
    // looking the new clip up, so a duplicate landing on another lane would
    // leave the selection pointing at the wrong one.
    REQUIRE(mgr.getClip(dupId)->laneId == mgr.getClip(clipId)->laneId);
    REQUIRE(fx.lane().clipIds == std::vector<AutomationClipId>{clipId, dupId});
    REQUIRE(undoMgr.undo());
    REQUIRE(mgr.getClip(dupId) == nullptr);

    // Delete restores the clip AND its lane reference on undo.
    mgr.addPointToClip(clipId, 1.0, 0.8);
    undoMgr.executeCommand(std::make_unique<DeleteAutomationClipCommand>(clipId));
    REQUIRE(mgr.getClip(clipId) == nullptr);
    REQUIRE(fx.lane().clipIds.empty());
    REQUIRE(undoMgr.undo());
    REQUIRE(mgr.getClip(clipId) != nullptr);
    REQUIRE(mgr.getClip(clipId)->points.size() == 3);  // seeded line + added point
    REQUIRE(fx.lane().clipIds == std::vector<AutomationClipId>{clipId});
}
