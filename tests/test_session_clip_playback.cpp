#include <algorithm>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>

#include "magda/daw/core/ClipInfo.hpp"
#include "magda/daw/core/ClipManager.hpp"

using namespace magda;

/**
 * Tests for session clip playback scheduling and loop behavior.
 *
 * These test the pure logic used by SessionClipScheduler and
 * WaveformGridComponent without requiring Tracktion Engine or JUCE UI.
 */

// =============================================================================
// Playhead position calculation (mirrors SessionClipScheduler logic)
// =============================================================================

namespace {

/**
 * Replicates the playhead position calculation from
 * SessionClipScheduler::getSessionPlayheadPosition().
 *
 * The real implementation reads transport position from TE; here we
 * accept elapsed time directly so we can test the math in isolation.
 */
double computeSessionPlayhead(double elapsed, double loopLength, double clipLength, bool looping) {
    if (clipLength <= 0.0)
        return -1.0;
    if (elapsed < 0.0)
        elapsed = 0.0;

    if (looping && loopLength > 0.0) {
        return std::fmod(elapsed, loopLength);
    }

    return std::min(elapsed, clipLength);
}

/**
 * Replicates the effective-boundary logic from
 * WaveformGridComponent::paintWaveform() for determining which region
 * of the waveform should be dimmed.
 */
double computeEffectiveLength(double clipLength, double loopEndSeconds) {
    if (loopEndSeconds > 0.0)
        return std::min(clipLength, loopEndSeconds);
    return clipLength;
}

}  // namespace

// =============================================================================
// Playhead: looping behavior
// =============================================================================

TEST_CASE("Session playhead wraps at loop boundary when looping", "[session][playhead][loop]") {
    double loopLength = 2.0;  // 2 seconds (e.g. 4 beats at 120 BPM)
    double clipLength = 8.0;  // full clip is 8 seconds
    bool looping = true;

    SECTION("Playhead at zero") {
        REQUIRE(computeSessionPlayhead(0.0, loopLength, clipLength, looping) == Catch::Approx(0.0));
    }

    SECTION("Playhead before loop end") {
        REQUIRE(computeSessionPlayhead(1.5, loopLength, clipLength, looping) == Catch::Approx(1.5));
    }

    SECTION("Playhead wraps at loop boundary") {
        REQUIRE(computeSessionPlayhead(2.0, loopLength, clipLength, looping) == Catch::Approx(0.0));
    }

    SECTION("Playhead wraps multiple times") {
        REQUIRE(computeSessionPlayhead(5.0, loopLength, clipLength, looping) == Catch::Approx(1.0));
        REQUIRE(computeSessionPlayhead(6.0, loopLength, clipLength, looping) == Catch::Approx(0.0));
        REQUIRE(computeSessionPlayhead(7.3, loopLength, clipLength, looping) == Catch::Approx(1.3));
    }

    SECTION("Playhead wraps at loop length, not clip length") {
        // This is the key distinction — fmod uses loopLength, not clipLength
        double pos = computeSessionPlayhead(3.0, loopLength, clipLength, looping);
        REQUIRE(pos == Catch::Approx(1.0));
        REQUIRE(pos < loopLength);
    }
}

// =============================================================================
// Playhead: non-looping behavior (the bug fix)
// =============================================================================

TEST_CASE("Session playhead runs to clip end when loop is off",
          "[session][playhead][no-loop][regression]") {
    /**
     * REGRESSION TEST
     *
     * Bug: playhead kept wrapping even with loop disabled because
     * launchClipLength_ was set from internalLoopLength instead of
     * the full clip duration, and fmod was used unconditionally.
     *
     * Fix: separated loopLength from clipLength; when not looping the
     * playhead clamps at the full clip duration.
     */

    double loopLength = 2.0;  // loop boundary (unused when not looping)
    double clipLength = 8.0;  // full clip duration
    bool looping = false;

    SECTION("Playhead advances normally") {
        REQUIRE(computeSessionPlayhead(0.0, loopLength, clipLength, looping) == Catch::Approx(0.0));
        REQUIRE(computeSessionPlayhead(3.0, loopLength, clipLength, looping) == Catch::Approx(3.0));
        REQUIRE(computeSessionPlayhead(7.9, loopLength, clipLength, looping) == Catch::Approx(7.9));
    }

    SECTION("Playhead does NOT wrap at loop boundary") {
        // Before the fix this would return fmod(3.0, 2.0) = 1.0
        double pos = computeSessionPlayhead(3.0, loopLength, clipLength, looping);
        REQUIRE(pos == Catch::Approx(3.0));
        REQUIRE(pos > loopLength);
    }

    SECTION("Playhead clamps at clip end") {
        REQUIRE(computeSessionPlayhead(8.0, loopLength, clipLength, looping) == Catch::Approx(8.0));
        REQUIRE(computeSessionPlayhead(10.0, loopLength, clipLength, looping) ==
                Catch::Approx(8.0));
        REQUIRE(computeSessionPlayhead(100.0, loopLength, clipLength, looping) ==
                Catch::Approx(8.0));
    }

    SECTION("Playhead reaches full clip duration, not loop length") {
        double pos = computeSessionPlayhead(5.0, loopLength, clipLength, looping);
        REQUIRE(pos == Catch::Approx(5.0));
        // Must exceed the loop length — the old bug would clamp/wrap at 2.0
        REQUIRE(pos > loopLength);
    }
}

