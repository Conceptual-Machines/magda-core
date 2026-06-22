#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <tracktion_engine/tracktion_engine.h>

#include "JuceTestStateGuard.hpp"
#include "SharedTestEngine.hpp"
#include "magda/daw/audio/AudioBridge.hpp"
#include "magda/daw/core/ClipManager.hpp"
#include "magda/daw/core/TrackManager.hpp"
#include "magda/daw/engine/TracktionEngineWrapper.hpp"

using namespace magda;
namespace te = tracktion;

namespace {

te::TimeRange beatRange(te::Edit& edit, double startBeat, double endBeat) {
    return {edit.tempoSequence.toTime(te::BeatPosition::fromBeats(startBeat)),
            edit.tempoSequence.toTime(te::BeatPosition::fromBeats(endBeat))};
}

juce::File testScratchDirectory() {
    auto envTmp = juce::SystemStats::getEnvironmentVariable("TMPDIR", {});
    auto root = envTmp.isNotEmpty() ? juce::File(envTmp)
                                    : juce::File::getSpecialLocation(juce::File::tempDirectory);
    root = root.getChildFile("magda_juce_tests");
    root.createDirectory();
    return root;
}

std::unique_ptr<juce::TemporaryFile> createSineWavFile(double sampleRate, double lengthBeats,
                                                       double bpm, float frequency = 220.0f) {
    const int numSamples = static_cast<int>(std::round(sampleRate * lengthBeats * 60.0 / bpm));
    juce::AudioBuffer<float> buffer(1, numSamples);
    float phase = 0.0f;
    const float phaseInc =
        static_cast<float>(frequency * juce::MathConstants<double>::twoPi / sampleRate);
    for (int i = 0; i < numSamples; ++i) {
        buffer.setSample(0, i, std::sin(phase));
        phase += phaseInc;
    }

    auto targetFile = testScratchDirectory().getNonexistentChildFile("session_slot_take", ".wav");
    auto file = std::make_unique<juce::TemporaryFile>(targetFile);
    juce::WavAudioFormat wavFormat;
    JUCE_BEGIN_IGNORE_WARNINGS_MSVC(4996)
    JUCE_BEGIN_IGNORE_WARNINGS_GCC_LIKE("-Wdeprecated-declarations")
    std::unique_ptr<juce::AudioFormatWriter> writer(wavFormat.createWriterFor(
        new juce::FileOutputStream(file->getFile()), sampleRate, 1, 16, {}, 0));
    JUCE_END_IGNORE_WARNINGS_GCC_LIKE
    JUCE_END_IGNORE_WARNINGS_MSVC
    if (writer)
        writer->writeFromAudioSampleBuffer(buffer, 0, numSamples);
    return file;
}

}  // namespace

class SessionSlotRecordingIntegrationTest final : public juce::UnitTest {
  public:
    SessionSlotRecordingIntegrationTest()
        : juce::UnitTest("Session Slot Recording Integration Tests", "magda") {}

    void runTest() override {
        magda::test::runWithCleanJuceState(
            [this] { testTeSlotRecordingFinalizesToMagdaSessionClip(); });
        magda::test::runWithCleanJuceState(
            [this] { testTeSlotAudioRecordingFinalizesToMagdaSessionClip(); });
        magda::test::runWithCleanJuceState(
            [this] { testSessionSlotPreviewClearedOnStopWithoutFinalize(); });
    }

    // Looks up the transient recording preview for a track, or nullptr.
    static const RecordingPreview* findPreview(TracktionEngineWrapper& wrapper, TrackId trackId) {
        const auto& previews = wrapper.getRecordingPreviews();
        auto it = previews.find(trackId);
        return it == previews.end() ? nullptr : &it->second;
    }

  private:
    struct Fixture {
        TracktionEngineWrapper& wrapper;
        AudioBridge* bridge = nullptr;
        te::Edit* edit = nullptr;
        TrackId trackId = INVALID_TRACK_ID;
        int sceneIndex = 0;

        Fixture() : wrapper(magda::test::getSharedEngine()) {
            magda::test::resetTransport(wrapper);

            bridge = wrapper.getAudioBridge();
            edit = wrapper.getEdit();

            ClipManager::getInstance().clearAllClips();
            TrackManager::getInstance().clearAllTracks();

            wrapper.setTempo(120.0);
            wrapper.setTimeSignature(4, 4);

            trackId = TrackManager::getInstance().createTrack("Session Record Target");
            TrackManager::getInstance().setTrackMidiInput(trackId, "all");
            TrackManager::getInstance().setTrackRecordArmed(trackId, true);

            if (bridge != nullptr) {
                bridge->createAudioTrack(trackId, "Session Record Target");
                bridge->setTrackMidiInput(trackId, "all");
                bridge->syncAllArmedTracksToTE();
            }
        }

