// Demucs ONNX backend (issue #1288): integration test against the real
// htdemucs.onnx. Needs the ~316 MB weights, so it is env-gated like the CLAP
// [needs-model] tests: set MAGDA_STEM_DEMUCS_MODEL to an htdemucs.onnx path.
//
// Verifies the actual ORT contract (tensor names, static shapes, chunked
// OLA stitching) and basic separation sanity: the four stems must roughly
// sum back to the mix, and a pure sine should land mostly outside Drums.

#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <numbers>

#include "stem_separation/DemucsSeparator.hpp"

namespace {

float rms(const juce::AudioBuffer<float>& buffer, int channel, int numSamples) {
    double sum = 0.0;
    const float* data = buffer.getReadPointer(channel);
    for (int i = 0; i < numSamples; ++i)
        sum += static_cast<double>(data[i]) * data[i];
    return static_cast<float>(std::sqrt(sum / numSamples));
}

}  // namespace

TEST_CASE("DemucsSeparator separates a synthetic mix", "[stems][demucs][needs-model]") {
    const char* envPath = std::getenv("MAGDA_STEM_DEMUCS_MODEL");
    if (envPath == nullptr || !std::filesystem::exists(envPath)) {
        SKIP("set MAGDA_STEM_DEMUCS_MODEL to an htdemucs.onnx to run this test");
    }

    magda::stems::DemucsSeparator separator(envPath);
    REQUIRE(separator.isLoaded());

    // ~10 s stereo at 44.1 kHz: spans two OLA chunks. A 220 Hz tone with a
    // soft attack envelope reads as tonal material, not drums.
    constexpr int kSr = 44100;
    constexpr int kLen = kSr * 10;
    juce::AudioBuffer<float> input(2, kLen);
    for (int ch = 0; ch < 2; ++ch) {
        float* data = input.getWritePointer(ch);
        for (int i = 0; i < kLen; ++i) {
            const float env = std::min(1.0F, static_cast<float>(i) / (kSr / 2.0F));
            data[i] =
                0.4F * env * static_cast<float>(std::sin(2.0 * std::numbers::pi * 220.0 * i / kSr));
        }
    }

    float lastProgress = -1.0F;
    auto stems = separator.separate(input, kSr, [&](float frac) {
        lastProgress = frac;
        return true;
    });

    REQUIRE(stems.size() == 4);
    CHECK(stems[0].name == "Drums");
    CHECK(stems[3].name == "Vocals");
    CHECK(lastProgress == 1.0F);
    for (const auto& s : stems) {
        REQUIRE(s.audio.getNumChannels() == 2);
        REQUIRE(s.audio.getNumSamples() == kLen);
    }

    // Ignore the OLA edges (first/last second) for the energy checks.
    const int checkLen = kLen - 2 * kSr;
    juce::AudioBuffer<float> sum(2, checkLen);
    sum.clear();
    for (const auto& s : stems)
        for (int ch = 0; ch < 2; ++ch)
            sum.addFrom(ch, 0, s.audio, ch, kSr, checkLen);

    juce::AudioBuffer<float> mid(2, checkLen);
    for (int ch = 0; ch < 2; ++ch)
        mid.copyFrom(ch, 0, input, ch, kSr, checkLen);

    const float inRms = rms(mid, 0, checkLen);
    REQUIRE(inRms > 0.1F);

    // Stems should reconstruct the mix reasonably (htdemucs is not
    // mask-based, so allow a loose tolerance).
    juce::AudioBuffer<float> diff(2, checkLen);
    for (int ch = 0; ch < 2; ++ch) {
        diff.copyFrom(ch, 0, mid, ch, 0, checkLen);
        diff.addFrom(ch, 0, sum, ch, 0, checkLen, -1.0F);
    }
    CHECK(rms(diff, 0, checkLen) < 0.25F * inRms);

    // A steady tone is not drums.
    const float drumsRms = rms(stems[0].audio, 0, checkLen);
    CHECK(drumsRms < 0.5F * inRms);
}
