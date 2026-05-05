#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <tracktion_engine/tracktion_engine.h>

#include <unordered_set>

// TE internal test utilities (not exposed via module public headers)
#include "SharedTestEngine.hpp"
#include "magda/daw/audio/AudioThumbnailManager.hpp"
#include "magda/daw/audio/TrackController.hpp"
#include "magda/daw/audio/WarpMarkerManager.hpp"
#include "magda/daw/audio/session/ClipSynchronizer.hpp"
#include "magda/daw/core/ClipInfo.hpp"
#include "magda/daw/core/ClipManager.hpp"
#include "magda/daw/core/ClipOperations.hpp"
#include "magda/daw/project/ProjectManager.hpp"
#include "third_party/tracktion_engine/modules/tracktion_engine/utilities/tracktion_TestUtilities.h"

using namespace magda;
namespace te = tracktion;

namespace {

/** Generate a mono sine WAV file and return it as a TemporaryFile. */
juce::File testScratchDirectory() {
    auto envTmp = juce::SystemStats::getEnvironmentVariable("TMPDIR", {});
    auto root = envTmp.isNotEmpty() ? juce::File(envTmp)
                                    : juce::File::getSpecialLocation(juce::File::tempDirectory);
    root = root.getChildFile("magda_juce_tests");
    root.createDirectory();
    return root;
}

std::unique_ptr<juce::TemporaryFile> createSineWavFile(double sampleRate, double durationSeconds,
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

    auto targetFile = testScratchDirectory().getNonexistentChildFile("clip_sync_sine", ".wav");
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

}  // namespace

/**
 * @brief Integration tests for ClipSynchronizer audio clip sync
 *
 * Tests the critical path: ClipManager model → ClipSynchronizer.syncClipToEngine() → TE clip
 * properties. This is where most audio playback bugs originate (wrong offset, loop range,
 * speed ratio, etc.) and previously had zero test coverage.
 */
class ClipSyncIntegrationTest final : public juce::UnitTest {
  public:
    ClipSyncIntegrationTest() : juce::UnitTest("ClipSynchronizer Integration Tests", "magda") {}

    void runTest() override {
        testCreateAndSyncAudioClip();
        testSessionImportUsesCachedDetectorFallback();
        testSessionSyncPreservesDetectedSourceInterpretation();
        testMoveClip();
        testResizeFromRight();
        testResizeFromLeft();
        testTrimAudioFromLeft();
        testTrimAudioFromRight();
        testSpeedRatio();
        testLoopEnableDisable();
        testBeatModeRoundTripPreservesTrimmedLength();
        testLoopTimeBased();
        testLoopTimeBasedWarpEnabled();
        testSplitAudioClip();
        testBeatModeSplitRendersRightSide();
        testBeatModeSplitWrapsLoopPhaseAtBoundary();
        testBeatModeDuplicateRendersCopy();
        testBeatModeClipboardPastePreservesRightSplitPhase();
        testBeatModeTimeRangePastePreservesTrimmedLoopPhase();
        testFadeInOut();
        testLaunchFadeSamples();
        testGainAndPan();
        testPitchChange();
        testRenderVerification();
        testCreateAudioClipDeletesFullyOverlappedNeighbour();
        testMoveAudioClipTrimsNeighbourFromRight();
        testMoveAudioClipTrimsNeighbourFromLeft();
        testDuplicateAudioClipResolvesOverlap();
        testPasteAudioClipResolvesOverlap();
        testCreateMidiClipResolvesOverlap();
        testMoveClipNoOverlapDoesNotMutateNeighbours();
        testMoveClipToTrackResolvesOverlapOnDestination();
    }

  private:
    // =========================================================================
    // Fixture: creates a fresh TE Edit, TrackController, ClipSynchronizer
    // per test and generates a 5s sine WAV.
    // =========================================================================
    struct Fixture {
        std::unique_ptr<te::Edit> edit;
        std::unique_ptr<TrackController> trackController;
        std::unique_ptr<WarpMarkerManager> warpMarkerManager;
        std::unique_ptr<ClipSynchronizer> clipSync;
        std::unique_ptr<juce::TemporaryFile> sinFile;
        TrackId trackId = 1;
        double originalProjectTempo = 120.0;

        Fixture() {
            // Reset ClipManager singleton
            ClipManager::getInstance().clearAllClips();
            originalProjectTempo = ProjectManager::getInstance().getCurrentProjectInfo().tempo;
            ProjectManager::getInstance().setTempo(60.0);

            auto& engineWrapper = magda::test::getSharedEngine();
            auto* engine = engineWrapper.getEngine();
            jassert(engine != nullptr);

            // Create fresh edit: 60 BPM, 1 audio track
            edit = te::test_utilities::createTestEdit(*engine, 1);
            jassert(edit != nullptr);

            // Create TrackController and map MAGDA trackId=1 to the first TE AudioTrack
            trackController = std::make_unique<TrackController>(*engine, *edit);
            trackController->ensureTrackMapping(trackId, "Test Track");

            warpMarkerManager = std::make_unique<WarpMarkerManager>();
            clipSync =
                std::make_unique<ClipSynchronizer>(*edit, *trackController, *warpMarkerManager);

            // Generate 5 second sine WAV at 44100 Hz
            sinFile = createSineWavFile(44100.0, 5.0);
        }

        ~Fixture() {
            // Destroy ClipSynchronizer first (unregisters listener)
            clipSync.reset();
            warpMarkerManager.reset();
            trackController.reset();
            edit.reset();
            ClipManager::getInstance().clearAllClips();
            ProjectManager::getInstance().setTempo(originalProjectTempo);
        }

        juce::String audioPath() const {
            return sinFile->getFile().getFullPathName();
        }

        te::WaveAudioClip* getTeAudioClip(ClipId clipId) const {
            auto* teClip = clipSync->getArrangementTeClip(clipId);
            return dynamic_cast<te::WaveAudioClip*>(teClip);
        }
    };

    static void configureBeatModeLoop(ClipInfo& clip, double projectBpm, double sourceBpm,
                                      double sourceDurationSeconds, double loopStartBeats,
                                      double loopLengthBeats, double placementLengthBeats) {
        clip.autoTempo = true;
        clip.loopEnabled = true;
        clip.audio().interpretation.bpm = sourceBpm;
        clip.audio().interpretation.totalBeats = sourceDurationSeconds * sourceBpm / 60.0;
        clip.loopStartBeats = loopStartBeats;
        clip.loopLengthBeats = loopLengthBeats;
        clip.offsetBeats = loopStartBeats;
        clip.loopStart = loopStartBeats * 60.0 / sourceBpm;
        clip.loopLength = loopLengthBeats * 60.0 / sourceBpm;
        clip.offset = clip.offsetBeats * 60.0 / sourceBpm;
        clip.setPlacementBeats(0.0, placementLengthBeats);
        clip.deriveTimesFromBeats(projectBpm);
    }

    bool expectAudioInRange(const juce::AudioBuffer<float>& buf, double sampleRate,
                            double startSeconds, double durationSeconds,
                            const juce::String& label) {
        const int startSample = static_cast<int>(startSeconds * sampleRate);
        const int numSamples = static_cast<int>(durationSeconds * sampleRate);
        const bool rangeAvailable =
            startSample >= 0 && numSamples > 0 && startSample + numSamples <= buf.getNumSamples();
        expect(rangeAvailable, "Buffer should include " + label);
        if (!rangeAvailable)
            return false;

        const float rms = buf.getRMSLevel(0, startSample, numSamples);
        expect(rms > 0.01f, label + " should render audio, RMS=" + juce::String(rms));
        return rms > 0.01f;
    }

    // =========================================================================
    // Test Cases
    // =========================================================================

    void testCreateAndSyncAudioClip() {
        beginTest("Create and sync audio clip");

        Fixture f;
        auto clipId = ClipManager::getInstance().createAudioClip(f.trackId, 0.0, 2.0, f.audioPath(),
                                                                 ClipView::Arrangement, 60.0);
        expect(clipId != INVALID_CLIP_ID, "Clip creation should succeed");

        f.clipSync->syncClipToEngine(clipId);

        auto* teClip = f.getTeAudioClip(clipId);
        expect(teClip != nullptr, "TE clip should exist after sync");

        auto pos = teClip->getPosition();
        expectWithinAbsoluteError(pos.getStart().inSeconds(), 0.0, 0.01);
        expectWithinAbsoluteError(pos.getEnd().inSeconds(), 2.0, 0.01);

        // Source file should match
        auto sourceFile = teClip->getCurrentSourceFile();
        expect(sourceFile == f.sinFile->getFile(), "Source file should match");
    }