TEST_CASE("Session playhead returns -1 when no clip is active", "[session][playhead]") {
    REQUIRE(computeSessionPlayhead(5.0, 2.0, 0.0, true) == Catch::Approx(-1.0));
    REQUIRE(computeSessionPlayhead(5.0, 2.0, -1.0, false) == Catch::Approx(-1.0));
}

TEST_CASE("Session playhead treats negative elapsed as zero", "[session][playhead]") {
    REQUIRE(computeSessionPlayhead(-1.0, 2.0, 8.0, true) == Catch::Approx(0.0));
    REQUIRE(computeSessionPlayhead(-5.0, 2.0, 8.0, false) == Catch::Approx(0.0));
}

// =============================================================================
// Waveform loop boundary dimming
// =============================================================================

TEST_CASE("Waveform effective length uses loop boundary when looping",
          "[waveform][loop][dimming]") {
    double clipLength = 8.0;

    SECTION("No loop — effective length is full clip") {
        REQUIRE(computeEffectiveLength(clipLength, 0.0) == Catch::Approx(8.0));
    }

    SECTION("Loop shorter than clip — effective length is loop end") {
        REQUIRE(computeEffectiveLength(clipLength, 2.0) == Catch::Approx(2.0));
    }

    SECTION("Loop equal to clip — effective length is clip length") {
        REQUIRE(computeEffectiveLength(clipLength, 8.0) == Catch::Approx(8.0));
    }

    SECTION("Loop longer than clip — clamped to clip length") {
        REQUIRE(computeEffectiveLength(clipLength, 12.0) == Catch::Approx(8.0));
    }
}

// =============================================================================
// ClipManager: loop property persistence
// =============================================================================

TEST_CASE("ClipManager persists loop enabled and loop length", "[session][clip][loop][property]") {
    ClipManager::getInstance().shutdown();

    ClipId clipId = ClipManager::getInstance().createAudioClip(1, 0.0, 8.0, "test.wav");
    REQUIRE(clipId != INVALID_CLIP_ID);

    const auto* clip = ClipManager::getInstance().getClip(clipId);
    REQUIRE(clip != nullptr);

    SECTION("Default loop state") {
        REQUIRE(clip->internalLoopEnabled == false);
        REQUIRE(clip->internalLoopLength == Catch::Approx(4.0));
    }

    SECTION("Enable loop") {
        ClipManager::getInstance().setClipLoopEnabled(clipId, true);
        clip = ClipManager::getInstance().getClip(clipId);
        REQUIRE(clip->internalLoopEnabled == true);
    }

    SECTION("Set loop length") {
        ClipManager::getInstance().setClipLoopLength(clipId, 8.0);
        clip = ClipManager::getInstance().getClip(clipId);
        REQUIRE(clip->internalLoopLength == Catch::Approx(8.0));
    }

    SECTION("Disable loop") {
        ClipManager::getInstance().setClipLoopEnabled(clipId, true);
        ClipManager::getInstance().setClipLoopEnabled(clipId, false);
        clip = ClipManager::getInstance().getClip(clipId);
        REQUIRE(clip->internalLoopEnabled == false);
    }
}

