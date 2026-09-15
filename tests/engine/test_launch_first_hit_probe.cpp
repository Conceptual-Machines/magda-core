// A measurement for #2682, hidden: how loud is a launched loop's first hit?
//
//   ./magda_tests "[.launch-probe]" --success
//
// The corpus's impulse launch case, rendered natively with the launch on the
// render's first beat and inside a later block, with and without the launch
// ramp, reading the amplitude of every impulse in the first pass and the next.

#include <juce_audio_basics/juce_audio_basics.h>

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstdio>

#include "../NullDiffCase.hpp"
#include "../NullDiffNativeLeg.hpp"

using namespace magda;
using namespace magda::nulldiff;

namespace {

juce::File scratch() {
    auto root = juce::File::getSpecialLocation(juce::File::tempDirectory)
                    .getChildFile("magda_launch_probe");
    root.createDirectory();
    return root;
}

/// Peak within a few samples of @p at, and where it was.
std::pair<float, int> peakNear(const juce::AudioBuffer<float>& audio, int at) {
    float best = 0.0f;
    int where = at;
    for (int i = std::max(0, at - 8); i < std::min(audio.getNumSamples(), at + 9); ++i) {
        const float v = std::abs(audio.getSample(0, i));
        if (v > best) {
            best = v;
            where = i;
        }
    }
    return {best, where - at};
}

}  // namespace

TEST_CASE("How loud is a launched loop's first hit", "[.launch-probe]") {
    const auto& corpus = sharedCorpus(scratch());
    const auto it = std::find_if(corpus.begin(), corpus.end(),
                                 [](const Case& c) { return c.name == "session.launch"; });
    REQUIRE(it != corpus.end());

    for (const double launchBeat : {0.0, 1.3, 4.0}) {
        for (const int fade : {256, 0}) {
            Case value = *it;
            value.endBeat = 40.0;  // two passes of the 16-beat slot
            value.launches.front().beat = launchBeat;
            for (auto& clip : value.clips)
                clip.launchFadeSamples = fade;

            const auto render = renderNative(value);
            REQUIRE(render.failure.empty());

            const double secondsPerBeat = 60.0 / value.tempo.front().bpm;
            const int runStart =
                static_cast<int>(std::floor(launchBeat * secondsPerBeat * value.sampleRate));
            const int impulseEvery = static_cast<int>(0.25 * value.sampleRate);
            const int cycle = static_cast<int>(16.0 * secondsPerBeat * value.sampleRate);

            std::printf("launch beat %.1f  fade %d  run starts at sample %d\n", launchBeat, fade,
                        runStart);
            for (int pass = 0; pass < 2; ++pass) {
                std::printf("  pass %d:", pass + 1);
                for (int k = 0; k < 4; ++k) {
                    const auto [peak, offset] =
                        peakNear(render.audio, runStart + pass * cycle + k * impulseEvery);
                    std::printf("  hit%d %.3f@%+d", k, peak, offset);
                }
                std::printf("\n");
            }
        }
    }
}