    void testSessionImportUsesCachedDetectorFallback() {
        beginTest("Session import uses cached detector fallback for defaulted source metadata");

        Fixture f;
        auto& thumbs = AudioThumbnailManager::getInstance();
        thumbs.clearCache();

        constexpr double detectedBpm = 172.0;
        const double sourceDuration = 5.0;
        const double expectedSourceBeats = sourceDuration * detectedBpm / 60.0;
        thumbs.cacheBPM(f.audioPath(), detectedBpm);

        auto clipId = ClipManager::getInstance().createAudioClip(
            f.trackId, 0.0, sourceDuration, f.audioPath(), ClipView::Session, 120.0);
        auto* clip = ClipManager::getInstance().getClip(clipId);
        expect(clip != nullptr, "Session clip should exist");
        if (clip == nullptr)
            return;
        expect(clip->view == ClipView::Session, "Clip should be a session clip");
        expect(clip->autoTempo, "Session audio clips should default to beat mode");
        expectWithinAbsoluteError(clip->audio().interpretation.bpm, detectedBpm, 0.01);
        expectWithinAbsoluteError(clip->audio().interpretation.totalBeats, expectedSourceBeats,
                                  0.01);
        expectWithinAbsoluteError(clip->loopLengthBeats, expectedSourceBeats, 0.01);

        thumbs.clearCache();
    }

    void testSessionSyncPreservesDetectedSourceInterpretation() {
        beginTest("Session sync preserves detector metadata when TE loopInfo is project-default");

        Fixture f;
        auto& thumbs = AudioThumbnailManager::getInstance();
        thumbs.clearCache();

        constexpr double detectedBpm = 172.0;
        const double sourceDuration = 5.0;
        const double expectedSourceBeats = sourceDuration * detectedBpm / 60.0;
        thumbs.cacheBPM(f.audioPath(), detectedBpm);

        auto clipId = ClipManager::getInstance().createAudioClip(
            f.trackId, 0.0, sourceDuration, f.audioPath(), ClipView::Session, 120.0);
        ClipManager::getInstance().setClipSceneIndex(clipId, 0);
        if (f.clipSync->getSessionTeClip(clipId) == nullptr)
            f.clipSync->syncSessionClipToSlot(clipId);

        auto* clip = ClipManager::getInstance().getClip(clipId);
        expect(clip != nullptr, "Session clip should still exist after sync");
        if (clip == nullptr)
            return;
        expectWithinAbsoluteError(clip->audio().interpretation.bpm, detectedBpm, 0.01);
        expectWithinAbsoluteError(clip->audio().interpretation.totalBeats, expectedSourceBeats,
                                  0.01);
        expectWithinAbsoluteError(clip->loopLengthBeats, expectedSourceBeats, 0.01);

        auto* teClip = dynamic_cast<te::WaveAudioClip*>(f.clipSync->getSessionTeClip(clipId));
        expect(teClip != nullptr, "Tracktion session clip should exist");
        if (teClip != nullptr) {
            auto waveInfo = teClip->getWaveInfo();
            auto& loopInfo = teClip->getLoopInfo();
            expectWithinAbsoluteError(loopInfo.getBpm(waveInfo), detectedBpm, 0.01);
            expectWithinAbsoluteError(loopInfo.getNumBeats(), expectedSourceBeats, 0.01);
        }

        thumbs.clearCache();
    }

    void testMoveClip() {
        beginTest("Move clip changes TE position, offset unchanged");

        Fixture f;
        auto clipId = ClipManager::getInstance().createAudioClip(f.trackId, 0.0, 2.0, f.audioPath(),
                                                                 ClipView::Arrangement, 60.0);
        f.clipSync->syncClipToEngine(clipId);

        auto* teClip = f.getTeAudioClip(clipId);
        expect(teClip != nullptr);
        double offsetBefore = teClip->getPosition().getOffset().inSeconds();

        // Move clip to t=2.0
        auto* clip = ClipManager::getInstance().getClip(clipId);
        expect(clip != nullptr);
        ClipOperations::moveContainer(*clip, 2.0);
        f.clipSync->syncClipToEngine(clipId);

        auto pos = teClip->getPosition();
        expectWithinAbsoluteError(pos.getStart().inSeconds(), 2.0, 0.01);
        expectWithinAbsoluteError(pos.getEnd().inSeconds(), 4.0, 0.01);

        // Offset should not change on move
        expectWithinAbsoluteError(pos.getOffset().inSeconds(), offsetBefore, 0.01);
    }

    void testResizeFromRight() {
        beginTest("Resize from right changes end, preserves start and offset");

        Fixture f;
        auto clipId = ClipManager::getInstance().createAudioClip(f.trackId, 0.0, 2.0, f.audioPath(),
                                                                 ClipView::Arrangement, 60.0);
        f.clipSync->syncClipToEngine(clipId);

        auto* teClip = f.getTeAudioClip(clipId);
        expect(teClip != nullptr);
        double startBefore = teClip->getPosition().getStart().inSeconds();
        double offsetBefore = teClip->getPosition().getOffset().inSeconds();

        // Resize to 4.0s
        auto* clip = ClipManager::getInstance().getClip(clipId);
        ClipOperations::resizeContainerFromRight(*clip, 4.0);
        f.clipSync->syncClipToEngine(clipId);

        auto pos = teClip->getPosition();
        expectWithinAbsoluteError(pos.getStart().inSeconds(), startBefore, 0.01);
        expectWithinAbsoluteError(pos.getEnd().inSeconds(), 4.0, 0.01);
        expectWithinAbsoluteError(pos.getOffset().inSeconds(), offsetBefore, 0.01);
    }

    void testResizeFromLeft() {
        beginTest("Resize from left adjusts start and offset, preserves audio alignment");

        Fixture f;
        auto clipId = ClipManager::getInstance().createAudioClip(f.trackId, 1.0, 3.0, f.audioPath(),
                                                                 ClipView::Arrangement, 60.0);
        f.clipSync->syncClipToEngine(clipId);

        auto* teClip = f.getTeAudioClip(clipId);
        expect(teClip != nullptr);

        // Resize from left: new length = 2.0 (start moves from 1.0 to 2.0)
        auto* clip = ClipManager::getInstance().getClip(clipId);
        ClipOperations::resizeContainerFromLeft(*clip, 2.0);
        f.clipSync->syncClipToEngine(clipId);

        auto pos = teClip->getPosition();
        expectWithinAbsoluteError(pos.getStart().inSeconds(), 2.0, 0.01);
        expectWithinAbsoluteError(pos.getEnd().inSeconds(), 4.0, 0.01);

        // Offset should have increased by 1.0 * speedRatio (1.0) = 1.0
        expectWithinAbsoluteError(pos.getOffset().inSeconds(), clip->getTeOffset(clip->loopEnabled),
                                  0.01);
    }

    void testTrimAudioFromLeft() {
        beginTest("Trim audio from left updates offset and start position");

        Fixture f;
        auto clipId = ClipManager::getInstance().createAudioClip(f.trackId, 0.0, 4.0, f.audioPath(),
                                                                 ClipView::Arrangement, 60.0);
        f.clipSync->syncClipToEngine(clipId);

        auto* clip = ClipManager::getInstance().getClip(clipId);
        double originalOffset = clip->offset;

        // Trim 1.0s from left
        ClipOperations::trimAudioFromLeft(*clip, 1.0);
        f.clipSync->syncClipToEngine(clipId);

        auto* teClip = f.getTeAudioClip(clipId);
        expect(teClip != nullptr);

        // Offset should have increased
        expect(clip->offset > originalOffset, "Offset should increase after left trim");

        // TE offset should match model's getTeOffset
        auto pos = teClip->getPosition();
        expectWithinAbsoluteError(pos.getOffset().inSeconds(), clip->getTeOffset(clip->loopEnabled),
                                  0.01);

        // Start should have moved right by ~1.0
        expectWithinAbsoluteError(pos.getStart().inSeconds(), 1.0, 0.01);

        // Length should have decreased
        expectWithinAbsoluteError(pos.getEnd().inSeconds() - pos.getStart().inSeconds(), 3.0, 0.01);
    }

