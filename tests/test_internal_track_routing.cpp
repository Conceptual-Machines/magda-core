#include <catch2/catch_test_macros.hpp>

#include "magda/daw/core/TrackManager.hpp"

using namespace magda;

// ============================================================================
// Test Fixture
// ============================================================================

struct InternalRoutingFixture {
    InternalRoutingFixture() {
        TrackManager::getInstance().clearAllTracks();
    }

    ~InternalRoutingFixture() {
        TrackManager::getInstance().clearAllTracks();
    }

    TrackManager& tm() {
        return TrackManager::getInstance();
    }

    TrackId createTrack(const juce::String& name = "Track") {
        return tm().createTrack(name, TrackType::Audio);
    }

    static juce::String trackInput(TrackId id) {
        return "track:" + juce::String(id);
    }
};

// ============================================================================
// Rejected assignments (#1690)
// ============================================================================

TEST_CASE("Self-routing track input is rejected", "[routing][input]") {
    InternalRoutingFixture fixture;

    auto trackA = fixture.createTrack("A");

    SECTION("audio input") {
        fixture.tm().setTrackAudioInput(trackA, fixture.trackInput(trackA));

        auto* a = fixture.tm().getTrack(trackA);
        REQUIRE(a != nullptr);
        REQUIRE(a->audioInputDevice.isEmpty());
    }

    SECTION("MIDI input") {
        // New tracks default to "all"; a rejected assignment must leave that unchanged
        const auto before = fixture.tm().getTrack(trackA)->midiInputDevice;
        fixture.tm().setTrackMidiInput(trackA, fixture.trackInput(trackA));

        auto* a = fixture.tm().getTrack(trackA);
        REQUIRE(a != nullptr);
        REQUIRE(a->midiInputDevice == before);
    }

    SECTION("a pre-existing valid input survives a rejected self-assignment") {
        auto trackB = fixture.createTrack("B");
        fixture.tm().setTrackAudioInput(trackB, fixture.trackInput(trackA));
        REQUIRE(fixture.tm().getTrack(trackB)->audioInputDevice == fixture.trackInput(trackA));

        fixture.tm().setTrackAudioInput(trackB, fixture.trackInput(trackB));
        REQUIRE(fixture.tm().getTrack(trackB)->audioInputDevice == fixture.trackInput(trackA));
    }
}

TEST_CASE("Unknown source track input is rejected", "[routing][input]") {
    InternalRoutingFixture fixture;

    auto trackA = fixture.createTrack("A");
    const auto bogus = juce::String("track:99999");

    SECTION("audio input") {
        fixture.tm().setTrackAudioInput(trackA, bogus);
        REQUIRE(fixture.tm().getTrack(trackA)->audioInputDevice.isEmpty());
    }

    SECTION("MIDI input") {
        const auto before = fixture.tm().getTrack(trackA)->midiInputDevice;
        fixture.tm().setTrackMidiInput(trackA, bogus);
        REQUIRE(fixture.tm().getTrack(trackA)->midiInputDevice == before);
    }
}

TEST_CASE("Cycle-creating track input assignments are rejected", "[routing][input][cycle]") {
    InternalRoutingFixture fixture;

    auto trackA = fixture.createTrack("A");
    auto trackB = fixture.createTrack("B");

    // B takes its input from A
    fixture.tm().setTrackAudioInput(trackB, fixture.trackInput(trackA));
    REQUIRE(fixture.tm().getTrack(trackB)->audioInputDevice == fixture.trackInput(trackA));

    SECTION("closing a two-node loop is rejected") {
        fixture.tm().setTrackAudioInput(trackA, fixture.trackInput(trackB));

        REQUIRE(fixture.tm().getTrack(trackA)->audioInputDevice.isEmpty());
        // B's original routing is untouched
        REQUIRE(fixture.tm().getTrack(trackB)->audioInputDevice == fixture.trackInput(trackA));
    }

    SECTION("closing a three-node loop is rejected") {
        auto trackC = fixture.createTrack("C");

        // A → B → C chain (each dest listens to the previous track)
        fixture.tm().setTrackAudioInput(trackC, fixture.trackInput(trackB));
        REQUIRE(fixture.tm().getTrack(trackC)->audioInputDevice == fixture.trackInput(trackB));

        // Closing edge C → A must be rejected
        fixture.tm().setTrackAudioInput(trackA, fixture.trackInput(trackC));
        REQUIRE(fixture.tm().getTrack(trackA)->audioInputDevice.isEmpty());

        // Intermediate edges are untouched
        REQUIRE(fixture.tm().getTrack(trackB)->audioInputDevice == fixture.trackInput(trackA));
        REQUIRE(fixture.tm().getTrack(trackC)->audioInputDevice == fixture.trackInput(trackB));
    }
}

