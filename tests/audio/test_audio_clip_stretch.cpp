#include <tracktion_engine/playback/graph/tracktion_LaunchDeClick.h>
#include <tracktion_engine/tracktion_engine.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "AudioClipTestHelpers.hpp"
#include "magda/daw/core/ClipInfo.hpp"
#include "magda/daw/core/ClipManager.hpp"
#include "magda/daw/core/ClipOperations.hpp"
#include "magda/daw/core/TimeStretchModes.hpp"

/**
 * Tests for audio clip time-stretching and trimming operations
 *
 * These tests verify:
 * - Audio stretch factor clamping and behavior
 * - Trim operations maintain absolute timeline positions
 * - Stretch operations maintain file time window
 * - Left-edge resize properly trims audio file offset
 */

TEST_CASE("Audio clip - Stretch factor basics", "[audio][clip][stretch]") {
    using namespace magda;
    constexpr double kBpm = 120.0;

    SECTION("Default stretch factor is 1.0") {
        ClipInfo clip;
        clip.setAudioContent();
        magda::test::giveAudioEvent(clip, "test.wav");
        clip.setPlacementBeats(0.0, 8.0);
        magda::test::audioEvent(clip).speedRatio = 1.0;

        // File window equals length when stretch factor is 1.0
        double fileWindow = clip.getTimelineLength(kBpm) * magda::test::audioEvent(clip).speedRatio;
        REQUIRE(fileWindow == 4.0);
    }

    SECTION("Stretch factor affects file time window") {
        ClipInfo clip;
        clip.setAudioContent();
        magda::test::giveAudioEvent(clip, "test.wav");
        magda::test::audioEvent(clip).setAnchorSeconds(0.0);
        clip.setPlacementBeats(0.0, 8.0);
        magda::test::audioEvent(clip).speedRatio = 2.0;  // 2x faster

        // File window is double the length when 2x faster
        double fileWindow = clip.getTimelineLength(kBpm) * magda::test::audioEvent(clip).speedRatio;
        REQUIRE(fileWindow == 8.0);

        // Reading from file offset 0-8, displaying as 0-4 seconds
    }

    SECTION("Stretch factor 0.5 = 2x slower") {
        ClipInfo clip;
        clip.setAudioContent();
        magda::test::giveAudioEvent(clip, "test.wav");
        magda::test::audioEvent(clip).setAnchorSeconds(0.0);
        clip.setPlacementBeats(0.0, 16.0);
        magda::test::audioEvent(clip).speedRatio = 0.5;  // 2x slower

        // File window is half the length when 2x slower
        double fileWindow = clip.getTimelineLength(kBpm) * magda::test::audioEvent(clip).speedRatio;
        REQUIRE(fileWindow == 4.0);

        // Reading from file offset 0-4, displaying as 0-8 seconds
    }
}

TEST_CASE("ClipManager - setSpeedRatio clamping", "[audio][clip][stretch]") {
    using namespace magda;

    // Reset and setup
    ClipManager::getInstance().shutdown();

    SECTION("Stretch factor clamped to [0.25, 4.0] range") {
        ClipId clipId = ClipManager::getInstance().createAudioClipBeats(1, 0.0, 8.0, "test.wav");
        REQUIRE(clipId != INVALID_CLIP_ID);

        const auto* clip = ClipManager::getInstance().getClip(clipId);
        REQUIRE(clip != nullptr);
        REQUIRE(primaryEventOf(clip)->sourceFilePath() == "test.wav");

        // Test minimum clamp
        ClipManager::getInstance().setSpeedRatio(clipId, 0.1);
        REQUIRE(primaryEventOf(clip)->speedRatio == 0.25);

        // Test maximum clamp
        ClipManager::getInstance().setSpeedRatio(clipId, 10.0);
        REQUIRE(primaryEventOf(clip)->speedRatio == 4.0);

        // Test valid range
        ClipManager::getInstance().setSpeedRatio(clipId, 1.5);
        REQUIRE(primaryEventOf(clip)->speedRatio == 1.5);

        ClipManager::getInstance().setSpeedRatio(clipId, 0.5);
        REQUIRE(primaryEventOf(clip)->speedRatio == 0.5);
    }
}