    void testTrimAudioFromRight() {
        beginTest("Trim audio from right changes end, offset unchanged");

        Fixture f;
        auto clipId = ClipManager::getInstance().createAudioClip(f.trackId, 0.0, 4.0, f.audioPath(),
                                                                 ClipView::Arrangement, 60.0);
        f.clipSync->syncClipToEngine(clipId);

        auto* teClip = f.getTeAudioClip(clipId);
        expect(teClip != nullptr);
        double offsetBefore = teClip->getPosition().getOffset().inSeconds();

        // Trim 1.0s from right
        auto* clip = ClipManager::getInstance().getClip(clipId);
        ClipOperations::trimAudioFromRight(*clip, 1.0);
        f.clipSync->syncClipToEngine(clipId);

        auto pos = teClip->getPosition();
        expectWithinAbsoluteError(pos.getStart().inSeconds(), 0.0, 0.01);
        expectWithinAbsoluteError(pos.getEnd().inSeconds(), 3.0, 0.01);
        expectWithinAbsoluteError(pos.getOffset().inSeconds(), offsetBefore, 0.01);
    }

    void testSpeedRatio() {
        beginTest("Speed ratio: TE follows model in time-based, model pins 1.0 in autoTempo");

        // The actual invariant lives in MAGDA's model:
        //  - In time-based mode, ClipSynchronizer writes the model's speedRatio
        //    to TE; getSpeedRatio() reflects it.
        //  - In autoTempo, MAGDA's clip->speedRatio is pinned to 1.0 (TE's
        //    autoTempo path requires it). TE's stored speedRatio may keep its
        //    pre-autoTempo value because AudioClipBase::setSpeedRatio is a
        //    no-op when autoTempo is on; TE doesn't use it for playback in
        //    that mode anyway (source-vs-project tempo governs the stretch).
        //  - When autoTempo flips back off, TE follows the model again.
        Fixture f;
        auto& cm = ClipManager::getInstance();

        auto clipId =
            cm.createAudioClip(f.trackId, 0.0, 2.0, f.audioPath(), ClipView::Arrangement, 60.0);
        f.clipSync->syncClipToEngine(clipId);

        auto* teClip = f.getTeAudioClip(clipId);
        expect(teClip != nullptr);
        if (teClip == nullptr)
            return;

        // 1) Time-based clip with speedRatio=2.0 → TE matches the model.
        cm.setSpeedRatio(clipId, 2.0);
        f.clipSync->syncClipToEngine(clipId);
        expectWithinAbsoluteError(teClip->getSpeedRatio(), 2.0, 0.01);

        // 2) Flip on autoTempo via the proper API. ClipOperations::setAutoTempo
        //    pins clip->speedRatio to 1.0 (TE's autoTempo path requires it).
        //    TE's getSpeedRatio() is undefined in this mode — TE silently
        //    rejects setSpeedRatio when autoTempo is on — so we don't assert
        //    TE's stored value here.
        {
            auto* clip = cm.getClip(clipId);
            if (clip == nullptr)
                return;
            clip->audio().interpretation.bpm = 60.0;
            clip->audio().interpretation.totalBeats = 2.0;
        }
        cm.setAutoTempo(clipId, true, 60.0);
        f.clipSync->syncClipToEngine(clipId);
        {
            const auto* clip = cm.getClip(clipId);
            if (clip == nullptr)
                return;
            expect(teClip->getAutoTempo(), "TE clip should be in autoTempo mode");
            expectWithinAbsoluteError(clip->speedRatio, 1.0, 0.01);
        }

        // 3) Disable autoTempo, then change model speedRatio to 0.5 → TE
        //    follows the model again.
        cm.setAutoTempo(clipId, false, 60.0);
        cm.setSpeedRatio(clipId, 0.5);
        f.clipSync->syncClipToEngine(clipId);
        expectWithinAbsoluteError(teClip->getSpeedRatio(), 0.5, 0.01);
    }

    void testLoopEnableDisable() {
        beginTest("Loop enable/disable syncs to TE");

        Fixture f;
        auto clipId = ClipManager::getInstance().createAudioClip(f.trackId, 0.0, 4.0, f.audioPath(),
                                                                 ClipView::Arrangement, 60.0);
        f.clipSync->syncClipToEngine(clipId);

        auto* teClip = f.getTeAudioClip(clipId);
        expect(teClip != nullptr);

        // Auto-tempo + beat-domain loop region (clips are beat-authoritative).
        auto* clip = ClipManager::getInstance().getClip(clipId);
        clip->autoTempo = true;
        clip->audio().interpretation.bpm = 60.0;
        clip->audio().interpretation.totalBeats = 5.0;  // 5s sine WAV at 60 BPM
        clip->loopEnabled = true;
        clip->loopStartBeats = 0.0;
        clip->loopLengthBeats = 2.0;
        clip->loopStart = clip->loopStartBeats * 60.0 / clip->audio().interpretation.bpm;
        clip->loopLength = clip->loopLengthBeats * 60.0 / clip->audio().interpretation.bpm;
        f.clipSync->syncClipToEngine(clipId);

        expect(teClip->isLooping(), "TE clip should be looping");

        auto loopRange = teClip->getLoopRangeBeats();
        expectWithinAbsoluteError(loopRange.getStart().inBeats(), 0.0, 0.01);
        expectWithinAbsoluteError(loopRange.getLength().inBeats(), 2.0, 0.01);

        clip->loopEnabled = false;
        f.clipSync->syncClipToEngine(clipId);

        expect(!teClip->isLooping(), "TE clip should not be looping after disable");
    }

    void testBeatModeRoundTripPreservesTrimmedLength() {
        beginTest("Beat-mode round trip preserves trimmed clip length");

        Fixture f;
        constexpr double projectBpm = 120.0;
        constexpr double sourceBpm = 172.0;
        constexpr double sourceBeats = 16.0;
        const double sourceDuration = sourceBeats * 60.0 / sourceBpm;
        constexpr double trimmedBeats = 0.5;

        auto trimmedId = ClipManager::getInstance().createAudioClip(
            f.trackId, 0.0, sourceDuration, f.audioPath(), ClipView::Arrangement, projectBpm);
        auto* trimmed = ClipManager::getInstance().getClip(trimmedId);
        expect(trimmed != nullptr, "Trimmed clip should exist");
        if (trimmed == nullptr)
            return;

        configureBeatModeLoop(*trimmed, projectBpm, sourceBpm, sourceDuration, 0.0, sourceBeats,
                              trimmedBeats);

        ClipManager::getInstance().setAutoTempo(trimmedId, false, projectBpm);
        trimmed = ClipManager::getInstance().getClip(trimmedId);
        expect(trimmed != nullptr, "Trimmed clip should survive disabling beat mode");
        if (trimmed == nullptr)
            return;
        expect(!trimmed->autoTempo, "Trimmed clip should be time-based after disabling beat mode");
        expectWithinAbsoluteError(trimmed->placement.lengthBeats, trimmedBeats, 0.001);

        ClipManager::getInstance().setAutoTempo(trimmedId, true, projectBpm);
        trimmed = ClipManager::getInstance().getClip(trimmedId);
        expect(trimmed != nullptr, "Trimmed clip should survive re-enabling beat mode");
        if (trimmed == nullptr)
            return;
        expect(trimmed->autoTempo, "Trimmed clip should be beat-based after re-enabling beat mode");
        expectWithinAbsoluteError(trimmed->placement.lengthBeats, trimmedBeats, 0.001);
        expectWithinAbsoluteError(trimmed->length, trimmedBeats * 60.0 / projectBpm, 0.001);

        auto fullId = ClipManager::getInstance().createAudioClip(
            f.trackId, 4.0, sourceDuration, f.audioPath(), ClipView::Arrangement, projectBpm);
        auto* full = ClipManager::getInstance().getClip(fullId);
        expect(full != nullptr, "Full source clip should exist");
        if (full == nullptr)
            return;

        full->audio().interpretation.bpm = sourceBpm;
        full->audio().interpretation.totalBeats = sourceBeats;
        ClipManager::getInstance().setAutoTempo(fullId, true, projectBpm);
        full = ClipManager::getInstance().getClip(fullId);
        expect(full != nullptr, "Full source clip should survive enabling beat mode");
        if (full == nullptr)
            return;
        expectWithinAbsoluteError(full->placement.lengthBeats, sourceBeats, 0.001);
    }

