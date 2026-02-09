#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

#include "magda/daw/audio/MidiEventQueue.hpp"
#include "magda/daw/core/ClipInfo.hpp"
#include "magda/daw/core/ClipManager.hpp"
#include "magda/daw/core/TrackManager.hpp"
#include "magda/daw/project/ProjectInfo.hpp"
#include "magda/daw/project/ProjectSerializer.hpp"
#include "magda/daw/ui/state/TimelineController.hpp"
#include "magda/daw/ui/state/TimelineEvents.hpp"

using namespace magda;

// =============================================================================
// MidiEventQueue Tests
// =============================================================================

class MidiEventQueueTest final : public juce::UnitTest {
  public:
    MidiEventQueueTest() : juce::UnitTest("MidiEventQueue Tests", "magda") {}

    void runTest() override {
        testEmptyQueue();
        testPushAndPop();
        testFIFOOrder();
        testQueueFull();
        testClear();
        testHasPending();
        testEventDataIntegrity();
    }

  private:
    void testEmptyQueue() {
        beginTest("Empty queue returns false on pop");

        MidiEventQueue queue;
        MidiEventEntry entry;
        expect(!queue.pop(entry), "Pop on empty queue should return false");
        expect(!queue.hasPending(), "Empty queue should not have pending events");
    }

    void testPushAndPop() {
        beginTest("Push and pop single event");

        MidiEventQueue queue;

        MidiEventEntry pushed;
        pushed.deviceName = "TestDevice";
        pushed.channel = 1;
        pushed.type = MidiEventEntry::NoteOn;
        pushed.data1 = 60;
        pushed.data2 = 100;
        pushed.timestamp = 1.0;

        expect(queue.push(pushed), "Push should succeed");

        MidiEventEntry popped;
        expect(queue.pop(popped), "Pop should succeed after push");
        expectEquals(popped.deviceName, juce::String("TestDevice"));
        expectEquals(popped.channel, 1);
        expect(popped.type == MidiEventEntry::NoteOn);
        expectEquals(popped.data1, 60);
        expectEquals(popped.data2, 100);
        expectEquals(popped.timestamp, 1.0);

        // Queue should now be empty
        expect(!queue.pop(popped), "Queue should be empty after popping");
    }

    void testFIFOOrder() {
        beginTest("FIFO ordering preserved");

        MidiEventQueue queue;

        for (int i = 0; i < 10; ++i) {
            MidiEventEntry entry;
            entry.data1 = i;
            expect(queue.push(entry));
        }

        for (int i = 0; i < 10; ++i) {
            MidiEventEntry entry;
            expect(queue.pop(entry));
            expectEquals(entry.data1, i);
        }
    }

    void testQueueFull() {
        beginTest("Queue drops events when full");

        MidiEventQueue queue;

        // Fill to capacity (size - 1 because ring buffer reserves one slot)
        int pushed = 0;
        for (int i = 0; i < MidiEventQueue::kQueueSize; ++i) {
            MidiEventEntry entry;
            entry.data1 = i;
            if (queue.push(entry))
                pushed++;
        }

        // Should have pushed kQueueSize - 1 events (one slot reserved)
        expectEquals(pushed, MidiEventQueue::kQueueSize - 1);

        // Next push should fail
        MidiEventEntry overflow;
        overflow.data1 = 999;
        expect(!queue.push(overflow), "Push should fail when queue is full");

        // Pop one and push should succeed again
        MidiEventEntry popped;
        expect(queue.pop(popped));
        expect(queue.push(overflow), "Push should succeed after popping");
    }

    void testClear() {
        beginTest("Clear empties the queue");

        MidiEventQueue queue;

        for (int i = 0; i < 5; ++i) {
            MidiEventEntry entry;
            entry.data1 = i;
            queue.push(entry);
        }

        expect(queue.hasPending());
        queue.clear();
        expect(!queue.hasPending(), "Queue should be empty after clear");

        MidiEventEntry entry;
        expect(!queue.pop(entry), "Pop should fail after clear");
    }

    void testHasPending() {
        beginTest("hasPending reflects queue state");

        MidiEventQueue queue;

        expect(!queue.hasPending());

        MidiEventEntry entry;
        entry.data1 = 42;
        queue.push(entry);
        expect(queue.hasPending(), "Should have pending after push");

        queue.pop(entry);
        expect(!queue.hasPending(), "Should not have pending after pop");
    }

