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

    SECTION("Default stretch factor is 1.0") {
        ClipInfo clip;
        clip.setAudioContent();
        magda::test::giveAudioEvent(clip, "test.wav");
        clip.length = 4.0;
        magda::test::audioEvent(clip).speedRatio = 1.0;

        // File window equals length when stretch factor is 1.0
        double fileWindow = clip.length * magda::test::audioEvent(clip).speedRatio;
        REQUIRE(fileWindow == 4.0);
    }

    SECTION("Stretch factor affects file time window") {
        ClipInfo clip;
        clip.setAudioContent();
        magda::test::giveAudioEvent(clip, "test.wav");
        magda::test::audioEvent(clip).setAnchorSeconds(0.0);
        clip.length = 4.0;
        magda::test::audioEvent(clip).speedRatio = 2.0;  // 2x faster

        // File window is double the length when 2x faster
        double fileWindow = clip.length * magda::test::audioEvent(clip).speedRatio;
        REQUIRE(fileWindow == 8.0);

        // Reading from file offset 0-8, displaying as 0-4 seconds
    }

    SECTION("Stretch factor 0.5 = 2x slower") {
        ClipInfo clip;
        clip.setAudioContent();
        magda::test::giveAudioEvent(clip, "test.wav");
        magda::test::audioEvent(clip).setAnchorSeconds(0.0);
        clip.length = 8.0;
        magda::test::audioEvent(clip).speedRatio = 0.5;  // 2x slower

        // File window is half the length when 2x slower
        double fileWindow = clip.length * magda::test::audioEvent(clip).speedRatio;
        REQUIRE(fileWindow == 4.0);

        // Reading from file offset 0-4, displaying as 0-8 seconds
    }
}

