#include <catch2/catch_test_macros.hpp>

#include "magda/daw/core/AutomationManager.hpp"
#include "magda/daw/core/SelectionManager.hpp"
#include "magda/daw/core/TrackManager.hpp"
#include "magda/daw/core/UndoManager.hpp"

using namespace magda;

// Pure-state unit tests for the AutomationManager API additions in #1039.
// Tests that construct an AutomationRecordingEngine (which needs a te::Edit)
// live in test_automation_modes_juce.cpp so they run inside magda_juce_tests
// where the shared TE engine is properly managed.

namespace {

void resetState() {
    AutomationManager::getInstance().clearAll();
    TrackManager::getInstance().clearAllTracks();
    UndoManager::getInstance().clearHistory();
    SelectionManager::getInstance().clearSelection();
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

}  // namespace

TEST_CASE("AutomationManager touch baseline: set / get / clear round-trip",
          "[automation][modes][touch-baseline]") {
    resetState();
    auto trackId = TrackManager::getInstance().createTrack("T", TrackType::Audio);
    auto& mgr = AutomationManager::getInstance();
    auto target = volumeTarget(trackId);

    REQUIRE_FALSE(mgr.getTouchBaseline(target).has_value());

    mgr.setTouchBaseline(target, 0.42);
    auto baseline = mgr.getTouchBaseline(target);
    REQUIRE(baseline.has_value());
    REQUIRE(*baseline == 0.42);

    mgr.clearTouchBaseline(target);
    REQUIRE_FALSE(mgr.getTouchBaseline(target).has_value());
}

TEST_CASE("AutomationManager touch baseline: setting twice overwrites the value",
          "[automation][modes][touch-baseline]") {
    resetState();
    auto trackId = TrackManager::getInstance().createTrack("T", TrackType::Audio);
    auto& mgr = AutomationManager::getInstance();
    auto target = volumeTarget(trackId);

    mgr.setTouchBaseline(target, 0.1);
    mgr.setTouchBaseline(target, 0.9);

    REQUIRE(*mgr.getTouchBaseline(target) == 0.9);
}

TEST_CASE("AutomationManager touch baseline: per-target storage is independent",
          "[automation][modes][touch-baseline]") {
    resetState();
    auto trackId = TrackManager::getInstance().createTrack("T", TrackType::Audio);
    auto& mgr = AutomationManager::getInstance();
    auto vol = volumeTarget(trackId);
    auto pan = panTarget(trackId);

    mgr.setTouchBaseline(vol, 0.3);
    mgr.setTouchBaseline(pan, 0.7);

    REQUIRE(*mgr.getTouchBaseline(vol) == 0.3);
    REQUIRE(*mgr.getTouchBaseline(pan) == 0.7);

    mgr.clearTouchBaseline(vol);
    REQUIRE_FALSE(mgr.getTouchBaseline(vol).has_value());
    REQUIRE(*mgr.getTouchBaseline(pan) == 0.7);
}

TEST_CASE("AutomationManager: getUserTouchedTargets reflects current set",
          "[automation][modes][touch-baseline]") {
    resetState();
    auto trackId = TrackManager::getInstance().createTrack("T", TrackType::Audio);
    auto& mgr = AutomationManager::getInstance();

    REQUIRE(mgr.getUserTouchedTargets().empty());

    mgr.setTargetUserTouched(volumeTarget(trackId), true);
    mgr.setTargetUserTouched(panTarget(trackId), true);

    auto touched = mgr.getUserTouchedTargets();
    REQUIRE(touched.size() == 2);

    mgr.setTargetUserTouched(volumeTarget(trackId), false);
    touched = mgr.getUserTouchedTargets();
    REQUIRE(touched.size() == 1);
    REQUIRE(touched[0] == panTarget(trackId));

    mgr.setTargetUserTouched(panTarget(trackId), false);
    REQUIRE(mgr.getUserTouchedTargets().empty());
}
