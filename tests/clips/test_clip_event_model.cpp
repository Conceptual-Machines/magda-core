#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "AudioClipTestHelpers.hpp"
#include "audio/AudioThumbnailManager.hpp"
#include "core/ClipInfo.hpp"
#include "core/ClipManager.hpp"
#include "core/SourcePool.hpp"
#include "core/TempoUtils.hpp"

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
        event.seedInterpretation(4.0, 120.0, Provenance::FileMetadata);
        REQUIRE(event.interpBpm == Approx(120.0));
        REQUIRE(event.interpTotalBeats == Approx(4.0));
        REQUIRE(event.bpmFrom == Provenance::FileMetadata);
    }

    SECTION("A value the user already set is never overwritten") {
        REQUIRE(event.adoptBpm(90.0, Provenance::User));
        REQUIRE(event.adoptTotalBeats(16.0, Provenance::User));
        event.seedInterpretation(4.0, 120.0, Provenance::FileMetadata);
        REQUIRE(event.interpBpm == Approx(90.0));
        REQUIRE(event.interpTotalBeats == Approx(16.0));
    }

    SECTION("A beat count without a tempo is not taken") {
        // It would claim musical content the file has not been calibrated to,
        // and render as a plausible integer that never gets corrected.
        event.seedInterpretation(4.0, 0.0, Provenance::FileMetadata);
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
        REQUIRE(event.adoptBpm(87.0, Provenance::User));
        event.keyRoot = "C";
        event.seedInterpretationFromSource();
        REQUIRE(event.interpBpm == Approx(87.0));
        REQUIRE(event.keyRoot == "C");
    }

    SECTION("A loop exported at its tempo seeds a whole beat count") {
        // 5.517 s at 174 is 15.9993 beats: a 16-beat loop, not a fraction.
        source->durationSeconds = 5.517;
        AudioEvent exact;
        exact.sourceId = sourceId;
        exact.seedInterpretationFromSource();
        REQUIRE(exact.interpTotalBeats == 16.0);
    }

    SECTION("A file half a beat off keeps its fraction") {
        source->durationSeconds = 16.5 * 60.0 / 174.0;
        AudioEvent off;
        off.sourceId = sourceId;
        off.seedInterpretationFromSource();
        REQUIRE(off.interpTotalBeats == Approx(16.5));
    }
}

TEST_CASE("beatCountForDuration snaps a whole-beat file and keeps a fraction",
          "[tempo][interpretation]") {
    REQUIRE(beatCountForDuration(5.517, 174.0) == 16.0);
    REQUIRE(beatCountForDuration(4.0, 120.0) == 8.0);

    SECTION("Within 0.02 of a beat snaps; further off does not") {
        REQUIRE(beatCountForDuration(16.015 * 60.0 / 174.0, 174.0) == 16.0);
        REQUIRE(beatCountForDuration(15.985 * 60.0 / 174.0, 174.0) == 16.0);
        REQUIRE(beatCountForDuration(16.05 * 60.0 / 174.0, 174.0) == Approx(16.05));
        REQUIRE(beatCountForDuration(15.95 * 60.0 / 174.0, 174.0) == Approx(15.95));
    }

    SECTION("A file that is not a loop keeps its fraction") {
        REQUIRE(beatCountForDuration(4.0, 174.0) == Approx(11.6));
        REQUIRE(beatCountForDuration(16.5 * 60.0 / 174.0, 174.0) == Approx(16.5));
    }

    SECTION("Nothing to measure gives zero") {
        REQUIRE(beatCountForDuration(0.0, 120.0) == 0.0);
        REQUIRE(beatCountForDuration(4.0, 0.0) == 0.0);
        REQUIRE(beatCountForDuration(-1.0, 120.0) == 0.0);
    }
}

TEST_CASE("Loop length in beats is a timeline view, not a source view", "[clip][event][loop]") {
    // The two domains only coincide under beat mode. Returning the event's own
    // beat count off beat mode hands source beats to callers that schedule in
    // timeline beats, which is how a 174 BPM loop in a 120 BPM project ended
    // up firing its follow action about 45% late.
    EventModelFixture fixture;
    constexpr double kProjectBpm = 120.0;

    ClipInfo clip;
    clip.setAudioContent();
    auto& event = magda::test::giveAudioEvent(clip, "/tmp/loop-174.wav", 8.0);
    event.interpBpm = 174.0;
    event.speedRatio = 1.0;
    event.setLoopLengthSeconds(2.0);
    event.setLoopStartSeconds(1.0);
    clip.loopEnabled = true;

    SECTION("Off beat mode it comes from seconds and the project tempo") {
        event.autoTempo = false;
        // Two seconds of audio at its recorded rate spans four beats at 120.
        REQUIRE(clip.loopLengthInBeats(kProjectBpm) == Approx(4.0));
        REQUIRE(clip.loopStartInBeats(kProjectBpm) == Approx(2.0));
        // The source-beat view is a different number, and not the one to use.
        REQUIRE(event.loopLengthBeats() == Approx(5.8));
    }

    SECTION("A speed change moves the timeline span, not the source one") {
        event.autoTempo = false;
        event.speedRatio = 2.0;  // plays twice as fast
        REQUIRE(clip.loopLengthInBeats(kProjectBpm) == Approx(2.0));
        REQUIRE(event.loopLengthSeconds() == Approx(2.0));
    }

    SECTION("Under beat mode the source is stretched onto the grid") {
        event.autoTempo = true;
        REQUIRE(clip.loopLengthInBeats(kProjectBpm) == Approx(5.8));
    }

    SECTION("A MIDI clip reports its own field for either accessor") {
        ClipInfo midiClip;
        midiClip.setMidiContent();
        midiClip.loopEnabled = true;
        midiClip.loopStartBeats = 1.0;
        midiClip.loopLengthBeats = 4.0;
        REQUIRE(midiClip.loopLengthInBeats(kProjectBpm) == Approx(4.0));
        REQUIRE(midiClip.loopStartInBeats(kProjectBpm) == Approx(1.0));
    }
}

