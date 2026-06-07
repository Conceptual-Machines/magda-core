#include "analysis/BandSpectrum.hpp"

#include <juce_dsp/juce_dsp.h>

#include <cmath>
#include <vector>

namespace magda::daw::audio {

namespace {
constexpr int kFftOrder = 11;             // 2048-point frame
constexpr int kFftSize = 1 << kFftOrder;  // 2048
constexpr float kFloorDb = -120.0f;
}  // namespace

void computeMaskingBandsDb(const AudioTapBuffer& ring, double sampleRate,
                           std::array<float, kNumMaskingBands>& outDb) {
    outDb.fill(kFloorDb);
    if (sampleRate <= 0.0)
        return;

    // Pull the most recent frame from the ring.
    std::vector<float> frame(static_cast<size_t>(kFftSize), 0.0f);
    ring.readLatest(frame.data(), kFftSize);

    // Hann window (coherent gain 0.5, compensated below).
    juce::dsp::WindowingFunction<float> window(static_cast<size_t>(kFftSize),
                                               juce::dsp::WindowingFunction<float>::hann);
    window.multiplyWithWindowingTable(frame.data(), static_cast<size_t>(kFftSize));

    // Frequency-only forward FFT needs 2*fftSize storage.
    std::vector<float> fftData(static_cast<size_t>(kFftSize) * 2, 0.0f);
    std::copy(frame.begin(), frame.end(), fftData.begin());
    juce::dsp::FFT fft(kFftOrder);
    fft.performFrequencyOnlyForwardTransform(fftData.data());

    // Per-bin amplitude so a full-scale sine peaks at 1.0 (0 dBFS) in its bin.
    // juce::dsp::WindowingFunction normalises the window (coherent gain ~1), so
    // only the one-sided (x2) factor over the frame length is needed. We take the
    // peak bin within each band (rather than summing the window-smeared mainlobe,
    // which over-counts): calibrated for tones, monotonic with energy otherwise.
    const float scale = 2.0f / static_cast<float>(kFftSize);
    const double binHz = sampleRate / static_cast<double>(kFftSize);

    std::array<float, kNumMaskingBands> peak{};
    peak.fill(0.0f);
    for (int bin = 1; bin <= kFftSize / 2; ++bin) {
        const double freq = bin * binHz;
        if (freq < maskingBandEdgeHz(0))
            continue;
        if (freq >= maskingBandEdgeHz(kNumMaskingBands))
            break;
        // Locate the band containing this bin (bands are monotonic in freq).
        int b = static_cast<int>(std::floor(3.0 * std::log2(freq / 20.0)));
        b = juce::jlimit(0, kNumMaskingBands - 1, b);
        const float amp = fftData[static_cast<size_t>(bin)] * scale;
        peak[static_cast<size_t>(b)] = juce::jmax(peak[static_cast<size_t>(b)], amp);
    }

    for (int b = 0; b < kNumMaskingBands; ++b) {
        if (peak[static_cast<size_t>(b)] > 0.0f)
            outDb[static_cast<size_t>(b)] =
                juce::jmax(kFloorDb, 20.0f * std::log10(peak[static_cast<size_t>(b)]));
    }
}

}  // namespace magda::daw::audio
