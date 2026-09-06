#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <cstdint>

/**
 * @file PcmQuantiser.hpp
 * @brief Float to fixed-point, and the dither that belongs with it (#2248).
 *
 * One unit rather than a step each writer takes for itself, because rounding
 * unquantised float is the mistake and a writer that owns its own rounding is
 * where that mistake lives. Everything on its way to 16- or 24-bit goes through
 * this: a render, a bounce, a freeze.
 *
 * Nothing here writes a file. What a quantised buffer is stored as is the
 * writer's business; what it is worth is this.
 */

namespace magda::engine {

/// What is added before the round.
enum class DitherMode : std::uint8_t {
    /// Round and nothing else. For a target with no quantisation to hide -- a
    /// 32-bit float file -- and for a caller that has a reason.
    none,

    /**
     * @brief Two uniform randoms summed, scaled to one LSB.
     *
     * The default wherever the target is fixed point. A triangular distribution
     * is what makes the error independent of the signal: with none, a quiet
     * fade quantises to harmonics of itself, which is a tone that was not
     * played; with this, it quantises to a flat noise floor, which is silence
     * with a hiss in it.
     */
    tpdf,

    /**
     * @brief TPDF, with the error fed back through an F-weighted filter.
     *
     * The same total noise power moved off the ear: the floor drops below
     * 10 kHz and rises above it, where there is less hearing to spend it on.
     * Lipshitz, Vanderkooy and Wannamaker's published nine-tap coefficients.
     */
    shaped,
};

/**
 * @brief Rounds a block to @p bits, dithering on the way.
 *
 * Stateful on purpose: the noise shaper's error feedback is a filter over the
 * samples before it, and the generator is a sequence rather than a draw. One
 * per channel-set per render, reset between renders.
 *
 * The seed is fixed rather than drawn from a clock, so a render of one project
 * produces one file. A dither nobody can reproduce is a dither the null-diff
 * corpus cannot carry (#1896).
 */
class PcmQuantiser {
  public:
    /// @p bits is the target depth: 16 or 24. @p channels sizes the shaper's
    /// per-channel history.
    PcmQuantiser(int bits, int channels, DitherMode mode, std::uint32_t seed = kDefaultSeed);

    /// Round @p buffer's first @p numSamples in place onto the target's grid.
    /// The values stay float and stay in [-1, 1]: this is what the number will
    /// be once it is stored, not how it is stored.
    void process(juce::AudioBuffer<float>& buffer, int numSamples);

    /// Forget the shaper's history and restart the sequence. What a second
    /// render of the same range needs to produce the same file.
    void reset();

    /// One least significant bit of the target, as a float amplitude.
    float lsb() const {
        return lsb_;
    }

    static constexpr std::uint32_t kDefaultSeed = 0x9e3779b9U;

    /// The shaper's length, and therefore how many samples of history a channel
    /// carries.
    static constexpr int kShaperTaps = 9;

  private:
    /// The next uniform in [-0.5, 0.5).
    float nextUniform();

    int bits_ = 16;
    DitherMode mode_ = DitherMode::tpdf;
    float lsb_ = 0.0f;
    float scale_ = 0.0f;
    std::uint32_t seed_ = kDefaultSeed;
    std::uint32_t state_ = kDefaultSeed;

    /// Per channel, most recent first. Sized at construction so process() never
    /// allocates.
    juce::AudioBuffer<float> errors_;
};

}  // namespace magda::engine
