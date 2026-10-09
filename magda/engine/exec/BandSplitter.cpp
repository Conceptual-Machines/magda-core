#include "exec/BandSplitter.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace magda::engine {

namespace {

// The Butterworth section Qs a Linkwitz-Riley filter squares: LR4 is two Butterworth 2nd orders,
// LR8 two Butterworth 4th orders.
constexpr double kButterworth2Q = 1.0 / std::numbers::sqrt2;
constexpr std::array<double, 2> kButterworth4Q{0.54119610014619698, 1.3065629648763766};

sdk::BiquadCoeffs<double> allPass(double sampleRate, double frequency, double q) {
    const double w = 2.0 * std::numbers::pi * frequency / sampleRate;
    const double alpha = std::sin(w) / (2.0 * q);
    const double a0 = 1.0 + alpha;
    return {(1.0 - alpha) / a0, -2.0 * std::cos(w) / a0, 1.0, -2.0 * std::cos(w) / a0,
            (1.0 - alpha) / a0};
}

sdk::BiquadCoeffs<double> firstOrderAllPass(double sampleRate, double frequency) {
    const double t = std::tan(std::numbers::pi * frequency / sampleRate);
    const double a = (t - 1.0) / (t + 1.0);
    return {a, 1.0, 0.0, a, 0.0};
}

}  // namespace

void BandSplitter::prepare(double sampleRate, int maxBlockSize) {
    sampleRate_ = sampleRate;
    rest_.assign(static_cast<std::size_t>(std::max(maxBlockSize, 1)), 0.0f);
    for (int i = 0; i < std::max(crossoverCount_, 0); ++i)
        designStage(stages_[static_cast<std::size_t>(i)],
                    stages_[static_cast<std::size_t>(i)].crossover);
    reset();
}

void BandSplitter::reset() {
    lowState_ = {};
    highState_ = {};
    allPassState_ = {};
}

void BandSplitter::designStage(Stage& stage, const Crossover& crossover) const {
    stage.crossover = crossover;
    const double hz =
        std::clamp(static_cast<double>(crossover.frequencyHz), 10.0, sampleRate_ * 0.49);
    switch (crossover.slope) {
        case CrossoverSlope::Db12:
            // LR2's halves sum to an allpass only with the high side inverted.
            stage.passSections = 1;
            stage.lowPass[0] = sdk::biquad::lowPass(sampleRate_, hz, 0.5);
            stage.highPass[0] = sdk::biquad::highPass(sampleRate_, hz, 0.5);
            stage.highPassSign = -1.0;
            stage.allPassSections = 1;
            stage.allPass[0] = firstOrderAllPass(sampleRate_, hz);
            break;
        case CrossoverSlope::Db24:
            stage.passSections = 2;
            for (int i = 0; i < 2; ++i) {
                stage.lowPass[static_cast<std::size_t>(i)] =
                    sdk::biquad::lowPass(sampleRate_, hz, kButterworth2Q);
                stage.highPass[static_cast<std::size_t>(i)] =
                    sdk::biquad::highPass(sampleRate_, hz, kButterworth2Q);
            }
            stage.highPassSign = 1.0;
            stage.allPassSections = 1;
            stage.allPass[0] = allPass(sampleRate_, hz, kButterworth2Q);
            break;
        case CrossoverSlope::Db48:
            stage.passSections = 4;
            for (std::size_t i = 0; i < 4; ++i) {
                const double q = kButterworth4Q[i % 2];
                stage.lowPass[i] = sdk::biquad::lowPass(sampleRate_, hz, q);
                stage.highPass[i] = sdk::biquad::highPass(sampleRate_, hz, q);
            }
            stage.highPassSign = 1.0;
            stage.allPassSections = 2;
            for (std::size_t i = 0; i < 2; ++i)
                stage.allPass[i] = allPass(sampleRate_, hz, kButterworth4Q[i]);
            break;
    }
}

void BandSplitter::setCrossovers(std::span<const Crossover> crossovers) {
    const int count = std::min(static_cast<int>(crossovers.size()), kMaxCrossovers);
    for (int i = 0; i < count; ++i) {
        auto& stage = stages_[static_cast<std::size_t>(i)];
        const auto& crossover = crossovers[static_cast<std::size_t>(i)];
        if (i < crossoverCount_ && stage.crossover == crossover)
            continue;
        // A new slope runs a different set of sections, whose old state means nothing.
        if (i >= crossoverCount_ || stage.crossover.slope != crossover.slope) {
            lowState_[static_cast<std::size_t>(i)] = {};
            highState_[static_cast<std::size_t>(i)] = {};
            for (auto& band : allPassState_)
                band[static_cast<std::size_t>(i)] = {};
        }
        designStage(stage, crossover);
    }
    crossoverCount_ = count;
}

void BandSplitter::process(std::span<const float* const> input, std::span<float* const> bands,
                           int numSamples) {
    const int channels = std::min(static_cast<int>(input.size()), kMaxChannels);
    if (channels == 0)
        return;
    const int bandCount = static_cast<int>(bands.size()) / static_cast<int>(input.size());
    const auto out = [&](int band, int channel) {
        return bands[static_cast<std::size_t>(band * static_cast<int>(input.size()) + channel)];
    };
    const auto count = static_cast<std::size_t>(numSamples);

    if (!isConfigured() || bandCount < 2) {
        for (int c = 0; c < channels; ++c) {
            std::copy_n(input[static_cast<std::size_t>(c)], count, out(0, c));
            for (int b = 1; b < bandCount; ++b)
                std::fill_n(out(b, c), count, 0.0f);
        }
        return;
    }

    const int splits = std::min(crossoverCount_, bandCount - 1);
    for (int c = 0; c < channels; ++c) {
        const auto ch = static_cast<std::size_t>(c);
        float* rest = rest_.data();
        std::copy_n(input[ch], count, rest);

        for (int k = 0; k < splits; ++k) {
            const auto& stage = stages_[static_cast<std::size_t>(k)];
            auto& low = lowState_[static_cast<std::size_t>(k)][ch];
            auto& high = highState_[static_cast<std::size_t>(k)][ch];
            float* band = out(k, c);
            for (std::size_t i = 0; i < count; ++i) {
                double lp = rest[i];
                double hp = rest[i];
                for (int s = 0; s < stage.passSections; ++s) {
                    const auto section = static_cast<std::size_t>(s);
                    lp = sdk::processBiquad(stage.lowPass[section], low[section], lp);
                    hp = sdk::processBiquad(stage.highPass[section], high[section], hp);
                }
                band[i] = static_cast<float>(lp);
                rest[i] = static_cast<float>(stage.highPassSign * hp);
            }
        }
        std::copy_n(rest, count, out(splits, c));
        for (int b = splits + 1; b < bandCount; ++b)
            std::fill_n(out(b, c), count, 0.0f);

        for (int b = 0; b < splits; ++b) {
            float* band = out(b, c);
            for (int j = b + 1; j < splits; ++j) {
                const auto& stage = stages_[static_cast<std::size_t>(j)];
                auto& state =
                    allPassState_[static_cast<std::size_t>(b)][static_cast<std::size_t>(j)][ch];
                for (int s = 0; s < stage.allPassSections; ++s) {
                    const auto section = static_cast<std::size_t>(s);
                    for (std::size_t i = 0; i < count; ++i)
                        band[i] = static_cast<float>(sdk::processBiquad(
                            stage.allPass[section], state[section], static_cast<double>(band[i])));
                }
            }
        }
    }
}

}  // namespace magda::engine
