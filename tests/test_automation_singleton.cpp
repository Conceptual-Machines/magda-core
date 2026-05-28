#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "magda/daw/core/AutomationCommands.hpp"
#include "magda/daw/core/AutomationManager.hpp"
#include "magda/daw/core/ClipManager.hpp"
#include "magda/daw/core/SelectionManager.hpp"
#include "magda/daw/core/TrackManager.hpp"
#include "magda/daw/core/UndoManager.hpp"

using namespace magda;

/**
 * Lane singleton invariant: there can be AT MOST ONE lane per unique target
 * on the AutomationManager. Enforced defensively inside createLane and
 * restoreLane so that forgetful callers can't accidentally produce
 * duplicates. This file exercises that invariant directly (no agent/parser
 * in the path).
 */

namespace {

void resetState() {
    AutomationManager::getInstance().clearAll();
    ClipManager::getInstance().clearAllClips();
    TrackManager::getInstance().clearAllTracks();
    UndoManager::getInstance().clearHistory();
    SelectionManager::getInstance().clearSelection();
}

TrackId makeTrack(const juce::String& name) {
    return TrackManager::getInstance().createTrack(name, TrackType::Audio);
}

AutomationTarget volumeTarget(TrackId id) {
    AutomationTarget t;
    t.kind = ControlTarget::Kind::TrackVolume;
    t.devicePath = ChainNodePath::trackLevel(id);
    return t;
}

AutomationTarget panTarget(TrackId id) {
    AutomationTarget t;
    t.kind = ControlTarget::Kind::TrackPan;
    t.devicePath = ChainNodePath::trackLevel(id);
    return t;
}

const AutomationPoint* findPointAt(const AutomationLaneInfo* lane, double beatPosition) {
    if (!lane)
        return nullptr;

    for (const auto& point : lane->absolutePoints) {
        if (point.beatPosition == Catch::Approx(beatPosition))
            return &point;
    }
    return nullptr;
}

}  // namespace

TEST_CASE("AutomationManager::createLane returns the existing id for a duplicate target",
          "[automation][singleton]") {
    resetState();
    auto trackId = makeTrack("T");
    auto& mgr = AutomationManager::getInstance();

    auto first = mgr.createLane(volumeTarget(trackId), AutomationLaneType::Absolute);
    auto second = mgr.createLane(volumeTarget(trackId), AutomationLaneType::Absolute);

    REQUIRE(first == second);
    REQUIRE(mgr.getLanesForTrack(trackId).size() == 1);
}

TEST_CASE("AutomationManager rejects post-fx device-scoped lanes", "[automation][postfx]") {
    resetState();
    auto trackId = makeTrack("T");
    auto postFxPath = ChainNodePath::postFxDevice(trackId, 1);
    auto& mgr = AutomationManager::getInstance();

    REQUIRE(mgr.createLane(ControlTarget::pluginParam(postFxPath, 0),
                           AutomationLaneType::Absolute) == INVALID_AUTOMATION_LANE_ID);
    REQUIRE(mgr.createLane(ControlTarget::deviceMacro(postFxPath, 0),
                           AutomationLaneType::Absolute) == INVALID_AUTOMATION_LANE_ID);
    REQUIRE(mgr.createLane(ControlTarget::modParam(postFxPath, 1, 0),
                           AutomationLaneType::Absolute) == INVALID_AUTOMATION_LANE_ID);
    REQUIRE(mgr.getLanes().empty());
}

TEST_CASE("AutomationManager: different target types on the same track coexist",
          "[automation][singleton]") {
    resetState();
    auto trackId = makeTrack("T");
    auto& mgr = AutomationManager::getInstance();

    auto vol = mgr.createLane(volumeTarget(trackId), AutomationLaneType::Absolute);
    auto pan = mgr.createLane(panTarget(trackId), AutomationLaneType::Absolute);

    REQUIRE(vol != pan);
    REQUIRE(mgr.getLanesForTrack(trackId).size() == 2);
}

TEST_CASE("AutomationManager: same target type on different tracks coexists",
          "[automation][singleton]") {
    resetState();
    auto a = makeTrack("A");
    auto b = makeTrack("B");
    auto& mgr = AutomationManager::getInstance();

    auto laneA = mgr.createLane(volumeTarget(a), AutomationLaneType::Absolute);
    auto laneB = mgr.createLane(volumeTarget(b), AutomationLaneType::Absolute);

    REQUIRE(laneA != laneB);
    REQUIRE(mgr.getLanesForTrack(a).size() == 1);
    REQUIRE(mgr.getLanesForTrack(b).size() == 1);
}

TEST_CASE("AutomationManager::getOrCreateLane never creates a second lane",
          "[automation][singleton]") {
    resetState();
    auto trackId = makeTrack("T");
    auto& mgr = AutomationManager::getInstance();

    auto a = mgr.getOrCreateLane(volumeTarget(trackId), AutomationLaneType::Absolute);
    auto b = mgr.getOrCreateLane(volumeTarget(trackId), AutomationLaneType::Absolute);
    auto c = mgr.createLane(volumeTarget(trackId), AutomationLaneType::Absolute);

    REQUIRE(a == b);
    REQUIRE(a == c);
    REQUIRE(mgr.getLanesForTrack(trackId).size() == 1);
}