    void testLoopTimeBased() {
        beginTest("Loop with container longer than region (non-integer multiple): partial second "
                  "cycle plays");

        // Reproduces the bug from the screenshot:
        //   120 BPM, clip = 3 bars, loop region = 2 bars.
        //   Expected: bars 1-2 play first loop cycle, bar 3 plays start of second cycle.
        //   Bug: bar 3 was silent — the partial second loop cycle didn't play.

        Fixture f;

        // 60 BPM edit (from createTestEdit) so 1 beat = 1s — keeps the
        // render-time RMS checks readable.
        // Scenario: 2-beat loop region inside a 3-beat clip container.
        // Should play [0-2s] (one full cycle) then [2-3s] (first 1s of next).

        auto clipId = ClipManager::getInstance().createAudioClip(f.trackId, 0.0, 2.0, f.audioPath(),
                                                                 ClipView::Arrangement, 60.0);
        expect(clipId != INVALID_CLIP_ID);

        auto* clip = ClipManager::getInstance().getClip(clipId);
        expect(clip != nullptr);

        // Auto-tempo + beat-domain loop region. Container extends to 3 beats
        // (1.5× the loop region) via setPlacementBeats; deriveTimesFromBeats
        // refreshes the seconds cache so the renderer agrees with TE.
        clip->autoTempo = true;
        clip->audio().interpretation.bpm = 60.0;
        clip->audio().interpretation.totalBeats = 5.0;
        clip->loopEnabled = true;
        clip->loopStartBeats = 0.0;
        clip->loopLengthBeats = 2.0;
        clip->loopStart = 0.0;
        clip->loopLength = 2.0;
        clip->setPlacementBeats(0.0, 3.0);
        clip->deriveTimesFromBeats(60.0);

        expect(clip->loopEnabled, "Model: loopEnabled should be true");
        expectWithinAbsoluteError(clip->loopLengthBeats, 2.0, 0.01);
        expectWithinAbsoluteError(clip->length, 3.0, 0.01);

        f.clipSync->syncClipToEngine(clipId);

        auto* teClip = f.getTeAudioClip(clipId);
        expect(teClip != nullptr, "TE clip should exist");

        // --- TE property checks ---
        expect(teClip->isLooping(), "TE clip should be looping");

        auto pos = teClip->getPosition();
        expectWithinAbsoluteError(pos.getStart().inSeconds(), 0.0, 0.01);
        expectWithinAbsoluteError(pos.getEnd().inSeconds(), 3.0, 0.01);

        // TE loop range in beats — at 60 BPM, 2 beats = 2 seconds.
        auto loopRange = teClip->getLoopRangeBeats();
        expectWithinAbsoluteError(loopRange.getLength().inBeats(), 2.0, 0.01);

        // --- Render: audio must be present throughout all 3s ---
        auto result = te::test_utilities::renderToAudioBuffer(*f.edit);
        expect(result.buffer.getNumSamples() > 0, "Rendered buffer should not be empty");

        double sr = result.sampleRate;
        auto& buf = result.buffer;

        auto renderedDuration = static_cast<double>(buf.getNumSamples()) / sr;
        expect(renderedDuration >= 2.9, "Rendered buffer too short for verification, duration=" +
                                            juce::String(renderedDuration, 3) + "s");

        // First loop cycle: [0s - 2s]
        {
            int startSample = static_cast<int>(0.1 * sr);
            int numSamples = static_cast<int>(1.8 * sr);
            expect(startSample + numSamples <= buf.getNumSamples(),
                   "Buffer too short for first loop cycle check");
            float rms = buf.getRMSLevel(0, startSample, numSamples);
            expect(rms > 0.01f,
                   "First loop cycle (0.1-1.9s) should have audio, RMS=" + juce::String(rms));
        }

        // Partial second loop cycle: [2s - 3s] — THIS IS THE BAR THAT GOES SILENT
        {
            int startSample = static_cast<int>(2.1 * sr);
            int numSamples = static_cast<int>(0.8 * sr);
            expect(startSample + numSamples <= buf.getNumSamples(),
                   "Buffer too short for second loop cycle check");
            float rms = buf.getRMSLevel(0, startSample, numSamples);
            expect(rms > 0.01f,
                   "Second loop cycle (2.1-2.9s) should have audio, RMS=" + juce::String(rms));
        }

        // Silence after clip end (3.1s+) — only check if buffer extends past clip
        {
            int startSample = static_cast<int>(3.1 * sr);
            int numSamples = buf.getNumSamples() - startSample;
            if (numSamples > 0) {
                float rms = buf.getRMSLevel(0, startSample, numSamples);
                expect(rms < 0.01f,
                       "Should be silence after clip (3.1s+), RMS=" + juce::String(rms));
            }
        }
    }

    void testLoopTimeBasedWarpEnabled() {
        beginTest("Time-based loop with warp enabled: partial second cycle should play");

        // Same scenario as testLoopTimeBased but with warpEnabled=true.
        // When warp is on, the sync path uses setLoopRangeBeats (beat-based) instead
        // of setLoopRange (time-based). getAutoTempoBeatRange() returns {0,0} when
        // autoTempo is false, which may break looping.

        Fixture f;

        // Create clip at 2s length, enable looping, then extend to 3s
        auto clipId = ClipManager::getInstance().createAudioClip(f.trackId, 0.0, 2.0, f.audioPath(),
                                                                 ClipView::Arrangement, 60.0);
        expect(clipId != INVALID_CLIP_ID);

        ClipManager::getInstance().setClipLoopEnabled(clipId, true, 60.0);

        auto* clip = ClipManager::getInstance().getClip(clipId);
        expect(clip != nullptr);
        ClipOperations::resizeContainerFromRight(*clip, 3.0);

        // Enable warp (this routes sync through the auto-tempo/warp code path)
        clip->warpEnabled = true;
        // Set a valid time-stretch mode (SoundTouch HQ = mode 4, but defaultMode works)
        clip->timeStretchMode = static_cast<int>(te::TimeStretcher::defaultMode);

        // Verify model state
        expect(clip->loopEnabled, "Model: loopEnabled should be true");
        expect(clip->warpEnabled, "Model: warpEnabled should be true");
        expect(!clip->autoTempo, "Model: autoTempo should be false");
        expectWithinAbsoluteError(clip->loopStart, 0.0, 0.01);
        expectWithinAbsoluteError(clip->loopLength, 2.0, 0.01);
        expectWithinAbsoluteError(clip->length, 3.0, 0.01);

        f.clipSync->syncClipToEngine(clipId);

        auto* teClip = f.getTeAudioClip(clipId);
        expect(teClip != nullptr, "TE clip should exist");

        // --- TE property checks ---
        expect(teClip->isLooping(), "TE clip should be looping with warp enabled");

        auto pos = teClip->getPosition();
        expectWithinAbsoluteError(pos.getStart().inSeconds(), 0.0, 0.01);
        expectWithinAbsoluteError(pos.getEnd().inSeconds(), 3.0, 0.01);

        // --- Render: audio must be present throughout all 3s ---
        auto result = te::test_utilities::renderToAudioBuffer(*f.edit);
        expect(result.buffer.getNumSamples() > 0, "Rendered buffer should not be empty");

        double sr = result.sampleRate;
        auto& buf = result.buffer;

        // First loop cycle: [0s - 2s]
        {
            int startSample = static_cast<int>(0.1 * sr);
            int numSamples = static_cast<int>(1.8 * sr);
            if (startSample + numSamples <= buf.getNumSamples()) {
                float rms = buf.getRMSLevel(0, startSample, numSamples);
                expect(rms > 0.01f,
                       "First loop cycle (0.1-1.9s) should have audio, RMS=" + juce::String(rms));
            }
        }

        // Partial second loop cycle: [2s - 3s]
        {
            int startSample = static_cast<int>(2.1 * sr);
            int numSamples = static_cast<int>(0.8 * sr);
            if (startSample + numSamples <= buf.getNumSamples()) {
                float rms = buf.getRMSLevel(0, startSample, numSamples);
                expect(rms > 0.01f,
                       "Partial second loop cycle (2.1-2.9s) should have audio with warp, RMS=" +
                           juce::String(rms));
            }
        }

        // Silence after clip end
        {
            int startSample = static_cast<int>(3.1 * sr);
            int numSamples = buf.getNumSamples() - startSample;
            if (numSamples > 0) {
                float rms = buf.getRMSLevel(0, startSample, numSamples);
                expect(rms < 0.01f,
                       "Should be silence after clip (3.1s+), RMS=" + juce::String(rms));
            }
        }
    }