        ~Fixture() {
            if (bridge != nullptr) {
                bridge->setSessionSlotMidiRecordingTarget(trackId, sceneIndex, false);
                bridge->setSessionSlotAudioRecordingTarget(trackId, sceneIndex, false);
            }

            wrapper.stop();
            ClipManager::getInstance().clearAllClips();
            TrackManager::getInstance().clearAllTracks();
        }

        te::ClipSlot* getSlot() const {
            if (bridge == nullptr)
                return nullptr;

            auto* track = bridge->getAudioTrack(trackId);
            if (track == nullptr)
                return nullptr;

            track->getClipSlotList().ensureNumberOfSlots(sceneIndex + 1);
            auto slots = track->getClipSlotList().getClipSlots();
            return sceneIndex < slots.size() ? slots[sceneIndex] : nullptr;
        }
    };

    void testTeSlotRecordingFinalizesToMagdaSessionClip() {
        beginTest("TE ClipSlot MIDI recording finalizes into a Magda session clip");

        Fixture fixture;
        expect(fixture.bridge != nullptr, "AudioBridge must exist");
        expect(fixture.edit != nullptr, "Tracktion edit must exist");
        if (fixture.bridge == nullptr || fixture.edit == nullptr)
            return;

        expectEquals(ClipManager::getInstance().getArrangementClips().size(),
                     static_cast<size_t>(0), "Test starts with no arrangement clips");
        expect(ClipManager::getInstance().getClipInSlot(fixture.trackId, fixture.sceneIndex) ==
                   INVALID_CLIP_ID,
               "Test starts with an empty session slot");

        fixture.wrapper.armSessionSlotRecording(fixture.trackId, fixture.sceneIndex);
        fixture.wrapper.testSetSessionSlotRecordingActive(fixture.trackId, fixture.sceneIndex);
        expect(fixture.wrapper.isSessionSlotRecording(fixture.trackId, fixture.sceneIndex),
               "Wrapper should mark the slot as actively recording");

        // A transient session-slot recording preview should now exist for the track.
        const auto* preview = findPreview(fixture.wrapper, fixture.trackId);
        expect(preview != nullptr, "Active session-slot recording should create a preview");
        if (preview != nullptr) {
            expect(preview->target == RecordingTargetKind::SessionSlot,
                   "Preview must be tagged as a session-slot target");
            expectEquals(preview->sceneIndex, fixture.sceneIndex,
                         "Preview must carry the target scene index");
            expect(!preview->isAudioRecording, "MIDI-input slot preview is not an audio recording");
        }

        auto* slot = fixture.getSlot();
        expect(slot != nullptr, "TE ClipSlot must exist");
        if (slot == nullptr)
            return;

        auto clipRef = te::insertMIDIClip(*slot, beatRange(*fixture.edit, 0.0, 1.5));
        expect(clipRef != nullptr, "TE should create a MIDI clip directly in the slot");
        if (!clipRef)
            return;

        auto& sequence = clipRef->getSequence();
        sequence.addNote(60, te::BeatPosition::fromBeats(0.5), te::BeatDuration::fromBeats(0.5),
                         100, 0, nullptr);
        sequence.addNote(64, te::BeatPosition::fromBeats(1.25), te::BeatDuration::fromBeats(0.25),
                         96, 0, nullptr);

        expect(fixture.wrapper.testFinalizeSessionSlotMidiRecording(fixture.trackId, *clipRef),
               "Recorded TE slot clip should finalize through the session recording path");

        const auto sessionClipId =
            ClipManager::getInstance().getClipInSlot(fixture.trackId, fixture.sceneIndex);
        expect(sessionClipId != INVALID_CLIP_ID,
               "Recorded TE slot clip should be mirrored into ClipManager");
        expect(!fixture.wrapper.isSessionSlotRecording(fixture.trackId, fixture.sceneIndex),
               "Slot recording state should clear after finalization");
        expect(findPreview(fixture.wrapper, fixture.trackId) == nullptr,
               "Recording preview must be cleared once the slot recording finalizes");
        expectEquals(ClipManager::getInstance().getArrangementClips().size(),
                     static_cast<size_t>(0),
                     "Session slot recording must not create an arrangement clip");

        const auto* clip = ClipManager::getInstance().getClip(sessionClipId);
        expect(clip != nullptr, "Mirrored Magda clip must exist");
        if (clip == nullptr)
            return;

        expect(clip->view == ClipView::Session, "Mirrored clip must be a session clip");
        expectEquals(clip->trackId, fixture.trackId);
        expectEquals(clip->sceneIndex, fixture.sceneIndex);
        expect(clip->isMidi(), "Mirrored clip must be MIDI");
        expect(clip->loopEnabled, "Recorded session clip should loop");
        expectWithinAbsoluteError(clip->placement.lengthBeats, 4.0, 0.0001,
                                  "Clip length should snap to a full 4/4 bar");
        expectWithinAbsoluteError(clip->loopLengthBeats, 4.0, 0.0001,
                                  "Loop length should match snapped clip length");

        expectEquals(clip->midiNotes.size(), static_cast<size_t>(2),
                     "Recorded MIDI notes should be copied from the TE slot clip");
        if (clip->midiNotes.size() >= 2) {
            expectEquals(clip->midiNotes[0].noteNumber, 60);
            expectWithinAbsoluteError(clip->midiNotes[0].startBeat, 0.5, 0.0001);
            expectWithinAbsoluteError(clip->midiNotes[0].lengthBeats, 0.5, 0.0001);
            expectEquals(clip->midiNotes[1].noteNumber, 64);
            expectWithinAbsoluteError(clip->midiNotes[1].startBeat, 1.25, 0.0001);
            expectWithinAbsoluteError(clip->midiNotes[1].lengthBeats, 0.25, 0.0001);
        }

        auto* teClip = slot->getClip();
        expect(teClip == clipRef.get(), "The TE slot clip should remain in the ClipSlot");
        if (auto* teMidiClip = dynamic_cast<te::MidiClip*>(teClip)) {
            expectWithinAbsoluteError(teMidiClip->getLengthInBeats().inBeats(), 4.0, 0.0001,
                                      "TE slot clip length should also be bar-snapped");
            expectWithinAbsoluteError(teMidiClip->getLoopLengthBeats().inBeats(), 4.0, 0.0001,
                                      "TE slot clip loop should match the snapped bar length");
        } else {
            expect(false, "TE slot clip should still be a MidiClip");
        }
    }