TEST_CASE("Audio Clip - Left edge resize trims file offset", "[audio][clip][trim]") {
    using namespace magda;
    constexpr double kBpm = 120.0;

    ClipManager::getInstance().shutdown();

    SECTION("Trim from left advances file offset (audio at clip start)") {
        ClipId clipId = ClipManager::getInstance().createAudioClipBeats(1, 0.0, 8.0, "test.wav");
        auto* clip = ClipManager::getInstance().getClip(clipId);
        REQUIRE(clip != nullptr);

        primaryEventOf(clip)->setAnchorSeconds(0.0);
        primaryEventOf(clip)->speedRatio = 1.0;

        // Trim from left by 2 beats (1 s at 120 BPM)
        ClipManager::getInstance().resizeClipBeats(clipId, 6.0, true, kBpm);

        REQUIRE(clip->placement.startBeat == Catch::Approx(2.0));
        REQUIRE(clip->placement.lengthBeats == Catch::Approx(6.0));

        // Audio offset advanced by 1.0 second
        REQUIRE(primaryEventOf(clip)->anchorSeconds() == Catch::Approx(1.0));
    }

    SECTION("Trim with stretch factor converts to file time") {
        ClipId clipId = ClipManager::getInstance().createAudioClipBeats(1, 0.0, 8.0, "test.wav");
        auto* clip = ClipManager::getInstance().getClip(clipId);

        primaryEventOf(clip)->setAnchorSeconds(0.0);
        primaryEventOf(clip)->speedRatio = 2.0;  // 2x faster, file window = 8.0

        // Trim from left by 4 beats (2 s at 120 BPM)
        ClipManager::getInstance().resizeClipBeats(clipId, 4.0, true, kBpm);

        REQUIRE(clip->placement.startBeat == Catch::Approx(4.0));
        REQUIRE(clip->placement.lengthBeats == Catch::Approx(4.0));

        // File trim amount = 2.0 * 2.0 = 4.0 file seconds
        REQUIRE(primaryEventOf(clip)->anchorSeconds() == Catch::Approx(4.0));
    }
}

TEST_CASE("Audio Clip - Right edge resize doesn't change offset", "[audio][clip][resize]") {
    using namespace magda;

    ClipManager::getInstance().shutdown();

    SECTION("Right edge resize only changes length") {
        ClipId clipId = ClipManager::getInstance().createAudioClipBeats(1, 0.0, 8.0, "test.wav");
        auto* clip = ClipManager::getInstance().getClip(clipId);

        primaryEventOf(clip)->setAnchorSeconds(1.0);

        // Resize from right edge
        ClipManager::getInstance().resizeClipBeats(clipId, 12.0, false, 120.0);

        REQUIRE(clip->placement.startBeat == 0.0);
        REQUIRE(clip->placement.lengthBeats == Catch::Approx(12.0));

        // Audio offset unchanged
        REQUIRE(primaryEventOf(clip)->anchorSeconds() == 1.0);
    }
}

TEST_CASE("Audio Clip - Stretch maintains file window", "[audio][clip][stretch]") {
    using namespace magda;
    constexpr double kBpm = 120.0;

    ClipManager::getInstance().shutdown();

    SECTION("Stretching by 2x halves length but file window stays same") {
        ClipId clipId = ClipManager::getInstance().createAudioClipBeats(1, 0.0, 8.0, "test.wav");
        auto* clip = ClipManager::getInstance().getClip(clipId);

        primaryEventOf(clip)->setAnchorSeconds(0.0);
        primaryEventOf(clip)->speedRatio = 1.0;

        double originalFileWindow =
            clip->getTimelineLength(kBpm) * primaryEventOf(clip)->speedRatio;
        REQUIRE(originalFileWindow == 4.0);

        // Stretch 2x slower: length becomes 16 beats, stretch factor becomes 0.5
        clip->setPlacementBeats(0.0, 16.0);
        ClipManager::getInstance().setSpeedRatio(clipId, 0.5);

        double newFileWindow = clip->getTimelineLength(kBpm) * primaryEventOf(clip)->speedRatio;
        REQUIRE(newFileWindow == Catch::Approx(originalFileWindow));
    }

    SECTION("Compressing by 2x halves length but file window stays same") {
        ClipId clipId = ClipManager::getInstance().createAudioClipBeats(1, 0.0, 8.0, "test.wav");
        auto* clip = ClipManager::getInstance().getClip(clipId);

        primaryEventOf(clip)->setAnchorSeconds(1.0);
        primaryEventOf(clip)->speedRatio = 1.0;

        double originalFileWindow =
            clip->getTimelineLength(kBpm) * primaryEventOf(clip)->speedRatio;
        REQUIRE(originalFileWindow == 4.0);

        // Compress 2x faster: length becomes 4 beats, stretch factor becomes 2.0
        clip->setPlacementBeats(0.0, 4.0);
        ClipManager::getInstance().setSpeedRatio(clipId, 2.0);

        double newFileWindow = clip->getTimelineLength(kBpm) * primaryEventOf(clip)->speedRatio;
        REQUIRE(newFileWindow == Catch::Approx(originalFileWindow));

        // File offset unchanged
        REQUIRE(primaryEventOf(clip)->anchorSeconds() == 1.0);
    }
}

