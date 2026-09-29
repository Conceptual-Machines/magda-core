#include <juce_core/juce_core.h>

#include <cmath>

#include "AssertionWatch.hpp"
#include "NullDiffNativeLeg.hpp"

using namespace magda;
using namespace magda::nulldiff;

class TrimmedSessionLaunchTests : public juce::UnitTest {
  public:
    TrimmedSessionLaunchTests() : juce::UnitTest("Trimmed Session Launch", "magda") {}

    void runTest() override {
        beginTest("A render-start launch corrects a trimmed steady source");
        expectTrimmedLaunch(buildTrimmedSessionLaunchCase(scratchDirectory()), 0);

        beginTest("Correction carries across callback blocks");
        {
            auto value = buildTrimmedSessionLaunchCase(scratchDirectory());
            value.blockSize = 64;
            expectTrimmedLaunch(value, 0);
        }

        beginTest("A launch inside a block corrects a trimmed steady source");
        {
            auto value = buildTrimmedSessionLaunchCase(scratchDirectory());
            value.launches.front().beat = 1.0;
            const auto launchSample =
                static_cast<int>(std::llround(value.sampleRate * 60.0 / value.startBpm()));
            expectTrimmedLaunch(value, launchSample);
        }

        beginTest("A zero correction length bypasses launch shaping");
        {
            auto value = buildTrimmedSessionLaunchCase(scratchDirectory());
            value.clips.front().launchFadeSamples = 0;
            expectTrimmedLaunch(value, 0);
        }

        beginTest("An untrimmed source attack is preserved");
        expectUntrimmedAttack(buildUntrimmedSessionAttackCase(scratchDirectory()));

        beginTest("A trimmed arrangement clip is not launch-shaped");
        expectUnshapedTrim(buildTrimmedArrangementCase(scratchDirectory()));

        beginTest("A reversed launch at its directional source edge preserves the attack");
        {
            auto value = buildTrimmedSessionLaunchCase(scratchDirectory());
            auto& clip = value.clips.front();
            clip.setPlacementBeats(0.0, 24.0);
            auto& event = *clip.primaryEvent();
            event.sourceAnchorSamples = 0;
            event.reversed = true;
            expectUnshapedTrim(value);
        }

        beginTest("A shortened reversed launch corrects its interior source edge");
        {
            auto value = buildTrimmedSessionLaunchCase(scratchDirectory());
            auto& event = *value.clips.front().primaryEvent();
            event.sourceAnchorSamples = 0;
            event.reversed = true;
            expectTrimmedLaunch(value, 0);
        }

        beginTest("A whole-loop phase at source zero preserves the source edge");
        {
            auto value = buildTrimmedSessionLaunchCase(scratchDirectory());
            auto& clip = value.clips.front();
            clip.loopEnabled = true;
            auto& event = *clip.primaryEvent();
            event.setLoopStartSeconds(0.0);
            event.setLoopLengthSeconds(1.0);
            event.setAnchorSeconds(1.0);
            expectUnshapedTrim(value);
        }

        beginTest("A loop whose mapped phase starts inside the source is corrected");
        {
            auto value = buildTrimmedSessionLaunchCase(scratchDirectory());
            auto& clip = value.clips.front();
            clip.loopEnabled = true;
            auto& event = *clip.primaryEvent();
            event.setLoopStartSeconds(1.0);
            event.setLoopLengthSeconds(1.0);
            event.setAnchorSeconds(0.0);
            expectTrimmedLaunch(value, 0);
        }
    }

  private:
    static juce::File scratchDirectory() {
        auto directory = juce::File::getSpecialLocation(juce::File::tempDirectory)
                             .getChildFile("magda_native_launch_waveforms");
        directory.createDirectory();
        return directory;
    }

