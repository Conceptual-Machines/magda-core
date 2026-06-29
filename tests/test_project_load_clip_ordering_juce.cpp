#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>
#include <tracktion_engine/tracktion_engine.h>

#include "JuceTestStateGuard.hpp"
#include "SharedTestEngine.hpp"
#include "magda/daw/audio/AudioBridge.hpp"
#include "magda/daw/core/ClipInfo.hpp"
#include "magda/daw/core/ClipManager.hpp"
#include "magda/daw/core/TrackManager.hpp"
#include "magda/daw/engine/TracktionEngineWrapper.hpp"
#include "magda/daw/project/ProjectManager.hpp"
#include "magda/daw/project/serialization/ProjectSerializer.hpp"

using namespace magda;
namespace te = tracktion;

namespace {

juce::File testScratchDirectory() {
    auto envTmp = juce::SystemStats::getEnvironmentVariable("TMPDIR", {});
    auto root = envTmp.isNotEmpty() ? juce::File(envTmp)
                                    : juce::File::getSpecialLocation(juce::File::tempDirectory);
    root = root.getChildFile("magda_juce_tests");
    root.createDirectory();
    return root;
}

/** Generate a short mono sine WAV on disk for audio-clip tests. */
std::unique_ptr<juce::TemporaryFile> createSineWavFile(double sampleRate = 44100.0,
                                                       double durationSeconds = 3.0,
                                                       float frequency = 220.0f) {
    int numSamples = static_cast<int>(sampleRate * durationSeconds);
    juce::AudioBuffer<float> buffer(1, numSamples);
    float phase = 0.0f;
    float phaseInc =
        static_cast<float>(frequency * juce::MathConstants<double>::twoPi / sampleRate);
    for (int i = 0; i < numSamples; ++i) {
        buffer.setSample(0, i, std::sin(phase));
        phase += phaseInc;
    }

    auto targetFile = testScratchDirectory().getNonexistentChildFile("load_order_sine", ".wav");
    auto f = std::make_unique<juce::TemporaryFile>(targetFile);
    juce::WavAudioFormat wavFormat;
    JUCE_BEGIN_IGNORE_WARNINGS_MSVC(4996)
    JUCE_BEGIN_IGNORE_WARNINGS_GCC_LIKE("-Wdeprecated-declarations")
    std::unique_ptr<juce::AudioFormatWriter> writer(wavFormat.createWriterFor(
        new juce::FileOutputStream(f->getFile()), sampleRate, 1, 16, {}, 0));
    JUCE_END_IGNORE_WARNINGS_GCC_LIKE
    JUCE_END_IGNORE_WARNINGS_MSVC
    if (writer)
        writer->writeFromAudioSampleBuffer(buffer, 0, numSamples);
    return f;
}

class ScopedTrackManagerEngine {
  public:
    explicit ScopedTrackManagerEngine(TracktionEngineWrapper& wrapper) {
        TrackManager::getInstance().setAudioEngine(&wrapper);
    }

    ~ScopedTrackManagerEngine() {
        TrackManager::getInstance().setAudioEngine(nullptr);
    }

    ScopedTrackManagerEngine(const ScopedTrackManagerEngine&) = delete;
    ScopedTrackManagerEngine& operator=(const ScopedTrackManagerEngine&) = delete;
};

}  // namespace

/**
 * @brief Regression tests for clip drop-out on project load (fix:
 *        ProjectSerializer::commitStagedData commits restored tracks before
 *        clips).
 *
 * The bug: on load, arrangement clips were synced to Tracktion Engine BEFORE
 * the TE AudioTracks existed (TE tracks are created lazily by
 * AudioBridge::syncAll when the track batch closes). ClipSynchronizer bailed
 * with "no TE track" and silently dropped each clip, leaving instruments and
 * audio tracks silent on playback even though the clip's data was present in
 * the MAGDA model. These tests drive the real save -> loadAndStage ->
 * commitStaged path and assert clips land on their TE tracks afterwards.
 */
class ProjectLoadClipOrderingTest final : public juce::UnitTest {
  public:
    ProjectLoadClipOrderingTest() : juce::UnitTest("Project Load Clip Ordering", "magda") {}