TEST_CASE("Audio Clip - Analog pitch resamples instead of time-stretching",
          "[audio][clip][pitch][analog]") {
    using namespace magda;
    constexpr double kBpm = 120.0;

    ClipManager::getInstance().shutdown();

    SECTION("Pitch down slows playback and grows timeline length") {
        ClipId clipId = ClipManager::getInstance().createAudioClipBeats(1, 0.0, 4.0, "test.wav");
        auto* clip = ClipManager::getInstance().getClip(clipId);
        REQUIRE(clip != nullptr);

        primaryEventOf(clip)->speedRatio = 1.0;

        ClipManager::getInstance().setAnalogPitch(clipId, true);
        ClipManager::getInstance().setPitchChange(clipId, -12.0f);

        REQUIRE(primaryEventOf(clip)->analogPitch);
        REQUIRE(primaryEventOf(clip)->speedRatio == Catch::Approx(0.5));
        REQUIRE(clip->getTimelineLength(kBpm) == Catch::Approx(4.0));
        REQUIRE(clip->placement.lengthBeats == Catch::Approx(8.0));
        REQUIRE(primaryEventOf(clip)->timelineToSource(clip->getTimelineLength(kBpm)) ==
                Catch::Approx(2.0));
    }

    SECTION("Pitch up speeds playback and shrinks timeline length") {
        ClipId clipId = ClipManager::getInstance().createAudioClipBeats(1, 0.0, 4.0, "test.wav");
        auto* clip = ClipManager::getInstance().getClip(clipId);
        REQUIRE(clip != nullptr);

        primaryEventOf(clip)->speedRatio = 1.0;

        ClipManager::getInstance().setAnalogPitch(clipId, true);
        ClipManager::getInstance().setPitchChange(clipId, 12.0f);

        REQUIRE(primaryEventOf(clip)->speedRatio == Catch::Approx(2.0));
        REQUIRE(clip->getTimelineLength(kBpm) == Catch::Approx(1.0));
        REQUIRE(clip->placement.lengthBeats == Catch::Approx(2.0));
        REQUIRE(primaryEventOf(clip)->timelineToSource(clip->getTimelineLength(kBpm)) ==
                Catch::Approx(2.0));
    }
}

TEST_CASE("Audio Clip - Real-world scenario: Amen break trim", "[audio][clip][integration]") {
    using namespace magda;
    constexpr double kBpm = 120.0;

    ClipManager::getInstance().shutdown();

    SECTION("Trim amen break from left preserves timeline positions") {
        // Amen break: 4.5 bars at 120 BPM = 18 beats, 9 seconds
        ClipId clipId = ClipManager::getInstance().createAudioClipBeats(1, 0.0, 18.0, "amen.wav");
        auto* clip = ClipManager::getInstance().getClip(clipId);

        primaryEventOf(clip)->setAnchorSeconds(0.0);
        primaryEventOf(clip)->speedRatio = 1.0;

        // Trim from left by 2 beats (to bar 1.3, where first snare is)
        ClipManager::getInstance().resizeClipBeats(clipId, 16.0, true, kBpm);

        REQUIRE(clip->placement.startBeat == Catch::Approx(2.0));
        REQUIRE(clip->placement.lengthBeats == Catch::Approx(16.0));

        // Audio offset advanced to 1.0s (skipping first bar)
        REQUIRE(primaryEventOf(clip)->anchorSeconds() == Catch::Approx(1.0));
    }

    SECTION("Trim stretched amen break converts to file time") {
        // Amen break stretched 2x slower: 36 beats, 18 seconds on the timeline
        ClipId clipId = ClipManager::getInstance().createAudioClipBeats(1, 0.0, 36.0, "amen.wav");
        auto* clip = ClipManager::getInstance().getClip(clipId);

        primaryEventOf(clip)->setAnchorSeconds(0.0);
        primaryEventOf(clip)->speedRatio = 0.5;  // 2x slower, file window = 9.0s

        // Trim from left by 4 beats, 2 timeline seconds (to first snare)
        ClipManager::getInstance().resizeClipBeats(clipId, 32.0, true, kBpm);

        REQUIRE(clip->placement.startBeat == Catch::Approx(4.0));
        REQUIRE(clip->placement.lengthBeats == Catch::Approx(32.0));

        // File trim amount = 2.0 * 0.5 = 1.0 file seconds
        REQUIRE(primaryEventOf(clip)->anchorSeconds() == Catch::Approx(1.0));
    }
}