TEST_CASE("ClipManager notifies listeners on loop property change",
          "[session][clip][loop][notify]") {
    ClipManager::getInstance().shutdown();

    class TestListener : public ClipManagerListener {
      public:
        void clipsChanged() override {
            clipsChangedCount++;
        }
        void clipPropertyChanged(ClipId id) override {
            propertyChangedCount++;
            lastChangedClipId = id;
        }
        void clipSelectionChanged(ClipId) override {}

        int clipsChangedCount = 0;
        int propertyChangedCount = 0;
        ClipId lastChangedClipId = INVALID_CLIP_ID;
    };

    TestListener listener;
    ClipManager::getInstance().addListener(&listener);

    ClipId clipId = ClipManager::getInstance().createAudioClip(1, 0.0, 8.0, "test.wav");
    REQUIRE(clipId != INVALID_CLIP_ID);

    SECTION("setClipLoopEnabled notifies") {
        listener.propertyChangedCount = 0;
        ClipManager::getInstance().setClipLoopEnabled(clipId, true);
        REQUIRE(listener.propertyChangedCount >= 1);
        REQUIRE(listener.lastChangedClipId == clipId);
    }

    SECTION("setClipLoopLength notifies") {
        listener.propertyChangedCount = 0;
        ClipManager::getInstance().setClipLoopLength(clipId, 2.0);
        REQUIRE(listener.propertyChangedCount >= 1);
        REQUIRE(listener.lastChangedClipId == clipId);
    }

    ClipManager::getInstance().removeListener(&listener);
}

// =============================================================================
// Session clip state management
// =============================================================================

TEST_CASE("Session clip trigger/stop state transitions", "[session][clip][state]") {
    ClipManager::getInstance().shutdown();

    ClipId clipId = ClipManager::getInstance().createAudioClip(1, 0.0, 4.0, "test.wav");
    REQUIRE(clipId != INVALID_CLIP_ID);

    auto* clip = ClipManager::getInstance().getClip(clipId);
    REQUIRE(clip != nullptr);

    // Make it a session clip
    clip->view = ClipView::Session;
    clip->sceneIndex = 0;

    SECTION("Initial state is stopped") {
        REQUIRE(clip->isPlaying == false);
        REQUIRE(clip->isQueued == false);
    }

    SECTION("triggerClip queues the clip") {
        ClipManager::getInstance().triggerClip(clipId);
        clip = ClipManager::getInstance().getClip(clipId);
        REQUIRE(clip->isQueued == true);
    }

    SECTION("setClipPlayingState marks as playing") {
        ClipManager::getInstance().triggerClip(clipId);
        ClipManager::getInstance().setClipPlayingState(clipId, true);
        clip = ClipManager::getInstance().getClip(clipId);
        REQUIRE(clip->isPlaying == true);
    }

    SECTION("stopClip resets both flags") {
        ClipManager::getInstance().triggerClip(clipId);
        ClipManager::getInstance().setClipPlayingState(clipId, true);
        ClipManager::getInstance().stopClip(clipId);
        clip = ClipManager::getInstance().getClip(clipId);
        REQUIRE(clip->isPlaying == false);
        REQUIRE(clip->isQueued == false);
    }
}

// =============================================================================
// MIDI clip creation and management
// =============================================================================

TEST_CASE("CreateMidiClip — create via ClipManager and verify type", "[session][midi][create]") {
    ClipManager::getInstance().shutdown();

    ClipId clipId = ClipManager::getInstance().createMidiClip(1, 0.0, 4.0, ClipView::Session);
    REQUIRE(clipId != INVALID_CLIP_ID);

    const auto* clip = ClipManager::getInstance().getClip(clipId);
    REQUIRE(clip != nullptr);
    REQUIRE(clip->type == ClipType::MIDI);
    REQUIRE(clip->view == ClipView::Session);
    REQUIRE(clip->trackId == 1);
    REQUIRE(clip->length == Catch::Approx(4.0));
    REQUIRE(clip->midiNotes.empty());
}

TEST_CASE("AddMidiNotes — add notes and verify storage", "[session][midi][notes]") {
    ClipManager::getInstance().shutdown();

    ClipId clipId = ClipManager::getInstance().createMidiClip(1, 0.0, 4.0, ClipView::Session);
    REQUIRE(clipId != INVALID_CLIP_ID);

    // Add notes
    MidiNote note1{60, 100, 0.0, 1.0};  // C4, beat 0, 1 beat
    MidiNote note2{64, 80, 1.0, 0.5};   // E4, beat 1, half beat
    MidiNote note3{67, 110, 2.0, 2.0};  // G4, beat 2, 2 beats

    ClipManager::getInstance().addMidiNote(clipId, note1);
    ClipManager::getInstance().addMidiNote(clipId, note2);
    ClipManager::getInstance().addMidiNote(clipId, note3);

    const auto* clip = ClipManager::getInstance().getClip(clipId);
    REQUIRE(clip != nullptr);
    REQUIRE(clip->midiNotes.size() == 3);

    REQUIRE(clip->midiNotes[0].noteNumber == 60);
    REQUIRE(clip->midiNotes[0].velocity == 100);
    REQUIRE(clip->midiNotes[0].startBeat == Catch::Approx(0.0));
    REQUIRE(clip->midiNotes[0].lengthBeats == Catch::Approx(1.0));

    REQUIRE(clip->midiNotes[1].noteNumber == 64);
    REQUIRE(clip->midiNotes[1].velocity == 80);
    REQUIRE(clip->midiNotes[1].startBeat == Catch::Approx(1.0));
    REQUIRE(clip->midiNotes[1].lengthBeats == Catch::Approx(0.5));

    REQUIRE(clip->midiNotes[2].noteNumber == 67);
    REQUIRE(clip->midiNotes[2].velocity == 110);
    REQUIRE(clip->midiNotes[2].startBeat == Catch::Approx(2.0));
    REQUIRE(clip->midiNotes[2].lengthBeats == Catch::Approx(2.0));
}