TEST_CASE("A zero loop length means the whole clip, not no loop", "[clip][event][loop][sentinel]") {
    // A v1 project can still carry 0 in loopLengthBeats. Handed to the engine
    // as-is it asks for a loop of nothing, so every caller wrote the same
    // fallback by hand; effectiveLoopLengthBeats is that fallback, once.
    EventModelFixture fixture;
    constexpr double kProjectBpm = 120.0;

    ClipInfo clip;
    clip.setMidiContent();
    clip.setPlacementBeats(0.0, 8.0);
    clip.loopEnabled = true;

    SECTION("A set length is its own answer") {
        clip.loopLengthBeats = 3.0;
        REQUIRE(clip.effectiveLoopLengthBeats(kProjectBpm) == Approx(3.0));
    }

    SECTION("A zero falls back to the clip's own length") {
        clip.loopLengthBeats = 0.0;
        REQUIRE(clip.effectiveLoopLengthBeats(kProjectBpm) == Approx(8.0));
    }

    SECTION("The fallback is the clip length, not a zero passed through") {
        clip.loopLengthBeats = 0.0;
        REQUIRE(clip.effectiveLoopLengthBeats(kProjectBpm) > 0.0);
    }
}

TEST_CASE("effectiveLoopLengthBeats and loopLengthInBeats are different questions",
          "[clip][event][loop][sentinel]") {
    // The names are one word apart and the answers are not interchangeable:
    // loopLengthInBeats maps an audio clip's source region onto the timeline,
    // effectiveLoopLengthBeats reads the clip-beat field with a fallback.
    // Swapping one for the other compiles and plays the wrong length, which is
    // exactly the edit this case exists to fail.
    EventModelFixture fixture;
    constexpr double kProjectBpm = 120.0;

    ClipInfo clip;
    clip.setAudioContent();
    clip.setPlacementBeats(0.0, 8.0);
    auto& event = magda::test::giveAudioEvent(clip, "/tmp/loop-174.wav", 8.0);
    event.interpBpm = 174.0;
    event.speedRatio = 1.0;
    event.autoTempo = false;
    event.setLoopStartSeconds(1.0);
    event.setLoopLengthSeconds(2.0);
    clip.loopEnabled = true;
    clip.loopLengthBeats = 0.0;

    // Two seconds at the project's own tempo is four beats; the field carries
    // the sentinel, so the other accessor answers with the whole clip.
    REQUIRE(clip.loopLengthInBeats(kProjectBpm) == Approx(4.0));
    REQUIRE(clip.effectiveLoopLengthBeats(kProjectBpm) == Approx(8.0));
}