    template <typename Expected>
    void expectWaveform(const juce::AudioBuffer<float>& audio, Expected&& expected,
                        const juce::String& engine, int samples = -1) {
        expect(audio.getNumChannels() > 0, engine + ": no channels");
        expect(audio.getNumSamples() > 0, engine + ": empty render");
        if (audio.getNumChannels() == 0 || audio.getNumSamples() == 0)
            return;

        if (samples >= 0) {
            expect(audio.getNumSamples() >= samples, engine + ": expected at least " +
                                                         juce::String(samples) + " samples, got " +
                                                         juce::String(audio.getNumSamples()));
            if (audio.getNumSamples() < samples)
                return;
        }

        auto maxError = 0.0f;
        auto worstSample = 0;
        auto worstChannel = 0;
        const auto compared =
            samples < 0 ? audio.getNumSamples() : std::min(samples, audio.getNumSamples());
        for (auto channel = 0; channel < audio.getNumChannels(); ++channel) {
            for (auto sample = 0; sample < compared; ++sample) {
                const auto actual = audio.getSample(channel, sample);
                const auto error = std::isfinite(actual) ? std::abs(actual - expected(sample))
                                                         : std::numeric_limits<float>::infinity();
                if (error > maxError) {
                    maxError = error;
                    worstSample = sample;
                    worstChannel = channel;
                }
            }
        }

        expect(maxError <= 1.0e-6f,
               engine + ": max error " + juce::String(maxError, 9) + " at sample " +
                   juce::String(worstSample) + " channel " + juce::String(worstChannel) +
                   ", expected " + juce::String(expected(worstSample), 9) + ", got " +
                   juce::String(audio.getSample(worstChannel, worstSample), 9));
    }

    void expectTrimmedLaunch(const Case& value, int launchSample) {
        const auto length = value.clips.front().launchFadeSamples;
        const auto expected = [launchSample, length](int sample) {
            if (sample < launchSample)
                return 0.0f;

            const auto elapsed = sample - launchSample;
            if (length == 0 || elapsed >= length)
                return 0.5f;
            if (length == 1)
                return 0.0f;

            const auto phase = static_cast<float>(elapsed) / static_cast<float>(length - 1);
            const auto correction =
                0.5f * (1.0f + std::cos(juce::MathConstants<float>::pi * phase));
            return 0.5f * (1.0f - correction);
        };
        renderAndExpect(value, expected);
    }

    void expectUntrimmedAttack(const Case& value) {
        renderAndExpect(
            value, [](int sample) { return sample == 0 ? 1.0f : 0.0f; }, 2);
    }

    void expectUnshapedTrim(const Case& value) {
        renderAndExpect(value, [](int) { return 0.5f; });
    }

    template <typename Expected>
    void renderAndExpect(const Case& value, Expected&& expected, int samples = -1) {
        auto& assertionWatch = magda::test::AssertionWatch::instance();
        assertionWatch.take();

        const auto native = renderNative(value);
        const auto assertions = assertionWatch.take();
        for (const auto& assertion : assertions)
            expect(false, "asserted while rendering: " + assertion);

        expect(native.failure.empty(), "native render: " + juce::String(native.failure));
        expect(native.diagnostics.empty(), "native render produced diagnostics");
        expect(native.starvedVoices == 0,
               "native render starved " + juce::String(native.starvedVoices) + " voices");
        expect(native.droppedMidiEvents == 0,
               "native render dropped " + juce::String(native.droppedMidiEvents) + " MIDI events");
        if (!native.failure.empty())
            return;

        const auto expectedSamples = static_cast<int>(std::llround(
            (value.endBeat - value.startBeat) * 60.0 / value.startBpm() * value.sampleRate));
        expect(native.audio.getNumSamples() == expectedSamples,
               "native render length " + juce::String(native.audio.getNumSamples()) +
                   ", expected " + juce::String(expectedSamples));

        expectWaveform(native.audio, expected, "native", samples);
    }
};

static TrimmedSessionLaunchTests trimmedSessionLaunchTests;
