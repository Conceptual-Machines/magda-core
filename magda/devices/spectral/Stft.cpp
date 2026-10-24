#include "devices/spectral/Stft.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace magda::devices::spectral {

bool Stft::prepare(int fftSize, int overlap, int maxChannels) {
    if (overlap <= 0 || maxChannels <= 0 || fftSize % overlap != 0 || !fft_.prepare(fftSize))
        return false;

    fftSize_ = fftSize;
    hopSize_ = fftSize / overlap;

    const auto size = static_cast<size_t>(fftSize);
    window_.resize(size);
    double squareSum = 0.0;
    for (size_t i = 0; i < size; ++i) {
        const double hann =
            0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * static_cast<double>(i) / fftSize);
        window_[i] = static_cast<float>(std::sqrt(hann));
        squareSum += hann;
    }
    overlapGain_ = static_cast<float>(hopSize_ / squareSum);
    magnitudeScale_ = static_cast<float>(1.0 / std::sqrt(squareSum));
    // Power adds per grain: the synthesis overlap times the analysis window's mean power.
    incoherentGain_ = static_cast<float>(overlapGain_ *
                                         std::sqrt((squareSum / hopSize_) * (squareSum / fftSize)));

    frame_.assign(size, 0.0f);
    spectrum_.assign(static_cast<size_t>(fft_.spectrumValues()), 0.0f);
    channels_.resize(static_cast<size_t>(maxChannels));
    for (auto& state : channels_) {
        state.input.assign(size, 0.0f);
        state.output.assign(size, 0.0f);
    }
    reset();
    return true;
}

void Stft::reset() {
    for (auto& state : channels_) {
        std::ranges::fill(state.input, 0.0f);
        std::ranges::fill(state.output, 0.0f);
        state.writePos = 0;
        state.readPos = 0;
    }
}

void Stft::analyse(const Channel& state) noexcept {
    // writePos is the oldest sample in the ring.
    const int head = fftSize_ - state.writePos;
    std::copy_n(state.input.begin() + state.writePos, head, frame_.begin());
    std::copy_n(state.input.begin(), state.writePos, frame_.begin() + head);
    for (int i = 0; i < fftSize_; ++i)
        frame_[static_cast<size_t>(i)] *= window_[static_cast<size_t>(i)];
    fft_.forward(frame_.data(), spectrum_.data());
}

void Stft::resynthesise(Channel& state) noexcept {
    fft_.inverse(spectrum_.data(), frame_.data());

    auto& out = state.output;
    std::copy(out.begin() + hopSize_, out.end(), out.begin());
    std::fill(out.end() - hopSize_, out.end(), 0.0f);
    for (int i = 0; i < fftSize_; ++i) {
        const auto k = static_cast<size_t>(i);
        out[k] += frame_[k] * window_[k] * overlapGain_;
    }
}

}  // namespace magda::devices::spectral