    void testSplitAudioClip() {
        beginTest("Split audio clip creates two clips with correct TE properties");

        Fixture f;
        auto clipId = ClipManager::getInstance().createAudioClip(f.trackId, 0.0, 4.0, f.audioPath(),
                                                                 ClipView::Arrangement, 60.0);
        f.clipSync->syncClipToEngine(clipId);

        // Split at t=2.0
        auto rightClipId = ClipManager::getInstance().splitClip(clipId, 2.0, 60.0);
        expect(rightClipId != INVALID_CLIP_ID, "Split should return valid right clip ID");

        // Sync both clips
        f.clipSync->syncClipToEngine(clipId);
        f.clipSync->syncClipToEngine(rightClipId);

        // Left clip: 0-2s
        auto* leftTeClip = f.getTeAudioClip(clipId);
        expect(leftTeClip != nullptr, "Left TE clip should exist");
        auto leftPos = leftTeClip->getPosition();
        expectWithinAbsoluteError(leftPos.getStart().inSeconds(), 0.0, 0.01);
        expectWithinAbsoluteError(leftPos.getEnd().inSeconds(), 2.0, 0.01);

        // Right clip: 2-4s
        auto* rightTeClip = f.getTeAudioClip(rightClipId);
        expect(rightTeClip != nullptr, "Right TE clip should exist");
        auto rightPos = rightTeClip->getPosition();
        expectWithinAbsoluteError(rightPos.getStart().inSeconds(), 2.0, 0.01);
        expectWithinAbsoluteError(rightPos.getEnd().inSeconds(), 4.0, 0.01);

        // Right clip should have increased offset (by 2.0 * speedRatio)
        auto* rightClip = ClipManager::getInstance().getClip(rightClipId);
        expect(rightClip != nullptr);
        expectWithinAbsoluteError(rightPos.getOffset().inSeconds(),
                                  rightClip->getTeOffset(rightClip->loopEnabled), 0.01);
        expect(rightClip->offset > 0.0, "Right clip offset should be > 0 after split");
    }

    void testBeatModeSplitRendersRightSide() {
        beginTest("Beat-mode split renders right-side audio");

        Fixture f;

        auto clipId = ClipManager::getInstance().createAudioClip(f.trackId, 0.0, 5.0, f.audioPath(),
                                                                 ClipView::Arrangement, 60.0);
        auto* clip = ClipManager::getInstance().getClip(clipId);
        expect(clip != nullptr, "Source clip should exist");
        if (clip == nullptr)
            return;

        clip->autoTempo = true;
        clip->loopEnabled = true;
        clip->audio().interpretation.bpm = 172.0;
        clip->audio().interpretation.totalBeats = 5.0 * 172.0 / 60.0;
        clip->loopStartBeats = 0.0;
        clip->loopLengthBeats = clip->audio().interpretation.totalBeats;
        clip->loopStart = 0.0;
        clip->loopLength = 5.0;
        clip->setPlacementBeats(0.0, clip->audio().interpretation.totalBeats);
        clip->deriveTimesFromBeats(60.0);

        f.clipSync->syncClipToEngine(clipId);

        constexpr double splitTime = 8.0;
        auto rightClipId = ClipManager::getInstance().splitClip(clipId, splitTime, 60.0);
        expect(rightClipId != INVALID_CLIP_ID, "Split should create right clip");

        f.clipSync->syncClipToEngine(clipId);
        f.clipSync->syncClipToEngine(rightClipId);
        f.edit->restartPlayback();

        auto result = te::test_utilities::renderToAudioBuffer(*f.edit);
        expect(result.buffer.getNumSamples() > 0, "Rendered buffer should not be empty");

        auto& buf = result.buffer;
        const double sr = result.sampleRate;
        {
            const int startSample = static_cast<int>(0.2 * sr);
            const int numSamples = static_cast<int>(1.0 * sr);
            const float rms = buf.getRMSLevel(0, startSample, numSamples);
            expect(rms > 0.01f, "Left side should render audio, RMS=" + juce::String(rms));
        }
        {
            const int startSample = static_cast<int>((splitTime + 0.2) * sr);
            const int numSamples = static_cast<int>(1.0 * sr);
            expect(startSample + numSamples <= buf.getNumSamples(),
                   "Buffer should include right-side split region");
            if (startSample + numSamples <= buf.getNumSamples()) {
                const float rms = buf.getRMSLevel(0, startSample, numSamples);
                expect(rms > 0.01f, "Right side should render audio, RMS=" + juce::String(rms));
            }
        }
    }

    void testBeatModeSplitWrapsLoopPhaseAtBoundary() {
        beginTest("Beat-mode split at loop boundary wraps right-side phase");

        Fixture f;
        auto clipId = ClipManager::getInstance().createAudioClip(f.trackId, 0.0, 5.0, f.audioPath(),
                                                                 ClipView::Arrangement, 60.0);
        auto* clip = ClipManager::getInstance().getClip(clipId);
        expect(clip != nullptr, "Source clip should exist");
        if (clip == nullptr)
            return;

        configureBeatModeLoop(*clip, 60.0, 120.0, 5.0, 0.0, 4.0, 12.0);
        f.clipSync->syncClipToEngine(clipId);

        auto rightClipId = ClipManager::getInstance().splitClip(clipId, 4.0, 60.0);
        expect(rightClipId != INVALID_CLIP_ID, "Boundary split should create right clip");
        auto* rightClip = ClipManager::getInstance().getClip(rightClipId);
        expect(rightClip != nullptr, "Right clip should exist");
        if (rightClip == nullptr)
            return;

        expectWithinAbsoluteError(rightClip->loopStartBeats, 0.0, 0.01);
        expectWithinAbsoluteError(rightClip->loopLengthBeats, 4.0, 0.01);
        expectWithinAbsoluteError(rightClip->offsetBeats, 4.0, 0.01);

        f.clipSync->syncClipToEngine(clipId);
        f.clipSync->syncClipToEngine(rightClipId);
        f.edit->restartPlayback();

        auto* rightTeClip = f.getTeAudioClip(rightClipId);
        expect(rightTeClip != nullptr, "Right TE clip should exist");
        if (rightTeClip != nullptr) {
            auto rightLoop = rightTeClip->getLoopRangeBeats();
            expectWithinAbsoluteError(rightLoop.getStart().inBeats(), 0.0, 0.01);
            expectWithinAbsoluteError(rightLoop.getLength().inBeats(), 4.0, 0.01);
            expectWithinAbsoluteError(rightTeClip->getPosition().getOffset().inSeconds(), 4.0,
                                      0.01);
        }

        auto result = te::test_utilities::renderToAudioBuffer(*f.edit);
        expectAudioInRange(result.buffer, result.sampleRate, 4.2, 0.7,
                           "right side after boundary split");
    }

    void testBeatModeDuplicateRendersCopy() {
        beginTest("Beat-mode duplicate renders copied audio");

        Fixture f;
        auto& cm = ClipManager::getInstance();
        auto clipId =
            cm.createAudioClip(f.trackId, 0.0, 5.0, f.audioPath(), ClipView::Arrangement, 60.0);
        auto* clip = cm.getClip(clipId);
        expect(clip != nullptr, "Source clip should exist");
        if (clip == nullptr)
            return;

        configureBeatModeLoop(*clip, 60.0, 172.0, 5.0, 0.0, 5.0 * 172.0 / 60.0, 5.0 * 172.0 / 60.0);
        f.clipSync->syncClipToEngine(clipId);

        const double originalOffsetBeats = clip->offsetBeats;
        const double originalLoopStartBeats = clip->loopStartBeats;
        const double originalLoopLengthBeats = clip->loopLengthBeats;

        const double duplicateStart = clip->getEndTime();
        auto duplicateId = cm.duplicateClipAt(clipId, duplicateStart, f.trackId, 60.0);
        expect(duplicateId != INVALID_CLIP_ID, "Duplicate should create copied clip");
        auto* duplicate = cm.getClip(duplicateId);
        expect(duplicate != nullptr, "Duplicate model clip should exist");
        if (duplicate == nullptr)
            return;

        // Source-domain phase is preserved verbatim — duplicating must not
        // change which audio plays. Compare in the source domain only.
        expectWithinAbsoluteError(duplicate->offsetBeats, originalOffsetBeats, 0.01);
        expectWithinAbsoluteError(duplicate->loopStartBeats, originalLoopStartBeats, 0.01);
        expectWithinAbsoluteError(duplicate->loopLengthBeats, originalLoopLengthBeats, 0.01);
        // Loop phase modulo the loop length: another way to express "same audio plays".
        expectWithinAbsoluteError(
            wrapPhase(duplicate->offsetBeats - duplicate->loopStartBeats,
                      duplicate->loopLengthBeats),
            wrapPhase(originalOffsetBeats - originalLoopStartBeats, originalLoopLengthBeats), 0.01);

        // Timeline placement moved to duplicateStart (separate domain — independent of phase).
        expectWithinAbsoluteError(duplicate->placement.startBeat, duplicateStart, 0.01);

        f.clipSync->syncClipToEngine(clipId);
        f.clipSync->syncClipToEngine(duplicateId);
        f.edit->restartPlayback();

        auto result = te::test_utilities::renderToAudioBuffer(*f.edit);
        expectAudioInRange(result.buffer, result.sampleRate, duplicateStart + 0.2, 1.0,
                           "duplicated beat-mode clip");
    }

