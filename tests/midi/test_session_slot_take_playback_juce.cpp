#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>
#include <tracktion_engine/tracktion_engine.h>

#include "JuceTestStateGuard.hpp"
#include "SharedTestEngine.hpp"
#include "magda/daw/audio/AudioBridge.hpp"
#include "magda/daw/core/ClipManager.hpp"
#include "magda/daw/core/TrackManager.hpp"
#include "magda/daw/core/UndoManager.hpp"
#include "magda/daw/engine/TracktionEngineWrapper.hpp"

using namespace magda;
namespace te = tracktion;

// A MIDI take recorded into a session slot under Tracktion must be launchable (#2967).
class SessionSlotTakePlaybackTest final : public juce::UnitTest {
  public:
    SessionSlotTakePlaybackTest() : juce::UnitTest("Session Slot Take Playback", "magda") {}

    void runTest() override {
        magda::test::runWithCleanJuceState([this] { testRecordedTakeIsLaunchable(); });
        magda::test::runWithCleanJuceState([this] { testRecordedAudioTakeIsLaunchable(); });
    }

  private:
    void testRecordedTakeIsLaunchable() {
        beginTest("A recorded MIDI slot take resolves to a launchable TE clip");

        auto& wrapper = magda::test::getSharedEngine();
        magda::test::resetTransport(wrapper);
        auto* bridge = wrapper.getAudioBridge();
        auto* edit = wrapper.getEdit();
        expect(bridge != nullptr && edit != nullptr);
        if (bridge == nullptr || edit == nullptr)
            return;

        ClipManager::getInstance().clearAllClips();
        TrackManager::getInstance().clearAllTracks();
        UndoManager::getInstance().clearHistory();
        wrapper.setTempo(120.0);
        wrapper.setTimeSignature(4, 4);

        const auto trackId = TrackManager::getInstance().createTrack("Take Target");
        TrackManager::getInstance().setTrackMidiInput(trackId, "all");
        TrackManager::getInstance().setTrackRecordArmed(trackId, true);
        bridge->createAudioTrack(trackId, "Take Target");
        bridge->setTrackMidiInput(trackId, "all");
        bridge->syncAllArmedTracksToTE();

        constexpr int sceneIndex = 0;
        wrapper.armSessionSlotRecording(trackId, sceneIndex);
        wrapper.testSetSessionSlotRecordingActive(trackId, sceneIndex);

        auto* track = bridge->getAudioTrack(trackId);
        expect(track != nullptr);
        if (track == nullptr)
            return;
        track->getClipSlotList().ensureNumberOfSlots(sceneIndex + 1);
        auto* slot = track->getClipSlotList().getClipSlots()[sceneIndex];

        auto clipRef = te::insertMIDIClip(
            *slot, {edit->tempoSequence.toTime(te::BeatPosition::fromBeats(0.0)),
                    edit->tempoSequence.toTime(te::BeatPosition::fromBeats(1.5))});
        expect(clipRef != nullptr);
        if (!clipRef)
            return;
        clipRef->getSequence().addNote(60, te::BeatPosition::fromBeats(0.5),
                                       te::BeatDuration::fromBeats(0.5), 100, 0, nullptr);

        expect(wrapper.testFinalizeSessionSlotMidiRecording(trackId, *clipRef));

        const auto clipId = ClipManager::getInstance().getClipInSlot(trackId, sceneIndex);
        expect(clipId != INVALID_CLIP_ID, "Take is in the model");

        auto* teClip = bridge->getSessionTeClip(clipId);
        expect(teClip != nullptr, "Take maps to a TE slot clip");
        if (teClip != nullptr) {
            expect(teClip == slot->getClip(), "The slot holds the mapped clip, not the raw take");
            auto handle = teClip->getLaunchHandle();
            expect(handle != nullptr);
            auto* midi = dynamic_cast<te::MidiClip*>(teClip);
            expect(midi != nullptr && midi->getSequence().getNotes().size() == 1,
                   "The mapped clip carries the recorded note");

            if (handle != nullptr) {
                bridge->launchSessionClip(clipId, true);
                const auto queued = handle->getQueuedStatus();
                expect(handle->getPlayingStatus() == te::LaunchHandle::PlayState::playing ||
                           (queued && *queued == te::LaunchHandle::QueueState::playQueued),
                       "Launching the slot clip starts or queues it");
                bridge->stopSessionClip(clipId);
            }
        }

        UndoManager::getInstance().clearHistory();
        ClipManager::getInstance().clearAllClips();
        TrackManager::getInstance().clearAllTracks();
    }

