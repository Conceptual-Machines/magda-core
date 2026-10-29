// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include "analysis/SpectrumAnalysis.hpp"

#include <juce_audio_basics/juce_audio_basics.h>

#include <algorithm>
#include <cmath>

namespace magda::daw::audio {

namespace {
constexpr float kFloorDb = -120.0f;
// Hann's worst scalloping loss: how far a tone between bins can peak above the nearer one.
constexpr float kHannScallopDb = 1.42f;
}  // namespace

void OverlappedSpectrum::prepare(int fftOrder) {
    fftOrder_ = juce::jlimit(kMinOrder, kMaxOrder, fftOrder);
    fftSize_ = 1 << fftOrder_;
    fft_ = std::make_unique<juce::dsp::FFT>(fftOrder_);
    // JUCE normalises the window to unity coherent gain, so 2/N scales a sine to its amplitude.
    window_ = std::make_unique<juce::dsp::WindowingFunction<float>>(
        static_cast<size_t>(fftSize_), juce::dsp::WindowingFunction<float>::hann);
    frame_.assign(static_cast<size_t>(fftSize_) * 2, 0.0f);
    power_.assign(static_cast<size_t>(numBins()), 0.0f);
    analysedTo_ = 0;
}

int OverlappedSpectrum::samplesToRead(std::size_t writePosition, int maxSamples) const {
    if (fftSize_ == 0 || writePosition == 0 || writePosition == analysedTo_)
        return 0;

    const int hop = fftSize_ / 2;
    const int maxFrames = std::max(1, (maxSamples - fftSize_) / hop + 1);
    int frames = 1;
    if (analysedTo_ != 0 && writePosition > analysedTo_) {
        const auto fresh = writePosition - analysedTo_;
        frames = static_cast<int>((fresh + static_cast<std::size_t>(hop) - 1) /
                                  static_cast<std::size_t>(hop));
    }
    return fftSize_ + (std::clamp(frames, 1, maxFrames) - 1) * hop;
}

void OverlappedSpectrum::analyse(const float* samples, int numSamples, std::size_t writePosition,
                                 std::vector<float>& dbOut) {
    const int hop = fftSize_ / 2;
    const int frames = std::max(1, (numSamples - fftSize_) / hop + 1);
    const auto n = static_cast<size_t>(fftSize_);
    std::fill(power_.begin(), power_.end(), 0.0f);

    for (int f = 0; f < frames; ++f) {
        const int start = std::max(0, numSamples - fftSize_ - f * hop);
        std::copy(samples + start, samples + start + fftSize_, frame_.begin());
        std::fill(frame_.begin() + static_cast<std::ptrdiff_t>(n), frame_.end(), 0.0f);
        window_->multiplyWithWindowingTable(frame_.data(), n);
        fft_->performFrequencyOnlyForwardTransform(frame_.data());
        const float scale = 2.0f / static_cast<float>(fftSize_);
        for (size_t i = 0; i < power_.size(); ++i) {
            const float amp = frame_[i] * scale;
            power_[i] += amp * amp;
        }
    }

    dbOut.resize(power_.size());
    const float perFrame = 1.0f / static_cast<float>(frames);
    for (size_t i = 0; i < power_.size(); ++i)
        dbOut[i] = std::max(kFloorDb, 10.0f * std::log10(power_[i] * perFrame + 1.0e-12f));
    analysedTo_ = writePosition;
}

void spectrumColumns(const std::vector<float>& binDb, double binHz, float minHz, float maxHz,
                     std::vector<float>& columnDb) {
    const int numBins = static_cast<int>(binDb.size());
    const int width = static_cast<int>(columnDb.size());
    if (numBins < 2 || width == 0 || binHz <= 0.0)
        return;

    auto bin = [&](int i) { return binDb[static_cast<size_t>(std::clamp(i, 1, numBins - 1))]; };
    const double ratio = static_cast<double>(maxHz) / static_cast<double>(minHz);

    for (int c = 0; c < width; ++c) {
        const double lo = minHz * std::pow(ratio, static_cast<double>(c) / width);
        const double hi = minHz * std::pow(ratio, static_cast<double>(c + 1) / width);

        const double x = std::sqrt(lo * hi) / binHz;
        const int i = static_cast<int>(std::floor(x));
        const auto t = static_cast<float>(x - i);
        const float p0 = bin(i - 1), p1 = bin(i), p2 = bin(i + 1), p3 = bin(i + 2);
        const float cubic =
            p1 +
            0.5f * t *
                (p2 - p0 +
                 t * (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3 + t * (3.0f * (p1 - p2) + p3 - p0)));
        float value = std::clamp(cubic, std::min(p1, p2), std::max(p1, p2) + kHannScallopDb);

        const int first = std::max(1, static_cast<int>(std::ceil(lo / binHz)));
        const int last = std::min(numBins - 1, static_cast<int>(std::ceil(hi / binHz)) - 1);
        for (int b = first; b <= last; ++b)
            value = std::max(value, binDb[static_cast<size_t>(b)]);
        columnDb[static_cast<size_t>(c)] = value;
    }
}

juce::String nearestNoteLabel(double hz) {
    if (hz <= 0.0)
        return {};
    const double midi = 69.0 + 12.0 * std::log2(hz / 440.0);
    const int note = static_cast<int>(std::lround(midi));
    if (note < 0 || note > 127)
        return {};
    const int cents = static_cast<int>(std::lround((midi - note) * 100.0));
    return juce::MidiMessage::getMidiNoteName(note, true, true, 3) + " " + (cents >= 0 ? "+" : "") +
           juce::String(cents) + "c";
}

}  // namespace magda::daw::audio