TEST_CASE("A warped loop reports its warped timeline span", "[clip][event][loop][warp]") {
    // warpEnabled is independent of autoTempo: setClipWarpEnabled does not
    // turn beat mode on, and the engine treats either as source-beat
    // processing. Falling through to the linear speedRatio conversion ignored
    // warpMarkers entirely, so a nonlinearly warped loop reported a start and
    // length that disagreed with where it actually plays.
    EventModelFixture fixture;
    constexpr double kProjectBpm = 120.0;

    ClipInfo clip;
    clip.setAudioContent();
    auto& event = magda::test::giveAudioEvent(clip, "/tmp/warped.wav", 8.0);
    event.interpBpm = 120.0;
    event.speedRatio = 1.0;
    event.autoTempo = false;
    event.warpEnabled = true;
    clip.loopEnabled = true;

    // The first source second is stretched to two; the second is compressed
    // back into one. A linear reading cannot tell these apart.
    event.warpMarkers = {{0.0, 0.0}, {1.0, 2.0}, {2.0, 3.0}};

    SECTION("The mapping is piecewise linear between markers") {
        REQUIRE(event.warpedSourceSeconds(0.5) == Approx(1.0));
        REQUIRE(event.warpedSourceSeconds(1.0) == Approx(2.0));
        REQUIRE(event.warpedSourceSeconds(1.5) == Approx(2.5));
    }

    SECTION("Outside the marker range the source carries on at its own rate") {
        REQUIRE(event.warpedSourceSeconds(3.0) == Approx(4.0));
        REQUIRE(event.warpedSourceSeconds(-1.0) == Approx(-1.0));
    }

    SECTION("Loop boundaries map through the warp, not around it") {
        event.setLoopStartSeconds(0.5);
        event.setLoopLengthSeconds(1.0);  // source 0.5 -> 1.5

        // Warped: 1.0 -> 2.5, so half a beat in and one and a half beats long
        // at 120. The linear reading would have said 1.0 and 2.0.
        REQUIRE(clip.loopStartInBeats(kProjectBpm) == Approx(2.0));
        REQUIRE(clip.loopLengthInBeats(kProjectBpm) == Approx(3.0));
    }

    SECTION("Turning warp off returns the linear reading") {
        event.warpEnabled = false;
        event.setLoopStartSeconds(0.5);
        event.setLoopLengthSeconds(1.0);
        REQUIRE(clip.loopStartInBeats(kProjectBpm) == Approx(1.0));
        REQUIRE(clip.loopLengthInBeats(kProjectBpm) == Approx(2.0));
    }

    SECTION("No markers is the identity, so warp-on alone changes nothing") {
        event.warpMarkers.clear();
        event.setLoopStartSeconds(0.5);
        event.setLoopLengthSeconds(1.0);
        REQUIRE(clip.loopStartInBeats(kProjectBpm) == Approx(1.0));
        REQUIRE(clip.loopLengthInBeats(kProjectBpm) == Approx(2.0));
    }
}

// =============================================================================
// Beat mode needs a tempo (#2676)
// =============================================================================

TEST_CASE("A session clip enters beat mode only when a tempo is known",
          "[clip][event][session][interpretation]") {
    EventModelFixture fixture;
    auto& clips = ClipManager::getInstance();
    clips.clearAllClips();
    AudioThumbnailManager::getInstance().clearCache();

    SECTION("A file that says what it is comes up in beat mode") {
        auto& pool = SourcePool::getInstance();
        pool.seedFactsForTesting("/tmp/known-tempo.wav", 8.0, 44100.0);

        const auto sourceId = pool.acquire("/tmp/known-tempo.wav");
        pool.getMutable(sourceId)->detectedBpm = 140.0;

        const auto clipId = clips.createAudioClipBeats(1, 0.0, 4.0, "/tmp/known-tempo.wav",
                                                       ClipView::Session, 120.0);

        const auto* event = clips.getClip(clipId)->primaryEvent();
        REQUIRE(event != nullptr);
        REQUIRE(event->interpBpm == Approx(140.0));
        // A drop does nothing more; BEAT is granted at once since the tempo is there.
        REQUIRE(!event->autoTempo);
        clips.setAutoTempo(clipId, true, 120.0);
        REQUIRE(event->autoTempo);
    }

    SECTION("A file that says nothing comes up in time mode") {
        // Beat mode with no tempo behind it is a claim nothing can honour: the
        // inspector reads BEAT, loopLengthBeats() answers zero, and the engine
        // declines the beat face and plays the material at its own rate anyway.
        SourcePool::getInstance().seedFactsForTesting("/tmp/no-tempo.wav", 8.0, 44100.0);

        const auto clipId =
            clips.createAudioClipBeats(1, 0.0, 4.0, "/tmp/no-tempo.wav", ClipView::Session, 120.0);

        const auto* clip = clips.getClip(clipId);
        const auto* event = clip->primaryEvent();
        REQUIRE(event != nullptr);
        REQUIRE(event->interpBpm == Approx(0.0));
        REQUIRE(!event->autoTempo);

        // Still a session clip: it loops, and the slot plays the whole source.
        REQUIRE(clip->loopEnabled);
    }

    SECTION("A tempo detected at creation is what puts it into beat mode") {
        // A real file, because the detection pass is guarded on one existing.
        // Its contents do not matter: the pool answers from the seeded facts.
        juce::TemporaryFile temp(".wav");
        temp.getFile().replaceWithText("not audio");
        const auto path = temp.getFile().getFullPathName();

        SourcePool::getInstance().seedFactsForTesting(path, 8.0, 44100.0);
        AudioThumbnailManager::getInstance().cacheBPM(path, 174.0);

        const auto clipId = clips.createAudioClipBeats(1, 0.0, 4.0, path, ClipView::Session, 120.0);

        const auto* event = clips.getClip(clipId)->primaryEvent();
        REQUIRE(event != nullptr);
        REQUIRE(!event->hasInterpretedBpm());
        REQUIRE(!event->autoTempo);

        // BEAT asks; the cached answer lands at once and the mode follows.
        clips.detectMissingTempo({clipId}, 120.0, nullptr);
        clips.setAutoTempo(clipId, true, 120.0);
        REQUIRE(event->autoTempo);
        REQUIRE(event->interpBpm == Approx(174.0));
    }

    clips.clearAllClips();
    AudioThumbnailManager::getInstance().clearCache();
}

