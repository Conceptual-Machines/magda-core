#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "AudioClipTestHelpers.hpp"
#include "core/ClipInfo.hpp"
#include "core/SourcePool.hpp"

using namespace magda;
using Catch::Approx;

namespace {

struct EventModelFixture {
    EventModelFixture() {
        SourcePool::getInstance().clear();
        SourcePool::getInstance().clearSeededFactsForTesting();
    }
    ~EventModelFixture() {
        SourcePool::getInstance().clear();
        SourcePool::getInstance().clearSeededFactsForTesting();
    }
};

/// An audio clip with one event on a 120 bpm, 8 beat source.
ClipInfo makeAudioClip(const juce::String& file = "loop.wav") {
    ClipInfo clip;
    auto& event = magda::test::giveAudioEvent(clip, file, 4.0);
    event.interpBpm = 120.0;
    event.interpTotalBeats = 8.0;
    clip.setPlacementBeats(0.0, 8.0);
    return clip;
}

}  // namespace

// =============================================================================
// Container and content stay the same extent (#1901 Phase A)
// =============================================================================

TEST_CASE("A single-event clip's event spans it", "[clip][event]") {
    EventModelFixture fixture;
    auto clip = makeAudioClip();

    REQUIRE(clip.events().size() == 1);
    REQUIRE(clip.primaryEvent()->startBeat == Approx(0.0));
    REQUIRE(clip.primaryEvent()->lengthBeats == Approx(8.0));

    SECTION("Resizing the clip resizes its event") {
        clip.setPlacementBeats(0.0, 12.0);
        REQUIRE(clip.primaryEvent()->lengthBeats == Approx(12.0));
    }

    SECTION("Moving the clip leaves the event at the clip's own origin") {
        // Event geometry is clip-relative, so a move changes nothing inside.
        clip.setPlacementBeats(16.0, 8.0);
        REQUIRE(clip.primaryEvent()->startBeat == Approx(0.0));
        REQUIRE(clip.primaryEvent()->lengthBeats == Approx(8.0));
    }

    SECTION("A clip holding several events is a window, so its bounds crop") {
        auto& second = clip.audio().addEvent({});
        second.startBeat = 8.0;
        second.lengthBeats = 4.0;

        clip.setPlacementBeats(0.0, 4.0);

        // Neither event was resized: the clip crops at render instead.
        REQUIRE(clip.events()[0].lengthBeats == Approx(8.0));
        REQUIRE(clip.events()[1].startBeat == Approx(8.0));
        REQUIRE(clip.events()[1].lengthBeats == Approx(4.0));
    }
}

// =============================================================================
// The source domain is samples, and beats are a view on it
// =============================================================================

TEST_CASE("Reinterpreting the source BPM moves no audio", "[clip][event][interpretation]") {
    EventModelFixture fixture;
    auto clip = makeAudioClip();
    auto& event = *clip.primaryEvent();

    event.setAnchorSeconds(1.0);
    event.setLoopStartSeconds(1.0);
    event.setLoopLengthSeconds(2.0);

    const auto anchorSamples = event.sourceAnchorSamples;
    const auto loopStartSamples = event.loopStartSamples;
    const auto loopLengthSamples = event.loopLengthSamples;

    REQUIRE(event.anchorBeats() == Approx(2.0));      // 1 s at 120 bpm
    REQUIRE(event.loopLengthBeats() == Approx(4.0));  // 2 s at 120 bpm

    // Halving the interpretation says the same audio is half as many beats.
    event.interpBpm = 60.0;

    SECTION("The audible region is untouched") {
        REQUIRE(event.sourceAnchorSamples == anchorSamples);
        REQUIRE(event.loopStartSamples == loopStartSamples);
        REQUIRE(event.loopLengthSamples == loopLengthSamples);
        REQUIRE(event.anchorSeconds() == Approx(1.0));
        REQUIRE(event.loopLengthSeconds() == Approx(2.0));
    }

    SECTION("Only the beat view changes") {
        REQUIRE(event.anchorBeats() == Approx(1.0));
        REQUIRE(event.loopLengthBeats() == Approx(2.0));
    }
}

