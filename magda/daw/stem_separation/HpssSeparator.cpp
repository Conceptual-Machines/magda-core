// See HpssSeparator.hpp.

#include "HpssSeparator.hpp"

#include <juce_dsp/juce_dsp.h>

#include <algorithm>
#include <cmath>
#include <vector>

namespace magda::stems {

namespace {

constexpr int kFftOrder = 12;
constexpr int kFftSize = 1 << kFftOrder;  // 4096
constexpr int kHop = kFftSize / 4;        // 1024
// Median-filter kernel (odd). 17 frames along time / 17 bins along frequency
// is Fitzgerald's original choice; larger kernels separate more aggressively
// but smear transients.
constexpr int kKernel = 17;
constexpr int kNumBins = kFftSize / 2 + 1;

// Median of a fixed-size window copied into scratch (nth_element mutates).
float medianOf(std::vector<float>& scratch) {
    const auto mid = scratch.begin() + static_cast<std::ptrdiff_t>(scratch.size() / 2);
    std::nth_element(scratch.begin(), mid, scratch.end());
    return *mid;
}

}  // namespace

std::vector<Stem> HpssSeparator::separate(const juce::AudioBuffer<float>& input,
                                          double /*sampleRate*/, const Progress& progress) {
    const int numChannels = input.getNumChannels();
    const int numSamples = input.getNumSamples();
    if (numChannels < 1 || numSamples < 1)
        return {};

    juce::dsp::FFT fft(kFftOrder);
    std::vector<float> window(static_cast<size_t>(kFftSize));
    juce::dsp::WindowingFunction<float>::fillWindowingTables(
        window.data(), static_cast<size_t>(kFftSize), juce::dsp::WindowingFunction<float>::hann,
        false);

    // Analysis grid: pad kFftSize/2 both sides (librosa center=true) so every
    // input sample gets full window overlap and reconstruction is exact.
    const int pad = kFftSize / 2;
    const int paddedLen = numSamples + 2 * pad;
    const int numFrames = 1 + (paddedLen - kFftSize + kHop - 1) / kHop;

    std::vector<Stem> stems(2);
    stems[0].name = "Harmonic";
    stems[1].name = "Percussive";
    for (auto& s : stems) {
        s.audio.setSize(numChannels, numSamples);
        s.audio.clear();
    }

    // Per-channel work; progress is fractional across channels.
    std::vector<float> frame(static_cast<size_t>(kFftSize) * 2);
    std::vector<float> magnitudes;  // numFrames x kNumBins
    std::vector<float> spectra;     // numFrames x kNumBins x (re, im)
    std::vector<float> harmEnh(static_cast<size_t>(numFrames) * kNumBins);
    std::vector<float> percEnh(static_cast<size_t>(numFrames) * kNumBins);
    std::vector<float> scratch(static_cast<size_t>(kKernel));

    magnitudes.resize(static_cast<size_t>(numFrames) * kNumBins);
    spectra.resize(static_cast<size_t>(numFrames) * kNumBins * 2);

    // Reconstruction normalizer: the summed squared window at each padded
    // sample position. Identical for every channel, so computed once.
    std::vector<float> norm(static_cast<size_t>(paddedLen), 0.0F);
    for (int t = 0; t < numFrames; ++t) {
        const int start = t * kHop;
        for (int i = 0; i < kFftSize && start + i < paddedLen; ++i)
            norm[static_cast<size_t>(start + i)] +=
                window[static_cast<size_t>(i)] * window[static_cast<size_t>(i)];
    }

    std::vector<float> padded(static_cast<size_t>(paddedLen));
    std::vector<float> outHarm(static_cast<size_t>(paddedLen));
    std::vector<float> outPerc(static_cast<size_t>(paddedLen));

    for (int ch = 0; ch < numChannels; ++ch) {
        const float chBase = static_cast<float>(ch) / static_cast<float>(numChannels);
        const float chSpan = 1.0F / static_cast<float>(numChannels);
        auto report = [&](float frac) {
            return progress == nullptr || progress(chBase + frac * chSpan);
        };

        std::fill(padded.begin(), padded.end(), 0.0F);
        const float* src = input.getReadPointer(ch);
        std::copy(src, src + numSamples, padded.begin() + pad);

        // --- STFT ---
        for (int t = 0; t < numFrames; ++t) {
            const int start = t * kHop;
            std::fill(frame.begin(), frame.end(), 0.0F);
            for (int i = 0; i < kFftSize && start + i < paddedLen; ++i)
                frame[static_cast<size_t>(i)] =
                    padded[static_cast<size_t>(start + i)] * window[static_cast<size_t>(i)];

            fft.performRealOnlyForwardTransform(frame.data(), true);

            for (int k = 0; k < kNumBins; ++k) {
                const float re = frame[static_cast<size_t>(2 * k)];
                const float im = frame[static_cast<size_t>(2 * k + 1)];
                const size_t idx = static_cast<size_t>(t) * kNumBins + static_cast<size_t>(k);
                spectra[idx * 2] = re;
                spectra[idx * 2 + 1] = im;
                magnitudes[idx] = std::sqrt(re * re + im * im);
            }
        }
        if (!report(0.25F))
            return {};

        // --- Median filtering ---
        // Harmonic enhancement: median across TIME per frequency bin.
        const int half = kKernel / 2;
        for (int k = 0; k < kNumBins; ++k) {
            for (int t = 0; t < numFrames; ++t) {
                for (int j = 0; j < kKernel; ++j) {
                    const int tt = std::clamp(t - half + j, 0, numFrames - 1);
                    scratch[static_cast<size_t>(j)] =
                        magnitudes[static_cast<size_t>(tt) * kNumBins + static_cast<size_t>(k)];
                }
                harmEnh[static_cast<size_t>(t) * kNumBins + static_cast<size_t>(k)] =
                    medianOf(scratch);
            }
        }
        if (!report(0.5F))
            return {};

        // Percussive enhancement: median across FREQUENCY per frame.
        for (int t = 0; t < numFrames; ++t) {
            for (int k = 0; k < kNumBins; ++k) {
                for (int j = 0; j < kKernel; ++j) {
                    const int kk = std::clamp(k - half + j, 0, kNumBins - 1);
                    scratch[static_cast<size_t>(j)] =
                        magnitudes[static_cast<size_t>(t) * kNumBins + static_cast<size_t>(kk)];
                }
                percEnh[static_cast<size_t>(t) * kNumBins + static_cast<size_t>(k)] =
                    medianOf(scratch);
            }
        }
        if (!report(0.75F))
            return {};

        // --- Soft masks + iSTFT ---
        // Wiener masks with power 2. Complementary by construction, so
        // harmonic + percussive == input wherever the input has energy.
        std::fill(outHarm.begin(), outHarm.end(), 0.0F);
        std::fill(outPerc.begin(), outPerc.end(), 0.0F);
        std::vector<float> frameH(static_cast<size_t>(kFftSize) * 2);
        std::vector<float> frameP(static_cast<size_t>(kFftSize) * 2);

        for (int t = 0; t < numFrames; ++t) {
            std::fill(frameH.begin(), frameH.end(), 0.0F);
            std::fill(frameP.begin(), frameP.end(), 0.0F);

            for (int k = 0; k < kNumBins; ++k) {
                const size_t idx = static_cast<size_t>(t) * kNumBins + static_cast<size_t>(k);
                const float h2 = harmEnh[idx] * harmEnh[idx];
                const float p2 = percEnh[idx] * percEnh[idx];
                const float denom = h2 + p2;
                const float maskH = denom > 1.0e-12F ? h2 / denom : 0.5F;
                const float maskP = 1.0F - maskH;

                const float re = spectra[idx * 2];
                const float im = spectra[idx * 2 + 1];
                frameH[static_cast<size_t>(2 * k)] = re * maskH;
                frameH[static_cast<size_t>(2 * k + 1)] = im * maskH;
                frameP[static_cast<size_t>(2 * k)] = re * maskP;
                frameP[static_cast<size_t>(2 * k + 1)] = im * maskP;
            }

            fft.performRealOnlyInverseTransform(frameH.data());
            fft.performRealOnlyInverseTransform(frameP.data());

            const int start = t * kHop;
            for (int i = 0; i < kFftSize && start + i < paddedLen; ++i) {
                const float w = window[static_cast<size_t>(i)];
                outHarm[static_cast<size_t>(start + i)] += frameH[static_cast<size_t>(i)] * w;
                outPerc[static_cast<size_t>(start + i)] += frameP[static_cast<size_t>(i)] * w;
            }
        }

        float* dstH = stems[0].audio.getWritePointer(ch);
        float* dstP = stems[1].audio.getWritePointer(ch);
        for (int i = 0; i < numSamples; ++i) {
            const float n = norm[static_cast<size_t>(i + pad)];
            const float scale = n > 1.0e-8F ? 1.0F / n : 0.0F;
            dstH[i] = outHarm[static_cast<size_t>(i + pad)] * scale;
            dstP[i] = outPerc[static_cast<size_t>(i + pad)] * scale;
        }
        if (!report(1.0F))
            return {};
    }

    return stems;
}

}  // namespace magda::stems
