// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include "devices/faust/CompiledOversampler.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace magda::devices::faust {

namespace {

// 90 dB stopband for both. The outer one passes 0.45 fs flat to 3e-5 and stops from 0.55 fs;
// the inner one only has to stop the images of that band, from 1.55 fs at 4x.
constexpr int kOuterHalfLength = 30;
constexpr int kInnerHalfLength = 6;
constexpr double kKaiserBeta = 9.0;

/** @brief Zeroth-order modified Bessel function of the first kind, by its power series. */
double besselI0(double x) {
    double sum = 1.0;
    double term = 1.0;
    for (int k = 1; k < 64; ++k) {
        const double half = x / (2.0 * k);
        term *= half * half;
        sum += term;
        if (term < sum * 1.0e-17)
            break;
    }
    return sum;
}

int halfbandLatency(int halfLength) {
    return 2 * halfLength - 1;
}

}  // namespace

// ===========================================================================

HalfbandStage::HalfbandStage(int halfLength, double kaiserBeta) : halfLength_(halfLength) {
    const int centre = 2 * halfLength - 1;
    std::vector<double> taps(static_cast<size_t>(halfLength));
    double sum = 0.0;
    for (int i = 0; i < halfLength; ++i) {
        const int offset = 2 * i + 1;
        const double r = static_cast<double>(offset) / centre;
        const double window =
            besselI0(kaiserBeta * std::sqrt(std::max(0.0, 1.0 - r * r))) / besselI0(kaiserBeta);
        const double sinc = ((i % 2) == 0 ? 1.0 : -1.0) / (std::numbers::pi * offset);
        taps[static_cast<size_t>(i)] = sinc * window;
        sum += taps[static_cast<size_t>(i)];
    }

    // The window costs the DC gain a little; the side taps carry half of it, the centre the rest.
    taps_.resize(taps.size());
    for (size_t i = 0; i < taps.size(); ++i)
        taps_[i] = static_cast<float>(taps[i] * 0.25 / sum);
}

void HalfbandStage::prepare(int numUpChannels, int numDownChannels, int maxLowRateSamples) {
    const auto history = static_cast<size_t>(2 * halfLength_ - 1);
    const auto block = static_cast<size_t>(std::max(0, maxLowRateSamples));

    up_.assign(static_cast<size_t>(std::max(0, numUpChannels)),
               std::vector<float>(history + block));
    down_.assign(static_cast<size_t>(std::max(0, numDownChannels)),
                 DownChannel{std::vector<float>(history + block),
                             std::vector<float>(static_cast<size_t>(halfLength_) + block)});
}

void HalfbandStage::reset() {
    for (auto& line : up_)
        std::fill(line.begin(), line.end(), 0.0f);
    for (auto& channel : down_) {
        std::fill(channel.even.begin(), channel.even.end(), 0.0f);
        std::fill(channel.odd.begin(), channel.odd.end(), 0.0f);
    }
}

void HalfbandStage::interpolate(int channel, const float* input, float* output, int numSamples) {
    auto& line = up_[static_cast<size_t>(channel)];
    const int history = 2 * halfLength_ - 1;
    std::copy(input, input + numSamples, line.begin() + history);

    // y[2m] is the side-tap branch, y[2m+1] the centre tap: x delayed by K-1.
    const float* taps = taps_.data();
    for (int m = 0; m < numSamples; ++m) {
        const float* centre = line.data() + m + halfLength_;
        float acc = 0.0f;
        for (int i = 0; i < halfLength_; ++i)
            acc += taps[i] * (centre[i] + centre[-1 - i]);
        output[2 * m] = 2.0f * acc;
        output[2 * m + 1] = centre[0];
    }

    std::copy(line.begin() + numSamples, line.begin() + numSamples + history, line.begin());
}

void HalfbandStage::decimate(int channel, const float* input, float* output, int numSamples) {
    auto& state = down_[static_cast<size_t>(channel)];
    const int evenHistory = 2 * halfLength_ - 1;
    const int oddHistory = halfLength_;
    for (int m = 0; m < numSamples; ++m) {
        state.even[static_cast<size_t>(evenHistory + m)] = input[2 * m];
        state.odd[static_cast<size_t>(oddHistory + m)] = input[2 * m + 1];
    }

    const float* taps = taps_.data();
    for (int m = 0; m < numSamples; ++m) {
        const float* centre = state.even.data() + m + halfLength_;
        float acc = 0.0f;
        for (int i = 0; i < halfLength_; ++i)
            acc += taps[i] * (centre[i] + centre[-1 - i]);
        output[m] = acc + 0.5f * state.odd[static_cast<size_t>(m)];
    }

    std::copy(state.even.begin() + numSamples, state.even.begin() + numSamples + evenHistory,
              state.even.begin());
    std::copy(state.odd.begin() + numSamples, state.odd.begin() + numSamples + oddHistory,
              state.odd.begin());
}