// A beat count typed into the wrong field implied 43,000 BPM, and the engine
// played a 20 ms sliver of the loop: silence (#2674).
TEST_CASE("ClipManager: setSourceTempo refuses an interpretation no file could have",
          "[clip][event][interpretation]") {
    EventModelFixture fixture;
    auto& clips = ClipManager::getInstance();
    clips.clearAllClips();
    AudioThumbnailManager::getInstance().clearCache();

    juce::TemporaryFile temp(".wav");
    temp.getFile().replaceWithText("not audio");
    const auto path = temp.getFile().getFullPathName();
    SourcePool::getInstance().seedFactsForTesting(path, 5.517, 44100.0);
    AudioThumbnailManager::getInstance().cacheBPM(path, 174.0);
    const auto clipId = clips.createAudioClipBeats(1, 0.0, 4.0, path, ClipView::Session, 120.0);
    clips.detectMissingTempo({clipId}, 120.0, nullptr);
    REQUIRE(clips.getClip(clipId)->primaryEvent()->interpBpm == Approx(174.0));

    clips.setSourceTempo(clipId, 4001.0 * 60.0 / 5.517);

    const auto* event = clips.getClip(clipId)->primaryEvent();
    REQUIRE(event->interpBpm == Approx(174.0));
    REQUIRE(event->interpTotalBeats == Approx(16.0).margin(0.01));

    clips.clearAllClips();
    AudioThumbnailManager::getInstance().clearCache();
}

// Tempo and beat count are one fact in two units, tied by the file length:
// stating either restates the other (#2674).
TEST_CASE("ClipManager: setSourceTempo and setSourceBeatCount each restate the unit "
          "they were not given",
          "[clip][event][interpretation]") {
    EventModelFixture fixture;
    auto& clips = ClipManager::getInstance();
    clips.clearAllClips();
    AudioThumbnailManager::getInstance().clearCache();

    juce::TemporaryFile temp(".wav");
    temp.getFile().replaceWithText("not audio");
    const auto path = temp.getFile().getFullPathName();
    SourcePool::getInstance().seedFactsForTesting(path, 5.486, 44100.0);
    AudioThumbnailManager::getInstance().cacheBPM(path, 175.0);
    const auto clipId = clips.createAudioClipBeats(1, 0.0, 4.0, path, ClipView::Session, 120.0);
    clips.detectMissingTempo({clipId}, 120.0, nullptr);
    REQUIRE(clips.getClip(clipId)->primaryEvent()->interpTotalBeats == Approx(16.0).margin(0.01));

    // 5.486 s at 174 is 15.909 beats, too far from a whole beat to snap.
    clips.setSourceTempo(clipId, 174.0);
    REQUIRE(clips.getClip(clipId)->primaryEvent()->interpBpm == Approx(174.0));
    REQUIRE(clips.getClip(clipId)->primaryEvent()->interpTotalBeats ==
            Approx(5.486 * 174.0 / 60.0));

    clips.setSourceBeatCount(clipId, 32.0);
    REQUIRE(clips.getClip(clipId)->primaryEvent()->interpBpm == Approx(32.0 * 60.0 / 5.486));
    REQUIRE(clips.getClip(clipId)->primaryEvent()->interpTotalBeats == Approx(32.0));

    clips.clearAllClips();
    AudioThumbnailManager::getInstance().clearCache();
}

// The path the BEAT toggle takes before it can be granted (#2674).
TEST_CASE("ClipManager: detectMissingTempo answers at once for clips that need nothing",
          "[clip][event][interpretation][session]") {
    EventModelFixture fixture;
    auto& clips = ClipManager::getInstance();
    clips.clearAllClips();
    AudioThumbnailManager::getInstance().clearCache();

    SECTION("A clip whose tempo was cached before creation") {
        juce::TemporaryFile temp(".wav");
        temp.getFile().replaceWithText("not audio");
        const auto path = temp.getFile().getFullPathName();

        SourcePool::getInstance().seedFactsForTesting(path, 8.0, 44100.0);
        AudioThumbnailManager::getInstance().cacheBPM(path, 174.0);

        const auto clipId = clips.createAudioClipBeats(1, 0.0, 4.0, path, ClipView::Session, 120.0);
        REQUIRE(!clips.getClip(clipId)->primaryEvent()->hasInterpretedBpm());

        bool ready = false;
        clips.detectMissingTempo({clipId}, 120.0, [&ready] { ready = true; });
        REQUIRE(ready);
        REQUIRE(clips.getClip(clipId)->primaryEvent()->interpBpm == Approx(174.0));
    }

    SECTION("A clip whose file is missing and has no cached tempo") {
        SourcePool::getInstance().seedFactsForTesting("/tmp/absent.wav", 8.0, 44100.0);

        const auto clipId =
            clips.createAudioClipBeats(1, 0.0, 4.0, "/tmp/absent.wav", ClipView::Session, 120.0);

        bool ready = false;
        clips.detectMissingTempo({clipId}, 120.0, [&ready] { ready = true; });
        REQUIRE(ready);

        const auto* event = clips.getClip(clipId)->primaryEvent();
        REQUIRE(event != nullptr);
        REQUIRE(!event->hasInterpretedBpm());
    }

    clips.clearAllClips();
    AudioThumbnailManager::getInstance().clearCache();
}

