#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <stdexcept>

#include "magda/daw/core/AutomationCommands.hpp"
#include "magda/daw/core/AutomationManager.hpp"
#include "magda/daw/core/ClipManager.hpp"
#include "magda/daw/core/SelectionManager.hpp"
#include "magda/daw/core/TempoAutomationCommands.hpp"
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
    return TrackManager::getInstance().createTrack(name, TrackType::Media);
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

TEST_CASE("Automation authority state machine has explicit stable and gesture states",
          "[automation][authority]") {
    using State = AutomationAuthorityState;
    using Event = AutomationAuthorityEvent;

    REQUIRE(transitionAutomationAuthority(State::Reading, Event::BeginTouch) == State::Touching);
    REQUIRE(transitionAutomationAuthority(State::Touching, Event::EndGesture) == State::Reading);
    REQUIRE(transitionAutomationAuthority(State::Reading, Event::BeginWrite) == State::Writing);
    REQUIRE(transitionAutomationAuthority(State::Writing, Event::EndGesture) == State::Reading);

    REQUIRE(transitionAutomationAuthority(State::Reading, Event::Disable) == State::Disabled);
    REQUIRE(transitionAutomationAuthority(State::Disabled, Event::BeginTouch) == State::Disabled);
    REQUIRE(transitionAutomationAuthority(State::Disabled, Event::BeginWrite) == State::Disabled);
    REQUIRE(transitionAutomationAuthority(State::Disabled, Event::EndGesture) == State::Disabled);
    REQUIRE(transitionAutomationAuthority(State::Disabled, Event::Enable) == State::Reading);

    REQUIRE(transitionAutomationAuthority(State::Touching, Event::ResetRuntime) == State::Reading);
    REQUIRE(transitionAutomationAuthority(State::Writing, Event::ResetRuntime) == State::Reading);
    REQUIRE(transitionAutomationAuthority(State::Disabled, Event::ResetRuntime) == State::Disabled);

    REQUIRE(automationAuthorityForPersistence(State::Touching) == State::Reading);
    REQUIRE(automationAuthorityForPersistence(State::Writing) == State::Reading);
    REQUIRE(automationAuthorityForPersistence(State::Disabled) == State::Disabled);
}

TEST_CASE("AutomationManager gestures never become persistent lane disablement",
          "[automation][authority]") {
    resetState();
    const auto trackId = makeTrack("T");
    const auto target = volumeTarget(trackId);
    auto& mgr = AutomationManager::getInstance();
    const auto laneId = mgr.createLane(target, AutomationLaneType::Absolute);

    REQUIRE(mgr.getLane(laneId)->authorityState == AutomationAuthorityState::Reading);
    REQUIRE(mgr.getVisualState(target) == AutomationVisualState::Active);

    mgr.beginTargetGesture(target);
    REQUIRE(mgr.getLane(laneId)->authorityState == AutomationAuthorityState::Touching);
    REQUIRE(mgr.getVisualState(target) == AutomationVisualState::Overridden);

    mgr.endTargetGesture(target);
    REQUIRE(mgr.getLane(laneId)->authorityState == AutomationAuthorityState::Reading);
    REQUIRE(mgr.getVisualState(target) == AutomationVisualState::Active);

    mgr.setLaneEnabled(laneId, false);
    mgr.beginTargetGesture(target);
    mgr.endTargetGesture(target);
    REQUIRE(mgr.getLane(laneId)->authorityState == AutomationAuthorityState::Disabled);

    mgr.setLaneEnabled(laneId, true);
    REQUIRE(mgr.getLane(laneId)->authorityState == AutomationAuthorityState::Reading);
}

TEST_CASE("Adding and removing modulation does not change automation authority",
          "[automation][authority][modulation]") {
    resetState();
    const auto trackId = makeTrack("T");
    const auto path = ChainNodePath::trackLevel(trackId);
    const auto target = volumeTarget(trackId);
    auto& automation = AutomationManager::getInstance();
    auto& tracks = TrackManager::getInstance();
    const auto laneId = automation.createLane(target, AutomationLaneType::Absolute);

    tracks.addMod(path, 0, ModType::LFO);
    tracks.setModTarget(path, 0, target);
    REQUIRE(automation.getLane(laneId)->authorityState == AutomationAuthorityState::Reading);

    tracks.removeMod(path, 0);
    REQUIRE(automation.getLane(laneId)->authorityState == AutomationAuthorityState::Reading);
}

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