TEST_CASE("Audio Clip - Edge cases", "[audio][clip][edge]") {
    using namespace magda;
    constexpr double kBpm = 120.0;

    ClipManager::getInstance().shutdown();

    SECTION("Minimum clip length enforced") {
        ClipId clipId = ClipManager::getInstance().createAudioClipBeats(1, 0.0, 8.0, "test.wav");
        auto* clip = ClipManager::getInstance().getClip(clipId);

        // Try to resize to very small length
        ClipManager::getInstance().resizeClipBeats(clipId, 0.02, false, kBpm);

        // Clamped to minimum 0.1 s
        REQUIRE(clip->getTimelineLength(kBpm) == Catch::Approx(0.1));
    }

    SECTION("Trim to zero start time") {
        ClipId clipId = ClipManager::getInstance().createAudioClipBeats(1, 2.0, 8.0, "test.wav");
        auto* clip = ClipManager::getInstance().getClip(clipId);

        // Resize from left past zero
        ClipManager::getInstance().resizeClipBeats(clipId, 12.0, true, kBpm);

        // Start clamped to zero
        REQUIRE(clip->placement.startBeat == 0.0);
        REQUIRE(clip->placement.lengthBeats == Catch::Approx(12.0));
    }
}

TEST_CASE("Audio Clip - Effective time-stretch mode", "[audio][clip][stretch][mode]") {
    using namespace magda;

    // getEffectiveTimeStretchMode() reports the mode that TE actually applies so
    // the inspector and the audio editor show the same value. When the raw mode
    // is "Off" (0) but beat mode / warp / speed / pitch silently engages the
    // stretcher, it reports the default Signalsmith mode.

    auto makeAudioClip = []() {
        ClipInfo clip;
        clip.setAudioContent();
        magda::test::giveAudioEvent(clip, "test.wav");
        return clip;
    };

    SECTION("Off mode with nothing active stays Off") {
        ClipInfo clip = makeAudioClip();
        REQUIRE(magda::test::audioEvent(clip).timeStretchMode == 0);
        REQUIRE(magda::test::audioEvent(clip).getEffectiveTimeStretchMode() == 0);
    }

    SECTION("Beat mode upgrades Off to Signalsmith") {
        ClipInfo clip = makeAudioClip();
        magda::test::audioEvent(clip).autoTempo = true;
        REQUIRE(magda::test::audioEvent(clip).getEffectiveTimeStretchMode() ==
                time_stretch_mode::kSignalsmith);
    }

    SECTION("Warp upgrades Off to Signalsmith") {
        ClipInfo clip = makeAudioClip();
        magda::test::audioEvent(clip).warpEnabled = true;
        REQUIRE(magda::test::audioEvent(clip).getEffectiveTimeStretchMode() ==
                time_stretch_mode::kSignalsmith);
    }

    SECTION("Non-unity speed ratio upgrades Off to Signalsmith") {
        ClipInfo clip = makeAudioClip();
        magda::test::audioEvent(clip).speedRatio = 1.5;
        REQUIRE(magda::test::audioEvent(clip).getEffectiveTimeStretchMode() ==
                time_stretch_mode::kSignalsmith);
    }

    SECTION("Pitch change upgrades Off to Signalsmith") {
        ClipInfo clip = makeAudioClip();
        magda::test::audioEvent(clip).pitchChange = -3.0f;
        REQUIRE(magda::test::audioEvent(clip).getEffectiveTimeStretchMode() ==
                time_stretch_mode::kSignalsmith);
    }

    SECTION("Active analog pitch keeps mode at Off (resamples, no stretch)") {
        ClipInfo clip = makeAudioClip();
        magda::test::audioEvent(clip).analogPitch = true;
        magda::test::audioEvent(clip).pitchChange = -12.0f;  // would otherwise trigger the upgrade
        REQUIRE(magda::test::audioEvent(clip).isAnalogPitchActive());
        REQUIRE(magda::test::audioEvent(clip).getEffectiveTimeStretchMode() == 0);
    }

    SECTION("Analog pitch with beat mode is not active, so still upgrades") {
        ClipInfo clip = makeAudioClip();
        magda::test::audioEvent(clip).analogPitch = true;
        magda::test::audioEvent(clip).autoTempo = true;  // autoTempo disables analog pitch in TE
        magda::test::audioEvent(clip).pitchChange = -12.0f;
        REQUIRE_FALSE(magda::test::audioEvent(clip).isAnalogPitchActive());
        REQUIRE(magda::test::audioEvent(clip).getEffectiveTimeStretchMode() ==
                time_stretch_mode::kSignalsmith);
    }

    SECTION("Explicitly chosen mode is preserved, never overridden") {
        ClipInfo clip = makeAudioClip();
        magda::test::audioEvent(clip).timeStretchMode = time_stretch_mode::kSoundTouchNormal;
        magda::test::audioEvent(clip).autoTempo = true;
        REQUIRE(magda::test::audioEvent(clip).getEffectiveTimeStretchMode() ==
                time_stretch_mode::kSoundTouchNormal);

        magda::test::audioEvent(clip).timeStretchMode = time_stretch_mode::kSoundTouchBetter;
        REQUIRE(magda::test::audioEvent(clip).getEffectiveTimeStretchMode() ==
                time_stretch_mode::kSoundTouchBetter);
    }
}