TEST_CASE("Beat writes into the source domain round-trip", "[clip][event][interpretation]") {
    EventModelFixture fixture;
    auto clip = makeAudioClip();
    auto& event = *clip.primaryEvent();

    event.setAnchorBeats(3.0);
    REQUIRE(event.anchorBeats() == Approx(3.0));
    REQUIRE(event.anchorSeconds() == Approx(1.5));

    SECTION("Without an interpretation there is no beat domain to write into") {
        // Silently converting at some default tempo would invent musical
        // content the file has not been calibrated to.
        event.interpBpm = 0.0;
        const auto before = event.sourceAnchorSamples;
        event.setAnchorBeats(99.0);
        REQUIRE(event.sourceAnchorSamples == before);
        REQUIRE(event.anchorBeats() == Approx(0.0));
    }
}

TEST_CASE("The loop phase is measured from the loop start", "[clip][event][loop]") {
    EventModelFixture fixture;
    auto clip = makeAudioClip();
    auto& event = *clip.primaryEvent();

    event.setLoopStartSeconds(1.0);
    event.setLoopLengthSeconds(2.0);
    event.setAnchorSeconds(1.5);

    REQUIRE(event.loopPhaseSeconds() == Approx(0.5));
}

// =============================================================================
// Events
// =============================================================================

TEST_CASE("Events get unique ids within their clip", "[clip][event]") {
    EventModelFixture fixture;
    auto clip = makeAudioClip();

    // Ids by value: addEvent appends to a vector, so the reference it returns
    // does not survive the next one.
    const auto firstId = clip.primaryEvent()->id;
    const auto secondId = clip.audio().addEvent({}).id;
    const auto thirdId = clip.audio().addEvent({}).id;

    REQUIRE(firstId != secondId);
    REQUIRE(secondId != thirdId);

    SECTION("findEvent addresses an event by id") {
        REQUIRE(clip.audio().findEvent(secondId) != nullptr);
        REQUIRE(clip.audio().findEvent(secondId)->id == secondId);
        REQUIRE(clip.audio().findEvent(INVALID_EVENT_ID) == nullptr);
    }
}

TEST_CASE("A MIDI clip has no audio event", "[clip][event][midi]") {
    EventModelFixture fixture;
    ClipInfo clip;
    clip.setMidiContent();

    REQUIRE(clip.primaryEvent() == nullptr);
    REQUIRE(primaryEventOf(&clip) == nullptr);

    SECTION("The read-only view is still total") {
        // Readers that used to reach for a clip-level audio field must not
        // have to branch on content type.
        REQUIRE(audioEventRef(clip).speedRatio == Approx(1.0));
        REQUIRE_FALSE(clip.loopEnabled);
    }

    SECTION("Its loop lives in clip beats") {
        clip.loopEnabled = true;
        clip.loopLengthBeats = 4.0;
        REQUIRE(clip.loopEnabled);
        REQUIRE(clip.loopLengthBeats == Approx(4.0));
    }
}

// =============================================================================
// Ghost clips: what an event shares and what stays per-instance
// =============================================================================

TEST_CASE("Ghost siblings share interpretation, not placement", "[clip][event][ghost]") {
    EventModelFixture fixture;
    auto source = makeAudioClip();
    auto& sourceEvent = *source.primaryEvent();
    sourceEvent.warpEnabled = true;
    sourceEvent.transpose = 5;
    sourceEvent.reversed = true;
    sourceEvent.interpBpm = 90.0;

    auto ghost = makeAudioClip();
    auto& ghostEvent = *ghost.primaryEvent();
    ghostEvent.setAnchorSeconds(2.0);
    ghostEvent.setLoopLengthSeconds(1.0);
    ghostEvent.speedRatio = 2.0;
    ghostEvent.fadeInSeconds = 0.25;
    ghostEvent.gainDB = -3.0f;

    ghost.copySharedContentFrom(source);

    SECTION("How the source is interpreted propagates") {
        REQUIRE(ghostEvent.warpEnabled);
        REQUIRE(ghostEvent.transpose == 5);
        REQUIRE(ghostEvent.reversed);
        REQUIRE(ghostEvent.interpBpm == Approx(90.0));
    }

    SECTION("Where it reads and how it mixes does not") {
        // One ghost's trim or resize must never corrupt its siblings.
        REQUIRE(ghostEvent.anchorSeconds() == Approx(2.0));
        REQUIRE(ghostEvent.loopLengthSeconds() == Approx(1.0));
        REQUIRE(ghostEvent.speedRatio == Approx(2.0));
        REQUIRE(ghostEvent.fadeInSeconds == Approx(0.25));
        REQUIRE(ghostEvent.gainDB == Approx(-3.0f));
    }
}

