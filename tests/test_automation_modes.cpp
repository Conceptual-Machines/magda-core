#include <catch2/catch_test_macros.hpp>

#include "SharedTestEngine.hpp"
#include "magda/daw/audio/AutomationRecordingEngine.hpp"
#include "magda/daw/core/AutomationManager.hpp"
#include "magda/daw/core/SelectionManager.hpp"
#include "magda/daw/core/TrackManager.hpp"
#include "magda/daw/core/UndoManager.hpp"

using namespace magda;

// Pure-state unit tests for the automation-modes work in #1039.
// Behavioral coverage of Touch / Latch (which require the transport to roll)
// belongs in an integration suite.

namespace {

void resetState() {
    AutomationManager::getInstance().clearAll();
    TrackManager::getInstance().clearAllTracks();
    UndoManager::getInstance().clearHistory();
    SelectionManager::getInstance().clearSelection();
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

TEST_CASE("AutomationMode: setMode / getMode round-trips every value",
          "[automation][modes]") {
    auto& engine = magda::test::getSharedEngine();
    AutomationRecordingEngine rec(*engine.getEdit());

    REQUIRE(rec.getMode() == AutomationMode::Off);
    rec.setMode(AutomationMode::Write);
    REQUIRE(rec.getMode() == AutomationMode::Write);
    rec.setMode(AutomationMode::Touch);
    REQUIRE(rec.getMode() == AutomationMode::Touch);
    rec.setMode(AutomationMode::Latch);
    REQUIRE(rec.getMode() == AutomationMode::Latch);
    rec.setMode(AutomationMode::Off);
    REQUIRE(rec.getMode() == AutomationMode::Off);
}

TEST_CASE("AutomationMode: setWriteEnabled is a thin shim over setMode",
          "[automation][modes]") {
    auto& engine = magda::test::getSharedEngine();
    AutomationRecordingEngine rec(*engine.getEdit());

    rec.setWriteEnabled(true);
    REQUIRE(rec.getMode() == AutomationMode::Write);
    REQUIRE(rec.isWriteEnabled());

    rec.setWriteEnabled(false);
    REQUIRE(rec.getMode() == AutomationMode::Off);
    REQUIRE_FALSE(rec.isWriteEnabled());
}

TEST_CASE("AutomationMode: isWriteEnabled is true for any non-Off mode",
          "[automation][modes]") {
    // Important for the existing UI surface — the transport "armed" indicator
    // and AutomationManager::isWriteModeEnabled keep working unchanged when
    // the user picks Touch or Latch.
    auto& engine = magda::test::getSharedEngine();
    AutomationRecordingEngine rec(*engine.getEdit());

    rec.setMode(AutomationMode::Off);
    REQUIRE_FALSE(rec.isWriteEnabled());
    rec.setMode(AutomationMode::Write);
    REQUIRE(rec.isWriteEnabled());
    rec.setMode(AutomationMode::Touch);
    REQUIRE(rec.isWriteEnabled());
    rec.setMode(AutomationMode::Latch);
    REQUIRE(rec.isWriteEnabled());
}

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