TEST_CASE("The source's tempo and beat count can be set on a clip in time mode",
          "[clip][event][interpretation][session]") {
    // Detection cannot answer for a pad, a vocal take or a one-shot, and a user
    // may simply disagree with what it found. Neither is a reason to refuse the
    // fields: what a file is, is a fact about the file, and beat mode is a
    // separate choice about how to play it (#2676).
    EventModelFixture fixture;
    auto& clips = ClipManager::getInstance();
    clips.clearAllClips();

    SourcePool::getInstance().seedFactsForTesting("/tmp/untellable.wav", 4.0, 44100.0);

    const auto clipId =
        clips.createAudioClipBeats(1, 0.0, 4.0, "/tmp/untellable.wav", ClipView::Session, 120.0);

    REQUIRE(!clips.getClip(clipId)->primaryEvent()->autoTempo);

    clips.setSourceTempo(clipId, 90.0);

    const AudioEvent* event = clips.getClip(clipId)->primaryEvent();
    REQUIRE(event->interpBpm == Approx(90.0));
    REQUIRE(event->interpTotalBeats == Approx(6.0));

    // Typing a tempo says what the file is; it does not touch the intent. A
    // drop asks for nothing, so the tempo alone does not grant beat mode.
    REQUIRE(event->playbackIntent == PlaybackIntent::Free);
    REQUIRE(!event->autoTempo);

    // BEAT asks for beat mode, and the known tempo grants it immediately.
    clips.setAutoTempo(clipId, true, 120.0);
    event = clips.getClip(clipId)->primaryEvent();
    REQUIRE(event->playbackIntent == PlaybackIntent::Beat);
    REQUIRE(event->autoTempo);

    // A slot the user put back in time mode stays there when a tempo is typed.
    clips.setAutoTempo(clipId, false, 120.0);
    clips.setSourceTempo(clipId, 95.0);
    event = clips.getClip(clipId)->primaryEvent();
    REQUIRE(event->interpBpm == Approx(95.0));
    REQUIRE(event->playbackIntent == PlaybackIntent::Free);
    REQUIRE(!event->autoTempo);

    clips.clearAllClips();
}

TEST_CASE("BEAT grants beat mode only with a tempo behind it",
          "[clip][event][interpretation][session]") {
    // The toggle is the one way into beat mode by hand, and it wrote autoTempo
    // directly -- so a clip could still claim the mode with every beat view at
    // zero, which is the state creation stopped producing (#2676).
    EventModelFixture fixture;
    auto& clips = ClipManager::getInstance();
    clips.clearAllClips();
    AudioThumbnailManager::getInstance().clearCache();

    SourcePool::getInstance().seedFactsForTesting("/tmp/nothing-says.wav", 4.0, 44100.0);

    const auto clipId =
        clips.createAudioClipBeats(1, 0.0, 4.0, "/tmp/nothing-says.wav", ClipView::Session, 120.0);
    REQUIRE(!clips.getClip(clipId)->primaryEvent()->autoTempo);

    clips.setAutoTempo(clipId, true, 120.0);
    REQUIRE(!clips.getClip(clipId)->primaryEvent()->autoTempo);

    // The request was kept, so the tempo the user then types grants it.
    REQUIRE(clips.getClip(clipId)->primaryEvent()->playbackIntent == PlaybackIntent::Beat);
    clips.setSourceTempo(clipId, 90.0);
    REQUIRE(clips.getClip(clipId)->primaryEvent()->autoTempo);

    clips.clearAllClips();
    AudioThumbnailManager::getInstance().clearCache();
}

// =============================================================================
// Interpretation ownership (#2674 phase 2)
// =============================================================================

// Anything may write a value until the user does; then only the user may.
TEST_CASE("A tempo is replaced by analysis until the user owns it", "[clip][tempo][ownership]") {
    EventModelFixture fixture;
    AudioEvent event;

    REQUIRE(event.adoptBpm(120.0, Provenance::Analysis));
    REQUIRE(event.bpmFrom == Provenance::Analysis);
    REQUIRE(event.adoptBpm(128.0, Provenance::Analysis));
    REQUIRE(event.interpBpm == Approx(128.0));

    REQUIRE(event.adoptBpm(100.0, Provenance::User));
    REQUIRE(event.bpmFrom == Provenance::User);

    // A refused adopt leaves both the value and its owner untouched.
    REQUIRE_FALSE(event.adoptBpm(174.0, Provenance::Analysis));
    REQUIRE(event.interpBpm == Approx(100.0));
    REQUIRE(event.bpmFrom == Provenance::User);

    REQUIRE(event.adoptBpm(90.0, Provenance::User));
    REQUIRE(event.interpBpm == Approx(90.0));
    REQUIRE(event.bpmFrom == Provenance::User);
}