TEST_CASE("sharedContentEquals ignores per-instance event state", "[clip][event][ghost]") {
    EventModelFixture fixture;
    auto a = makeAudioClip();
    auto b = makeAudioClip();

    REQUIRE(a.sharedContentEquals(b));

    SECTION("A per-instance edit is not a content change") {
        // The propagation pass skips the deep sibling copies when only
        // per-instance state moved, so this has to stay in lockstep with
        // copySharedContentFrom.
        b.primaryEvent()->setAnchorSeconds(1.0);
        b.primaryEvent()->speedRatio = 2.0;
        b.primaryEvent()->fadeOutSeconds = 0.5;
        REQUIRE(a.sharedContentEquals(b));
    }

    SECTION("A shared edit is") {
        b.primaryEvent()->transpose = 7;
        REQUIRE_FALSE(a.sharedContentEquals(b));
    }

    SECTION("So is a different source") {
        b.primaryEvent()->sourceId = SourcePool::getInstance().acquire("other.wav");
        REQUIRE_FALSE(a.sharedContentEquals(b));
    }
}

TEST_CASE("A MIDI clip's loop stays per-instance across ghosts", "[clip][event][ghost][midi]") {
    EventModelFixture fixture;

    ClipInfo source;
    source.setMidiContent();
    source.setPlacementBeats(0.0, 8.0);
    source.midiNotes.push_back(MidiNote{60, 100, 0.0, 1.0});
    source.loopEnabled = true;
    source.loopLengthBeats = 4.0;

    ClipInfo ghost;
    ghost.setMidiContent();
    ghost.setPlacementBeats(16.0, 8.0);

    ghost.copySharedContentFrom(source);

    // The loop moved inside the content variant when audio events landed, but
    // it is still per-instance: one ghost's loop toggle must not silence or
    // re-length its siblings.
    REQUIRE(ghost.midiNotes.size() == 1);
    REQUIRE_FALSE(ghost.loopEnabled);
    REQUIRE(ghost.loopLengthBeats == Approx(0.0));
    REQUIRE(source.sharedContentEquals(ghost));
}

// =============================================================================
// Interpretation seeding
// =============================================================================

TEST_CASE("Seeding an interpretation only fills gaps", "[clip][event][interpretation]") {
    EventModelFixture fixture;
    AudioEvent event;

    SECTION("Both land when unset") {
        event.seedInterpretation(4.0, 120.0);
        REQUIRE(event.interpBpm == Approx(120.0));
        REQUIRE(event.interpTotalBeats == Approx(4.0));
    }

    SECTION("A value the user already set is never overwritten") {
        event.interpBpm = 90.0;
        event.interpTotalBeats = 16.0;
        event.seedInterpretation(4.0, 120.0);
        REQUIRE(event.interpBpm == Approx(90.0));
        REQUIRE(event.interpTotalBeats == Approx(16.0));
    }

    SECTION("A beat count without a tempo is not taken") {
        // It would claim musical content the file has not been calibrated to,
        // and render as a plausible integer that never gets corrected.
        event.seedInterpretation(4.0, 0.0);
        REQUIRE(event.interpTotalBeats == Approx(0.0));
    }
}

TEST_CASE("New events inherit the source's detected facts", "[clip][event][interpretation]") {
    EventModelFixture fixture;
    auto& pool = SourcePool::getInstance();

    const auto sourceId = pool.acquire("detected.wav");
    auto* source = pool.getMutable(sourceId);
    source->detectedBpm = 174.0;
    source->durationSeconds = 4.0;
    source->detectedKeyRoot = "F";
    source->detectedKeyScale = "minor";

    AudioEvent event;
    event.sourceId = sourceId;
    event.seedInterpretationFromSource();

    REQUIRE(event.interpBpm == Approx(174.0));
    REQUIRE(event.interpTotalBeats == Approx(4.0 * 174.0 / 60.0));
    REQUIRE(event.keyRoot == "F");
    REQUIRE(event.keyScale == "minor");

    SECTION("Re-seeding cannot rewrite what the user changed") {
        event.interpBpm = 87.0;
        event.keyRoot = "C";
        event.seedInterpretationFromSource();
        REQUIRE(event.interpBpm == Approx(87.0));
        REQUIRE(event.keyRoot == "C");
    }
}
