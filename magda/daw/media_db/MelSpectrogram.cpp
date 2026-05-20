#include "MelSpectrogram.hpp"

#include <juce_dsp/juce_dsp.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace magda::media {

namespace {

// HTK mel scale: m = 2595 * log10(1 + f/700)
constexpr float kEpsLogMel = 1e-10F;

float hzToMel(float hz) {
    return 2595.0F * std::log10(1.0F + hz / 700.0F);
}

float melToHz(float mel) {
    return 700.0F * (std::pow(10.0F, mel / 2595.0F) - 1.0F);
}

}  // namespace

std::vector<float> buildMelFilterbank(const MelConfig& cfg) {
    const int nFftBins = cfg.nFft / 2 + 1;
    std::vector<float> filterbank(static_cast<size_t>(cfg.nMels) * nFftBins, 0.0F);

    const float melMin = hzToMel(cfg.fMin);
    const float melMax = hzToMel(cfg.fMax);

    // n_mels + 2 mel-spaced points (each filter spans 3 of them).
    std::vector<float> melPoints(cfg.nMels + 2);
    for (int i = 0; i < cfg.nMels + 2; ++i) {
        melPoints[i] = melMin + (melMax - melMin) * static_cast<float>(i) / (cfg.nMels + 1);
    }
    // Mel points -> Hz -> FFT bin index (continuous).
    std::vector<float> hzPoints(cfg.nMels + 2);
    for (int i = 0; i < cfg.nMels + 2; ++i) {
        hzPoints[i] = melToHz(melPoints[i]);
    }

    for (int m = 0; m < cfg.nMels; ++m) {
        const float fLeft = hzPoints[m];
        const float fCenter = hzPoints[m + 1];
        const float fRight = hzPoints[m + 2];
        for (int k = 0; k < nFftBins; ++k) {
            const float f = static_cast<float>(k) * cfg.sampleRate / cfg.nFft;
            float w = 0.0F;
            if (f >= fLeft && f <= fCenter) {
                w = (f - fLeft) / (fCenter - fLeft + 1e-9F);
            } else if (f > fCenter && f <= fRight) {
                w = (fRight - f) / (fRight - fCenter + 1e-9F);
            }
            filterbank[static_cast<size_t>(m) * nFftBins + k] = w;
        }
    }
    return filterbank;
}

std::vector<float> computeLogMel(const float* mono, int numSamples, const MelConfig& cfg) {
    // CLAP expects exactly cfg.targetSamples worth of audio per chunk; the
    // caller is responsible for padding/truncating. Number of STFT frames is
    // targetSamples / hopLength + 1 (matches torchaudio / librosa center=True
    // with reflect padding; we approximate with zero padding which is the
    // dominant parity-risk vs the Python pipeline — TODO bit-parity check
    // against a reference run once we have the model loaded).
    const int numFrames = cfg.targetSamples / cfg.hopLength + 1;
    const int nFftBins = cfg.nFft / 2 + 1;

    const auto filterbank = buildMelFilterbank(cfg);

    // Stage buffers
    std::vector<float> fftBuf(static_cast<size_t>(cfg.nFft) * 2, 0.0F);
    std::vector<float> magSq(nFftBins, 0.0F);
    std::vector<float> windowed(cfg.nFft, 0.0F);

    // Pad the input so we have enough samples for `numFrames` frames.
    const int paddedLen = cfg.targetSamples + cfg.nFft;
    std::vector<float> padded(paddedLen, 0.0F);
    const int copyLen = std::min(numSamples, cfg.targetSamples);
    std::memcpy(padded.data() + cfg.nFft / 2, mono, static_cast<size_t>(copyLen) * sizeof(float));

    juce::dsp::FFT fft(static_cast<int>(std::log2(cfg.nFft)));
    juce::dsp::WindowingFunction<float> hann(cfg.nFft, juce::dsp::WindowingFunction<float>::hann);

    // Output is (n_mels x n_frames) in row-major; matches torch tensor shape
    // [n_mels, n_frames] which the model expects after a transpose to
    // [n_frames, n_mels].
    std::vector<float> out(static_cast<size_t>(cfg.nMels) * numFrames, 0.0F);

    for (int frame = 0; frame < numFrames; ++frame) {
        const int start = frame * cfg.hopLength;
        std::memcpy(windowed.data(), padded.data() + start,
                    static_cast<size_t>(cfg.nFft) * sizeof(float));
        hann.multiplyWithWindowingTable(windowed.data(), cfg.nFft);

        std::memcpy(fftBuf.data(), windowed.data(), static_cast<size_t>(cfg.nFft) * sizeof(float));
        std::memset(fftBuf.data() + cfg.nFft, 0, static_cast<size_t>(cfg.nFft) * sizeof(float));
        fft.performRealOnlyForwardTransform(fftBuf.data());

        for (int k = 0; k < nFftBins; ++k) {
            const float re = fftBuf[2 * k];
            const float im = fftBuf[2 * k + 1];
            magSq[k] = re * re + im * im;
        }

        // mel[m, frame] = log(filterbank[m] · magSq + eps)
        for (int m = 0; m < cfg.nMels; ++m) {
            const float* row = &filterbank[static_cast<size_t>(m) * nFftBins];
            float acc = 0.0F;
            for (int k = 0; k < nFftBins; ++k) {
                acc += row[k] * magSq[k];
            }
            out[static_cast<size_t>(m) * numFrames + frame] = std::log(acc + kEpsLogMel);
        }
    }
    return out;
}

}  // namespace magda::media