TEST_CASE("A beat count is replaced by analysis until the user owns it",
          "[clip][tempo][ownership]") {
    EventModelFixture fixture;
    AudioEvent event;

    REQUIRE(event.adoptTotalBeats(8.0, Provenance::Analysis));
    REQUIRE(event.beatsFrom == Provenance::Analysis);
    REQUIRE(event.adoptTotalBeats(16.0, Provenance::Analysis));
    REQUIRE(event.interpTotalBeats == Approx(16.0));

    REQUIRE(event.adoptTotalBeats(12.0, Provenance::User));
    REQUIRE(event.beatsFrom == Provenance::User);

    REQUIRE_FALSE(event.adoptTotalBeats(32.0, Provenance::Analysis));
    REQUIRE(event.interpTotalBeats == Approx(12.0));
    REQUIRE(event.beatsFrom == Provenance::User);

    REQUIRE(event.adoptTotalBeats(13.0, Provenance::User));
    REQUIRE(event.interpTotalBeats == Approx(13.0));
}

TEST_CASE("A tempo of zero is never adopted", "[clip][tempo][ownership]") {
    EventModelFixture fixture;
    AudioEvent event;
    REQUIRE(event.adoptBpm(120.0, Provenance::Analysis));

    REQUIRE_FALSE(event.adoptBpm(0.0, Provenance::User));
    REQUIRE(event.interpBpm == Approx(120.0));
    REQUIRE(event.bpmFrom == Provenance::Analysis);
}

TEST_CASE("The loop region follows its extent", "[clip][tempo][ownership]") {
    EventModelFixture fixture;
    auto& pool = SourcePool::getInstance();
    pool.seedFactsForTesting("/tmp/ownership-loop.wav", 5.486, 44100.0);

    AudioEvent event;
    event.sourceId = pool.acquire("/tmp/ownership-loop.wav");
    REQUIRE(event.adoptBpm(175.0, Provenance::Analysis));
    REQUIRE(event.adoptTotalBeats(16.0, Provenance::Analysis));

    SECTION("WholeSource is the zero-length sentinel") {
        event.setLoopLengthSeconds(2.0);
        event.setLoopExtent(RegionExtent::WholeSource);
        REQUIRE(event.loopLengthSamples == 0);
    }

    SECTION("Interpretation is the beat count at the tempo, and refits with it") {
        event.setLoopExtent(RegionExtent::Interpretation);
        REQUIRE(event.loopLengthSeconds() == Approx(5.486).margin(0.001));

        REQUIRE(event.adoptBpm(174.0, Provenance::User));
        REQUIRE(event.loopExtent == RegionExtent::Interpretation);
        REQUIRE(event.loopLengthSeconds() == Approx(5.517).margin(0.001));
    }

    SECTION("Explicit is a range of its own and does not move with the tempo") {
        event.setLoopLengthSeconds(2.0);
        REQUIRE(event.loopExtent == RegionExtent::Explicit);
        const auto lengthSamples = event.loopLengthSamples;

        REQUIRE(event.adoptBpm(174.0, Provenance::User));
        REQUIRE(event.loopLengthSamples == lengthSamples);
        REQUIRE(event.loopLengthSeconds() == Approx(2.0));
    }
}

// autoTempo is granted, never set: the intent asks and a tempo has to exist.
TEST_CASE("Beat mode is granted only when the intent asks and a tempo exists",
          "[clip][tempo][ownership]") {
    EventModelFixture fixture;
    AudioEvent event;

    SECTION("Free stays in time mode even with a tempo") {
        REQUIRE(event.adoptBpm(120.0, Provenance::Analysis));
        event.setPlaybackIntent(PlaybackIntent::Free);
        REQUIRE_FALSE(event.autoTempo);
    }

    SECTION("An asking intent waits for a tempo, and the tempo grants it") {
        AudioEvent asking;
        asking.setPlaybackIntent(PlaybackIntent::Beat);
        REQUIRE_FALSE(asking.autoTempo);

        REQUIRE(asking.adoptBpm(120.0, Provenance::Analysis));
        REQUIRE(asking.autoTempo);
    }

    SECTION("The BEAT toggle off records Free") {
        REQUIRE(event.adoptBpm(120.0, Provenance::Analysis));
        event.setBeatMode(true);
        REQUIRE(event.autoTempo);

        event.setBeatMode(false);
        REQUIRE(event.playbackIntent == PlaybackIntent::Free);
        REQUIRE_FALSE(event.autoTempo);
    }
}

// =============================================================================
// Ownership helpers (#2674 phase 2)
// =============================================================================

