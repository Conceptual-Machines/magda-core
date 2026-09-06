#include "io/PcmQuantiser.hpp"

#include <cmath>

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
constexpr float kFWeighted[PcmQuantiser::kShaperTaps] = {2.412f,  -3.370f, 3.937f,  -4.174f, 3.353f,
                                                         -2.205f, 1.281f,  -0.569f, 0.0847f};

/// The largest magnitude a signed sample of @p bits can hold, as a float. The
/// negative side reaches one step further, and the positive side is what a
/// conversion has to stay inside.
float fullScaleFor(int bits) {
    return static_cast<float>((1ULL << static_cast<unsigned>(bits - 1)) - 1ULL);
}

}  // namespace

PcmQuantiser::PcmQuantiser(int bits, int channels, DitherMode mode, std::uint32_t seed)
    : bits_(juce::jlimit(2, 32, bits)),
      mode_(mode),
      lsb_(1.0f / fullScaleFor(juce::jlimit(2, 32, bits))),
      scale_(fullScaleFor(juce::jlimit(2, 32, bits))),
      seed_(seed),
      state_(seed) {
    errors_.setSize(juce::jmax(1, channels), kShaperTaps);
    errors_.clear();
}

void PcmQuantiser::reset() {
    state_ = seed_;
    errors_.clear();
}

float PcmQuantiser::nextUniform() {
    // xorshift32: one multiply-free step, the same sequence on every platform,
    // and long enough for a render. Nothing here is cryptographic and nothing
    // here should be a std::mt19937 the size of a cache line.
    state_ ^= state_ << 13U;
    state_ ^= state_ >> 17U;
    state_ ^= state_ << 5U;

    // The top 24 bits, which is where an xorshift's quality lives, mapped onto
    // [-0.5, 0.5).
    constexpr float kInverse = 1.0f / 16777216.0f;
    return (static_cast<float>(state_ >> 8U) * kInverse) - 0.5f;
}

void PcmQuantiser::process(juce::AudioBuffer<float>& buffer, int numSamples) {
    const auto channels = juce::jmin(buffer.getNumChannels(), errors_.getNumChannels());
    const auto count = juce::jmin(numSamples, buffer.getNumSamples());

    for (auto channel = 0; channel < channels; ++channel) {
        auto* samples = buffer.getWritePointer(channel);
        auto* history = errors_.getWritePointer(channel);

        for (auto at = 0; at < count; ++at) {
            // In the target's own units, so one LSB is one and the round is a
            // round. Coming back out is the only place the scale is undone.
            auto value = samples[at] * scale_;

            if (mode_ == DitherMode::shaped) {
                // The error the last nine samples were left with, weighted.
                // Added rather than subtracted: the history holds what the
                // round took away, so feeding it forward is what puts the noise
                // where the curve wants it.
                for (auto tap = 0; tap < kShaperTaps; ++tap)
                    value += kFWeighted[tap] * history[tap];
            }

            const auto wanted = value;

            if (mode_ != DitherMode::none)
                value += nextUniform() + nextUniform();

            const auto rounded = std::nearbyint(value);

            if (mode_ == DitherMode::shaped) {
                for (auto tap = kShaperTaps - 1; tap > 0; --tap)
                    history[tap] = history[tap - 1];

                // Measured against the value the shaper asked for, before the
                // dither, so what is fed back is everything the round and the
                // dither together did to it. Shaping only the round would
                // leave the larger half of the added noise flat.
                history[0] = wanted - rounded;
            }

            // Clipped rather than wrapped. A sample at full scale plus a dither
            // step is over the top of the format, and the alternative to
            // holding it there is a sign flip: the loudest possible click.
            samples[at] = juce::jlimit(-1.0f, 1.0f, rounded / scale_);
        }
    }
}

}  // namespace magda::engine