TEST_CASE("Signalsmith is the default time-stretch engine", "[audio][clip][stretch][signalsmith]") {
    namespace te = tracktion::engine;
    using namespace magda;

    STATIC_REQUIRE(static_cast<int>(te::TimeStretcher::disabled) == time_stretch_mode::kDisabled);
    STATIC_REQUIRE(static_cast<int>(te::TimeStretcher::soundtouchNormal) ==
                   time_stretch_mode::kSoundTouchNormal);
    STATIC_REQUIRE(static_cast<int>(te::TimeStretcher::soundtouchBetter) ==
                   time_stretch_mode::kSoundTouchBetter);
    STATIC_REQUIRE(static_cast<int>(te::TimeStretcher::signalsmith) ==
                   time_stretch_mode::kSignalsmith);
    STATIC_REQUIRE(te::TimeStretcher::defaultMode == te::TimeStretcher::signalsmith);

    REQUIRE(te::TimeStretcher::checkModeIsAvailable(te::TimeStretcher::signalsmith) ==
            te::TimeStretcher::signalsmith);
    REQUIRE(te::TimeStretcher::getNameOfMode(te::TimeStretcher::signalsmith) ==
            "Signalsmith Stretch");
}

TEST_CASE("Auto-tempo selects the default quality tier",
          "[audio][clip][stretch][signalsmith][auto-tempo]") {
    using namespace magda;

    auto makeAudioClip = [] {
        ClipInfo clip;
        clip.setAudioContent();
        magda::test::giveAudioEvent(clip, "test.wav");
        clip.setPlacementBeats(0.0, 8.0);
        // Beat mode is granted only with an interpretation behind it (#2676),
        // and this case is about the tier it picks once it is in.
        magda::test::audioEvent(clip).interpBpm = 120.0;
        magda::test::audioEvent(clip).interpTotalBeats = 8.0;
        return clip;
    };

    SECTION("Off upgrades to Signalsmith") {
        auto clip = makeAudioClip();
        ClipOperations::setAutoTempo(clip, true, 120.0);
        REQUIRE(magda::test::audioEvent(clip).timeStretchMode == time_stretch_mode::kSignalsmith);
    }

    SECTION("Explicit SoundTouch remains selected") {
        auto clip = makeAudioClip();
        magda::test::audioEvent(clip).timeStretchMode = time_stretch_mode::kSoundTouchNormal;
        ClipOperations::setAutoTempo(clip, true, 120.0);
        REQUIRE(magda::test::audioEvent(clip).timeStretchMode ==
                time_stretch_mode::kSoundTouchNormal);
    }

    SECTION("Explicit SoundTouch HQ remains selected") {
        auto clip = makeAudioClip();
        magda::test::audioEvent(clip).timeStretchMode = time_stretch_mode::kSoundTouchBetter;
        ClipOperations::setAutoTempo(clip, true, 120.0);
        REQUIRE(magda::test::audioEvent(clip).timeStretchMode ==
                time_stretch_mode::kSoundTouchBetter);
    }
}

TEST_CASE("Signalsmith adapter honours Tracktion's pull contract",
          "[audio][clip][stretch][signalsmith]") {
    namespace te = tracktion::engine;

    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 480;
    constexpr int sourceSamples = 12000;
    float speedRatio = 1.5f;

    juce::AudioBuffer<float> source(2, sourceSamples);
    for (int sample = 0; sample < sourceSamples; ++sample) {
        const auto value =
            std::sin(juce::MathConstants<double>::twoPi * 440.0 * sample / sampleRate);
        source.setSample(0, sample, static_cast<float>(value));
        source.setSample(1, sample, static_cast<float>(value));
    }

    juce::AudioBuffer<float> result(2, sourceSamples * 3);
    te::TimeStretcher stretcher;
    stretcher.initialise(sampleRate, blockSize, 2, te::TimeStretcher::signalsmith, {}, true);
    REQUIRE(stretcher.isInitialised());
    REQUIRE(stretcher.setSpeedAndPitch(speedRatio, 0.0f));

    int inputPosition = 0;
    int outputPosition = 0;
    double expectedOutputSamples = 0.0;
    bool changedSpeed = false;
    while (inputPosition + stretcher.getFramesNeeded() <= sourceSamples) {
        const auto framesNeeded = stretcher.getFramesNeeded();
        const float* inputs[] = {source.getReadPointer(0, inputPosition),
                                 source.getReadPointer(1, inputPosition)};
        float* outputs[] = {result.getWritePointer(0, outputPosition),
                            result.getWritePointer(1, outputPosition)};

        const auto produced = stretcher.processData(inputs, framesNeeded, outputs);
        REQUIRE(produced == blockSize);
        inputPosition += framesNeeded;
        outputPosition += produced;
        expectedOutputSamples += framesNeeded * speedRatio;

        if (!changedSpeed && inputPosition >= sourceSamples / 2) {
            speedRatio = 0.75f;
            REQUIRE(stretcher.setSpeedAndPitch(speedRatio, 0.0f));
            changedSpeed = true;
        }
    }

    for (int guard = 0; guard < 100; ++guard) {
        float* outputs[] = {result.getWritePointer(0, outputPosition),
                            result.getWritePointer(1, outputPosition)};
        const auto produced = stretcher.flush(outputs);
        if (produced == 0)
            break;
        REQUIRE(produced <= blockSize);
        outputPosition += produced;
    }

    REQUIRE(changedSpeed);
    REQUIRE(outputPosition == Catch::Approx(expectedOutputSamples).margin(2.0));
    REQUIRE(result.getRMSLevel(0, 0, outputPosition) > 0.1f);
}