TEST_CASE("A ghost's interpretation-sized region refits to what it receives",
          "[clip][event][ghost][ownership]") {
    EventModelFixture fixture;
    auto source = makeAudioClip();  // 120 bpm, 8 beats

    auto ghost = makeAudioClip();
    auto& ghostEvent = *ghost.primaryEvent();
    ghostEvent.interpBpm = 90.0;
    ghostEvent.setLoopExtent(RegionExtent::Interpretation);
    REQUIRE(ghostEvent.loopLengthSeconds() == Approx(8.0 * 60.0 / 90.0));

    ghost.copySharedContentFrom(source);

    REQUIRE(ghostEvent.interpBpm == Approx(120.0));
    REQUIRE(ghostEvent.loopExtent == RegionExtent::Interpretation);
    REQUIRE(ghostEvent.loopLengthSeconds() == Approx(4.0));
}

TEST_CASE("A ghost's beat mode is resolved from the intent and tempo it receives",
          "[clip][event][ghost][ownership]") {
    EventModelFixture fixture;
    auto source = makeAudioClip();
    auto& sourceEvent = *source.primaryEvent();
    auto ghost = makeAudioClip();
    auto& ghostEvent = *ghost.primaryEvent();

    SECTION("An asking intent with a tempo is granted, whatever the flag said") {
        sourceEvent.playbackIntent = PlaybackIntent::Beat;
        sourceEvent.autoTempo = false;  // stale: written before the tempo landed
        ghost.copySharedContentFrom(source);
        REQUIRE(ghostEvent.autoTempo);
    }

    SECTION("A claim with no tempo behind it is not copied") {
        sourceEvent.playbackIntent = PlaybackIntent::Beat;
        sourceEvent.interpBpm = 0.0;
        sourceEvent.autoTempo = true;
        ghost.copySharedContentFrom(source);
        REQUIRE_FALSE(ghostEvent.autoTempo);
    }

    SECTION("Free stays in time mode") {
        sourceEvent.playbackIntent = PlaybackIntent::Free;
        sourceEvent.autoTempo = true;
        ghost.copySharedContentFrom(source);
        REQUIRE_FALSE(ghostEvent.autoTempo);
    }
}

TEST_CASE("Adopting another event's interpretation carries its ownership",
          "[clip][tempo][ownership]") {
    EventModelFixture fixture;
    AudioEvent src;
    REQUIRE(src.adoptBpm(120.0, Provenance::FileMetadata));
    REQUIRE(src.adoptTotalBeats(8.0, Provenance::Analysis));

    SECTION("An unowned destination takes both values and their owners") {
        AudioEvent dst;
        dst.adoptInterpretationFrom(src);
        REQUIRE(dst.interpBpm == Approx(120.0));
        REQUIRE(dst.bpmFrom == Provenance::FileMetadata);
        REQUIRE(dst.interpTotalBeats == Approx(8.0));
        REQUIRE(dst.beatsFrom == Provenance::Analysis);
    }

    SECTION("A user-owned destination keeps its values against a non-user source") {
        AudioEvent dst;
        REQUIRE(dst.adoptBpm(100.0, Provenance::User));
        REQUIRE(dst.adoptTotalBeats(12.0, Provenance::User));
        dst.adoptInterpretationFrom(src);
        REQUIRE(dst.interpBpm == Approx(100.0));
        REQUIRE(dst.interpTotalBeats == Approx(12.0));
        REQUIRE(dst.bpmFrom == Provenance::User);
        REQUIRE(dst.beatsFrom == Provenance::User);
    }

    SECTION("A user-owned source overwrites a user-owned destination") {
        AudioEvent dst;
        REQUIRE(dst.adoptBpm(100.0, Provenance::User));
        REQUIRE(dst.adoptTotalBeats(12.0, Provenance::User));
        AudioEvent typed;
        REQUIRE(typed.adoptBpm(90.0, Provenance::User));
        REQUIRE(typed.adoptTotalBeats(6.0, Provenance::User));
        dst.adoptInterpretationFrom(typed);
        REQUIRE(dst.interpBpm == Approx(90.0));
        REQUIRE(dst.interpTotalBeats == Approx(6.0));
        REQUIRE(dst.bpmFrom == Provenance::User);
        REQUIRE(dst.beatsFrom == Provenance::User);
    }

    SECTION("Each field is judged on its own owner") {
        AudioEvent dst;
        REQUIRE(dst.adoptBpm(100.0, Provenance::User));
        dst.adoptInterpretationFrom(src);
        REQUIRE(dst.interpBpm == Approx(100.0));
        REQUIRE(dst.interpTotalBeats == Approx(8.0));
        REQUIRE(dst.beatsFrom == Provenance::Analysis);
    }
}

