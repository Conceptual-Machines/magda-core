#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <tracktion_engine/tracktion_engine.h>

// TE internal test utilities (not exposed via module public headers)
#include "SharedTestEngine.hpp"
#include "magda/daw/audio/ClipSynchronizer.hpp"
#include "magda/daw/audio/TrackController.hpp"
#include "magda/daw/audio/WarpMarkerManager.hpp"
#include "magda/daw/core/ClipInfo.hpp"
#include "magda/daw/core/ClipManager.hpp"
#include "magda/daw/core/ClipOperations.hpp"
#include "third_party/tracktion_engine/modules/tracktion_engine/utilities/tracktion_TestUtilities.h"

using namespace magda;
namespace te = tracktion;

namespace {

/** Generate a mono sine WAV file and return it as a TemporaryFile. */
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

    auto f = std::make_unique<juce::TemporaryFile>(".wav");
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
        testMoveClip();
        testResizeFromRight();
        testResizeFromLeft();
        testTrimAudioFromLeft();
        testTrimAudioFromRight();
        testSpeedRatio();
        testLoopEnableDisable();
        testLoopTimeBased();
        testLoopTimeBasedWarpEnabled();
        testSplitAudioClip();
        testFadeInOut();
        testGainAndPan();
        testPitchChange();
        testRenderVerification();
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

        Fixture() {
            // Reset ClipManager singleton
            ClipManager::getInstance().clearAllClips();

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
        }

        juce::String audioPath() const {
            return sinFile->getFile().getFullPathName();
        }

        te::WaveAudioClip* getTeAudioClip(ClipId clipId) const {
            auto* teClip = clipSync->getArrangementTeClip(clipId);
            return dynamic_cast<te::WaveAudioClip*>(teClip);
        }
    };

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
        beginTest("Speed ratio syncs to TE");

        Fixture f;
        auto clipId = ClipManager::getInstance().createAudioClip(f.trackId, 0.0, 2.0, f.audioPath(),
                                                                 ClipView::Arrangement, 60.0);
        f.clipSync->syncClipToEngine(clipId);

        auto* teClip = f.getTeAudioClip(clipId);
        expect(teClip != nullptr);
        expectWithinAbsoluteError(teClip->getSpeedRatio(), 1.0, 0.01);

        // Set speed ratio to 2.0
        ClipManager::getInstance().setSpeedRatio(clipId, 2.0);
        f.clipSync->syncClipToEngine(clipId);

        expectWithinAbsoluteError(teClip->getSpeedRatio(), 2.0, 0.01);
    }

    void testLoopEnableDisable() {
        beginTest("Loop enable/disable syncs to TE");

        Fixture f;
        auto clipId = ClipManager::getInstance().createAudioClip(f.trackId, 0.0, 4.0, f.audioPath(),
                                                                 ClipView::Arrangement, 60.0);
        f.clipSync->syncClipToEngine(clipId);

        auto* teClip = f.getTeAudioClip(clipId);
        expect(teClip != nullptr);

        // Enable looping with explicit loop region
        auto* clip = ClipManager::getInstance().getClip(clipId);
        clip->loopEnabled = true;
        clip->loopStart = 0.0;
        clip->loopLength = 2.0;
        f.clipSync->syncClipToEngine(clipId);

        expect(teClip->isLooping(), "TE clip should be looping");

        // Verify loop range
        auto loopRange = teClip->getLoopRange();
        expectWithinAbsoluteError(loopRange.getStart().inSeconds(), clip->getTeLoopStart(), 0.01);
        expectWithinAbsoluteError(loopRange.getEnd().inSeconds(), clip->getTeLoopEnd(), 0.01);

        // Disable looping
        clip->loopEnabled = false;
        f.clipSync->syncClipToEngine(clipId);

        expect(!teClip->isLooping(), "TE clip should not be looping after disable");
    }

    void testLoopTimeBased() {
        beginTest("Time-based loop: clip container longer than loop region (non-integer multiple)");

        // Reproduces the bug from the screenshot:
        //   120 BPM, clip = 3 bars (6s), loop region = 2 bars (4s).
        //   Expected: bars 1-2 play first loop cycle, bar 3 plays start of second cycle.
        //   Bug: bar 3 is silent — the partial second loop cycle doesn't play.

        Fixture f;

        // Use 60 BPM edit (from createTestEdit) so 1 beat = 1s, easy math.
        // Scenario: 2s loop region inside a 3s clip container.
        // The loop should play [0-2s] then [2-3s] is the first 1s of the loop again.

        // Create clip at 2s length (matching one full loop cycle)
        auto clipId = ClipManager::getInstance().createAudioClip(f.trackId, 0.0, 2.0, f.audioPath(),
                                                                 ClipView::Arrangement, 60.0);
        expect(clipId != INVALID_CLIP_ID);

        // Enable looping — sets loopStart=0.0, loopLength=2.0
        ClipManager::getInstance().setClipLoopEnabled(clipId, true, 60.0);

        // Extend clip container to 3s (1.5× the loop region)
        auto* clip = ClipManager::getInstance().getClip(clipId);
        expect(clip != nullptr);
        ClipOperations::resizeContainerFromRight(*clip, 3.0);

        // Verify model state
        expect(clip->loopEnabled, "Model: loopEnabled should be true");
        expectWithinAbsoluteError(clip->loopStart, 0.0, 0.01);
        expectWithinAbsoluteError(clip->loopLength, 2.0, 0.01);
        expectWithinAbsoluteError(clip->length, 3.0, 0.01);

        f.clipSync->syncClipToEngine(clipId);

        auto* teClip = f.getTeAudioClip(clipId);
        expect(teClip != nullptr, "TE clip should exist");

        // --- TE property checks ---
        expect(teClip->isLooping(), "TE clip should be looping");

        auto pos = teClip->getPosition();
        expectWithinAbsoluteError(pos.getStart().inSeconds(), 0.0, 0.01);
        expectWithinAbsoluteError(pos.getEnd().inSeconds(), 3.0, 0.01);

        // TE loop range should be 2s
        auto loopRange = teClip->getLoopRange();
        expectWithinAbsoluteError(loopRange.getLength().inSeconds(), 2.0, 0.01);

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

    void testGainAndPan() {
        beginTest("Gain and pan sync to TE");

        Fixture f;
        auto clipId = ClipManager::getInstance().createAudioClip(f.trackId, 0.0, 2.0, f.audioPath(),
                                                                 ClipView::Arrangement, 60.0);
        f.clipSync->syncClipToEngine(clipId);

        // Set gain and pan
        ClipManager::getInstance().setClipGainDB(clipId, -6.0f);
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
};

static ClipSyncIntegrationTest clipSyncIntegrationTest;
