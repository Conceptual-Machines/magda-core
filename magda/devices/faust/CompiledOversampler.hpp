// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <vector>

/**
 * @file CompiledOversampler.hpp
 * @brief The polyphase halfband pair a compiled effect runs its dsp inside (#2276).
 */

namespace magda::devices::faust {

/**
 * @brief One linear-phase halfband FIR, as a 2x interpolator and a 2x decimator.
 *
 * Kaiser-windowed sinc of length 4K-1, so the round trip is 2K-1 samples at the
 * lower rate. Only the odd-offset taps are non-zero, which is what the
 * polyphase split runs on.
 */
class HalfbandStage {
  public:
    HalfbandStage(int halfLength, double kaiserBeta);

    void prepare(int numUpChannels, int numDownChannels, int maxLowRateSamples);
    void reset();

    /// Round-trip delay in lower-rate samples.
    int latencySamples() const {
        return 2 * halfLength_ - 1;
    }

    /// @p numSamples lower-rate samples of @p input into 2 * @p numSamples of @p output.
    void interpolate(int channel, const float* input, float* output, int numSamples);
    /// 2 * @p numSamples higher-rate samples of @p input into @p numSamples of @p output.
    void decimate(int channel, const float* input, float* output, int numSamples);

    /// a_i = h[c + 2i + 1] for the filter centred on c, scaled so h's DC gain is 1.
    const std::vector<float>& sideTaps() const {
        return taps_;
    }

  private:
    struct DownChannel {
        std::vector<float> even;
        std::vector<float> odd;
    };

    int halfLength_;
    std::vector<float> taps_;
    /// Per channel: the last 2K-1 inputs, then room for the block.
    std::vector<std::vector<float>> up_;
    std::vector<DownChannel> down_;
};

/**
 * @brief Up to 4x around a dsp: 2x is one halfband, 4x two in cascade.
 *
 * The inner stage of the 4x cascade is short because the outer one has already
 * removed what it would have to. Allocates only in prepare().
 */
class CompiledOversampler {
  public:
    static constexpr int kMaxFactor = 4;

    CompiledOversampler();

    void prepare(int numInputs, int numOutputs, int maxBlockSize);
    void reset();

    /// Base-rate samples the up/down round trip delays by at @p factor; 0 for 1x.
    static int latencySamples(int factor);

    int maxBlockSize() const {
        return maxBlockSize_;
    }

    /**
     * @brief Upsample @p numChannels channels into inputs().
     *
     * @p numSamples is at most maxBlockSize(); each of inputs() then holds
     * @p numSamples * @p factor samples.
     */
    void upsample(const float* const* input, int numChannels, int numSamples, int factor);
    /** @brief Downsample @p numChannels channels of outputs() into @p output. */
    void downsample(float* const* output, int numChannels, int numSamples, int factor);

    /// Non-const pointers because Faust's compute() takes FAUSTFLOAT**.
    float** inputs() {
        return inputPtrs_.data();
    }
    float** outputs() {
        return outputPtrs_.data();
    }

  private:
    HalfbandStage outer_;
    HalfbandStage inner_;
    int maxBlockSize_ = 0;

    std::vector<std::vector<float>> inputBuffers_;
    std::vector<std::vector<float>> outputBuffers_;
    std::vector<float*> inputPtrs_;
    std::vector<float*> outputPtrs_;
    std::vector<float> twiceRate_;
    /// The 4x path's extra 2x-rate sample per output channel, which makes its latency whole.
    std::vector<float> innerAlign_;
};

/**
 * @brief A per-channel whole-sample delay, up to a fixed length.
 *
 * What lines a device up with the latency it reported when the running factor
 * adds less than the largest one would.
 */
class LatencyPad {
  public:
    void prepare(int numChannels, int maxDelay);
    void reset();
    void reset(int channel);
    /// Delays @p samples in place by @p delay; a channel past those prepared is left as it is.
    void process(int channel, float* samples, int numSamples, int delay);

  private:
    struct Line {
        std::vector<float> buffer;
        int write = 0;
    };

    std::vector<Line> lines_;
};

}  // namespace magda::devices::faust