    void testRecordedAudioTakeIsLaunchable() {
        beginTest("A recorded audio slot take resolves to a launchable TE clip");

        auto& wrapper = magda::test::getSharedEngine();
        magda::test::resetTransport(wrapper);
        auto* bridge = wrapper.getAudioBridge();
        auto* edit = wrapper.getEdit();
        expect(bridge != nullptr && edit != nullptr);
        if (bridge == nullptr || edit == nullptr)
            return;

        ClipManager::getInstance().clearAllClips();
        TrackManager::getInstance().clearAllTracks();
        UndoManager::getInstance().clearHistory();
        wrapper.setTempo(120.0);
        wrapper.setTimeSignature(4, 4);

        const auto trackId = TrackManager::getInstance().createTrack("Audio Take Target");
        bridge->createAudioTrack(trackId, "Audio Take Target");

        // Two seconds of silence at 120 BPM is one bar.
        juce::TemporaryFile tempFile(".wav");
        {
            juce::WavAudioFormat wav;
            std::unique_ptr<juce::AudioFormatWriter> writer(wav.createWriterFor(
                new juce::FileOutputStream(tempFile.getFile()), 44100.0, 1, 16, {}, 0));
            expect(writer != nullptr);
            if (writer == nullptr)
                return;
            juce::AudioBuffer<float> silence(1, 88200);
            silence.clear();
            writer->writeFromAudioSampleBuffer(silence, 0, silence.getNumSamples());
        }
        expect(tempFile.getFile().existsAsFile());

        constexpr int sceneIndex = 0;
        wrapper.armSessionSlotRecording(trackId, sceneIndex);
        wrapper.testSetSessionSlotRecordingActive(trackId, sceneIndex);

        auto* track = bridge->getAudioTrack(trackId);
        expect(track != nullptr);
        if (track == nullptr)
            return;
        track->getClipSlotList().ensureNumberOfSlots(sceneIndex + 1);
        auto* slot = track->getClipSlotList().getClipSlots()[sceneIndex];

        auto clipRef = te::insertWaveClip(
            *slot, "take", tempFile.getFile(),
            {{te::TimePosition::fromSeconds(0.0), te::TimePosition::fromSeconds(2.0)}},
            te::DeleteExistingClips::no);
        expect(clipRef != nullptr);
        if (!clipRef)
            return;

        expect(wrapper.testFinalizeSessionSlotAudioRecording(trackId, *clipRef));

        const auto clipId = ClipManager::getInstance().getClipInSlot(trackId, sceneIndex);
        expect(clipId != INVALID_CLIP_ID, "Take is in the model");
        expect(tempFile.getFile().existsAsFile(), "The recorded file survives the slot swap");

        auto* teClip = bridge->getSessionTeClip(clipId);
        expect(teClip != nullptr, "Take maps to a TE slot clip");
        if (teClip != nullptr) {
            expect(teClip == slot->getClip(), "The slot holds the mapped clip, not the raw take");
            auto handle = teClip->getLaunchHandle();
            expect(handle != nullptr);
            if (handle != nullptr) {
                bridge->launchSessionClip(clipId, true);
                const auto queued = handle->getQueuedStatus();
                expect(handle->getPlayingStatus() == te::LaunchHandle::PlayState::playing ||
                           (queued && *queued == te::LaunchHandle::QueueState::playQueued),
                       "Launching the slot clip starts or queues it");
                bridge->stopSessionClip(clipId);
            }
        }

        UndoManager::getInstance().clearHistory();
        ClipManager::getInstance().clearAllClips();
        TrackManager::getInstance().clearAllTracks();
    }
};

static SessionSlotTakePlaybackTest sessionSlotTakePlaybackTest;
