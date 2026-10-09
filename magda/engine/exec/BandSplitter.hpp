#pragma once

#include <array>
#include <magda/sdk/dsp/Biquad.hpp>
#include <span>
#include <vector>

#include "core/Crossover.hpp"

namespace magda::engine {

/**
 * @brief A multiband rack's crossover: splits a signal into bands that sum back flat.
 *
 * Linkwitz-Riley at each crossover, split in a tree from the bottom up. Every band below a
 * crossover also passes that crossover's allpass, so all bands share one phase response and their
 * sum is an allpass of the input, never a notch.
 */
class BandSplitter {
  public:
    static constexpr int kMaxChannels = 2;
    static constexpr int kMaxBands = kMaxCrossovers + 1;

    /** Allocates the scratch the split works in. Off the audio thread. */
    void prepare(double sampleRate, int maxBlockSize);
    void reset();
    bool hasConfiguration(double sampleRate, int maxBlockSize) const {
        return sampleRate_ == sampleRate && static_cast<int>(rest_.size()) >= maxBlockSize;
    }

    /** Redesigns only the crossovers that changed. Real-time safe. */
    void setCrossovers(std::span<const Crossover> crossovers);
    bool isConfigured() const {
        return crossoverCount_ >= 0;
    }

    /**
     * @param input  @p channels input channels.
     * @param bands  band-major outputs: band b, channel c is `bands[b * channels + c]`.
     *
     * Unconfigured, the input goes to the lowest band and the rest are silent.
     */
    void process(std::span<const float* const> input, std::span<float* const> bands,
                 int numSamples);

  private:
    using Coeffs = sdk::BiquadCoeffs<double>;
    using State = sdk::BiquadState<double>;

    struct Stage {
        Crossover crossover;
        std::array<Coeffs, 4> lowPass{};
        std::array<Coeffs, 4> highPass{};
        std::array<Coeffs, 2> allPass{};
        int passSections = 0;
        int allPassSections = 0;
        double highPassSign = 1.0;
    };

    void designStage(Stage& stage, const Crossover& crossover) const;

    double sampleRate_ = 44100.0;
    int crossoverCount_ = -1;
    std::array<Stage, kMaxCrossovers> stages_{};

    std::array<std::array<std::array<State, 4>, kMaxChannels>, kMaxCrossovers> lowState_{};
    std::array<std::array<std::array<State, 4>, kMaxChannels>, kMaxCrossovers> highState_{};
    /// Band b's pass through crossover j's allpass, for j above b.
    std::array<std::array<std::array<std::array<State, 2>, kMaxChannels>, kMaxCrossovers>,
               kMaxBands>
        allPassState_{};

    std::vector<float> rest_;
};

}  // namespace magda::engine