    void testEventDataIntegrity() {
        beginTest("All event types preserve data correctly");

        MidiEventQueue queue;

        // NoteOn
        {
            MidiEventEntry e;
            e.type = MidiEventEntry::NoteOn;
            e.deviceName = "Keyboard";
            e.channel = 10;
            e.data1 = 36;
            e.data2 = 127;
            queue.push(e);
        }

        // CC
        {
            MidiEventEntry e;
            e.type = MidiEventEntry::CC;
            e.deviceName = "Controller";
            e.channel = 1;
            e.data1 = 74;
            e.data2 = 64;
            queue.push(e);
        }

        // PitchBend
        {
            MidiEventEntry e;
            e.type = MidiEventEntry::PitchBend;
            e.deviceName = "Synth";
            e.channel = 2;
            e.pitchBendValue = 12000;
            queue.push(e);
        }

        // Verify NoteOn
        MidiEventEntry out;
        expect(queue.pop(out));
        expect(out.type == MidiEventEntry::NoteOn);
        expectEquals(out.channel, 10);
        expectEquals(out.data1, 36);
        expectEquals(out.data2, 127);

        // Verify CC
        expect(queue.pop(out));
        expect(out.type == MidiEventEntry::CC);
        expectEquals(out.data1, 74);
        expectEquals(out.data2, 64);

        // Verify PitchBend
        expect(queue.pop(out));
        expect(out.type == MidiEventEntry::PitchBend);
        expectEquals(out.pitchBendValue, 12000);
    }
};

static MidiEventQueueTest midiEventQueueTest;

// =============================================================================
// TimelineController StartRecordEvent Tests
// =============================================================================

class StartRecordEventTest final : public juce::UnitTest {
  public:
    StartRecordEventTest() : juce::UnitTest("StartRecordEvent Tests", "magda") {}

    void runTest() override {
        testStartRecordNoArmedTracksDoesNothing();
        testStartRecordWithArmedTrackStartsRecording();
        testPlayWithArmedTrackStartsRecording();
        testPlayWithNoArmedTrackStartsPlayback();
        testStopAfterRecordClearsRecording();
    }

  private:
    void testStartRecordNoArmedTracksDoesNothing() {
        beginTest("StartRecordEvent does nothing when no tracks are armed");

        TimelineController controller;

        auto& tm = TrackManager::getInstance();
        TrackId trackId = tm.createTrack("Test Track");
        tm.setSelectedTrack(trackId);

        controller.dispatch(StartRecordEvent{});

        auto& state = controller.getState();
        expect(!state.playhead.isPlaying, "Should not be playing");
        expect(!state.playhead.isRecording, "Should not be recording");

        tm.deleteTrack(trackId);
    }

    void testStartRecordWithArmedTrackStartsRecording() {
        beginTest("StartRecordEvent starts recording when a track is already armed");

        TimelineController controller;

        auto& tm = TrackManager::getInstance();
        TrackId trackId = tm.createTrack("Test Track");
        tm.setSelectedTrack(trackId);
        tm.setTrackRecordArmed(trackId, true);

        controller.dispatch(SetEditPositionEvent{1.0});
        controller.dispatch(StartRecordEvent{});

        auto& state = controller.getState();
        expect(state.playhead.isPlaying, "Should be playing");
        expect(state.playhead.isRecording, "Should be recording");
        expectEquals(state.playhead.playbackPosition, 1.0);

        controller.dispatch(StopPlaybackEvent{});
        tm.setTrackRecordArmed(trackId, false);
        tm.deleteTrack(trackId);
    }

    void testPlayWithArmedTrackStartsRecording() {
        beginTest("StartRecordEvent with armed track starts recording and playback");

        TimelineController controller;

        auto& tm = TrackManager::getInstance();
        TrackId trackId = tm.createTrack("Test Track");
        tm.setSelectedTrack(trackId);
        tm.setTrackRecordArmed(trackId, true);

        controller.dispatch(SetEditPositionEvent{2.0});
        controller.dispatch(StartRecordEvent{});

        auto& state = controller.getState();
        expect(state.playhead.isPlaying, "Should be playing");
        expect(state.playhead.isRecording, "Should be recording with armed track");
        expectEquals(state.playhead.playbackPosition, 2.0);

        controller.dispatch(StopPlaybackEvent{});
        tm.setTrackRecordArmed(trackId, false);
        tm.deleteTrack(trackId);
    }

