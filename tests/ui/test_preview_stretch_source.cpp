#include <juce_audio_basics/juce_audio_basics.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <numbers>

#include "../../magda/daw/ui/components/common/PreviewStretchSource.hpp"

namespace {

constexpr double kRate = 48000.0;
constexpr int kBlock = 512;

juce::AudioBuffer<float> sine(double hz, double seconds) {
    const int n = static_cast<int>(seconds * kRate);
    juce::AudioBuffer<float> buf(2, n);
    for (int i = 0; i < n; ++i) {
        const auto v = static_cast<float>(
            0.5 * std::sin(2.0 * std::numbers::pi * hz * static_cast<double>(i) / kRate));
        buf.setSample(0, i, v);
        buf.setSample(1, i, v);
    }
    return buf;
}

/** @brief Render @p blocks blocks of @p src into one buffer. */
juce::AudioBuffer<float> render(magda::PreviewStretchSource& src, int blocks) {
    juce::AudioBuffer<float> out(2, blocks * kBlock);
    for (int b = 0; b < blocks; ++b) {
        juce::AudioSourceChannelInfo info(&out, b * kBlock, kBlock);
        src.getNextAudioBlock(info);
    }
    return out;
}

/** @brief Frequency from rising zero crossings over channel 0 of [start, end). */
double measuredHz(const juce::AudioBuffer<float>& buf, int start, int end) {
    int crossings = 0;
    int first = -1;
    int last = -1;
    for (int i = start + 1; i < end; ++i) {
        if (buf.getSample(0, i - 1) < 0.0f && buf.getSample(0, i) >= 0.0f) {
            if (first < 0)
                first = i;
            last = i;
            ++crossings;
        }
    }
    if (crossings < 2)
        return 0.0;
    return (crossings - 1) * kRate / (last - first);
}

}  // namespace

TEST_CASE("PreviewStretchSource passes the file through at rate 1", "[preview][stretch]") {
    auto material = sine(440.0, 1.0);
    juce::MemoryAudioSource input(material, false);
    magda::PreviewStretchSource src(&input, 2);
    src.prepareToPlay(kBlock, kRate);

    const auto out = render(src, 8);
    for (int i = 0; i < out.getNumSamples(); ++i)
        REQUIRE(out.getSample(0, i) == material.getSample(0, i));
    REQUIRE(src.getNextReadPosition() == 8 * kBlock);
}

TEST_CASE("PreviewStretchSource keeps pitch and reports the audible position",
          "[preview][stretch]") {
    auto material = sine(440.0, 4.0);
    juce::MemoryAudioSource input(material, false);
    magda::PreviewStretchSource src(&input, 2);
    src.prepareToPlay(kBlock, kRate);

    SECTION("twice as fast") {
        src.setRate(2.0);
        src.setNextReadPosition(0);
        const auto out = render(src, 40);
        REQUIRE(src.getNextReadPosition() == Catch::Approx(2.0 * 40 * kBlock).margin(2.0));
        REQUIRE(measuredHz(out, out.getNumSamples() / 4, out.getNumSamples()) ==
                Catch::Approx(440.0).epsilon(0.02));
    }

    SECTION("half speed") {
        src.setRate(0.5);
        src.setNextReadPosition(0);
        const auto out = render(src, 40);
        REQUIRE(src.getNextReadPosition() == Catch::Approx(0.5 * 40 * kBlock).margin(2.0));
        REQUIRE(measuredHz(out, out.getNumSamples() / 4, out.getNumSamples()) ==
                Catch::Approx(440.0).epsilon(0.02));
    }

    SECTION("a seek lands where asked") {
        src.setRate(1.5);
        render(src, 4);
        src.setNextReadPosition(24000);
        REQUIRE(src.getNextReadPosition() == 24000);
        render(src, 1);
        REQUIRE(src.getNextReadPosition() == Catch::Approx(24000 + 1.5 * kBlock).margin(2.0));
    }

    SECTION("returning to rate 1 resumes from the audible position") {
        src.setRate(2.0);
        render(src, 10);
        const auto audible = src.getNextReadPosition();
        src.setRate(1.0);
        render(src, 1);
        REQUIRE(src.getNextReadPosition() == audible + kBlock);
    }
}