TEST_CASE("wouldCreateInputRoutingCycle walks mixed audio and MIDI edges",
          "[routing][input][cycle]") {
    InternalRoutingFixture fixture;

    auto trackA = fixture.createTrack("A");
    auto trackB = fixture.createTrack("B");
    auto trackC = fixture.createTrack("C");
    auto trackD = fixture.createTrack("D");

    // MIDI edge: B listens to A; audio edge: C listens to B
    fixture.tm().setTrackMidiInput(trackB, fixture.trackInput(trackA));
    fixture.tm().setTrackAudioInput(trackC, fixture.trackInput(trackB));
    REQUIRE(fixture.tm().getTrack(trackB)->midiInputDevice == fixture.trackInput(trackA));
    REQUIRE(fixture.tm().getTrack(trackC)->audioInputDevice == fixture.trackInput(trackB));

    // Routing C into A closes the loop across both edge kinds
    REQUIRE(fixture.tm().wouldCreateInputRoutingCycle(trackA, trackC));
    // Self-routing is always a cycle
    REQUIRE(fixture.tm().wouldCreateInputRoutingCycle(trackA, trackA));
    // An unrelated track is not part of the chain
    REQUIRE_FALSE(fixture.tm().wouldCreateInputRoutingCycle(trackA, trackD));
    // Downstream direction is fine: A listening nowhere, D may listen to C
    REQUIRE_FALSE(fixture.tm().wouldCreateInputRoutingCycle(trackD, trackC));

    // The setter enforces the same check for the closing edge
    fixture.tm().setTrackAudioInput(trackA, fixture.trackInput(trackC));
    REQUIRE(fixture.tm().getTrack(trackA)->audioInputDevice.isEmpty());
    const auto midiBefore = fixture.tm().getTrack(trackA)->midiInputDevice;
    fixture.tm().setTrackMidiInput(trackA, fixture.trackInput(trackC));
    REQUIRE(fixture.tm().getTrack(trackA)->midiInputDevice == midiBefore);
}

// ============================================================================
// Valid assignments and mutual exclusivity
// ============================================================================

TEST_CASE("Valid track input assignment updates the model", "[routing][input]") {
    InternalRoutingFixture fixture;

    auto trackA = fixture.createTrack("A");
    auto trackB = fixture.createTrack("B");

    SECTION("audio input") {
        fixture.tm().setTrackAudioInput(trackB, fixture.trackInput(trackA));

        auto* b = fixture.tm().getTrack(trackB);
        REQUIRE(b != nullptr);
        REQUIRE(b->audioInputDevice == fixture.trackInput(trackA));
    }

    SECTION("MIDI input") {
        fixture.tm().setTrackMidiInput(trackB, fixture.trackInput(trackA));

        auto* b = fixture.tm().getTrack(trackB);
        REQUIRE(b != nullptr);
        REQUIRE(b->midiInputDevice == fixture.trackInput(trackA));
    }
}

TEST_CASE("Track audio and MIDI inputs are mutually exclusive", "[routing][input]") {
    InternalRoutingFixture fixture;

    auto trackA = fixture.createTrack("A");
    auto trackB = fixture.createTrack("B");

    SECTION("setting a track MIDI input clears the audio input") {
        fixture.tm().setTrackAudioInput(trackB, fixture.trackInput(trackA));
        REQUIRE(fixture.tm().getTrack(trackB)->audioInputDevice == fixture.trackInput(trackA));

        fixture.tm().setTrackMidiInput(trackB, fixture.trackInput(trackA));

        auto* b = fixture.tm().getTrack(trackB);
        REQUIRE(b->midiInputDevice == fixture.trackInput(trackA));
        REQUIRE(b->audioInputDevice.isEmpty());
    }

    SECTION("setting a track audio input clears the MIDI input") {
        fixture.tm().setTrackMidiInput(trackB, fixture.trackInput(trackA));
        REQUIRE(fixture.tm().getTrack(trackB)->midiInputDevice == fixture.trackInput(trackA));

        fixture.tm().setTrackAudioInput(trackB, fixture.trackInput(trackA));

        auto* b = fixture.tm().getTrack(trackB);
        REQUIRE(b->audioInputDevice == fixture.trackInput(trackA));
        REQUIRE(b->midiInputDevice.isEmpty());
    }
}

// ============================================================================
// Track deletion cleanup
// ============================================================================

TEST_CASE("Deleting a source track clears track-input references on listeners",
          "[routing][input][delete]") {
    InternalRoutingFixture fixture;

    auto trackA = fixture.createTrack("A");
    auto trackB = fixture.createTrack("B");

    SECTION("audio input") {
        fixture.tm().setTrackAudioInput(trackB, fixture.trackInput(trackA));
        REQUIRE(fixture.tm().getTrack(trackB)->audioInputDevice == fixture.trackInput(trackA));

        fixture.tm().deleteTrack(trackA);

        auto* b = fixture.tm().getTrack(trackB);
        REQUIRE(b != nullptr);
        REQUIRE(b->audioInputDevice.isEmpty());
    }

    SECTION("MIDI input") {
        fixture.tm().setTrackMidiInput(trackB, fixture.trackInput(trackA));
        REQUIRE(fixture.tm().getTrack(trackB)->midiInputDevice == fixture.trackInput(trackA));

        fixture.tm().deleteTrack(trackA);

        auto* b = fixture.tm().getTrack(trackB);
        REQUIRE(b != nullptr);
        REQUIRE(b->midiInputDevice.isEmpty());
    }

    SECTION("multiple listeners are all cleared") {
        auto trackC = fixture.createTrack("C");
        fixture.tm().setTrackAudioInput(trackB, fixture.trackInput(trackA));
        fixture.tm().setTrackMidiInput(trackC, fixture.trackInput(trackA));

        fixture.tm().deleteTrack(trackA);

        REQUIRE(fixture.tm().getTrack(trackB)->audioInputDevice.isEmpty());
        REQUIRE(fixture.tm().getTrack(trackC)->midiInputDevice.isEmpty());
    }
}