TEST_CASE("ClipManager - setSpeedRatio clamping", "[audio][clip][stretch]") {
    using namespace magda;

    // Reset and setup
    ClipManager::getInstance().shutdown();

    SECTION("Stretch factor clamped to [0.25, 4.0] range") {
        ClipId clipId = ClipManager::getInstance().createAudioClip(1, 0.0, 4.0, "test.wav");
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

    ClipManager::getInstance().shutdown();

    SECTION("Trim from left advances file offset (audio at clip start)") {
        // Create audio clip: starts at 0, length 4.0
        ClipId clipId = ClipManager::getInstance().createAudioClip(1, 0.0, 4.0, "test.wav");
        auto* clip = ClipManager::getInstance().getClip(clipId);
        REQUIRE(clip != nullptr);

        primaryEventOf(clip)->setAnchorSeconds(0.0);
        primaryEventOf(clip)->speedRatio = 1.0;

        // Trim from left by 1.0 seconds
        ClipManager::getInstance().resizeClip(clipId, 3.0, true);

        // Clip moved right by 1.0 second
        REQUIRE(clip->startTime == 1.0);
        REQUIRE(clip->length == 3.0);

        // Audio offset advanced by 1.0 second
        REQUIRE(primaryEventOf(clip)->anchorSeconds() == Catch::Approx(1.0));
    }

    SECTION("Trim with stretch factor converts to file time") {
        ClipId clipId = ClipManager::getInstance().createAudioClip(1, 0.0, 4.0, "test.wav");
        auto* clip = ClipManager::getInstance().getClip(clipId);

        primaryEventOf(clip)->setAnchorSeconds(0.0);
        primaryEventOf(clip)->speedRatio = 2.0;  // 2x faster, file window = 8.0

        // Trim from left by 2.0 timeline seconds
        ClipManager::getInstance().resizeClip(clipId, 2.0, true);

        REQUIRE(clip->startTime == 2.0);
        REQUIRE(clip->length == 2.0);

        // File trim amount = 2.0 * 2.0 = 4.0 file seconds
        REQUIRE(primaryEventOf(clip)->anchorSeconds() == Catch::Approx(4.0));
    }
}

TEST_CASE("Audio Clip - Right edge resize doesn't change offset", "[audio][clip][resize]") {
    using namespace magda;

    ClipManager::getInstance().shutdown();

    SECTION("Right edge resize only changes length") {
        ClipId clipId = ClipManager::getInstance().createAudioClip(1, 0.0, 4.0, "test.wav");
        auto* clip = ClipManager::getInstance().getClip(clipId);

        primaryEventOf(clip)->setAnchorSeconds(1.0);

        // Resize from right edge
        ClipManager::getInstance().resizeClip(clipId, 6.0, false);

        REQUIRE(clip->startTime == 0.0);
        REQUIRE(clip->length == 6.0);

        // Audio offset unchanged
        REQUIRE(primaryEventOf(clip)->anchorSeconds() == 1.0);
    }
}

TEST_CASE("Audio Clip - Stretch maintains file window", "[audio][clip][stretch]") {
    using namespace magda;

    ClipManager::getInstance().shutdown();

    SECTION("Stretching by 2x halves length but file window stays same") {
        ClipId clipId = ClipManager::getInstance().createAudioClip(1, 0.0, 4.0, "test.wav");
        auto* clip = ClipManager::getInstance().getClip(clipId);

        primaryEventOf(clip)->setAnchorSeconds(0.0);
        primaryEventOf(clip)->speedRatio = 1.0;

        double originalFileWindow = clip->length * primaryEventOf(clip)->speedRatio;
        REQUIRE(originalFileWindow == 4.0);

        // Stretch 2x slower: length becomes 8, stretch factor becomes 0.5
        clip->length = 8.0;
        ClipManager::getInstance().setSpeedRatio(clipId, 0.5);

        double newFileWindow = clip->length * primaryEventOf(clip)->speedRatio;
        REQUIRE(newFileWindow == Catch::Approx(originalFileWindow));
    }

    SECTION("Compressing by 2x halves length but file window stays same") {
        ClipId clipId = ClipManager::getInstance().createAudioClip(1, 0.0, 4.0, "test.wav");
        auto* clip = ClipManager::getInstance().getClip(clipId);

        primaryEventOf(clip)->setAnchorSeconds(1.0);
        primaryEventOf(clip)->speedRatio = 1.0;

        double originalFileWindow = clip->length * primaryEventOf(clip)->speedRatio;
        REQUIRE(originalFileWindow == 4.0);

        // Compress 2x faster: length becomes 2, stretch factor becomes 2.0
        clip->length = 2.0;
        ClipManager::getInstance().setSpeedRatio(clipId, 2.0);

        double newFileWindow = clip->length * primaryEventOf(clip)->speedRatio;
        REQUIRE(newFileWindow == Catch::Approx(originalFileWindow));

        // File offset unchanged
        REQUIRE(primaryEventOf(clip)->anchorSeconds() == 1.0);
    }
}

TEST_CASE("Audio Clip - Analog pitch resamples instead of time-stretching",
          "[audio][clip][pitch][analog]") {
    using namespace magda;

    ClipManager::getInstance().shutdown();

    SECTION("Pitch down slows playback and grows timeline length") {
        ClipId clipId = ClipManager::getInstance().createAudioClip(1, 0.0, 2.0, "test.wav");
        auto* clip = ClipManager::getInstance().getClip(clipId);
        REQUIRE(clip != nullptr);

        primaryEventOf(clip)->speedRatio = 1.0;
        clip->length = 2.0;
        clip->setPlacementBeats(0.0, 4.0);

        ClipManager::getInstance().setAnalogPitch(clipId, true);
        ClipManager::getInstance().setPitchChange(clipId, -12.0f);

        REQUIRE(primaryEventOf(clip)->analogPitch);
        REQUIRE(primaryEventOf(clip)->speedRatio == Catch::Approx(0.5));
        REQUIRE(clip->length == Catch::Approx(4.0));
        REQUIRE(clip->lengthBeats == Catch::Approx(8.0));
        REQUIRE(primaryEventOf(clip)->timelineToSource(clip->length) == Catch::Approx(2.0));
    }

    SECTION("Pitch up speeds playback and shrinks timeline length") {
        ClipId clipId = ClipManager::getInstance().createAudioClip(1, 0.0, 2.0, "test.wav");
        auto* clip = ClipManager::getInstance().getClip(clipId);
        REQUIRE(clip != nullptr);

        primaryEventOf(clip)->speedRatio = 1.0;
        clip->length = 2.0;
        clip->setPlacementBeats(0.0, 4.0);

        ClipManager::getInstance().setAnalogPitch(clipId, true);
        ClipManager::getInstance().setPitchChange(clipId, 12.0f);

        REQUIRE(primaryEventOf(clip)->speedRatio == Catch::Approx(2.0));
        REQUIRE(clip->length == Catch::Approx(1.0));
        REQUIRE(clip->lengthBeats == Catch::Approx(2.0));
        REQUIRE(primaryEventOf(clip)->timelineToSource(clip->length) == Catch::Approx(2.0));
    }
}

TEST_CASE("Audio Clip - Real-world scenario: Amen break trim", "[audio][clip][integration]") {
    using namespace magda;

    ClipManager::getInstance().shutdown();

    SECTION("Trim amen break from left preserves timeline positions") {
        // Amen break: ~4.5 bars at given BPM = 9 seconds
        constexpr double kBPM = 120.0;
        constexpr double kSecondsPerBeat = 60.0 / kBPM;  // 0.5s at 120 BPM
        juce::ignoreUnused(kSecondsPerBeat);

        ClipId clipId = ClipManager::getInstance().createAudioClip(1, 0.0, 9.0, "amen.wav");
        auto* clip = ClipManager::getInstance().getClip(clipId);

        primaryEventOf(clip)->setAnchorSeconds(0.0);
        primaryEventOf(clip)->speedRatio = 1.0;

        // Trim from left by 1.0 second (to bar 1.3, where first snare is)
        ClipManager::getInstance().resizeClip(clipId, 8.0, true);

        // Clip now starts at 1.0s
        REQUIRE(clip->startTime == 1.0);
        REQUIRE(clip->length == 8.0);

        // Audio offset advanced to 1.0s (skipping first bar)
        REQUIRE(primaryEventOf(clip)->anchorSeconds() == Catch::Approx(1.0));
    }

    SECTION("Trim stretched amen break converts to file time") {
        // Amen break stretched 2x slower: 18 seconds timeline duration
        ClipId clipId = ClipManager::getInstance().createAudioClip(1, 0.0, 18.0, "amen.wav");
        auto* clip = ClipManager::getInstance().getClip(clipId);

        primaryEventOf(clip)->setAnchorSeconds(0.0);
        primaryEventOf(clip)->speedRatio = 0.5;  // 2x slower, file window = 9.0s

        // Trim from left by 2.0 timeline seconds (to first snare)
        ClipManager::getInstance().resizeClip(clipId, 16.0, true);

        REQUIRE(clip->startTime == 2.0);
        REQUIRE(clip->length == 16.0);

        // File trim amount = 2.0 * 0.5 = 1.0 file seconds
        REQUIRE(primaryEventOf(clip)->anchorSeconds() == Catch::Approx(1.0));
    }
}

TEST_CASE("Audio Clip - Edge cases", "[audio][clip][edge]") {
    using namespace magda;

    ClipManager::getInstance().shutdown();

    SECTION("Minimum clip length enforced") {
        ClipId clipId = ClipManager::getInstance().createAudioClip(1, 0.0, 4.0, "test.wav");
        auto* clip = ClipManager::getInstance().getClip(clipId);

        // Try to resize to very small length
        ClipManager::getInstance().resizeClip(clipId, 0.01, false);

        // Clamped to minimum 0.1
        REQUIRE(clip->length == Catch::Approx(0.1));
    }

    SECTION("Trim to zero start time") {
        ClipId clipId = ClipManager::getInstance().createAudioClip(1, 1.0, 4.0, "test.wav");
        auto* clip = ClipManager::getInstance().getClip(clipId);

        // Resize from left past zero
        ClipManager::getInstance().resizeClip(clipId, 6.0, true);

        // Start time clamped to zero
        REQUIRE(clip->startTime == 0.0);
        REQUIRE(clip->length == 6.0);
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
        clip.length = 4.0;
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

    SECTION("Multiple stretch events maintain fixed right edge") {
        ClipInfo clip;
        clip.setAudioContent();
        magda::test::giveAudioEvent(clip, "test.wav");
        magda::test::audioEvent(clip).setAnchorSeconds(0.0);
        clip.startTime = 10.0;
        clip.length = 5.0;
        magda::test::audioEvent(clip).speedRatio = 1.0;

        // Calculate expected right edge (should never change)
        double expectedRightEdge = 10.0 + 5.0;  // 15.0
        REQUIRE(expectedRightEdge == 15.0);

        // Capture original values at "mouseDown"
        double originalLength = clip.length;
        double originalStretchFactor = magda::test::audioEvent(clip).speedRatio;

        // Simulate drag event 1: stretch to 6.0 seconds
        ClipOperations::stretchAudioFromLeft(clip, 6.0, originalLength, originalStretchFactor);

        double rightEdge1 = clip.startTime + clip.length;
        REQUIRE(rightEdge1 == Catch::Approx(expectedRightEdge));
        REQUIRE(clip.startTime == Catch::Approx(9.0));  // 15.0 - 6.0
        REQUIRE(clip.length == Catch::Approx(6.0));
        REQUIRE(magda::test::audioEvent(clip).speedRatio ==
                Catch::Approx(1.0 / 1.2));  // 1.0 / (6.0 / 5.0) = 5.0 / 6.0

        // Simulate drag event 2: stretch to 7.0 seconds (more stretching)
        ClipOperations::stretchAudioFromLeft(clip, 7.0, originalLength, originalStretchFactor);

        double rightEdge2 = clip.startTime + clip.length;
        REQUIRE(rightEdge2 == Catch::Approx(expectedRightEdge));  // Still 15.0!
        REQUIRE(clip.startTime == Catch::Approx(8.0));            // 15.0 - 7.0
        REQUIRE(clip.length == Catch::Approx(7.0));
        REQUIRE(magda::test::audioEvent(clip).speedRatio ==
                Catch::Approx(1.0 / 1.4));  // 1.0 / (7.0 / 5.0) = 5.0 / 7.0

        // Simulate drag event 3: compress to 4.0 seconds (user dragged right)
        ClipOperations::stretchAudioFromLeft(clip, 4.0, originalLength, originalStretchFactor);

        double rightEdge3 = clip.startTime + clip.length;
        REQUIRE(rightEdge3 == Catch::Approx(expectedRightEdge));  // Still 15.0!
        REQUIRE(clip.startTime == Catch::Approx(11.0));           // 15.0 - 4.0
        REQUIRE(clip.length == Catch::Approx(4.0));
        REQUIRE(magda::test::audioEvent(clip).speedRatio ==
                Catch::Approx(1.0 / 0.8));  // 1.0 / (4.0 / 5.0) = 5.0 / 4.0 = 1.25

        // Simulate drag event 4: back to original length
        ClipOperations::stretchAudioFromLeft(clip, 5.0, originalLength, originalStretchFactor);

        double rightEdge4 = clip.startTime + clip.length;
        REQUIRE(rightEdge4 == Catch::Approx(expectedRightEdge));  // Still 15.0!
        REQUIRE(clip.startTime == Catch::Approx(10.0));           // Back to original
        REQUIRE(clip.length == Catch::Approx(originalLength));    // Back to 5.0
        REQUIRE(magda::test::audioEvent(clip).speedRatio == Catch::Approx(1.0));  // Back to 1.0
    }

    SECTION("Stretch factor clamping doesn't break right edge anchoring") {
        ClipInfo clip;
        clip.setAudioContent();
        magda::test::giveAudioEvent(clip, "test.wav");
        magda::test::audioEvent(clip).setAnchorSeconds(0.0);
        clip.startTime = 5.0;
        clip.length = 2.0;
        magda::test::audioEvent(clip).speedRatio = 1.0;

        double expectedRightEdge = 5.0 + 2.0;  // 7.0
        double originalLength = clip.length;
        double originalStretchFactor = magda::test::audioEvent(clip).speedRatio;

        // Try to stretch to 10.0 (5.0x ratio). The requested speed would clamp at the
        // minimum speed, but keeping the right edge fixed must not push the clip before
        // the timeline origin.
        ClipOperations::stretchAudioFromLeft(clip, 10.0, originalLength, originalStretchFactor);

        REQUIRE(magda::test::audioEvent(clip).speedRatio == Catch::Approx(2.0 / 7.0));
        REQUIRE(clip.startTime == Catch::Approx(0.0));
        REQUIRE(clip.length == Catch::Approx(7.0));

        // Right edge maintained
        double rightEdge = clip.startTime + clip.length;
        REQUIRE(rightEdge == Catch::Approx(expectedRightEdge));
    }

    SECTION("Stretch with pre-stretched audio maintains correct calculations") {
        ClipInfo clip;
        clip.setAudioContent();
        magda::test::giveAudioEvent(clip, "test.wav");
        magda::test::audioEvent(clip).setAnchorSeconds(0.0);
        clip.startTime = 20.0;
        clip.length = 10.0;
        magda::test::audioEvent(clip).speedRatio = 2.0;  // Already stretched 2x

        double expectedRightEdge = 20.0 + 10.0;  // 30.0
        double originalLength = clip.length;
        double originalStretchFactor = magda::test::audioEvent(clip).speedRatio;

        // Stretch from 10.0 to 15.0 (1.5x stretch on top of existing 2.0x)
        ClipOperations::stretchAudioFromLeft(clip, 15.0, originalLength, originalStretchFactor);

        // New stretch factor: 2.0 / (15.0 / 10.0) = 2.0 / 1.5 = 1.333...
        REQUIRE(magda::test::audioEvent(clip).speedRatio == Catch::Approx(2.0 / 1.5));
        REQUIRE(clip.length == Catch::Approx(15.0));

        // Right edge still anchored
        double rightEdge = clip.startTime + clip.length;
        REQUIRE(rightEdge == Catch::Approx(expectedRightEdge));
        REQUIRE(clip.startTime == Catch::Approx(15.0));  // 30.0 - 15.0
    }
}
