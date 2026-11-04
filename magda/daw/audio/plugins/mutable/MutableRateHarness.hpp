// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file MutableRateHarness.hpp
 * @brief The rate crossing the Mutable ports share (#2409).
 *
 * Each module runs at its own native rate (Elements and Clouds 32 kHz, Rings 48 kHz) and derives
 * its tuning from it, so the DSP stays there and the host audio is resampled around it.
 */

#pragma once

#include <cstdint>

namespace magda::daw::audio {

/**
 * @brief One leg of a rate crossing: a stereo Kaiser-windowed sinc resampler.
 *
 * The cutoff sits on the lower of the two Nyquists, so one kernel band-limits both an
 * interpolation and a decimation. The phase is an exact fraction of the two rates, so no render
 * length drifts it. At equal rates samples pass straight through.
 */
class MutableResampler {
  public:
    /// Kernel half-width, in samples of the lower rate.
    static constexpr int kZeroCrossings = 24;
    /// History, in input frames: the widest kernel, 768 kHz into 32 kHz, is 1153 taps.
    static constexpr int kCapacity = 4096;

    /** @brief Design for @p inRate in, @p outRate out, and reset with no delay placed. */
    void prepare(double inRate, double outRate);

    /**
     * @brief Empty the history and place the first input frame pushed after this on output frame
     *        @p delayOutputFrames.
     *
     * The silence ahead of it is primed here, so the kernel's left half has history from the
     * first output on.
     */
    void reset(int delayOutputFrames);

    void push(float l, float r);

    /** @brief Whether the next output's window has all its input. */
    bool canPull() const;

    void pull(float& l, float& r);

    /** @brief The kernel's half-width in input frames; zero when passing through. */
    double halfWidth() const {
        return halfWidth_;
    }

    /** @brief Input frames per output frame. */
    double step() const {
        return static_cast<double>(stepNumerator_) / static_cast<double>(denominator_);
    }

  private:
    int lastTap() const;
    void dropBefore(int index);

    float l_[kCapacity];
    float r_[kCapacity];
    int head_ = 0;
    int count_ = 0;

    // The next output's centre, in input frames from head_: position_ + fraction_ / denominator_.
    int position_ = 0;
    std::int64_t fraction_ = 0;
    std::int64_t denominator_ = 1;
    std::int64_t stepNumerator_ = 1;

    /// min(1, out / in): the kernel's stretch, and its gain, when decimating.
    double scale_ = 1.0;
    double halfWidth_ = 0.0;
    bool passThrough_ = true;
};

/**
 * @brief The rate crossing a Mutable port runs inside: its DSP at @p internalRate in blocks of
 *        @p blockFrames, the host at whatever rate it prepares.
 *
 * An instrument renders internal blocks on demand from inside the output loop, so it has nothing
 * to starve. An effect pushes host audio through a second leg into those blocks and back out,
 * with the output leg primed far enough ahead that it never waits on a block. Either way the
 * delay is placed at reset, and latencySamples() is it.
 */
class MutableRateHarness {
  public:
    enum class Role { Instrument, Effect };

    /// The largest DSP block among the ports, Clouds' 32, with room to spare.
    static constexpr int kMaxBlockFrames = 64;

    MutableRateHarness(double internalRate, int blockFrames, Role role);

    void prepare(double hostRate);
    void reset();

    /** @brief Host frames from an input, or a note, to the output it lands on. */
    int latencySamples() const {
        return latency_;
    }

    /**
     * @brief Fill @p numSamples host frames, @p outR null for mono, rendering internal blocks as
     *        the output needs them.
     * @param renderBlock `void(float* l, float* r, int frames)`, one internal block per call.
     */
    template <typename RenderBlock>
    void render(float* outL, float* outR, int numSamples, RenderBlock&& renderBlock) {
        for (int i = 0; i < numSamples; ++i) {
            while (!out_.canPull()) {
                if (blockFill_ == blockFrames_) {
                    renderBlock(blockL_, blockR_, blockFrames_);
                    blockFill_ = 0;
                }
                out_.push(blockL_[blockFill_], blockR_[blockFill_]);
                ++blockFill_;
            }
            float l = 0.0f, r = 0.0f;
            out_.pull(l, r);
            outL[i] = l;
            if (outR != nullptr)
                outR[i] = r;
        }
    }

    /**
     * @brief Run @p numSamples host frames through the DSP in place, @p r null for mono.
     * @param processBlock `void(float* l, float* r, int frames)`, one internal block in place.
     */
    template <typename ProcessBlock>
    void process(float* l, float* r, int numSamples, ProcessBlock&& processBlock) {
        for (int i = 0; i < numSamples; ++i) {
            in_.push(l[i], r != nullptr ? r[i] : l[i]);
            while (in_.canPull()) {
                in_.pull(blockL_[blockFill_], blockR_[blockFill_]);
                if (++blockFill_ == blockFrames_) {
                    processBlock(blockL_, blockR_, blockFrames_);
                    for (int j = 0; j < blockFrames_; ++j)
                        out_.push(blockL_[j], blockR_[j]);
                    blockFill_ = 0;
                }
            }
            float outL = 0.0f, outR = 0.0f;
            pullPlaced(outL, outR);
            l[i] = outL;
            if (r != nullptr)
                r[i] = outR;
        }
    }

  private:
    void pullPlaced(float& l, float& r);

    double internalRate_;
    int blockFrames_;
    Role role_;

    MutableResampler in_;
    MutableResampler out_;
    int latency_ = 0;

    float blockL_[kMaxBlockFrames]{};
    float blockR_[kMaxBlockFrames]{};
    int blockFill_ = 0;
};

}  // namespace magda::daw::audio