TEST_CASE("SyncMidiClipToSlot — verify ClipManager state for MIDI session clip",
          "[session][midi][sync]") {
    // Note: Full TE sync requires a running engine. This test verifies the
    // ClipManager side: creating a MIDI clip and assigning it to a session slot.
    ClipManager::getInstance().shutdown();

    ClipId clipId = ClipManager::getInstance().createMidiClip(1, 0.0, 4.0, ClipView::Session);
    REQUIRE(clipId != INVALID_CLIP_ID);

    // Add some notes
    ClipManager::getInstance().addMidiNote(clipId, {60, 100, 0.0, 1.0});
    ClipManager::getInstance().addMidiNote(clipId, {64, 80, 1.0, 1.0});

    // Assign to scene slot
    ClipManager::getInstance().setClipSceneIndex(clipId, 0);

    const auto* clip = ClipManager::getInstance().getClip(clipId);
    REQUIRE(clip != nullptr);
    REQUIRE(clip->sceneIndex == 0);
    REQUIRE(clip->type == ClipType::MIDI);
    REQUIRE(clip->midiNotes.size() == 2);

    // Verify the clip is retrievable by slot
    ClipId slotClipId = ClipManager::getInstance().getClipInSlot(1, 0);
    REQUIRE(slotClipId == clipId);
}

TEST_CASE("LaunchMidiClip — verify launch/stop cycle via ClipManager state",
          "[session][midi][launch]") {
    // Note: Actual audio playback requires TE. This test verifies the
    // ClipManager state transitions for MIDI clips match audio clips.
    ClipManager::getInstance().shutdown();

    ClipId clipId = ClipManager::getInstance().createMidiClip(1, 0.0, 4.0, ClipView::Session);
    REQUIRE(clipId != INVALID_CLIP_ID);
    ClipManager::getInstance().setClipSceneIndex(clipId, 0);

    auto* clip = ClipManager::getInstance().getClip(clipId);
    REQUIRE(clip != nullptr);
    clip->view = ClipView::Session;

    SECTION("Initial state is stopped") {
        REQUIRE(clip->isPlaying == false);
        REQUIRE(clip->isQueued == false);
    }

    SECTION("triggerClip queues the MIDI clip") {
        ClipManager::getInstance().triggerClip(clipId);
        clip = ClipManager::getInstance().getClip(clipId);
        REQUIRE(clip->isQueued == true);
    }

    SECTION("setClipPlayingState marks MIDI clip as playing") {
        ClipManager::getInstance().triggerClip(clipId);
        ClipManager::getInstance().setClipPlayingState(clipId, true);
        clip = ClipManager::getInstance().getClip(clipId);
        REQUIRE(clip->isPlaying == true);
    }

    SECTION("stopClip resets MIDI clip state") {
        ClipManager::getInstance().triggerClip(clipId);
        ClipManager::getInstance().setClipPlayingState(clipId, true);
        ClipManager::getInstance().stopClip(clipId);
        clip = ClipManager::getInstance().getClip(clipId);
        REQUIRE(clip->isPlaying == false);
        REQUIRE(clip->isQueued == false);
    }
}