    void testBeatModeClipboardPastePreservesRightSplitPhase() {
        beginTest("Beat-mode clipboard paste preserves right split phase");

        Fixture f;
        auto& cm = ClipManager::getInstance();
        auto clipId =
            cm.createAudioClip(f.trackId, 0.0, 5.0, f.audioPath(), ClipView::Arrangement, 60.0);
        auto* clip = cm.getClip(clipId);
        expect(clip != nullptr, "Source clip should exist");
        if (clip == nullptr)
            return;

        configureBeatModeLoop(*clip, 60.0, 172.0, 5.0, 0.0, 5.0 * 172.0 / 60.0, 5.0 * 172.0 / 60.0);
        auto rightClipId = cm.splitClip(clipId, 8.0, 60.0);
        auto* rightClip = cm.getClip(rightClipId);
        expect(rightClip != nullptr, "Right split clip should exist");
        if (rightClip == nullptr)
            return;

        const double srcOffsetBeats = rightClip->offsetBeats;
        const double srcLoopStartBeats = rightClip->loopStartBeats;
        const double srcLoopLengthBeats = rightClip->loopLengthBeats;

        std::unordered_set<ClipId> copiedIds{rightClipId};
        cm.copyToClipboard(copiedIds);
        constexpr double kPasteTime = 12.0;
        auto pastedIds = cm.pasteFromClipboard(kPasteTime, f.trackId, ClipView::Arrangement);
        expectEquals(static_cast<int>(pastedIds.size()), 1);
        if (pastedIds.empty())
            return;

        auto pastedId = pastedIds.front();
        auto* pastedClip = cm.getClip(pastedId);
        expect(pastedClip != nullptr, "Pasted clip should exist");
        if (pastedClip == nullptr)
            return;

        // Source-domain phase carries over verbatim — paste must not change which
        // audio plays.
        expectWithinAbsoluteError(pastedClip->offsetBeats, srcOffsetBeats, 0.01);
        expectWithinAbsoluteError(pastedClip->loopStartBeats, srcLoopStartBeats, 0.01);
        expectWithinAbsoluteError(pastedClip->loopLengthBeats, srcLoopLengthBeats, 0.01);
        // Phase within loop — same modulo invariant.
        expectWithinAbsoluteError(wrapPhase(pastedClip->offsetBeats - pastedClip->loopStartBeats,
                                            pastedClip->loopLengthBeats),
                                  wrapPhase(srcOffsetBeats - srcLoopStartBeats, srcLoopLengthBeats),
                                  0.01);

        f.clipSync->syncClipToEngine(pastedId);
        f.edit->restartPlayback();

        auto* pastedTeClip = f.getTeAudioClip(pastedId);
        expect(pastedTeClip != nullptr, "Pasted TE clip should exist");
        if (pastedTeClip != nullptr) {
            // TE's getOffset is clip-relative seconds derived from
            // (offsetBeats - loopStartBeats) × 60 / projectBpm.
            const double expectedTeOffset = pastedClip->getTeOffset(true, 60.0);
            expectWithinAbsoluteError(pastedTeClip->getPosition().getOffset().inSeconds(),
                                      expectedTeOffset, 0.01);
            // Sanity: TE clip's edit start matches the paste timeline position.
            expectWithinAbsoluteError(pastedTeClip->getPosition().getStart().inSeconds(),
                                      kPasteTime, 0.01);
        }

        auto result = te::test_utilities::renderToAudioBuffer(*f.edit);
        expectAudioInRange(result.buffer, result.sampleRate, kPasteTime + 0.2, 1.0,
                           "pasted right split clip");
    }

    void testBeatModeTimeRangePastePreservesTrimmedLoopPhase() {
        beginTest("Beat-mode time-range paste preserves trimmed loop phase");

        Fixture f;
        auto& cm = ClipManager::getInstance();
        auto clipId =
            cm.createAudioClip(f.trackId, 0.0, 5.0, f.audioPath(), ClipView::Arrangement, 60.0);
        auto* clip = cm.getClip(clipId);
        expect(clip != nullptr, "Source clip should exist");
        if (clip == nullptr)
            return;

        // Source: 120 BPM, loop region beats [2, 6) of source (loopStart=2,
        // loopLength=4). Placement: 0..12 beats at projectBpm=60. clip.offsetBeats
        // = loopStartBeats = 2 (configureBeatModeLoop).
        configureBeatModeLoop(*clip, 60.0, 120.0, 5.0, 2.0, 4.0, 12.0);
        const double clipLoopStartBeats = clip->loopStartBeats;
        const double clipLoopLengthBeats = clip->loopLengthBeats;
        const double clipOffsetBeats = clip->offsetBeats;

        // Time-range copy: trim from 5.5s..7.5s within the original timeline.
        // copyTimeRangeToClipboard moves trimmed.offsetBeats forward by
        // trimFromLeftBeats (= 5.5 at projectBpm=60) → expected 2 + 5.5 = 7.5.
        constexpr double kRangeStart = 5.5;
        constexpr double kRangeEnd = 7.5;
        cm.copyTimeRangeToClipboard(kRangeStart, kRangeEnd, {f.trackId}, 60.0);

        constexpr double kPasteTime = 10.0;
        auto pastedIds = cm.pasteFromClipboard(kPasteTime, f.trackId, ClipView::Arrangement);
        expectEquals(static_cast<int>(pastedIds.size()), 1);
        if (pastedIds.empty())
            return;

        auto pastedId = pastedIds.front();
        auto* pastedClip = cm.getClip(pastedId);
        expect(pastedClip != nullptr, "Pasted trimmed clip should exist");
        if (pastedClip == nullptr)
            return;

        // Loop region in source-domain unchanged by trimming or pasting.
        expectWithinAbsoluteError(pastedClip->loopStartBeats, clipLoopStartBeats, 0.01);
        expectWithinAbsoluteError(pastedClip->loopLengthBeats, clipLoopLengthBeats, 0.01);

        // offsetBeats advanced by the trim distance, then preserved verbatim
        // by paste. (The paste's timeline position is irrelevant to source phase.)
        const double expectedOffsetBeats = clipOffsetBeats + (kRangeStart - 0.0);  // = 7.5
        expectWithinAbsoluteError(pastedClip->offsetBeats, expectedOffsetBeats, 0.01);

        // TE offset is clip-relative seconds: (offsetBeats - loopStartBeats) × 60 / projectBpm.
        const double expectedTeOffset =
            (expectedOffsetBeats - clipLoopStartBeats) * 60.0 / 60.0;  // = 5.5
        expectWithinAbsoluteError(pastedClip->getTeOffset(true, 60.0), expectedTeOffset, 0.01);

        // Phase modulo loop length is what determines audible content alignment.
        expectWithinAbsoluteError(
            wrapPhase(pastedClip->offsetBeats - pastedClip->loopStartBeats,
                      pastedClip->loopLengthBeats),
            wrapPhase(expectedOffsetBeats - clipLoopStartBeats, clipLoopLengthBeats), 0.01);

        f.clipSync->syncClipToEngine(pastedId);
        f.edit->restartPlayback();

        auto result = te::test_utilities::renderToAudioBuffer(*f.edit);
        expectAudioInRange(result.buffer, result.sampleRate, kPasteTime + 0.2, 1.0,
                           "time-range pasted beat-mode clip");
    }

