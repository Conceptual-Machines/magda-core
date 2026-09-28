#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <array>
#include <cstdint>
#include <vector>

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
    /// @p bits is the target depth: 16 or 24. @p channels sizes the per-channel
    /// state, which is one shaper history and one generator each.
    PcmQuantiser(int bits, int channels, DitherMode mode, std::uint32_t seed = kDefaultSeed);

    /// Round @p buffer's first @p numSamples in place onto the target's grid.
    /// The values stay float and stay in [-1, 1]: this is what the number will
    /// be once it is stored, not how it is stored.
    void process(juce::AudioBuffer<float>& buffer, int numSamples);

    /**
     * @brief The same, also writing each code into @p codes.
     *
     * One int32 per sample per channel, the target's bits at the top, which is
     * what juce::AudioFormatWriter::write takes. That path is the only one a
     * writer may use after this unit.
     *
     * `writeFromAudioSampleBuffer` must not be: it converts float to int32 by
     * `roundToInt(INT_MAX * sample)` and the format then keeps the top bits, so
     * the effective factor is `2^(bits-1) - 2^(bits-31)` rather than
     * `2^(bits-1)` and a code lands one low wherever the product falls short.
     * No float can carry a chosen code through it, which is the whole reason
     * the codes are handed over rather than inferred.
     */
    void processToCodes(juce::AudioBuffer<float>& buffer, int numSamples, int* const* codes);

    /// Forget the shaper's history and restart every generator. What a second
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
    /// One channel's own generator and shaper history.
    ///
    /// Per channel rather than shared, so a render cut into different block
    /// sizes hands each channel the same draws: OfflineRenderRequest promises
    /// the audio is sample-identical at any block size.
    struct Channel {
        std::uint32_t random = kDefaultSeed;
        std::array<double, kShaperTaps> errors{};
    };

    /// The next uniform on [-0.5, 0.5), advancing @p state.
    static double nextUniform(std::uint32_t& state);

    /// The shared walk. @p codes is null for the float-only path.
    void quantise(juce::AudioBuffer<float>& buffer, int numSamples, int* const* codes);

    int bits_ = 16;
    DitherMode mode_ = DitherMode::tpdf;
    float lsb_ = 0.0f;

    /// In double throughout: at 24 bits a float's spacing near full scale is a
    /// whole code, which would round the dither away before it was applied.
    double scale_ = 0.0;
    double maxCode_ = 0.0;

    std::uint32_t seed_ = kDefaultSeed;
    std::vector<Channel> channels_;
};

}  // namespace magda::engine