    void runTest() override {
        magda::test::runWithCleanJuceState([this] { testMidiClipSurvivesLoad(); });
        magda::test::runWithCleanJuceState([this] { testAudioClipSurvivesLoad(); });
        magda::test::runWithCleanJuceState([this] { testMultipleTracksAllClipsSurviveLoad(); });
    }

  private:
    // =========================================================================
    // Shared round-trip helper: serialize current managers to a temp .mgd,
    // simulate "close" (clear the TE edit), then reload through the real
    // loadAndStage -> commitStaged path with the engine wired so the full
    // track-create -> clip-sync chain runs.
    // =========================================================================
    struct RoundTrip {
        std::unique_ptr<juce::TemporaryFile> projectFile;

        bool save() {
            auto target =
                testScratchDirectory().getNonexistentChildFile("load_order_project", ".mgd");
            projectFile = std::make_unique<juce::TemporaryFile>(target);
            const auto& info = ProjectManager::getInstance().getCurrentProjectInfo();
            return ProjectSerializer::saveToFile(projectFile->getFile(), info);
        }

        bool reload() {
            if (!projectFile)
                return false;

            StagedProjectData staged;
            if (!ProjectSerializer::loadAndStage(projectFile->getFile(), staged))
                return false;
            ProjectSerializer::commitStaged(staged);
            return true;
        }
    };

    // Clear the model with the engine wired so the shared TE edit empties out,
    // reproducing a fresh "open project into empty session" before reload.
    static void simulateClose() {
        ClipManager::getInstance().clearAllClips();
        TrackManager::getInstance().clearAllTracks();
    }

    void testMidiClipSurvivesLoad() {
        beginTest("MIDI arrangement clip survives save/load and lands on its TE track");

        auto& wrapper = magda::test::getSharedEngine();
        magda::test::resetTransport(wrapper);
        auto* bridge = wrapper.getAudioBridge();
        expect(bridge != nullptr, "AudioBridge must exist");
        if (!bridge)
            return;

        auto& tm = TrackManager::getInstance();
        auto& cm = ClipManager::getInstance();
        ScopedTrackManagerEngine engineScope(wrapper);

        const auto trackId = tm.createTrack("MIDI Track");
        const auto clipId = cm.createMidiClip(trackId, 0.0, 4.0, ClipView::Arrangement);
        expect(clipId != INVALID_CLIP_ID, "MIDI clip should be created");

        constexpr int kNoteCount = 6;
        for (int i = 0; i < kNoteCount; ++i) {
            MidiNote n;
            n.noteNumber = 60 + i;
            n.velocity = 100;
            n.startBeat = static_cast<double>(i) * 0.5;
            n.lengthBeats = 0.25;
            cm.addMidiNote(clipId, n);
        }

        RoundTrip rt;
        expect(rt.save(), "Project should save");
        simulateClose();
        expect(rt.reload(), "Project should reload");

        // The clip id round-trips; locate it on the track to be robust.
        auto clipIds = cm.getClipsOnTrack(trackId, ClipView::Arrangement);
        expect(clipIds.size() == 1, "Track should have exactly one arrangement clip after load");
        if (clipIds.empty())
            return;
        const auto loadedClipId = clipIds.front();

        auto* teClip = bridge->getArrangementTeClip(loadedClipId);
        expect(teClip != nullptr,
               "REGRESSION: MIDI clip must exist in the TE engine after load (was dropped when "
               "clips synced before the TE track existed)");

        auto* midiClip = dynamic_cast<te::MidiClip*>(teClip);
        expect(midiClip != nullptr, "TE clip should be a MidiClip");
        if (midiClip != nullptr) {
            expectEquals(midiClip->getSequence().getNumNotes(), kNoteCount,
                         "All MIDI notes should be present in the TE clip after load");
        }

        auto* teTrack = bridge->getAudioTrack(trackId);
        expect(teTrack != nullptr, "TE track should exist after load");
        if (teTrack != nullptr)
            expect(!teTrack->getClips().isEmpty(), "TE track should hold the restored clip");
    }