TEST_CASE("AutomationManager::restoreLane skips duplicate targets", "[automation][singleton]") {
    resetState();
    auto trackId = makeTrack("T");
    auto& mgr = AutomationManager::getInstance();

    auto first = mgr.createLane(volumeTarget(trackId), AutomationLaneType::Absolute);

    // Simulate a corrupt save file offering a second volume lane.
    AutomationLaneInfo dup;
    dup.id = first + 1000;  // distinct id — the dedup check uses target, not id
    dup.target = volumeTarget(trackId);
    dup.type = AutomationLaneType::Absolute;
    mgr.restoreLane(dup);

    REQUIRE(mgr.getLanesForTrack(trackId).size() == 1);
    REQUIRE(mgr.getLane(first) != nullptr);
    REQUIRE(mgr.getLane(first + 1000) == nullptr);
}

TEST_CASE("DuplicateAutomationTimeSelectionCommand duplicates visible absolute lane points",
          "[automation][commands][duplicate]") {
    resetState();
    auto trackId = makeTrack("T");
    auto otherTrackId = makeTrack("Other");
    auto& mgr = AutomationManager::getInstance();

    auto volumeLaneId = mgr.createLane(volumeTarget(trackId), AutomationLaneType::Absolute);
    auto hiddenLaneId = mgr.createLane(panTarget(trackId), AutomationLaneType::Absolute);
    auto otherTrackLaneId =
        mgr.createLane(volumeTarget(otherTrackId), AutomationLaneType::Absolute);
    REQUIRE(volumeLaneId != INVALID_AUTOMATION_LANE_ID);
    REQUIRE(hiddenLaneId != INVALID_AUTOMATION_LANE_ID);
    REQUIRE(otherTrackLaneId != INVALID_AUTOMATION_LANE_ID);

    auto* hiddenLane = mgr.getLane(hiddenLaneId);
    REQUIRE(hiddenLane != nullptr);
    hiddenLane->visible = false;

    auto firstPointId = mgr.addPoint(volumeLaneId, 1.0, 0.25, AutomationCurveType::Bezier);
    mgr.addPoint(volumeLaneId, 2.5, 0.75, AutomationCurveType::Linear);
    mgr.addPoint(volumeLaneId, 3.0, 0.5, AutomationCurveType::Linear);
    mgr.addPoint(hiddenLaneId, 1.5, 0.1, AutomationCurveType::Linear);
    mgr.addPoint(otherTrackLaneId, 1.5, 0.9, AutomationCurveType::Linear);

    BezierHandle inHandle;
    inHandle.beatOffset = -0.25;
    inHandle.value = -0.1;
    BezierHandle outHandle;
    outHandle.beatOffset = 0.25;
    outHandle.value = 0.1;
    outHandle.linked = false;
    mgr.setPointTension(volumeLaneId, firstPointId, 1.25);
    mgr.setPointHandles(volumeLaneId, firstPointId, inHandle, outHandle);

    DuplicateAutomationTimeSelectionCommand cmd(1.0, 3.0, {trackId}, 5.0);
    REQUIRE(cmd.canDuplicatePoints());

    cmd.execute();
    REQUIRE(cmd.hasDuplicatedPoints());

    auto* volumeLane = mgr.getLane(volumeLaneId);
    REQUIRE(volumeLane != nullptr);
    REQUIRE(volumeLane->absolutePoints.size() == 7);

    auto* duplicatedFirst = findPointAt(volumeLane, 5.0);
    REQUIRE(duplicatedFirst != nullptr);
    REQUIRE(duplicatedFirst->value == Catch::Approx(0.25));
    REQUIRE(duplicatedFirst->curveType == AutomationCurveType::Bezier);
    REQUIRE(duplicatedFirst->tension == Catch::Approx(1.25));
    REQUIRE(duplicatedFirst->inHandle.beatOffset == Catch::Approx(-0.25));
    REQUIRE(duplicatedFirst->outHandle.beatOffset == Catch::Approx(0.25));
    REQUIRE_FALSE(duplicatedFirst->outHandle.linked);

    auto* duplicatedSecond = findPointAt(volumeLane, 6.5);
    REQUIRE(duplicatedSecond != nullptr);
    REQUIRE(duplicatedSecond->value == Catch::Approx(0.75));

    auto* duplicatedEnd = findPointAt(volumeLane, 7.0);
    REQUIRE(duplicatedEnd != nullptr);
    REQUIRE(duplicatedEnd->value == Catch::Approx(0.5));

    REQUIRE(mgr.getLane(hiddenLaneId)->absolutePoints.size() == 2);
    REQUIRE(mgr.getLane(otherTrackLaneId)->absolutePoints.size() == 2);

    cmd.undo();
    REQUIRE(mgr.getLane(volumeLaneId)->absolutePoints.size() == 4);
    REQUIRE(findPointAt(mgr.getLane(volumeLaneId), 5.0) == nullptr);
    REQUIRE(findPointAt(mgr.getLane(volumeLaneId), 7.0) == nullptr);
}