TEST_CASE("Session launch de-click preserves the leading transient",
          "[audio][clip][session][transient]") {
    constexpr int numSamples = 256;
    juce::AudioBuffer<float> transient(1, numSamples);
    juce::AudioBuffer<float> baseline(1, numSamples);
    juce::AudioBuffer<float> combined(1, numSamples);

    for (int sample = 0; sample < numSamples; ++sample) {
        const auto attack =
            static_cast<float>(std::sin(juce::MathConstants<double>::twoPi * sample / 37.0) *
                               std::exp(-static_cast<double>(sample) / 80.0));
        transient.setSample(0, sample, attack);
        baseline.setSample(0, sample, 0.5f);
        combined.setSample(0, sample, 0.5f + attack);
    }

    auto baselineView = tracktion::engine::toBufferView(baseline);
    auto combinedView = tracktion::engine::toBufferView(combined);
    tracktion::engine::applyAudioStartDeClick(baselineView, numSamples);
    tracktion::engine::applyAudioStartDeClick(combinedView, numSamples);

    REQUIRE(baseline.getSample(0, 0) == Catch::Approx(0.0f).margin(1.0e-6f));
    REQUIRE(combined.getSample(0, 0) == Catch::Approx(0.0f).margin(1.0e-6f));
    REQUIRE(baseline.getSample(0, numSamples - 1) == Catch::Approx(0.5f).margin(1.0e-6f));

    for (int sample = 0; sample < numSamples; ++sample) {
        const auto preservedTransient =
            combined.getSample(0, sample) - baseline.getSample(0, sample);
        REQUIRE(preservedTransient ==
                Catch::Approx(transient.getSample(0, sample)).margin(1.0e-6f));
    }

    auto transientView = tracktion::engine::toBufferView(transient);
    const juce::AudioBuffer<float> originalTransient(transient);
    tracktion::engine::applyAudioStartDeClick(transientView, numSamples);

    for (int sample = 0; sample < numSamples; ++sample)
        REQUIRE(transient.getSample(0, sample) ==
                Catch::Approx(originalTransient.getSample(0, sample)).margin(1.0e-6f));
}

TEST_CASE("Session launch de-click continues across callback blocks",
          "[audio][clip][session][transient]") {
    namespace te = tracktion::engine;

    constexpr int numSamples = 12;
    constexpr int fadeSamples = 8;
    constexpr int blockSize = 2;
    juce::AudioBuffer<float> oneBlock(2, numSamples);

    for (int sample = 0; sample < numSamples; ++sample) {
        const auto transient =
            static_cast<float>(0.2 * std::sin(juce::MathConstants<double>::twoPi * sample / 7.0));
        oneBlock.setSample(0, sample, 0.5f + transient);
        oneBlock.setSample(1, sample, -0.25f - transient * 0.5f);
    }

    juce::AudioBuffer<float> chunked(oneBlock);
    auto oneBlockView = te::toBufferView(oneBlock);
    te::applyAudioStartDeClick(oneBlockView, fadeSamples);

    te::AudioStartDeClick deClick;
    deClick.prepare(2);
    auto chunkedView = te::toBufferView(chunked);

    for (int start = 0; start < numSamples; start += blockSize) {
        const auto end = std::min(start + blockSize, numSamples);
        auto block = chunkedView.getFrameRange({static_cast<choc::buffer::FrameCount>(start),
                                                static_cast<choc::buffer::FrameCount>(end)});

        if (start == 0)
            deClick.begin(block, fadeSamples);
        else
            deClick.process(block);
    }

    for (int channel = 0; channel < chunked.getNumChannels(); ++channel)
        for (int sample = 0; sample < numSamples; ++sample)
            REQUIRE(chunked.getSample(channel, sample) ==
                    Catch::Approx(oneBlock.getSample(channel, sample)).margin(1.0e-6f));

    const juce::AudioBuffer<float> unchanged(chunked);
    deClick.begin(chunkedView, 0);

    for (int channel = 0; channel < chunked.getNumChannels(); ++channel)
        for (int sample = 0; sample < numSamples; ++sample)
            REQUIRE(chunked.getSample(channel, sample) == unchanged.getSample(channel, sample));
}

