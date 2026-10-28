// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <magda/sdk/dsp/Fft.hpp>
#include <vector>

namespace magda::devices::spectral {

/**
 * @brief Streaming STFT with overlap-add resynthesis, the frame loop shared by spectral devices.
 *
 * Periodic sqrt-Hann on analysis and synthesis, so an untouched spectrum reconstructs the input
 * delayed by fftSize() samples. JUCE-free; prepare() allocates, nothing after it does.
 */
class Stft {
  public:
    /** @brief @p fftSize is a power of two, @p overlap divides it. False leaves it unprepared. */
    bool prepare(int fftSize, int overlap, int maxChannels);
    void reset();

    int fftSize() const noexcept {
        return fftSize_;
    }
    int hopSize() const noexcept {
        return hopSize_;
    }
    int bins() const noexcept {
        return fftSize_ / 2 + 1;
    }
    int maxChannels() const noexcept {
        return static_cast<int>(channels_.size());
    }
    int latencySamples() const noexcept {
        return fftSize_;
    }

    /// Scales a bin magnitude so broadband material reads its RMS: white noise at -20 dBFS reads
    /// -20 dB in every bin, where a sine-referenced scale would read it 26 dB lower.
    float magnitudeScale() const noexcept {
        return magnitudeScale_;
    }

    /**
     * @brief Run @p numFrames samples of one channel through the STFT.
     *
     * @p wet may alias @p in. @p dry, when not null, receives the input delayed by the latency.
     * @p onFrame(channel, spectrum) edits each frame's interleaved (re, im) bins in place.
     */
    template <typename OnFrame>
    void process(int channel, const float* in, float* wet, float* dry, int numFrames,
                 OnFrame&& onFrame) {
        auto& state = channels_[static_cast<size_t>(channel)];
        for (int i = 0; i < numFrames; ++i) {
            const float x = in[i];
            if (dry != nullptr)
                dry[i] = state.input[static_cast<size_t>(state.writePos)];
            state.input[static_cast<size_t>(state.writePos)] = x;
            state.writePos = (state.writePos + 1) % fftSize_;

            wet[i] = state.output[static_cast<size_t>(state.readPos++)];
            if (state.readPos == hopSize_) {
                state.readPos = 0;
                analyse(state);
                onFrame(channel, spectrum_.data());
                resynthesise(state);
            }
        }
    }

    /// Output level of a frame whose phases were randomised, relative to an untouched one: the
    /// overlapping grains no longer add coherently.
    float incoherentGain() const noexcept {
        return incoherentGain_;
    }

  private:
    struct Channel {
        std::vector<float> input;   // ring of the last fftSize() samples
        std::vector<float> output;  // overlap-add accumulator, sample 0 plays next
        int writePos = 0;
        int readPos = 0;
    };

    void analyse(const Channel& state) noexcept;
    void resynthesise(Channel& state) noexcept;

    sdk::RealFft fft_;
    std::vector<Channel> channels_;
    std::vector<float> window_;
    std::vector<float> frame_;
    std::vector<float> spectrum_;
    int fftSize_ = 0;
    int hopSize_ = 0;
    float overlapGain_ = 1.0f;
    float magnitudeScale_ = 1.0f;
    float incoherentGain_ = 1.0f;
};

}  // namespace magda::devices::spectral