TEST_CASE("A whole-source region follows the interpretation once there is one",
          "[clip][tempo][ownership]") {
    EventModelFixture fixture;
    auto clip = makeAudioClip();  // 120 bpm, 8 beats on a 4 s file
    auto& event = *clip.primaryEvent();
    REQUIRE(event.loopExtent == RegionExtent::WholeSource);

    SECTION("With a tempo and a beat count it becomes the fitted length") {
        event.followInterpretationIfWholeSource();
        REQUIRE(event.loopExtent == RegionExtent::Interpretation);
        REQUIRE(event.loopLengthSeconds() == Approx(4.0));
    }

    SECTION("Without a beat count there is nothing to follow") {
        event.interpTotalBeats = 0.0;
        event.followInterpretationIfWholeSource();
        REQUIRE(event.loopExtent == RegionExtent::WholeSource);
        REQUIRE(event.loopLengthSamples == 0);
    }

    SECTION("Without a tempo there is nothing to follow") {
        event.interpBpm = 0.0;
        event.followInterpretationIfWholeSource();
        REQUIRE(event.loopExtent == RegionExtent::WholeSource);
        REQUIRE(event.loopLengthSamples == 0);
    }

    SECTION("An explicit range is left alone") {
        event.setLoopLengthSeconds(1.0);
        event.followInterpretationIfWholeSource();
        REQUIRE(event.loopExtent == RegionExtent::Explicit);
        REQUIRE(event.loopLengthSeconds() == Approx(1.0));
    }
}

TEST_CASE("Restoring a loop length puts the snapshot back without re-tagging",
          "[clip][tempo][ownership]") {
    EventModelFixture fixture;
    auto clip = makeAudioClip();  // 8 beats at 120 would fit to 4 s
    auto& event = *clip.primaryEvent();
    const auto samples = event.secondsToSourceSamples(1.5);

    SECTION("An interpretation snapshot keeps its samples, not a refit") {
        event.restoreLoopLength(samples, RegionExtent::Interpretation);
        REQUIRE(event.loopLengthSamples == samples);
        REQUIRE(event.loopExtent == RegionExtent::Interpretation);
    }

    SECTION("An explicit snapshot") {
        event.restoreLoopLength(samples, RegionExtent::Explicit);
        REQUIRE(event.loopLengthSamples == samples);
        REQUIRE(event.loopExtent == RegionExtent::Explicit);
    }

    SECTION("A whole-source snapshot") {
        event.setLoopLengthSeconds(2.0);
        event.restoreLoopLength(0, RegionExtent::WholeSource);
        REQUIRE(event.loopLengthSamples == 0);
        REQUIRE(event.loopExtent == RegionExtent::WholeSource);
    }

    SECTION("A negative length is clamped to zero") {
        event.restoreLoopLength(-5, RegionExtent::Explicit);
        REQUIRE(event.loopLengthSamples == 0);
    }
}

TEST_CASE("Clamping the loop region keeps it inside the file", "[clip][tempo][ownership]") {
    EventModelFixture fixture;
    auto clip = makeAudioClip();  // 4 s file
    auto& event = *clip.primaryEvent();

    SECTION("The start is clamped for every extent") {
        for (const auto extent :
             {RegionExtent::WholeSource, RegionExtent::Interpretation, RegionExtent::Explicit}) {
            event.restoreLoopLength(0, extent);
            event.setLoopStartSeconds(10.0);
            event.clampLoopRegionToSource(4.0);
            REQUIRE(event.loopStartSeconds() == Approx(4.0));
            REQUIRE(event.loopExtent == extent);
        }
    }

    SECTION("An explicit range past the end is shortened to what is left") {
        event.setLoopStartSeconds(1.0);
        event.setLoopLengthSeconds(5.0);
        event.clampLoopRegionToSource(4.0);
        REQUIRE(event.loopStartSeconds() == Approx(1.0));
        REQUIRE(event.loopLengthSeconds() == Approx(3.0));
        REQUIRE(event.loopExtent == RegionExtent::Explicit);
    }

    SECTION("An explicit range inside the file is untouched") {
        event.setLoopStartSeconds(1.0);
        event.setLoopLengthSeconds(2.0);
        event.clampLoopRegionToSource(4.0);
        REQUIRE(event.loopLengthSeconds() == Approx(2.0));
    }

    SECTION("A region sized by the interpretation may overrun the file") {
        event.restoreLoopLength(event.secondsToSourceSamples(6.0), RegionExtent::Interpretation);
        event.clampLoopRegionToSource(4.0);
        REQUIRE(event.loopLengthSeconds() == Approx(6.0));
        REQUIRE(event.loopExtent == RegionExtent::Interpretation);
    }

    SECTION("A whole-source region has no length to clamp") {
        event.setLoopStartSeconds(1.0);
        event.clampLoopRegionToSource(4.0);
        REQUIRE(event.loopLengthSamples == 0);
        REQUIRE(event.loopExtent == RegionExtent::WholeSource);
    }

    SECTION("An unknown file duration clamps nothing") {
        event.setLoopStartSeconds(10.0);
        event.setLoopLengthSeconds(5.0);
        event.clampLoopRegionToSource(0.0);
        REQUIRE(event.loopStartSeconds() == Approx(10.0));
        REQUIRE(event.loopLengthSeconds() == Approx(5.0));
    }
}