TEST_CASE("Signalsmith preserves a transient at the start of a stream",
          "[audio][clip][stretch][signalsmith][transient]") {
    namespace te = tracktion::engine;

    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 480;
    constexpr int hitSpacing = 9600;
    constexpr int hitLength = 1600;
    constexpr int sourceSamples = hitSpacing * 5;
    constexpr float speedRatio = 1.25f;

    juce::AudioBuffer<float> source(1, sourceSamples);
    source.clear();

    for (int hit = 0; hit < 5; ++hit) {
        const auto hitStart = hit * hitSpacing;

        for (int sample = 0; sample < hitLength; ++sample) {
            const auto envelope = std::exp(-static_cast<double>(sample) / 260.0);
            const auto body =
                std::sin(juce::MathConstants<double>::twoPi * 95.0 * sample / sampleRate);
            source.setSample(0, hitStart + sample, static_cast<float>(envelope * body));
        }
    }

    juce::AudioBuffer<float> result(1, sourceSamples * 2);
    result.clear();

    te::TimeStretcher stretcher;
    stretcher.initialise(sampleRate, blockSize, 1, te::TimeStretcher::signalsmith, {}, true);
    REQUIRE(stretcher.isInitialised());
    REQUIRE(stretcher.setSpeedAndPitch(speedRatio, 0.0f));

    int inputPosition = 0;
    int outputPosition = 0;

    while (inputPosition + stretcher.getFramesNeeded() <= sourceSamples) {
        const auto framesNeeded = stretcher.getFramesNeeded();
        const float* inputs[] = {source.getReadPointer(0, inputPosition)};
        float* outputs[] = {result.getWritePointer(0, outputPosition)};

        const auto produced = stretcher.processData(inputs, framesNeeded, outputs);
        REQUIRE(produced == blockSize);
        inputPosition += framesNeeded;
        outputPosition += produced;
    }

    for (int guard = 0; guard < 100; ++guard) {
        float* outputs[] = {result.getWritePointer(0, outputPosition)};
        const auto produced = stretcher.flush(outputs);
        if (produced == 0)
            break;
        outputPosition += produced;
    }

    const auto peakAround = [&result, outputPosition](int centre) {
        const auto start = std::max(0, centre - 1000);
        const auto end =
            std::min(outputPosition, centre + juce::roundToInt(hitLength * speedRatio) + 1000);
        return result.getMagnitude(0, start, end - start);
    };

    const auto firstPeak = peakAround(0);
    float laterPeak = 0.0f;
    for (int hit = 1; hit < 4; ++hit)
        laterPeak += peakAround(juce::roundToInt(hit * hitSpacing * speedRatio));
    laterPeak /= 3.0f;

    const auto attackRms = [&result](int start) { return result.getRMSLevel(0, start, blockSize); };
    const auto firstAttackRms = attackRms(0);
    float laterAttackRms = 0.0f;
    for (int hit = 1; hit < 4; ++hit)
        laterAttackRms += attackRms(juce::roundToInt(hit * hitSpacing * speedRatio));
    laterAttackRms /= 3.0f;

    CAPTURE(firstPeak, laterPeak, firstAttackRms, laterAttackRms, outputPosition);
    REQUIRE(firstPeak >= laterPeak * 0.9f);
    REQUIRE(firstAttackRms >= laterAttackRms * 0.9f);
}