    void testTeSlotAudioRecordingFinalizesToMagdaSessionClip() {
        beginTest("TE ClipSlot audio recording finalizes into a Magda session clip");

        Fixture fixture;
        expect(fixture.bridge != nullptr, "AudioBridge must exist");
        expect(fixture.edit != nullptr, "Tracktion edit must exist");
        if (fixture.bridge == nullptr || fixture.edit == nullptr)
            return;

        const auto sourceTrackId = TrackManager::getInstance().createTrack("Session Source");
        fixture.bridge->createAudioTrack(sourceTrackId, "Session Source");
        const auto trackInput = "track:" + juce::String(sourceTrackId);
        TrackManager::getInstance().setTrackAudioInput(fixture.trackId, trackInput);
        fixture.bridge->setTrackAudioInput(fixture.trackId, trackInput);
        fixture.bridge->syncAllArmedTracksToTE();

        fixture.wrapper.armSessionSlotRecording(fixture.trackId, fixture.sceneIndex);
        fixture.wrapper.testSetSessionSlotRecordingActive(fixture.trackId, fixture.sceneIndex);
        expect(fixture.wrapper.isSessionSlotRecording(fixture.trackId, fixture.sceneIndex),
               "Wrapper should mark the audio slot as actively recording");

        const auto* preview = findPreview(fixture.wrapper, fixture.trackId);
        expect(preview != nullptr, "Active audio session-slot recording should create a preview");
        if (preview != nullptr) {
            expect(preview->target == RecordingTargetKind::SessionSlot,
                   "Preview must be tagged as a session-slot target");
            expect(preview->isAudioRecording,
                   "Audio-input slot preview must be an audio recording");
        }

        auto* slot = fixture.getSlot();
        expect(slot != nullptr, "TE ClipSlot must exist");
        if (slot == nullptr)
            return;

        auto audioFile = createSineWavFile(44100.0, 1.5, 120.0);
        expect(audioFile != nullptr && audioFile->getFile().existsAsFile(),
               "Test wave file should exist");
        if (audioFile == nullptr || !audioFile->getFile().existsAsFile())
            return;

        auto clipRef = te::insertWaveClip(*slot, "Session Audio Take", audioFile->getFile(),
                                          te::ClipPosition{beatRange(*fixture.edit, 0.0, 1.5)},
                                          te::DeleteExistingClips::no);
        expect(clipRef != nullptr, "TE should create a wave clip directly in the slot");
        if (!clipRef)
            return;

        expect(fixture.wrapper.testFinalizeSessionSlotAudioRecording(fixture.trackId, *clipRef),
               "Recorded TE audio slot clip should finalize through the session recording path");

        const auto sessionClipId =
            ClipManager::getInstance().getClipInSlot(fixture.trackId, fixture.sceneIndex);
        expect(sessionClipId != INVALID_CLIP_ID,
               "Recorded TE audio slot clip should be mirrored into ClipManager");
        expect(!fixture.wrapper.isSessionSlotRecording(fixture.trackId, fixture.sceneIndex),
               "Slot recording state should clear after finalization");
        expect(findPreview(fixture.wrapper, fixture.trackId) == nullptr,
               "Recording preview must be cleared once the audio slot recording finalizes");
        expectEquals(ClipManager::getInstance().getArrangementClips().size(),
                     static_cast<size_t>(0),
                     "Session slot recording must not create an arrangement clip");

        const auto* clip = ClipManager::getInstance().getClip(sessionClipId);
        expect(clip != nullptr, "Mirrored Magda clip must exist");
        if (clip == nullptr)
            return;

        expect(clip->view == ClipView::Session, "Mirrored clip must be a session clip");
        expectEquals(clip->trackId, fixture.trackId);
        expectEquals(clip->sceneIndex, fixture.sceneIndex);
        expect(clip->isAudio(), "Mirrored clip must be audio");
        expect(clip->loopEnabled, "Recorded session clip should loop");
        expect(clip->autoTempo, "Recorded session audio should follow the project tempo");
        expectEquals(clip->audio().source.filePath, audioFile->getFile().getFullPathName());
        expectWithinAbsoluteError(clip->placement.lengthBeats, 4.0, 0.0001,
                                  "Clip length should snap to a full 4/4 bar");
        expectWithinAbsoluteError(clip->loopLengthBeats, 4.0, 0.0001,
                                  "Loop length should match snapped clip length");
        expectWithinAbsoluteError(clip->audio().interpretation.bpm, 120.0, 0.0001,
                                  "Recorded source BPM should match the project");
        expectWithinAbsoluteError(clip->audio().interpretation.totalBeats, 4.0, 0.0001,
                                  "Recorded source beats should match the snapped bar length");

        auto* teClip = slot->getClip();
        expect(teClip == clipRef.get(), "The TE slot clip should remain in the ClipSlot");
        if (auto* teAudioClip = dynamic_cast<te::WaveAudioClip*>(teClip)) {
            expect(teAudioClip->getAutoTempo(), "TE slot audio clip should use auto tempo");
            expectWithinAbsoluteError(teAudioClip->getLengthBeats().inBeats(), 4.0, 0.0001,
                                      "TE slot audio clip length should also be bar-snapped");
            expectWithinAbsoluteError(
                teAudioClip->getLoopLengthBeats().inBeats(), 4.0, 0.0001,
                "TE slot audio clip loop should match the snapped bar length");
        } else {
            expect(false, "TE slot clip should still be a WaveAudioClip");
        }
    }

    void testSessionSlotPreviewClearedOnStopWithoutFinalize() {
        beginTest("Session-slot recording preview is cleared when recording stops uncommitted");

        Fixture fixture;
        expect(fixture.bridge != nullptr, "AudioBridge must exist");
        if (fixture.bridge == nullptr)
            return;

        fixture.wrapper.armSessionSlotRecording(fixture.trackId, fixture.sceneIndex);
        fixture.wrapper.testSetSessionSlotRecordingActive(fixture.trackId, fixture.sceneIndex);
        expect(findPreview(fixture.wrapper, fixture.trackId) != nullptr,
               "Active session-slot recording should create a preview");

        // Stopping without a TE clip to finalize (e.g. no input captured) must
        // still tear the transient preview down rather than leak it.
        fixture.wrapper.testFinishSessionSlotRecordings();

        expect(findPreview(fixture.wrapper, fixture.trackId) == nullptr,
               "Recording preview must be cleared when the slot recording stops uncommitted");
        expect(!fixture.wrapper.isSessionSlotRecording(fixture.trackId, fixture.sceneIndex),
               "Slot recording state should clear when stopped uncommitted");
    }
};

static SessionSlotRecordingIntegrationTest sessionSlotRecordingIntegrationTest;
