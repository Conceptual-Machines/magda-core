#include <catch2/catch_test_macros.hpp>

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
    t.type = AutomationTargetType::TrackVolume;
    t.trackId = id;
    return t;
}

AutomationTarget panTarget(TrackId id) {
    AutomationTarget t;
    t.type = AutomationTargetType::TrackPan;
    t.trackId = id;
    return t;
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