TEST_CASE("ClipOperations - stretchAudioFromLeft right edge anchoring",
          "[audio][clip][stretch][regression]") {
    using namespace magda;
    // stretchAudioFromLeft takes timeline seconds; at 120 BPM one second is two beats.
    constexpr double kBpm = 120.0;

    SECTION("Multiple stretch events maintain fixed right edge") {
        ClipInfo clip;
        clip.setAudioContent();
        magda::test::giveAudioEvent(clip, "test.wav");
        magda::test::audioEvent(clip).setAnchorSeconds(0.0);
        clip.setPlacementBeats(20.0, 10.0);
        magda::test::audioEvent(clip).speedRatio = 1.0;

        const double expectedEndBeat = 30.0;

        // Capture original values at "mouseDown"
        double originalLength = clip.getTimelineLength(kBpm);
        double originalStretchFactor = magda::test::audioEvent(clip).speedRatio;
        REQUIRE(originalLength == Catch::Approx(5.0));

        // Simulate drag event 1: stretch to 6.0 seconds
        ClipOperations::stretchAudioFromLeft(clip, 6.0, originalLength, originalStretchFactor,
                                             kBpm);

        REQUIRE(clip.placement.endBeat() == Catch::Approx(expectedEndBeat));
        REQUIRE(clip.placement.startBeat == Catch::Approx(18.0));
        REQUIRE(clip.placement.lengthBeats == Catch::Approx(12.0));
        REQUIRE(magda::test::audioEvent(clip).speedRatio ==
                Catch::Approx(1.0 / 1.2));  // 1.0 / (6.0 / 5.0) = 5.0 / 6.0

        // Simulate drag event 2: stretch to 7.0 seconds (more stretching)
        ClipOperations::stretchAudioFromLeft(clip, 7.0, originalLength, originalStretchFactor,
                                             kBpm);

        REQUIRE(clip.placement.endBeat() == Catch::Approx(expectedEndBeat));
        REQUIRE(clip.placement.startBeat == Catch::Approx(16.0));
        REQUIRE(clip.placement.lengthBeats == Catch::Approx(14.0));
        REQUIRE(magda::test::audioEvent(clip).speedRatio ==
                Catch::Approx(1.0 / 1.4));  // 1.0 / (7.0 / 5.0) = 5.0 / 7.0

        // Simulate drag event 3: compress to 4.0 seconds (user dragged right)
        ClipOperations::stretchAudioFromLeft(clip, 4.0, originalLength, originalStretchFactor,
                                             kBpm);

        REQUIRE(clip.placement.endBeat() == Catch::Approx(expectedEndBeat));
        REQUIRE(clip.placement.startBeat == Catch::Approx(22.0));
        REQUIRE(clip.placement.lengthBeats == Catch::Approx(8.0));
        REQUIRE(magda::test::audioEvent(clip).speedRatio ==
                Catch::Approx(1.0 / 0.8));  // 1.0 / (4.0 / 5.0) = 5.0 / 4.0 = 1.25

        // Simulate drag event 4: back to original length
        ClipOperations::stretchAudioFromLeft(clip, 5.0, originalLength, originalStretchFactor,
                                             kBpm);

        REQUIRE(clip.placement.endBeat() == Catch::Approx(expectedEndBeat));
        REQUIRE(clip.placement.startBeat == Catch::Approx(20.0));
        REQUIRE(clip.placement.lengthBeats == Catch::Approx(10.0));
        REQUIRE(magda::test::audioEvent(clip).speedRatio == Catch::Approx(1.0));
    }

    SECTION("Stretch factor clamping doesn't break right edge anchoring") {
        ClipInfo clip;
        clip.setAudioContent();
        magda::test::giveAudioEvent(clip, "test.wav");
        magda::test::audioEvent(clip).setAnchorSeconds(0.0);
        clip.setPlacementBeats(10.0, 4.0);
        magda::test::audioEvent(clip).speedRatio = 1.0;

        double originalLength = clip.getTimelineLength(kBpm);
        double originalStretchFactor = magda::test::audioEvent(clip).speedRatio;

        // Try to stretch to 10.0 s (5.0x ratio). The requested speed would clamp at the
        // minimum speed, but keeping the right edge fixed must not push the clip before
        // the timeline origin.
        ClipOperations::stretchAudioFromLeft(clip, 10.0, originalLength, originalStretchFactor,
                                             kBpm);

        REQUIRE(magda::test::audioEvent(clip).speedRatio == Catch::Approx(2.0 / 7.0));
        REQUIRE(clip.placement.startBeat == Catch::Approx(0.0));
        REQUIRE(clip.placement.lengthBeats == Catch::Approx(14.0));
        REQUIRE(clip.placement.endBeat() == Catch::Approx(14.0));
    }

    SECTION("Stretch with pre-stretched audio maintains correct calculations") {
        ClipInfo clip;
        clip.setAudioContent();
        magda::test::giveAudioEvent(clip, "test.wav");
        magda::test::audioEvent(clip).setAnchorSeconds(0.0);
        clip.setPlacementBeats(40.0, 20.0);
        magda::test::audioEvent(clip).speedRatio = 2.0;  // Already stretched 2x

        double originalLength = clip.getTimelineLength(kBpm);
        double originalStretchFactor = magda::test::audioEvent(clip).speedRatio;

        // Stretch from 10.0 s to 15.0 s (1.5x stretch on top of existing 2.0x)
        ClipOperations::stretchAudioFromLeft(clip, 15.0, originalLength, originalStretchFactor,
                                             kBpm);

        // New stretch factor: 2.0 / (15.0 / 10.0) = 2.0 / 1.5 = 1.333...
        REQUIRE(magda::test::audioEvent(clip).speedRatio == Catch::Approx(2.0 / 1.5));
        REQUIRE(clip.placement.lengthBeats == Catch::Approx(30.0));

        // Right edge still anchored at beat 60
        REQUIRE(clip.placement.endBeat() == Catch::Approx(60.0));
        REQUIRE(clip.placement.startBeat == Catch::Approx(30.0));
    }
}
