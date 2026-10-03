#include "devices/faust/effects/Limiter.hpp"

#include <algorithm>
#include <bit>
#include <cmath>

namespace magda::devices::faust {

namespace {

float ampToDb(float amp) {
    return 20.0f * std::log10(std::max(amp, 1.0e-6f));
}

}  // namespace

Limiter::Limiter() {
    initEffect();
}

std::vector<SlotInfo> Limiter::slotInfos() const {
    std::vector<SlotInfo> infos(kHostSlotCount);
    infos[kThresholdSlot] = {.name = "Threshold",
                             .unit = "dB",
                             .scale = sdk::ParameterScale::Linear,
                             .minValue = -24.0f,
                             .maxValue = 0.0f,
                             .defaultValue = -1.0f};
    infos[kAttackSlot] = {.name = "Attack",
                          .unit = "ms",
                          .scale = sdk::ParameterScale::Logarithmic,
                          .minValue = 0.1f,
                          .maxValue = 50.0f,
                          .defaultValue = 1.0f,
                          .scaleAnchor = 1.0f};
    infos[kReleaseSlot] = {.name = "Release",
                           .unit = "ms",
                           .scale = sdk::ParameterScale::Logarithmic,
                           .minValue = 10.0f,
                           .maxValue = 2000.0f,
                           .defaultValue = 200.0f,
                           .scaleAnchor = 200.0f};
    infos[kOutputSlot] = {.name = "Output",
                          .unit = "dB",
                          .scale = sdk::ParameterScale::Linear,
                          .minValue = -24.0f,
                          .maxValue = 0.0f,
                          .defaultValue = 0.0f};

    return infos;
}

float LimiterDspCore::dbToGain(float db) {
    return std::pow(10.0f, db / 20.0f);
}

float LimiterDspCore::coefficient(float timeMs, double sampleRate) {
    const auto samples = std::max(1.0, static_cast<double>(timeMs) * 0.001 * sampleRate);
    return static_cast<float>(std::exp(-1.0 / samples));
}

void LimiterDspCore::prepare(double sampleRate, int, int numChannels) {
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 44100.0;
    delaySamples_ = std::max(1, static_cast<int>(std::lrint(sampleRate_ * kLookaheadSeconds)));
    numLines_ = std::max(1, numChannels);
    lineStride_ = static_cast<int>(std::bit_ceil(static_cast<unsigned>(delaySamples_ + 1)));
    lineMask_ = lineStride_ - 1;

    delayLines_.assign(static_cast<size_t>(numLines_) * static_cast<size_t>(lineStride_), 0.0f);
    frame_.assign(static_cast<size_t>(numLines_), 0.0f);
    reset();
}

void LimiterDspCore::reset() {
    writeIndex_ = 0;
    gain_ = 1.0f;
    std::fill(delayLines_.begin(), delayLines_.end(), 0.0f);
}

LimiterDspCore::Stats LimiterDspCore::process(magda::BufferView buffer, int startSample,
                                              int numSamples, const Settings& settings) {
    Stats stats;
    const int channels = buffer.numChannels();
    if (channels <= 0 || numSamples <= 0)
        return stats;

    if (numLines_ < channels)
        prepare(sampleRate_, numSamples, channels);

    const float thresholdDb = std::clamp(settings.thresholdDb, -24.0f, 0.0f);
    const float preGain = dbToGain(-thresholdDb);
    const float outputGain = dbToGain(std::clamp(settings.outputDb, -24.0f, 0.0f));
    const float attackCoeff = coefficient(std::max(0.1f, settings.attackMs), sampleRate_);
    const float releaseCoeff = coefficient(std::max(10.0f, settings.releaseMs), sampleRate_);
    // The gain follower is a recursion, so the sample loop stays serial; what it
    // does not need is a bounds-checked accessor and a division per sample.
    float* const* const io = buffer.channels();
    float* const lines = delayLines_.data();
    float* const frame = frame_.data();

    float maxReduction = 0.0f;
    for (int i = 0; i < numSamples; ++i) {
        const int sample = startSample + i;
        const int readIndex = (writeIndex_ + lineStride_ - delaySamples_) & lineMask_;

        float detectorPeak = 0.0f;
        for (int ch = 0; ch < channels; ++ch) {
            const float input = io[ch][sample];
            const float finiteInput = std::isfinite(input) ? input : 0.0f;
            stats.inputPeak = std::max(stats.inputPeak, std::abs(finiteInput));

            const float driven = finiteInput * preGain;
            lines[ch * lineStride_ + writeIndex_] = driven;
            detectorPeak = std::max(detectorPeak, std::abs(driven));
        }

        const float desiredGain = detectorPeak > 1.0f ? 1.0f / detectorPeak : 1.0f;
        const float coeff = desiredGain < gain_ ? attackCoeff : releaseCoeff;
        gain_ = desiredGain + coeff * (gain_ - desiredGain);
        maxReduction = std::max(maxReduction, gain_ < 1.0f ? -ampToDb(gain_) : 0.0f);

        float postPeak = 0.0f;
        for (int ch = 0; ch < channels; ++ch) {
            const float limited = lines[ch * lineStride_ + readIndex] * gain_;
            frame[ch] = limited;
            postPeak = std::max(postPeak, std::abs(limited));
        }

        const float safetyGain = postPeak > 1.0f ? 1.0f / postPeak : 1.0f;
        for (int ch = 0; ch < channels; ++ch) {
            const float output = frame[ch] * safetyGain * outputGain;
            const float clean = std::isfinite(output) ? std::clamp(output, -1.0f, 1.0f) : 0.0f;
            io[ch][sample] = clean;
            stats.outputPeak = std::max(stats.outputPeak, std::abs(clean));
        }

        writeIndex_ = (writeIndex_ + 1) & lineMask_;
    }

    stats.gainReductionDb = maxReduction;
    return stats;
}

void Limiter::onPrepare(double sampleRate, int maximumBlockSize) {
    limiter_.prepare(sampleRate, maximumBlockSize, 2);
}

void Limiter::onRelease() {
    limiter_.reset();
}

void Limiter::onReset() {
    limiter_.reset();
}

void Limiter::processAudio(sdk::ProcessContext& context) {
    // No Faust engine: the lookahead line and its gain follower are the whole block.
    const LimiterDspCore::Settings settings{
        .thresholdDb = slotDisplayValue(kThresholdSlot),
        .attackMs = slotDisplayValue(kAttackSlot),
        .releaseMs = slotDisplayValue(kReleaseSlot),
        .outputDb = slotDisplayValue(kOutputSlot),
    };

    const auto stats = limiter_.process(context.audio, 0, context.numSamples(), settings);

    inputPeakDb_.store(ampToDb(stats.inputPeak), std::memory_order_relaxed);
    outputPeakDb_.store(ampToDb(stats.outputPeak), std::memory_order_relaxed);
    gainReductionDb_.store(stats.gainReductionDb, std::memory_order_relaxed);
}

}  // namespace magda::devices::faust