TEST_CASE("AutomationManager owns an untethered edit-scoped Tempo lane",
          "[automation][editscoped]") {
    resetState();
    auto trackId = makeTrack("T");
    auto& mgr = AutomationManager::getInstance();

    auto trackLane = mgr.createLane(volumeTarget(trackId), AutomationLaneType::Absolute);
    auto tempoLane = mgr.createLane(ControlTarget::tempo(), AutomationLaneType::Absolute);

    REQUIRE(tempoLane != INVALID_AUTOMATION_LANE_ID);
    REQUIRE(tempoLane != trackLane);

    // The tempo lane is global: it does not appear under the real track...
    REQUIRE(mgr.getLanesForTrack(trackId).size() == 1);
    REQUIRE(mgr.getLanesForTrack(trackId).front() == trackLane);

    // ...and is surfaced through the edit-scoped accessor instead.
    auto editScoped = mgr.getEditScopedLanes();
    REQUIRE(editScoped.size() == 1);
    REQUIRE(editScoped.front() == tempoLane);
}

TEST_CASE("AutomationManager: Tempo lane is a singleton and survives post-fx guard",
          "[automation][editscoped]") {
    resetState();
    auto& mgr = AutomationManager::getInstance();

    auto first = mgr.createLane(ControlTarget::tempo(), AutomationLaneType::Absolute);
    auto second = mgr.createLane(ControlTarget::tempo(), AutomationLaneType::Absolute);

    REQUIRE(first != INVALID_AUTOMATION_LANE_ID);
    REQUIRE(first == second);  // at most one lane per edit-scoped target
    REQUIRE(mgr.getEditScopedLanes().size() == 1);

    // Edit-scoped targets seed an initial point at the default tempo, not the
    // 0.5 range-midpoint fallback used for unresolved targets.
    const auto* lane = mgr.getLane(first);
    REQUIRE(lane != nullptr);
    REQUIRE(lane->absolutePoints.size() == 1);
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

TEST_CASE("InsertTimeAutomationCommand shifts points at/after the insert beat right",
          "[automation][commands][insert]") {
    resetState();
    auto trackId = makeTrack("T");
    auto otherTrackId = makeTrack("Other");
    auto& mgr = AutomationManager::getInstance();

    auto laneId = mgr.createLane(volumeTarget(trackId), AutomationLaneType::Absolute);
    auto otherLaneId = mgr.createLane(volumeTarget(otherTrackId), AutomationLaneType::Absolute);
    REQUIRE(laneId != INVALID_AUTOMATION_LANE_ID);
    REQUIRE(otherLaneId != INVALID_AUTOMATION_LANE_ID);

    mgr.addPoint(laneId, 1.0, 0.25, AutomationCurveType::Linear);
    mgr.addPoint(laneId, 2.5, 0.75, AutomationCurveType::Linear);
    mgr.addPoint(laneId, 3.0, 0.5, AutomationCurveType::Linear);
    mgr.addPoint(otherLaneId, 2.5, 0.9, AutomationCurveType::Linear);

    // Insert 2 beats at beat 2 on trackId only.
    InsertTimeAutomationCommand cmd(2.0, 2.0, {trackId});
    REQUIRE(cmd.canShiftPoints());

    cmd.execute();

    auto* lane = mgr.getLane(laneId);
    REQUIRE(lane != nullptr);
    // createLane seeds a default point, so the count (default + 3 added) is
    // unchanged by a move: points are repositioned, never added or removed.
    const size_t pointCount = lane->absolutePoints.size();
    REQUIRE(findPointAt(lane, 1.0) != nullptr);  // before insert beat: untouched
    REQUIRE(findPointAt(lane, 2.5) == nullptr);  // shifted away
    REQUIRE(findPointAt(lane, 3.0) == nullptr);  // shifted away
    auto* shiftedA = findPointAt(lane, 4.5);
    REQUIRE(shiftedA != nullptr);
    REQUIRE(shiftedA->value == Catch::Approx(0.75));
    auto* shiftedB = findPointAt(lane, 5.0);
    REQUIRE(shiftedB != nullptr);
    REQUIRE(shiftedB->value == Catch::Approx(0.5));
    REQUIRE(lane->absolutePoints.size() == pointCount);

    // Other track is unaffected by the track filter.
    REQUIRE(findPointAt(mgr.getLane(otherLaneId), 2.5) != nullptr);

    cmd.undo();
    lane = mgr.getLane(laneId);
    REQUIRE(findPointAt(lane, 1.0) != nullptr);
    REQUIRE(findPointAt(lane, 2.5) != nullptr);
    REQUIRE(findPointAt(lane, 3.0) != nullptr);
    REQUIRE(findPointAt(lane, 4.5) == nullptr);
    REQUIRE(findPointAt(lane, 5.0) == nullptr);
}

TEST_CASE("A Linear segment's shaper handle bends the evaluated value", "[automation][singleton]") {
    // Regression: the shaper bends a Linear segment via bezier handles, but
    // value evaluation used to read only the tension scalar -> the curve was
    // visible but played back straight (inaudible).
    resetState();
    auto& mgr = AutomationManager::getInstance();
    auto trackId = makeTrack("Vol");
    auto laneId = mgr.getOrCreateLane(volumeTarget(trackId), AutomationLaneType::Absolute);
    auto p0 = mgr.addPoint(laneId, 0.0, 0.0, AutomationCurveType::Linear);
    mgr.addPoint(laneId, 4.0, 1.0, AutomationCurveType::Linear);

    // Straight segment: the midpoint is the linear average.
    REQUIRE(mgr.getValueAtBeat(laneId, 2.0) == Catch::Approx(0.5).margin(0.01));

    // Bend it: a shaper apex below the line (outHandle on the left point).
    BezierHandle in;
    BezierHandle out;
    out.beatOffset = 2.0;  // apex at the segment midpoint
    out.value = 0.1;       // apex value 0.1 -> well below the 0.5 straight line
    mgr.setPointHandles(laneId, p0, in, out);

    // The midpoint must now follow the bend, not the straight line.
    REQUIRE(mgr.getValueAtBeat(laneId, 2.0) < 0.45);
}

namespace {
struct ThrowingPointsListener : AutomationManagerListener {
    bool called = false;
    void automationLanesChanged() override {}
    void automationPointsChanged(AutomationLaneId) override {
        called = true;
        throw std::runtime_error("listener boom");
    }
};
}  // namespace

TEST_CASE("AutomationManager::BatchScope destructor survives a throwing listener",
          "[automation][batch]") {
    // #2395: ~BatchScope calls endNotificationBatch(), which fans out to
    // listeners. A listener throw used to escape an implicitly-noexcept
    // destructor (std::terminate); it must now be swallowed.
    resetState();
    auto& mgr = AutomationManager::getInstance();
    auto trackId = makeTrack("Batch");
    auto laneId = mgr.createLane(volumeTarget(trackId), AutomationLaneType::Absolute);

    ThrowingPointsListener listener;
    mgr.addListener(&listener);

    {
        AutomationManager::BatchScope batch;
        mgr.addPoint(laneId, 1.0, 0.5);
    }

    mgr.removeListener(&listener);
    REQUIRE(listener.called);
}

TEST_CASE("Native tempo automation follows insert and duplicate with undo",
          "[automation][commands][tempo]") {
    resetState();
    auto& mgr = AutomationManager::getInstance();
    const auto laneId = mgr.createLane(ControlTarget::tempo(), AutomationLaneType::Absolute);
    REQUIRE(laneId != INVALID_AUTOMATION_LANE_ID);
    mgr.addPoint(laneId, 2.0, 0.4, AutomationCurveType::Linear);
    InsertTimeAutomationCommand insert(1.0, 3.0, {}, {laneId});
    REQUIRE(insert.canShiftPoints());
    insert.execute();
    REQUIRE(findPointAt(mgr.getLane(laneId), 5.0) != nullptr);
    REQUIRE(findPointAt(mgr.getLane(laneId), 2.0) == nullptr);
    insert.undo();
    REQUIRE(findPointAt(mgr.getLane(laneId), 2.0) != nullptr);
    DuplicateAutomationTimeSelectionCommand duplicate(1.0, 3.0, {}, 5.0, {laneId});
    REQUIRE(duplicate.canDuplicatePoints());
    duplicate.execute();
    const auto* point = findPointAt(mgr.getLane(laneId), 6.0);
    REQUIRE(point != nullptr);
    REQUIRE(point->value == Catch::Approx(0.4));
    duplicate.undo();
    REQUIRE(findPointAt(mgr.getLane(laneId), 6.0) == nullptr);
    REQUIRE(findPointAt(mgr.getLane(laneId), 2.0) != nullptr);
}

TEST_CASE("Global time edits move hidden tempo with actual track IDs",
          "[automation][commands][tempo][2928]") {
    resetState();
    auto& mgr = AutomationManager::getInstance();
    const auto track = makeTrack("Arrangement");
    const auto tempo = mgr.createLane(ControlTarget::tempo(), AutomationLaneType::Absolute);
    const auto volume = mgr.createLane(volumeTarget(track), AutomationLaneType::Absolute);
    const auto tempoPoint = mgr.addPoint(tempo, 2.0, 0.4, AutomationCurveType::Linear);
    mgr.addPoint(volume, 2.0, 0.7, AutomationCurveType::Linear);
    mgr.getLane(tempo)->visible = false;

    InsertTimeAutomationCommand tracks(1.0, 3.0, {track});
    RippleTempoAutomationCommand global(RippleTempoAutomationCommand::Mode::Insert, 1.0, 4.0);
    tracks.execute();
    REQUIRE(findPointAt(mgr.getLane(volume), 5.0) != nullptr);
    REQUIRE(findPointAt(mgr.getLane(tempo), 2.0) != nullptr);
    global.execute();
    REQUIRE(findPointAt(mgr.getLane(tempo), 2.0) == nullptr);
    REQUIRE(findPointAt(mgr.getLane(tempo), 5.0)->id == tempoPoint);
    REQUIRE(findPointAt(mgr.getLane(tempo), 0.0) != nullptr);
    global.undo();
    tracks.undo();
    REQUIRE(findPointAt(mgr.getLane(tempo), 2.0)->id == tempoPoint);
    REQUIRE(findPointAt(mgr.getLane(volume), 2.0) != nullptr);
    tracks.execute();
    global.execute();
    REQUIRE(findPointAt(mgr.getLane(tempo), 5.0)->id == tempoPoint);
}

TEST_CASE("Global tempo ripple preserves curve data and undo across range operations",
          "[automation][commands][tempo][2928]") {
    resetState();
    auto& mgr = AutomationManager::getInstance();
    const auto lane = mgr.createLane(ControlTarget::tempo(), AutomationLaneType::Absolute);
    mgr.addPoint(lane, 0.0, 0.2, AutomationCurveType::Linear);
    const auto pointId = mgr.addPoint(lane, 2.0, 0.4, AutomationCurveType::Linear);
    mgr.setPointTension(lane, pointId, 0.6);
    mgr.setPointHandles(lane, pointId, {-0.2, -0.1, false}, {0.3, 0.1, false});
    mgr.addPoint(lane, 4.0, 0.6, AutomationCurveType::Linear);
    mgr.addPoint(lane, 8.0, 0.8, AutomationCurveType::Linear);
    mgr.getLane(lane)->visible = false;
    const auto before = mgr.getLane(lane)->absolutePoints;
    using Mode = RippleTempoAutomationCommand::Mode;
    auto mode = Mode::Insert;
    double start = 0.0, end = 3.0;
    SECTION("Insert at zero retains the initial tempo") {
        RippleTempoAutomationCommand command(mode, start, end);
        command.execute();
        REQUIRE(findPointAt(mgr.getLane(lane), 0.0)->value == Catch::Approx(0.2));
        REQUIRE(findPointAt(mgr.getLane(lane), 5.0)->id == pointId);
        command.undo();
    }
    SECTION("Duplicate copies the initial tempo and shifts the range-end point") {
        RippleTempoAutomationCommand command(Mode::Duplicate, 0.0, 4.0);
        command.execute();
        REQUIRE(mgr.getLane(lane)->absolutePoints.size() == 6);
        REQUIRE(findPointAt(mgr.getLane(lane), 4.0)->value == Catch::Approx(0.2));
        const auto* copy = findPointAt(mgr.getLane(lane), 6.0);
        REQUIRE(copy != nullptr);
        REQUIRE(copy->id != pointId);
        REQUIRE(copy->tension == Catch::Approx(0.6));
        REQUIRE(copy->inHandle.beatOffset == Catch::Approx(-0.2));
        REQUIRE(copy->outHandle.value == Catch::Approx(0.1));
        REQUIRE(findPointAt(mgr.getLane(lane), 8.0)->value == Catch::Approx(0.6));
        REQUIRE(findPointAt(mgr.getLane(lane), 12.0)->value == Catch::Approx(0.8));
        const auto copyId = copy->id;
        command.undo();
        command.execute();
        REQUIRE(findPointAt(mgr.getLane(lane), 6.0)->id == copyId);
        command.undo();
    }
    SECTION("Delete removes enclosed points and closes the gap") {
        RippleTempoAutomationCommand command(Mode::Delete, 1.0, 3.0);
        command.execute();
        REQUIRE(mgr.getLane(lane)->absolutePoints.size() == 3);
        REQUIRE(findPointAt(mgr.getLane(lane), 2.0)->value == Catch::Approx(0.6));
        REQUIRE(findPointAt(mgr.getLane(lane), 6.0)->value == Catch::Approx(0.8));
        command.undo();
    }
    SECTION("Deleting from zero keeps a single boundary point") {
        RippleTempoAutomationCommand command(Mode::Delete, 0.0, 4.0);
        command.execute();
        REQUIRE(mgr.getLane(lane)->absolutePoints.size() == 2);
        REQUIRE(findPointAt(mgr.getLane(lane), 0.0)->value == Catch::Approx(0.6));
        REQUIRE(findPointAt(mgr.getLane(lane), 4.0)->value == Catch::Approx(0.8));
        command.undo();
    }
    const auto& restored = mgr.getLane(lane)->absolutePoints;
    REQUIRE(restored.size() == before.size());
    for (size_t i = 0; i < before.size(); ++i) {
        REQUIRE(restored[i].id == before[i].id);
        REQUIRE(restored[i].beatPosition == before[i].beatPosition);
        REQUIRE(restored[i].value == before[i].value);
        REQUIRE(restored[i].tension == before[i].tension);
        REQUIRE(restored[i].inHandle.beatOffset == before[i].inHandle.beatOffset);
        REQUIRE(restored[i].outHandle.value == before[i].outHandle.value);
    }
}

TEST_CASE("Track-scoped automation edits leave global tempo alone",
          "[automation][commands][tempo][2928]") {
    resetState();
    auto& mgr = AutomationManager::getInstance();
    const auto track = makeTrack("Only this track");
    const auto tempo = mgr.createLane(ControlTarget::tempo(), AutomationLaneType::Absolute);
    mgr.addPoint(tempo, 2.0, 0.4, AutomationCurveType::Linear);
    SECTION("Selected tracks") {
        InsertTimeAutomationCommand command(1.0, 3.0, {track});
        command.execute();
    }
    SECTION("All-track pass before the separate global ripple") {
        InsertTimeAutomationCommand command(1.0, 3.0, {});
        command.execute();
    }
    SECTION("Track duplication") {
        DuplicateAutomationTimeSelectionCommand command(1.0, 3.0, {}, 5.0);
        command.execute();
    }
    REQUIRE(findPointAt(mgr.getLane(tempo), 2.0) != nullptr);
    REQUIRE(mgr.getLane(tempo)->absolutePoints.size() == 2);
}
