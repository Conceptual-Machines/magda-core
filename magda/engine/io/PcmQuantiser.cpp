#include "io/PcmQuantiser.hpp"

#include <cmath>
#include <cstdint>

namespace magda::engine {

namespace {

/**
 * @brief The F-weighted error-feedback coefficients, nine taps.
 *
 * Lipshitz, Vanderkooy and Wannamaker, "Minimally Audible Noise Shaping"
 * (JAES 39/11, 1991), the F-weighted set. Published figures rather than a
 * filter designed here: the point of a named curve is that it is the one whose
 * listening tests were run.
 */
constexpr double kFWeighted[PcmQuantiser::kShaperTaps] = {2.412,  -3.370, 3.937,  -4.174, 3.353,
                                                          -2.205, 1.281,  -0.569, 0.0847};

/**
 * @brief What one code is worth, as JUCE's writers count it.
 *
 * `2^(bits-1)`, which is `1.0 + maxValue` in `AudioData::Int16::setAsFloatLE`
 * and its 24-bit sibling. Using `maxValue` itself would put this unit's grid a
 * part in 32767 off the writer's, and every code would be rounded a second
 * time on the way to disk: after the dither and outside the shaper's feedback,
 * which is the one place a second rounding must not happen.
 */
double scaleFor(int bits) {
    return static_cast<double>(1ULL << static_cast<unsigned>(bits - 1));
}

/// The largest code a writer will store. It clips to `maxValue` at both ends,
/// so the negative side stops one short of what the format could hold.
double maxCodeFor(int bits) {
    return scaleFor(bits) - 1.0;
}

/// splitmix64's finaliser, so two channels seeded one apart are unrelated.
std::uint32_t mix(std::uint32_t value) {
    std::uint64_t wide = static_cast<std::uint64_t>(value) + 0x9e3779b97f4a7c15ULL;
    wide = (wide ^ (wide >> 30U)) * 0xbf58476d1ce4e5b9ULL;
    wide = (wide ^ (wide >> 27U)) * 0x94d049bb133111ebULL;
    wide ^= wide >> 31U;

    // Never zero: an xorshift seeded with zero stays there.
    const auto seed = static_cast<std::uint32_t>(wide);
    return seed == 0 ? 1U : seed;
}

}  // namespace

PcmQuantiser::PcmQuantiser(int bits, int channels, DitherMode mode, std::uint32_t seed)
    : bits_(juce::jlimit(2, 32, bits)),
      mode_(mode),
      lsb_(static_cast<float>(1.0 / scaleFor(juce::jlimit(2, 32, bits)))),
      scale_(scaleFor(juce::jlimit(2, 32, bits))),
      maxCode_(maxCodeFor(juce::jlimit(2, 32, bits))),
      seed_(seed) {
    channels_.resize(static_cast<std::size_t>(juce::jmax(1, channels)));
    reset();
}

void PcmQuantiser::reset() {
    for (std::size_t channel = 0; channel < channels_.size(); ++channel) {
        auto& state = channels_[channel];

        // Per channel, and not one sequence shared between them. A shared one
        // hands a channel different draws when the same render is cut into
        // different blocks, and OfflineRenderRequest promises that block size
        // is a batching choice and nothing else.
        state.random = mix(seed_ + static_cast<std::uint32_t>(channel));
        state.errors.fill(0.0);
    }
}

double PcmQuantiser::nextUniform(std::uint32_t& state) {
    // xorshift32: the same sequence on every platform, and long enough for a
    // render. Nothing here is cryptographic.
    state ^= state << 13U;
    state ^= state >> 17U;
    state ^= state << 5U;

    // The top 24 bits, which is where an xorshift's quality lives, on [-0.5, 0.5).
    constexpr double kInverse = 1.0 / 16777216.0;
    return (static_cast<double>(state >> 8U) * kInverse) - 0.5;
}

void PcmQuantiser::process(juce::AudioBuffer<float>& buffer, int numSamples) {
    quantise(buffer, numSamples, nullptr);
}

void PcmQuantiser::processToCodes(juce::AudioBuffer<float>& buffer, int numSamples,
                                  int* const* codes) {
    quantise(buffer, numSamples, codes);
}

void PcmQuantiser::quantise(juce::AudioBuffer<float>& buffer, int numSamples, int* const* codes) {
    const auto channels = juce::jmin(buffer.getNumChannels(), static_cast<int>(channels_.size()));
    const auto count = juce::jmin(numSamples, buffer.getNumSamples());

    for (auto channel = 0; channel < channels; ++channel) {
        auto* samples = buffer.getWritePointer(channel);
        auto& state = channels_[static_cast<std::size_t>(channel)];

        for (auto at = 0; at < count; ++at) {
            // In double for the whole step. At 24 bits a code near full scale
            // is around eight million, where a float's own spacing is a whole
            // LSB: adding half a step to one in float rounds the dither away
            // before nearbyint sees it, and what is left depends on the
            // signal's parity rather than on the noise.
            auto value = static_cast<double>(samples[at]) * scale_;

            if (mode_ == DitherMode::shaped)
                for (auto tap = 0; tap < kShaperTaps; ++tap)
                    value += kFWeighted[static_cast<std::size_t>(tap)] *
                             state.errors[static_cast<std::size_t>(tap)];

            const auto wanted = value;

            if (mode_ != DitherMode::none)
                value += nextUniform(state.random) + nextUniform(state.random);

            const auto rounded = std::nearbyint(value);

            if (mode_ == DitherMode::shaped) {
                for (auto tap = kShaperTaps - 1; tap > 0; --tap)
                    state.errors[static_cast<std::size_t>(tap)] =
                        state.errors[static_cast<std::size_t>(tap - 1)];

                // Measured against the value the shaper asked for, before the
                // dither, so what is fed back is everything the round and the
                // dither together did to it. Shaping only the round would
                // leave the larger half of the added noise flat.
                state.errors[0] = wanted - rounded;
            }

            // Clipped where the writer clips. A code past the end is stored as
            // the end whatever this does, and letting it through would put the
            // shaper's feedback a step away from what the file holds.
            const auto code = juce::jlimit(-maxCode_, maxCode_, rounded);
            samples[at] = static_cast<float>(code / scale_);

            if (codes != nullptr)
                codes[channel][at] = static_cast<int>(static_cast<std::int64_t>(code)
                                                      << static_cast<unsigned>(32 - bits_));
        }
    }
}

}  // namespace magda::engine
