#include <algorithm>
#include <catch2/catch_test_macros.hpp>

#include "magda/daw/api/automation_api_live.hpp"
#include "magda/daw/core/AutomationManager.hpp"
#include "magda/daw/core/ClipManager.hpp"
#include "magda/daw/core/SelectionManager.hpp"
#include "magda/daw/core/TrackManager.hpp"
#include "magda/daw/core/UndoManager.hpp"

using namespace magda;

namespace {

void resetState() {
    AutomationManager::getInstance().clearAll();
    ClipManager::getInstance().clearAllClips();
    TrackManager::getInstance().clearAllTracks();
    UndoManager::getInstance().clearHistory();
    SelectionManager::getInstance().clearSelection();
}

AutomationTarget volumeTarget(TrackId id) {
    AutomationTarget target;
    target.kind = ControlTarget::Kind::TrackVolume;
    target.devicePath = ChainNodePath::trackLevel(id);
    return target;
}

bool contains(const std::vector<AutomationLaneId>& ids, AutomationLaneId id) {
    return std::find(ids.begin(), ids.end(), id) != ids.end();
}

}  // namespace

TEST_CASE("Automation lanes and their targets are enumerable through the facade",
          "[automation-api][enumeration]") {
    resetState();
    auto& tracks = TrackManager::getInstance();
    auto& automation = AutomationManager::getInstance();

    const auto trackA = tracks.createTrack("A", TrackType::Media);
    const auto trackB = tracks.createTrack("B", TrackType::Media);

    const auto laneA = automation.createLane(volumeTarget(trackA), AutomationLaneType::Absolute);
    const auto laneB = automation.createLane(volumeTarget(trackB), AutomationLaneType::Absolute);

    AutomationApiLive api;
    const auto& lanes = api.getLanes();
    REQUIRE(lanes.size() == 2);

    // Every lane carries the target it drives, so a caller can discover both
    // the automation and the parameters behind it in one pass.
    for (const auto& lane : lanes) {
        REQUIRE(lane.target.isValid());
        REQUIRE(lane.target.kind == ControlTarget::Kind::TrackVolume);
    }

    REQUIRE(contains(api.getLanesForTrack(trackA), laneA));
    REQUIRE_FALSE(contains(api.getLanesForTrack(trackA), laneB));
    REQUIRE(contains(api.getLanesForTrack(trackB), laneB));

    resetState();
}

TEST_CASE("Edit-scoped lanes are enumerable even though no track owns them",
          "[automation-api][enumeration]") {
    resetState();
    auto& automation = AutomationManager::getInstance();
    const auto trackId = TrackManager::getInstance().createTrack("A", TrackType::Media);

    const auto trackLane =
        automation.createLane(volumeTarget(trackId), AutomationLaneType::Absolute);
    const auto tempoLane =
        automation.createLane(AutomationTarget::tempo(), AutomationLaneType::Absolute);

    AutomationApiLive api;
    // Tempo has no devicePath, so getLanesForTrack can never reach it — without
    // this accessor it would be invisible to a remote caller.
    const auto editScoped = api.getEditScopedLanes();
    REQUIRE(contains(editScoped, tempoLane));
    REQUIRE_FALSE(contains(editScoped, trackLane));

    resetState();
}

TEST_CASE("Bulk point writes are one undo step", "[automation-api][bulk]") {
    resetState();
    auto& automation = AutomationManager::getInstance();
    const auto trackId = TrackManager::getInstance().createTrack("A", TrackType::Media);
    const auto laneId = automation.createLane(volumeTarget(trackId), AutomationLaneType::Absolute);

    AutomationApiLive api;
    // createLane seeds an absolute lane with one point at the target's current
    // value, so "before" is that lane, not an empty one.
    const auto pointsBefore = api.getLane(laneId)->absolutePoints.size();

    std::vector<AutomationPoint> points;
    for (int i = 0; i < 64; ++i) {
        AutomationPoint point;
        point.beatPosition = i * 0.25;
        point.value = static_cast<double>(i) / 64.0;
        point.curveType = AutomationCurveType::Linear;
        points.push_back(point);
    }

    REQUIRE(api.setLanePoints(laneId, points));
    REQUIRE(api.getLane(laneId)->absolutePoints.size() == 64);

    // The whole curve is one entry, not 64 — otherwise undoing a generated
    // curve means one Undo per sample.
    auto& undo = UndoManager::getInstance();
    REQUIRE(undo.canUndo());
    undo.undo();
    REQUIRE(api.getLane(laneId)->absolutePoints.size() == pointsBefore);
    REQUIRE_FALSE(undo.canUndo());

    undo.redo();
    REQUIRE(api.getLane(laneId)->absolutePoints.size() == 64);

    resetState();
}

TEST_CASE("Bulk point writes replace rather than append", "[automation-api][bulk]") {
    resetState();
    auto& automation = AutomationManager::getInstance();
    const auto trackId = TrackManager::getInstance().createTrack("A", TrackType::Media);
    const auto laneId = automation.createLane(volumeTarget(trackId), AutomationLaneType::Absolute);

    AutomationApiLive api;
    AutomationPoint first;
    first.beatPosition = 0.0;
    first.value = 0.1;
    REQUIRE(api.setLanePoints(laneId, {first}));
    REQUIRE(api.getLane(laneId)->absolutePoints.size() == 1);

    AutomationPoint second;
    second.beatPosition = 4.0;
    second.value = 0.9;
    REQUIRE(api.setLanePoints(laneId, {second}));

    const auto& applied = api.getLane(laneId)->absolutePoints;
    REQUIRE(applied.size() == 1);
    REQUIRE(applied.front().beatPosition == 4.0);

    // Undo restores the previous curve, not an empty lane.
    UndoManager::getInstance().undo();
    REQUIRE(api.getLane(laneId)->absolutePoints.size() == 1);
    REQUIRE(api.getLane(laneId)->absolutePoints.front().beatPosition == 0.0);

    resetState();
}

TEST_CASE("Bulk point writes are refused where they do not apply", "[automation-api][bulk]") {
    resetState();
    auto& automation = AutomationManager::getInstance();
    const auto trackId = TrackManager::getInstance().createTrack("A", TrackType::Media);
    AutomationApiLive api;

    SECTION("unknown lane") {
        REQUIRE_FALSE(api.setLanePoints(INVALID_AUTOMATION_LANE_ID, {}));
        REQUIRE_FALSE(api.setLanePoints(9999, {}));
    }

    SECTION("clip-based lane keeps its points on clips") {
        const auto laneId =
            automation.createLane(volumeTarget(trackId), AutomationLaneType::ClipBased);
        REQUIRE_FALSE(api.setLanePoints(laneId, {}));
    }

    resetState();
}

TEST_CASE("Deleting a lane through the facade is undoable", "[automation-api][bulk]") {
    resetState();
    auto& automation = AutomationManager::getInstance();
    const auto trackId = TrackManager::getInstance().createTrack("A", TrackType::Media);
    const auto laneId = automation.createLane(volumeTarget(trackId), AutomationLaneType::Absolute);

    AutomationApiLive api;
    REQUIRE(api.getLanes().size() == 1);
    REQUIRE(api.deleteLane(laneId));
    REQUIRE(api.getLanes().empty());

    UndoManager::getInstance().undo();
    REQUIRE(api.getLanes().size() == 1);

    REQUIRE_FALSE(api.deleteLane(9999));

    resetState();
}