    void testAudioClipSurvivesLoad() {
        beginTest("Audio arrangement clip survives save/load and lands on its TE track");

        auto& wrapper = magda::test::getSharedEngine();
        magda::test::resetTransport(wrapper);
        auto* bridge = wrapper.getAudioBridge();
        expect(bridge != nullptr, "AudioBridge must exist");
        if (!bridge)
            return;

        auto& tm = TrackManager::getInstance();
        auto& cm = ClipManager::getInstance();
        ScopedTrackManagerEngine engineScope(wrapper);

        auto wav = createSineWavFile();
        const auto trackId = tm.createTrack("Audio Track");
        const auto clipId = cm.createAudioClip(trackId, 0.0, 2.0, wav->getFile().getFullPathName(),
                                               ClipView::Arrangement, 120.0);
        expect(clipId != INVALID_CLIP_ID, "Audio clip should be created");

        RoundTrip rt;
        expect(rt.save(), "Project should save");
        simulateClose();
        expect(rt.reload(), "Project should reload");

        auto clipIds = cm.getClipsOnTrack(trackId, ClipView::Arrangement);
        expect(clipIds.size() == 1, "Track should have exactly one arrangement clip after load");
        if (clipIds.empty())
            return;
        const auto loadedClipId = clipIds.front();

        auto* teClip = bridge->getArrangementTeClip(loadedClipId);
        expect(teClip != nullptr, "REGRESSION: audio clip must exist in the TE engine after load");

        auto* waveClip = dynamic_cast<te::WaveAudioClip*>(teClip);
        expect(waveClip != nullptr, "TE clip should be a WaveAudioClip");
        if (waveClip != nullptr) {
            auto pos = waveClip->getPosition();
            expectWithinAbsoluteError(pos.getStart().inSeconds(), 0.0, 0.05);
            expect(pos.getEnd().inSeconds() > pos.getStart().inSeconds(),
                   "Audio clip should have a non-zero length after load");
        }

        auto* teTrack = bridge->getAudioTrack(trackId);
        expect(teTrack != nullptr, "TE track should exist after load");
        if (teTrack != nullptr)
            expect(!teTrack->getClips().isEmpty(), "TE track should hold the restored clip");
    }

    void testMultipleTracksAllClipsSurviveLoad() {
        beginTest("Clips on every track survive load (ordering holds beyond the first track)");

        auto& wrapper = magda::test::getSharedEngine();
        magda::test::resetTransport(wrapper);
        auto* bridge = wrapper.getAudioBridge();
        expect(bridge != nullptr, "AudioBridge must exist");
        if (!bridge)
            return;

        auto& tm = TrackManager::getInstance();
        auto& cm = ClipManager::getInstance();
        ScopedTrackManagerEngine engineScope(wrapper);

        constexpr int kTrackCount = 4;
        std::vector<TrackId> trackIds;
        for (int t = 0; t < kTrackCount; ++t) {
            const auto trackId = tm.createTrack("Track " + juce::String(t + 1));
            trackIds.push_back(trackId);
            const auto clipId = cm.createMidiClip(trackId, 0.0, 2.0, ClipView::Arrangement);
            MidiNote n;
            n.noteNumber = 48 + t;
            n.startBeat = 0.0;
            n.lengthBeats = 1.0;
            cm.addMidiNote(clipId, n);
        }

        RoundTrip rt;
        expect(rt.save(), "Project should save");
        simulateClose();
        expect(rt.reload(), "Project should reload");

        for (int t = 0; t < kTrackCount; ++t) {
            const auto trackId = trackIds[static_cast<size_t>(t)];
            auto clipIds = cm.getClipsOnTrack(trackId, ClipView::Arrangement);
            expect(clipIds.size() == 1,
                   "Track " + juce::String(t + 1) + " should have its clip after load");
            if (clipIds.empty())
                continue;
            auto* teClip = bridge->getArrangementTeClip(clipIds.front());
            expect(teClip != nullptr,
                   "Track " + juce::String(t + 1) + ": clip must exist in TE engine after load");
        }
    }
};

static ProjectLoadClipOrderingTest projectLoadClipOrderingTest;