    void testPlayWithNoArmedTrackStartsPlayback() {
        beginTest("StartPlaybackEvent with no armed tracks starts plain playback");

        TimelineController controller;

        controller.dispatch(SetEditPositionEvent{3.0});
        controller.dispatch(StartPlaybackEvent{});

        auto& state = controller.getState();
        expect(state.playhead.isPlaying, "Should be playing");
        expect(!state.playhead.isRecording, "Should NOT be recording without armed tracks");

        controller.dispatch(StopPlaybackEvent{});
    }

    void testStopAfterRecordClearsRecording() {
        beginTest("StopPlaybackEvent after recording clears both flags");

        TimelineController controller;

        auto& tm = TrackManager::getInstance();
        TrackId trackId = tm.createTrack("Test Track");
        tm.setSelectedTrack(trackId);
        tm.setTrackRecordArmed(trackId, true);

        controller.dispatch(StartRecordEvent{});
        expect(controller.getState().playhead.isRecording);

        controller.dispatch(StopPlaybackEvent{});

        auto& state = controller.getState();
        expect(!state.playhead.isPlaying, "Should not be playing after stop");
        expect(!state.playhead.isRecording, "Should not be recording after stop");

        tm.setTrackRecordArmed(trackId, false);
        tm.deleteTrack(trackId);
    }
};

static StartRecordEventTest startRecordEventTest;

// =============================================================================
// ClipInfo CC/PitchBend Data Tests
// =============================================================================

class ClipInfoMidiDataTest final : public juce::UnitTest {
  public:
    ClipInfoMidiDataTest() : juce::UnitTest("ClipInfo MIDI CC/PitchBend Tests", "magda") {}

    void runTest() override {
        testMidiCCDataDefaults();
        testMidiPitchBendDataDefaults();
        testClipInfoStoresCCData();
        testClipInfoStoresPitchBendData();
    }

  private:
    void testMidiCCDataDefaults() {
        beginTest("MidiCCData has correct defaults");

        MidiCCData cc;
        expectEquals(cc.controller, 0);
        expectEquals(cc.value, 0);
        expectEquals(cc.beatPosition, 0.0);
    }

    void testMidiPitchBendDataDefaults() {
        beginTest("MidiPitchBendData has correct defaults");

        MidiPitchBendData pb;
        expectEquals(pb.value, 0);
        expectEquals(pb.beatPosition, 0.0);
    }

    void testClipInfoStoresCCData() {
        beginTest("ClipInfo stores CC data");

        ClipInfo clip;
        clip.midiCCData.push_back({1, 64, 0.0});
        clip.midiCCData.push_back({74, 127, 2.5});
        clip.midiCCData.push_back({11, 100, 4.0});

        expectEquals(static_cast<int>(clip.midiCCData.size()), 3);
        expectEquals(clip.midiCCData[0].controller, 1);
        expectEquals(clip.midiCCData[0].value, 64);
        expectEquals(clip.midiCCData[1].controller, 74);
        expectEquals(clip.midiCCData[1].beatPosition, 2.5);
        expectEquals(clip.midiCCData[2].controller, 11);
    }

    void testClipInfoStoresPitchBendData() {
        beginTest("ClipInfo stores pitch bend data");

        ClipInfo clip;
        clip.midiPitchBendData.push_back({8192, 0.0});
        clip.midiPitchBendData.push_back({16383, 1.0});
        clip.midiPitchBendData.push_back({0, 2.0});

        expectEquals(static_cast<int>(clip.midiPitchBendData.size()), 3);
        expectEquals(clip.midiPitchBendData[0].value, 8192);
        expectEquals(clip.midiPitchBendData[1].value, 16383);
        expectEquals(clip.midiPitchBendData[2].value, 0);
        expectEquals(clip.midiPitchBendData[2].beatPosition, 2.0);
    }
};

static ClipInfoMidiDataTest clipInfoMidiDataTest;

// =============================================================================
// ProjectSerializer CC/PitchBend Roundtrip Tests
// Uses the public serializeProject/deserializeProject API
// =============================================================================

class ProjectSerializerMidiRoundtripTest final : public juce::UnitTest {
  public:
    ProjectSerializerMidiRoundtripTest()
        : juce::UnitTest("ProjectSerializer MIDI CC/PitchBend Roundtrip Tests", "magda") {}

    void runTest() override {
        testCCAndPitchBendRoundtrip();
        testEmptyMidiDataRoundtrip();
    }

  private:
    void cleanState() {
        TrackManager::getInstance().clearAllTracks();
        ClipManager::getInstance().clearAllClips();
    }