// ===========================================================================

CompiledOversampler::CompiledOversampler()
    : outer_(kOuterHalfLength, kKaiserBeta), inner_(kInnerHalfLength, kKaiserBeta) {}

int CompiledOversampler::latencySamples(int factor) {
    if (factor >= 4)
        // The inner round trip is odd at 2x; innerAlign_ adds the sample that makes it whole.
        return halfbandLatency(kOuterHalfLength) + (halfbandLatency(kInnerHalfLength) + 1) / 2;
    if (factor >= 2)
        return halfbandLatency(kOuterHalfLength);
    return 0;
}

void CompiledOversampler::prepare(int numInputs, int numOutputs, int maxBlockSize) {
    maxBlockSize_ = std::max(0, maxBlockSize);
    const auto oversampledLength = static_cast<size_t>(maxBlockSize_ * kMaxFactor);

    outer_.prepare(numInputs, numOutputs, maxBlockSize_);
    inner_.prepare(numInputs, numOutputs, 2 * maxBlockSize_);

    inputBuffers_.assign(static_cast<size_t>(std::max(0, numInputs)),
                         std::vector<float>(oversampledLength));
    outputBuffers_.assign(static_cast<size_t>(std::max(0, numOutputs)),
                          std::vector<float>(oversampledLength));
    inputPtrs_.clear();
    for (auto& buffer : inputBuffers_)
        inputPtrs_.push_back(buffer.data());
    outputPtrs_.clear();
    for (auto& buffer : outputBuffers_)
        outputPtrs_.push_back(buffer.data());

    twiceRate_.assign(static_cast<size_t>(2 * maxBlockSize_), 0.0f);
    innerAlign_.assign(outputBuffers_.size(), 0.0f);
}

void CompiledOversampler::reset() {
    outer_.reset();
    inner_.reset();
    std::fill(innerAlign_.begin(), innerAlign_.end(), 0.0f);
}

void CompiledOversampler::upsample(const float* const* input, int numChannels, int numSamples,
                                   int factor) {
    numChannels = std::min(numChannels, static_cast<int>(inputBuffers_.size()));
    for (int channel = 0; channel < numChannels; ++channel) {
        float* destination = inputPtrs_[static_cast<size_t>(channel)];
        if (factor >= 4) {
            outer_.interpolate(channel, input[channel], twiceRate_.data(), numSamples);
            inner_.interpolate(channel, twiceRate_.data(), destination, 2 * numSamples);
        } else {
            outer_.interpolate(channel, input[channel], destination, numSamples);
        }
    }
}

void CompiledOversampler::downsample(float* const* output, int numChannels, int numSamples,
                                     int factor) {
    numChannels = std::min(numChannels, static_cast<int>(outputBuffers_.size()));
    for (int channel = 0; channel < numChannels; ++channel) {
        const float* source = outputPtrs_[static_cast<size_t>(channel)];
        if (factor < 4) {
            outer_.decimate(channel, source, output[channel], numSamples);
            continue;
        }

        inner_.decimate(channel, source, twiceRate_.data(), numSamples * 2);
        float carried = innerAlign_[static_cast<size_t>(channel)];
        for (int i = 0; i < numSamples * 2; ++i)
            std::swap(carried, twiceRate_[static_cast<size_t>(i)]);
        innerAlign_[static_cast<size_t>(channel)] = carried;
        outer_.decimate(channel, twiceRate_.data(), output[channel], numSamples);
    }
}

// ===========================================================================

void LatencyPad::prepare(int numChannels, int maxDelay) {
    lines_.assign(static_cast<size_t>(std::max(0, numChannels)),
                  Line{std::vector<float>(static_cast<size_t>(std::max(1, maxDelay))), 0});
}

void LatencyPad::reset() {
    for (int channel = 0; channel < static_cast<int>(lines_.size()); ++channel)
        reset(channel);
}

void LatencyPad::reset(int channel) {
    if (channel < 0 || channel >= static_cast<int>(lines_.size()))
        return;
    auto& line = lines_[static_cast<size_t>(channel)];
    std::fill(line.buffer.begin(), line.buffer.end(), 0.0f);
    line.write = 0;
}

void LatencyPad::process(int channel, float* samples, int numSamples, int delay) {
    if (delay <= 0 || channel < 0 || channel >= static_cast<int>(lines_.size()))
        return;

    auto& line = lines_[static_cast<size_t>(channel)];
    const int size = static_cast<int>(line.buffer.size());
    delay = std::min(delay, size);
    for (int i = 0; i < numSamples; ++i) {
        const int read = (line.write - delay + size) % size;
        const float delayed = line.buffer[static_cast<size_t>(read)];
        line.buffer[static_cast<size_t>(line.write)] = samples[i];
        samples[i] = delayed;
        line.write = (line.write + 1) % size;
    }
}

}  // namespace magda::devices::faust