TEST_CASE("StopMidiClipSendsAllNotesOff — verify MIDI clip type is detectable for all-notes-off",
          "[session][midi][allnotesoff]") {
    // Note: Actual MIDI message sending requires TE's DeviceManager.
    // This test verifies the clip type detection that gates the all-notes-off logic.
    ClipManager::getInstance().shutdown();

    ClipId midiClipId = ClipManager::getInstance().createMidiClip(1, 0.0, 4.0, ClipView::Session);
    ClipId audioClipId =
        ClipManager::getInstance().createAudioClip(1, 0.0, 4.0, "test.wav", ClipView::Session);

    const auto* midiClip = ClipManager::getInstance().getClip(midiClipId);
    const auto* audioClip = ClipManager::getInstance().getClip(audioClipId);

    REQUIRE(midiClip != nullptr);
    REQUIRE(audioClip != nullptr);

    // Only MIDI clips should trigger all-notes-off
    REQUIRE(midiClip->type == ClipType::MIDI);
    REQUIRE(audioClip->type == ClipType::Audio);

    // Verify the type check used in AudioBridge::stopSessionClip
    REQUIRE((midiClip->type == ClipType::MIDI) == true);
    REQUIRE((audioClip->type == ClipType::MIDI) == false);
}

TEST_CASE("MidiClipSlotAppearance — clip slot shows as occupied after MIDI clip creation",
          "[session][midi][appearance]") {
    ClipManager::getInstance().shutdown();

    TrackId trackId = 1;
    int sceneIndex = 0;

    // No clip in slot initially
    REQUIRE(ClipManager::getInstance().getClipInSlot(trackId, sceneIndex) == INVALID_CLIP_ID);

    // Create MIDI clip and assign to slot
    ClipId clipId = ClipManager::getInstance().createMidiClip(trackId, 0.0, 4.0, ClipView::Session);
    REQUIRE(clipId != INVALID_CLIP_ID);
    ClipManager::getInstance().setClipSceneIndex(clipId, sceneIndex);

    // Slot should now be occupied
    ClipId slotClipId = ClipManager::getInstance().getClipInSlot(trackId, sceneIndex);
    REQUIRE(slotClipId == clipId);

    // Clip should be identifiable as MIDI
    const auto* clip = ClipManager::getInstance().getClip(slotClipId);
    REQUIRE(clip != nullptr);
    REQUIRE(clip->type == ClipType::MIDI);
}

TEST_CASE("Session MIDI clip loop offset", "[session][midi][loop]") {
    auto& cm = ClipManager::getInstance();
    cm.clearAllClips();

    TrackId trackId = 1;
    int sceneIndex = 0;

    // Create a 4-beat session MIDI clip (defaults to loop enabled)
    ClipId clipId = cm.createMidiClip(trackId, 0.0, 4.0, ClipView::Session);
    REQUIRE(clipId != INVALID_CLIP_ID);
    cm.setClipSceneIndex(clipId, sceneIndex);

    const auto* clip = cm.getClip(clipId);
    REQUIRE(clip != nullptr);

    SECTION("Session clips default to loop enabled") {
        REQUIRE(clip->internalLoopEnabled == true);
        REQUIRE(clip->internalLoopLength == Catch::Approx(4.0));
    }

    SECTION("Loop offset defaults to zero") {
        REQUIRE(clip->internalLoopOffset == Catch::Approx(0.0));
    }

    SECTION("Setting loop offset updates clip state") {
        cm.setClipLoopOffset(clipId, 2.0);
        clip = cm.getClip(clipId);
        REQUIRE(clip->internalLoopOffset == Catch::Approx(2.0));
    }

    SECTION("Loop region accounts for offset") {
        cm.setClipLoopOffset(clipId, 2.0);
        cm.setClipLoopLength(clipId, 4.0);
        clip = cm.getClip(clipId);

        double loopStart = clip->internalLoopOffset;
        double loopEnd = loopStart + clip->internalLoopLength;
        REQUIRE(loopStart == Catch::Approx(2.0));
        REQUIRE(loopEnd == Catch::Approx(6.0));
    }

    SECTION("Notes extending past loop end should be truncatable") {
        // Add a note that extends past the loop boundary
        cm.setClipLoopOffset(clipId, 1.0);
        cm.setClipLoopLength(clipId, 2.0);
        clip = cm.getClip(clipId);

        double loopEnd = clip->internalLoopOffset + clip->internalLoopLength;
        REQUIRE(loopEnd == Catch::Approx(3.0));

        // A note at beat 2.5 with length 1.0 would end at 3.5, past loop end
        double noteStart = 2.5;
        double noteLength = 1.0;
        double noteEnd = noteStart + noteLength;
        REQUIRE(noteEnd > loopEnd);

        // Truncated length should clamp to loop end
        double truncatedLength = std::max(0.001, loopEnd - noteStart);
        REQUIRE(truncatedLength == Catch::Approx(0.5));
    }
}
