#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "magda/daw/core/TrackManager.hpp"
#include "magda/daw/core/TrackPropertyCommands.hpp"
#include "magda/daw/core/UndoManager.hpp"

using namespace magda;

// ============================================================================
// Test Fixture
// ============================================================================

struct SendsTestFixture {
    SendsTestFixture() {
        TrackManager::getInstance().clearAllTracks();
    }

    ~SendsTestFixture() {
        TrackManager::getInstance().clearAllTracks();
    }

    TrackManager& tm() {
        return TrackManager::getInstance();
    }

    TrackId createTrack(const juce::String& name = "Track") {
        return tm().createTrack(name, TrackType::Audio);
    }
};

// ============================================================================
// Send Cap (#961)
// ============================================================================

TEST_CASE("Sends are capped at MAX_SENDS_PER_TRACK", "[sends][cap]") {
    SendsTestFixture fixture;

    auto source = fixture.createTrack("Source");

    // Create enough destination tracks to exceed the cap
    std::vector<TrackId> dests;
    for (int i = 0; i < TrackManager::MAX_SENDS_PER_TRACK + 4; ++i) {
        dests.push_back(fixture.createTrack("Dest " + juce::String(i)));
    }

    // Add sends up to the cap
    for (int i = 0; i < TrackManager::MAX_SENDS_PER_TRACK; ++i) {
        fixture.tm().addSend(source, dests[static_cast<size_t>(i)]);
    }

    auto* track = fixture.tm().getTrack(source);
    REQUIRE(track != nullptr);
    REQUIRE(static_cast<int>(track->sends.size()) == TrackManager::MAX_SENDS_PER_TRACK);

    // Trying to add more sends should be silently ignored
    for (int i = TrackManager::MAX_SENDS_PER_TRACK; i < TrackManager::MAX_SENDS_PER_TRACK + 4;
         ++i) {
        fixture.tm().addSend(source, dests[static_cast<size_t>(i)]);
    }

    REQUIRE(static_cast<int>(track->sends.size()) == TrackManager::MAX_SENDS_PER_TRACK);
}

TEST_CASE("Duplicate sends are not added", "[sends]") {
    SendsTestFixture fixture;

    auto source = fixture.createTrack("Source");
    auto dest = fixture.createTrack("Dest");

    fixture.tm().addSend(source, dest);
    fixture.tm().addSend(source, dest);  // duplicate

    auto* track = fixture.tm().getTrack(source);
    REQUIRE(track != nullptr);
    REQUIRE(track->sends.size() == 1);
}

// ============================================================================
// Sends cleaned up on track deletion (#960)
// ============================================================================

TEST_CASE("Sends targeting deleted track are removed", "[sends][delete]") {
    SendsTestFixture fixture;

    auto trackA = fixture.createTrack("A");
    auto trackB = fixture.createTrack("B");
    auto trackC = fixture.createTrack("C");

    // A sends to B and C
    fixture.tm().addSend(trackA, trackB);
    fixture.tm().addSend(trackA, trackC);

    auto* a = fixture.tm().getTrack(trackA);
    REQUIRE(a->sends.size() == 2);

    // Delete B — send from A to B should be removed
    fixture.tm().deleteTrack(trackB);

    a = fixture.tm().getTrack(trackA);
    REQUIRE(a != nullptr);
    REQUIRE(a->sends.size() == 1);
    REQUIRE(a->sends[0].destTrackId == trackC);
}

TEST_CASE("Sends from multiple tracks to deleted track are all removed", "[sends][delete]") {
    SendsTestFixture fixture;

    auto trackA = fixture.createTrack("A");
    auto trackB = fixture.createTrack("B");
    auto dest = fixture.createTrack("Dest");

    fixture.tm().addSend(trackA, dest);
    fixture.tm().addSend(trackB, dest);

    REQUIRE(fixture.tm().getTrack(trackA)->sends.size() == 1);
    REQUIRE(fixture.tm().getTrack(trackB)->sends.size() == 1);

    // Delete dest — sends from both A and B should be cleaned up
    fixture.tm().deleteTrack(dest);

    REQUIRE(fixture.tm().getTrack(trackA)->sends.empty());
    REQUIRE(fixture.tm().getTrack(trackB)->sends.empty());
}

// ============================================================================
// Send undo commands (#6)
// ============================================================================

TEST_CASE("AddSendCommand adds a send; undo removes it", "[sends][undo]") {
    SendsTestFixture fixture;
    auto src = fixture.createTrack("Src");
    auto dst = fixture.createTrack("Dst");
    UndoManager::getInstance().clearHistory();

    UndoManager::getInstance().executeCommand(std::make_unique<AddSendCommand>(src, dst));
    REQUIRE(fixture.tm().getTrack(src)->sends.size() == 1);

    REQUIRE(UndoManager::getInstance().undo());
    REQUIRE(fixture.tm().getTrack(src)->sends.empty());

    REQUIRE(UndoManager::getInstance().redo());
    REQUIRE(fixture.tm().getTrack(src)->sends.size() == 1);
    UndoManager::getInstance().clearHistory();
}

TEST_CASE("RemoveSendCommand removes a send; undo restores it with level", "[sends][undo]") {
    SendsTestFixture fixture;
    auto src = fixture.createTrack("Src");
    auto dst = fixture.createTrack("Dst");
    fixture.tm().addSend(src, dst);
    const int busIndex = fixture.tm().getTrack(src)->sends[0].busIndex;
    fixture.tm().setSendLevel(src, busIndex, 0.5f);
    UndoManager::getInstance().clearHistory();

    UndoManager::getInstance().executeCommand(std::make_unique<RemoveSendCommand>(src, busIndex));
    REQUIRE(fixture.tm().getTrack(src)->sends.empty());

    REQUIRE(UndoManager::getInstance().undo());
    REQUIRE(fixture.tm().getTrack(src)->sends.size() == 1);
    CHECK(fixture.tm().getTrack(src)->sends[0].level == Catch::Approx(0.5f));
    UndoManager::getInstance().clearHistory();
}