    void testFadeInOut() {
        beginTest("Fade in/out values sync to TE");

        Fixture f;
        auto clipId = ClipManager::getInstance().createAudioClip(f.trackId, 0.0, 4.0, f.audioPath(),
                                                                 ClipView::Arrangement, 60.0);
        f.clipSync->syncClipToEngine(clipId);

        // Set fades
        ClipManager::getInstance().setFadeIn(clipId, 0.5);
        ClipManager::getInstance().setFadeOut(clipId, 0.3);
        f.clipSync->syncClipToEngine(clipId);

        auto* teClip = f.getTeAudioClip(clipId);
        expect(teClip != nullptr);

        expectWithinAbsoluteError(teClip->getFadeIn().inSeconds(), 0.5, 0.01);
        expectWithinAbsoluteError(teClip->getFadeOut().inSeconds(), 0.3, 0.01);
    }

    void testLaunchFadeSamples() {
        beginTest("Launch fade samples sync to TE AudioClipBase");

        Fixture f;
        auto clipId = ClipManager::getInstance().createAudioClip(f.trackId, 0.0, 4.0, f.audioPath(),
                                                                 ClipView::Arrangement, 60.0);
        f.clipSync->syncClipToEngine(clipId);

        auto* teClip = f.getTeAudioClip(clipId);
        expect(teClip != nullptr);
        // Default = 256 to preserve TE's prior hard-coded behaviour.
        expectEquals(teClip->getLaunchFadeSamples(), 256);

        ClipManager::getInstance().setLaunchFadeSamples(clipId, 0);
        f.clipSync->syncClipToEngine(clipId);
        expectEquals(teClip->getLaunchFadeSamples(), 0);

        ClipManager::getInstance().setLaunchFadeSamples(clipId, 1024);
        f.clipSync->syncClipToEngine(clipId);
        expectEquals(teClip->getLaunchFadeSamples(), 1024);

        // Out-of-range values clamp.
        ClipManager::getInstance().setLaunchFadeSamples(clipId, -10);
        f.clipSync->syncClipToEngine(clipId);
        expectEquals(teClip->getLaunchFadeSamples(), 0);

        ClipManager::getInstance().setLaunchFadeSamples(clipId, 999999);
        f.clipSync->syncClipToEngine(clipId);
        expectEquals(teClip->getLaunchFadeSamples(), 16384);
    }

    void testGainAndPan() {
        beginTest("Gain and pan sync to TE");

        Fixture f;
        auto clipId = ClipManager::getInstance().createAudioClip(f.trackId, 0.0, 2.0, f.audioPath(),
                                                                 ClipView::Arrangement, 60.0);
        f.clipSync->syncClipToEngine(clipId);

        // Set gain and pan
        ClipManager::getInstance().setClipVolumeDB(clipId, -6.0f);
        ClipManager::getInstance().setClipPan(clipId, 0.5f);
        f.clipSync->syncClipToEngine(clipId);

        auto* teClip = f.getTeAudioClip(clipId);
        expect(teClip != nullptr);

        expectWithinAbsoluteError(static_cast<double>(teClip->getGainDB()), -6.0, 0.01);
        expectWithinAbsoluteError(static_cast<double>(teClip->getPan()), 0.5, 0.01);
    }

    void testPitchChange() {
        beginTest("Pitch change syncs to TE");

        Fixture f;
        auto clipId = ClipManager::getInstance().createAudioClip(f.trackId, 0.0, 2.0, f.audioPath(),
                                                                 ClipView::Arrangement, 60.0);
        f.clipSync->syncClipToEngine(clipId);

        // Set pitch change
        ClipManager::getInstance().setPitchChange(clipId, 2.0f);
        f.clipSync->syncClipToEngine(clipId);

        auto* teClip = f.getTeAudioClip(clipId);
        expect(teClip != nullptr);

        expectWithinAbsoluteError(static_cast<double>(teClip->getPitchChange()), 2.0, 0.01);
    }

    void testRenderVerification() {
        beginTest(
            "Render: audio at correct position (silence before, signal during, silence after)");

        Fixture f;

        // Create clip with sine at t=1.0, length=2.0 → audio in [1s, 3s]
        auto clipId = ClipManager::getInstance().createAudioClip(f.trackId, 1.0, 2.0, f.audioPath(),
                                                                 ClipView::Arrangement, 60.0);
        f.clipSync->syncClipToEngine(clipId);

        // Render the edit
        auto result = te::test_utilities::renderToAudioBuffer(*f.edit);
        expect(result.buffer.getNumSamples() > 0, "Rendered buffer should not be empty");

        double sr = result.sampleRate;
        auto& buf = result.buffer;

        auto renderedDuration = static_cast<double>(buf.getNumSamples()) / sr;
        expect(renderedDuration >= 2.9, "Rendered buffer too short for verification, duration=" +
                                            juce::String(renderedDuration, 3) + "s");

        // Check silence in [0, 0.9s] — small margin to avoid boundary artifacts
        {
            int startSample = 0;
            int numSamples = static_cast<int>(0.9 * sr);
            expect(numSamples <= buf.getNumSamples(),
                   "Buffer too short for pre-clip silence check");
            float rms = buf.getRMSLevel(0, startSample, numSamples);
            expect(rms < 0.01f, "Should be silence before clip (0-0.9s), RMS=" + juce::String(rms));
        }

        // Check non-silence in [1.1s, 2.9s]
        {
            int startSample = static_cast<int>(1.1 * sr);
            int numSamples = static_cast<int>(1.8 * sr);
            expect(startSample + numSamples <= buf.getNumSamples(),
                   "Buffer too short for audio-during-clip check");
            float rms = buf.getRMSLevel(0, startSample, numSamples);
            expect(rms > 0.01f,
                   "Should have audio during clip (1.1-2.9s), RMS=" + juce::String(rms));
        }

        // Check silence after [3.1s, end] — only if buffer extends past clip
        {
            int startSample = static_cast<int>(3.1 * sr);
            int numSamples = buf.getNumSamples() - startSample;
            if (numSamples > 0) {
                float rms = buf.getRMSLevel(0, startSample, numSamples);
                expect(rms < 0.01f,
                       "Should be silence after clip (3.1s+), RMS=" + juce::String(rms));
            }
        }
    }

    // =========================================================================
    // Overlap resolution
    // =========================================================================

    void testCreateAudioClipDeletesFullyOverlappedNeighbour() {
        beginTest("createAudioClip deletes a fully-covered arrangement neighbour");

        Fixture f;
        auto& cm = ClipManager::getInstance();

        // Existing clip: bars 1-3 (= 8 beats at 60 BPM = 8s)
        auto existing =
            cm.createAudioClip(f.trackId, 0.0, 8.0, f.audioPath(), ClipView::Arrangement, 60.0);
        expect(existing != INVALID_CLIP_ID);

        // New clip fully covers it: 0-12 beats
        auto incoming =
            cm.createAudioClip(f.trackId, 0.0, 12.0, f.audioPath(), ClipView::Arrangement, 60.0);
        expect(incoming != INVALID_CLIP_ID);

        expect(cm.getClip(existing) == nullptr, "Fully-covered neighbour should have been deleted");
        expect(cm.getClip(incoming) != nullptr, "Incoming clip should remain");
    }

    void testMoveAudioClipTrimsNeighbourFromRight() {
        beginTest("moveClip trims neighbour's right edge when overlapping from the left");

        Fixture f;
        auto& cm = ClipManager::getInstance();

        // Static neighbour at 0-8s, mover starts further right.
        auto stationary =
            cm.createAudioClip(f.trackId, 0.0, 8.0, f.audioPath(), ClipView::Arrangement, 60.0);
        auto mover =
            cm.createAudioClip(f.trackId, 16.0, 4.0, f.audioPath(), ClipView::Arrangement, 60.0);
        expect(stationary != INVALID_CLIP_ID);
        expect(mover != INVALID_CLIP_ID);

        // Move the mover left so it overlaps the right half of stationary.
        cm.moveClip(mover, 4.0, 60.0);

        const auto* s = cm.getClip(stationary);
        const auto* m = cm.getClip(mover);
        expect(s != nullptr && m != nullptr);
        if (!s || !m)
            return;

        // Stationary's right edge should be trimmed back to mover's start beat.
        expectWithinAbsoluteError(s->placement.startBeat, 0.0, 0.01);
        expectWithinAbsoluteError(s->placement.lengthBeats, 4.0, 0.01);
        expectWithinAbsoluteError(m->placement.startBeat, 4.0, 0.01);
        expectWithinAbsoluteError(m->placement.lengthBeats, 4.0, 0.01);
    }