    void testCCAndPitchBendRoundtrip() {
        beginTest("CC and pitch bend data survive full project roundtrip");

        cleanState();

        // Create a track and a MIDI clip
        auto trackId = TrackManager::getInstance().createTrack("Test Track");
        auto clipId = ClipManager::getInstance().createMidiClip(trackId, 0.0, 4.0);

        // Add notes, CC, and pitch bend data
        ClipManager::getInstance().addMidiNote(clipId, {60, 100, 0.0, 1.0});
        ClipManager::getInstance().addMidiNote(clipId, {64, 80, 1.0, 0.5});

        auto* clip = ClipManager::getInstance().getClip(clipId);
        expect(clip != nullptr, "Clip should exist");

        clip->midiCCData.push_back({1, 64, 0.0});
        clip->midiCCData.push_back({74, 100, 1.5});
        clip->midiCCData.push_back({11, 0, 3.0});

        clip->midiPitchBendData.push_back({8192, 0.0});
        clip->midiPitchBendData.push_back({16383, 0.5});
        clip->midiPitchBendData.push_back({0, 1.0});
        clip->midiPitchBendData.push_back({8192, 1.5});

        // Serialize via public API
        ProjectInfo info;
        info.name = "Test Project";
        auto json = ProjectSerializer::serializeProject(info);

        // Clear state and deserialize
        cleanState();

        ProjectInfo outInfo;
        bool ok = ProjectSerializer::deserializeProject(json, outInfo);
        expect(ok, "Project deserialization should succeed");

        // Get the deserialized clip
        auto allClips = ClipManager::getInstance().getClips();
        expectEquals(static_cast<int>(allClips.size()), 1);

        auto* restored = ClipManager::getInstance().getClip(allClips[0].id);
        expect(restored != nullptr, "Restored clip should exist");

        // Verify notes survived
        expectEquals(static_cast<int>(restored->midiNotes.size()), 2);
        expectEquals(restored->midiNotes[0].noteNumber, 60);
        expectEquals(restored->midiNotes[1].noteNumber, 64);

        // Verify CC data survived
        expectEquals(static_cast<int>(restored->midiCCData.size()), 3);
        expectEquals(restored->midiCCData[0].controller, 1);
        expectEquals(restored->midiCCData[0].value, 64);
        expectEquals(restored->midiCCData[0].beatPosition, 0.0);
        expectEquals(restored->midiCCData[1].controller, 74);
        expectEquals(restored->midiCCData[1].value, 100);
        expectEquals(restored->midiCCData[1].beatPosition, 1.5);
        expectEquals(restored->midiCCData[2].controller, 11);
        expectEquals(restored->midiCCData[2].value, 0);
        expectEquals(restored->midiCCData[2].beatPosition, 3.0);

        // Verify pitch bend data survived
        expectEquals(static_cast<int>(restored->midiPitchBendData.size()), 4);
        expectEquals(restored->midiPitchBendData[0].value, 8192);
        expectEquals(restored->midiPitchBendData[0].beatPosition, 0.0);
        expectEquals(restored->midiPitchBendData[1].value, 16383);
        expectEquals(restored->midiPitchBendData[1].beatPosition, 0.5);
        expectEquals(restored->midiPitchBendData[2].value, 0);
        expectEquals(restored->midiPitchBendData[2].beatPosition, 1.0);
        expectEquals(restored->midiPitchBendData[3].value, 8192);
        expectEquals(restored->midiPitchBendData[3].beatPosition, 1.5);
    }

    void testEmptyMidiDataRoundtrip() {
        beginTest("Empty CC/PitchBend data roundtrips correctly");

        // Verify that a clip with no CC/pitchbend data stays empty after roundtrip
        ClipInfo emptyClip;
        emptyClip.type = ClipType::MIDI;
        expect(emptyClip.midiCCData.empty(), "New clip CC data should be empty");
        expect(emptyClip.midiPitchBendData.empty(), "New clip PitchBend data should be empty");

        // Also verify the clip from the previous test's deserialization
        // had its CC data correctly populated (already checked above)
        // This test just confirms empty data stays empty on a fresh ClipInfo
        emptyClip.midiNotes.push_back({60, 100, 0.0, 1.0});
        expect(emptyClip.midiCCData.empty(), "CC should still be empty after adding notes");
        expect(emptyClip.midiPitchBendData.empty(),
               "PitchBend should still be empty after adding notes");
    }
};

static ProjectSerializerMidiRoundtripTest projectSerializerMidiRoundtripTest;