    void testMoveAudioClipTrimsNeighbourFromLeft() {
        beginTest("moveClip trims neighbour's left edge when overlapping from the right");

        Fixture f;
        auto& cm = ClipManager::getInstance();

        // stationary: beats 8-16. mover: 0-8 length 8, then move startTime=4
        // → mover spans beats 4-12, covering stationary's left half (beats
        // 8-12). resolveOverlaps should trim stationary's left edge to 12.
        auto stationary =
            cm.createAudioClip(f.trackId, 8.0, 8.0, f.audioPath(), ClipView::Arrangement, 60.0);
        auto mover =
            cm.createAudioClip(f.trackId, 0.0, 8.0, f.audioPath(), ClipView::Arrangement, 60.0);

        cm.moveClip(mover, 4.0, 60.0);

        const auto* s = cm.getClip(stationary);
        const auto* m = cm.getClip(mover);
        expect(s != nullptr && m != nullptr);
        if (!s || !m)
            return;

        expectWithinAbsoluteError(m->placement.startBeat, 4.0, 0.01);
        expectWithinAbsoluteError(m->placement.lengthBeats, 8.0, 0.01);
        expectWithinAbsoluteError(s->placement.startBeat, 12.0, 0.01);
        expectWithinAbsoluteError(s->placement.lengthBeats, 4.0, 0.01);
    }

    void testDuplicateAudioClipResolvesOverlap() {
        beginTest("duplicateClipAt resolves overlap with existing clip on same track");

        Fixture f;
        auto& cm = ClipManager::getInstance();

        // Source at 0-4s, victim at 8-12s. Duplicate source to 6s — that lands
        // at 6-10s and partially covers the victim's left half.
        auto source =
            cm.createAudioClip(f.trackId, 0.0, 4.0, f.audioPath(), ClipView::Arrangement, 60.0);
        auto victim =
            cm.createAudioClip(f.trackId, 8.0, 4.0, f.audioPath(), ClipView::Arrangement, 60.0);

        auto copy = cm.duplicateClipAt(source, 6.0, f.trackId, 60.0);
        expect(copy != INVALID_CLIP_ID);

        const auto* v = cm.getClip(victim);
        const auto* c = cm.getClip(copy);
        expect(v != nullptr && c != nullptr);
        if (!v || !c)
            return;

        // Victim should now start at beat 10 and be 2 beats long.
        expectWithinAbsoluteError(c->placement.startBeat, 6.0, 0.01);
        expectWithinAbsoluteError(c->placement.lengthBeats, 4.0, 0.01);
        expectWithinAbsoluteError(v->placement.startBeat, 10.0, 0.01);
        expectWithinAbsoluteError(v->placement.lengthBeats, 2.0, 0.01);
    }

    void testPasteAudioClipResolvesOverlap() {
        beginTest("pasteFromClipboard resolves overlap with existing clip");

        Fixture f;
        auto& cm = ClipManager::getInstance();

        auto source =
            cm.createAudioClip(f.trackId, 0.0, 4.0, f.audioPath(), ClipView::Arrangement, 60.0);
        auto victim =
            cm.createAudioClip(f.trackId, 6.0, 6.0, f.audioPath(), ClipView::Arrangement, 60.0);

        cm.copyToClipboard(std::unordered_set<ClipId>{source});
        auto pastedIds = cm.pasteFromClipboard(8.0, f.trackId, ClipView::Arrangement);
        expectEquals(static_cast<int>(pastedIds.size()), 1);
        if (pastedIds.empty())
            return;

        const auto* v = cm.getClip(victim);
        const auto* p = cm.getClip(pastedIds.front());
        expect(v != nullptr && p != nullptr);
        if (!v || !p)
            return;

        expectWithinAbsoluteError(p->placement.startBeat, 8.0, 0.01);
        expectWithinAbsoluteError(p->placement.lengthBeats, 4.0, 0.01);
        // Victim originally 6-12. Paste lands 8-12 → fully covers victim's right half,
        // so victim's right edge trims back to beat 8.
        expectWithinAbsoluteError(v->placement.startBeat, 6.0, 0.01);
        expectWithinAbsoluteError(v->placement.lengthBeats, 2.0, 0.01);
    }

    void testCreateMidiClipResolvesOverlap() {
        beginTest("createMidiClip resolves overlap (MIDI uses the same code path)");

        Fixture f;
        auto& cm = ClipManager::getInstance();

        auto stationary = cm.createMidiClip(f.trackId, 0.0, 8.0, ClipView::Arrangement);
        auto incoming = cm.createMidiClip(f.trackId, 4.0, 8.0, ClipView::Arrangement);
        expect(stationary != INVALID_CLIP_ID && incoming != INVALID_CLIP_ID);

        const auto* s = cm.getClip(stationary);
        const auto* i = cm.getClip(incoming);
        expect(s != nullptr && i != nullptr);
        if (!s || !i)
            return;

        // Incoming covers stationary's right half (beats 4-8). Stationary's right
        // edge should trim to beat 4.
        expectWithinAbsoluteError(s->placement.startBeat, 0.0, 0.01);
        expectWithinAbsoluteError(s->placement.lengthBeats, 4.0, 0.01);
        expectWithinAbsoluteError(i->placement.startBeat, 4.0, 0.01);
        expectWithinAbsoluteError(i->placement.lengthBeats, 8.0, 0.01);
    }

    void testMoveClipNoOverlapDoesNotMutateNeighbours() {
        beginTest("moveClip without overlap leaves neighbours untouched");

        Fixture f;
        auto& cm = ClipManager::getInstance();

        auto a =
            cm.createAudioClip(f.trackId, 0.0, 4.0, f.audioPath(), ClipView::Arrangement, 60.0);
        auto b =
            cm.createAudioClip(f.trackId, 16.0, 4.0, f.audioPath(), ClipView::Arrangement, 60.0);

        // Move b further right — no overlap with a.
        cm.moveClip(b, 32.0, 60.0);

        const auto* aClip = cm.getClip(a);
        const auto* bClip = cm.getClip(b);
        expect(aClip != nullptr && bClip != nullptr);
        if (!aClip || !bClip)
            return;
        expectWithinAbsoluteError(aClip->placement.startBeat, 0.0, 0.01);
        expectWithinAbsoluteError(aClip->placement.lengthBeats, 4.0, 0.01);
        expectWithinAbsoluteError(bClip->placement.startBeat, 32.0, 0.01);
        expectWithinAbsoluteError(bClip->placement.lengthBeats, 4.0, 0.01);
    }

    void testMoveClipToTrackResolvesOverlapOnDestination() {
        beginTest("moveClipToTrack resolves overlap on the destination track");

        Fixture f;
        auto& cm = ClipManager::getInstance();

        constexpr TrackId secondTrackId = 2;
        f.trackController->ensureTrackMapping(secondTrackId, "Track 2");

        auto sitter =
            cm.createAudioClip(secondTrackId, 0.0, 8.0, f.audioPath(), ClipView::Arrangement, 60.0);
        auto mover =
            cm.createAudioClip(f.trackId, 0.0, 8.0, f.audioPath(), ClipView::Arrangement, 60.0);
        expect(sitter != INVALID_CLIP_ID && mover != INVALID_CLIP_ID);

        cm.moveClipToTrack(mover, secondTrackId);

        // Mover ends up on track 2 fully covering sitter → sitter deleted.
        expect(cm.getClip(sitter) == nullptr,
               "Fully-covered clip on destination track should be deleted");
        const auto* m = cm.getClip(mover);
        expect(m != nullptr);
        if (m) {
            expect(m->trackId == secondTrackId);
            expectWithinAbsoluteError(m->placement.startBeat, 0.0, 0.01);
            expectWithinAbsoluteError(m->placement.lengthBeats, 8.0, 0.01);
        }
    }
};

static ClipSyncIntegrationTest clipSyncIntegrationTest;
