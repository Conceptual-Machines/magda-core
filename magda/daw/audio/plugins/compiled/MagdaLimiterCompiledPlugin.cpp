#include "plugins/compiled/MagdaLimiterCompiledPlugin.hpp"

#include <algorithm>
#include <cmath>

#include "core/ParameterInfo.hpp"
#include "plugins/compiled/CompiledPluginRegistry.hpp"

namespace magda::daw::audio::compiled {

const char* MagdaLimiterCompiledPlugin::xmlTypeName = "magda_limiter";

namespace {

float ampToDb(float amp) {
    return 20.0f * std::log10(std::max(amp, 1.0e-6f));
}

}  // namespace

MagdaLimiterCompiledPlugin::MagdaLimiterCompiledPlugin() {
    initEffect();
}

std::vector<MagdaLimiterCompiledPlugin::HostSlotInfo> MagdaLimiterCompiledPlugin::slotInfos()
    const {
    std::vector<HostSlotInfo> infos(kHostSlotCount);
    infos[kThresholdSlot] = {.name = "Threshold",
                             .unit = magda::technicalText(magda::TechnicalTextToken::Decibels),
                             .scale = magda::ParameterScale::Linear,
                             .minValue = -24.0f,
                             .maxValue = 0.0f,
                             .defaultValue = -1.0f};
    infos[kAttackSlot] = {.name = "Attack",
                          .unit = magda::technicalText(magda::TechnicalTextToken::Milliseconds),
                          .scale = magda::ParameterScale::Logarithmic,
                          .minValue = 0.1f,
                          .maxValue = 50.0f,
                          .defaultValue = 1.0f,
                          .scaleAnchor = 1.0f};
    infos[kReleaseSlot] = {.name = "Release",
                           .unit = magda::technicalText(magda::TechnicalTextToken::Milliseconds),
                           .scale = magda::ParameterScale::Logarithmic,
                           .minValue = 10.0f,
                           .maxValue = 2000.0f,
                           .defaultValue = 200.0f,
                           .scaleAnchor = 200.0f};
    infos[kOutputSlot] = {.name = "Output",
                          .unit = magda::technicalText(magda::TechnicalTextToken::Decibels),
                          .scale = magda::ParameterScale::Linear,
                          .minValue = -24.0f,
                          .maxValue = 0.0f,
                          .defaultValue = 0.0f};

    return infos;
}

float MagdaLimiterDspCore::dbToGain(float db) {
    return std::pow(10.0f, db / 20.0f);
}

float MagdaLimiterDspCore::coefficient(float timeMs, double sampleRate) {
    const auto samples = std::max(1.0, static_cast<double>(timeMs) * 0.001 * sampleRate);
    return static_cast<float>(std::exp(-1.0 / samples));
}

void MagdaLimiterDspCore::prepare(double sampleRate, int, int numChannels) {
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 44100.0;
    delaySamples_ = std::max(1, static_cast<int>(std::ceil(sampleRate_ * 0.005)));
    const auto channels = static_cast<size_t>(std::max(1, numChannels));
    const auto lineLength = static_cast<size_t>(delaySamples_ + 1);

    delayLines_.assign(channels, std::vector<float>(lineLength, 0.0f));
    frame_.assign(channels, 0.0f);
    reset();
}

void MagdaLimiterDspCore::reset() {
    writeIndex_ = 0;
    gain_ = 1.0f;
    for (auto& line : delayLines_)
        std::fill(line.begin(), line.end(), 0.0f);
}

MagdaLimiterDspCore::Stats MagdaLimiterDspCore::process(juce::AudioBuffer<float>& buffer,
                                                        int startSample, int numSamples,
                                                        const Settings& settings) {
    Stats stats;
    const int channels = buffer.getNumChannels();
    if (channels <= 0 || numSamples <= 0)
        return stats;

    if (static_cast<int>(delayLines_.size()) < channels)
        prepare(sampleRate_, numSamples, channels);

    const float thresholdDb = juce::jlimit(-24.0f, 0.0f, settings.thresholdDb);
    const float preGain = dbToGain(-thresholdDb);
    const float outputGain = dbToGain(juce::jlimit(-24.0f, 0.0f, settings.outputDb));
    const float attackCoeff = coefficient(std::max(0.1f, settings.attackMs), sampleRate_);
    const float releaseCoeff = coefficient(std::max(10.0f, settings.releaseMs), sampleRate_);
    const int lineLength = static_cast<int>(delayLines_.front().size());

    float maxReduction = 0.0f;
    for (int i = 0; i < numSamples; ++i) {
        float detectorPeak = 0.0f;
        for (int ch = 0; ch < channels; ++ch) {
            const float input = buffer.getSample(ch, startSample + i);
            const float finiteInput = std::isfinite(input) ? input : 0.0f;
            stats.inputPeak = std::max(stats.inputPeak, std::abs(finiteInput));

            const float driven = finiteInput * preGain;
            delayLines_[static_cast<size_t>(ch)][static_cast<size_t>(writeIndex_)] = driven;
            detectorPeak = std::max(detectorPeak, std::abs(driven));
        }

        const float desiredGain = detectorPeak > 1.0f ? 1.0f / detectorPeak : 1.0f;
        const float coeff = desiredGain < gain_ ? attackCoeff : releaseCoeff;
        gain_ = desiredGain + coeff * (gain_ - desiredGain);
        maxReduction = std::max(maxReduction, gain_ < 1.0f ? -ampToDb(gain_) : 0.0f);

        const int readIndex = (writeIndex_ + 1) % lineLength;
        float postPeak = 0.0f;
        for (int ch = 0; ch < channels; ++ch) {
            const float limited =
                delayLines_[static_cast<size_t>(ch)][static_cast<size_t>(readIndex)] * gain_;
            frame_[static_cast<size_t>(ch)] = limited;
            postPeak = std::max(postPeak, std::abs(limited));
        }

        const float safetyGain = postPeak > 1.0f ? 1.0f / postPeak : 1.0f;
        for (int ch = 0; ch < channels; ++ch) {
            const float output = frame_[static_cast<size_t>(ch)] * safetyGain * outputGain;
            const float clean = std::isfinite(output) ? juce::jlimit(-1.0f, 1.0f, output) : 0.0f;
            buffer.setSample(ch, startSample + i, clean);
            stats.outputPeak = std::max(stats.outputPeak, std::abs(clean));
        }

        writeIndex_ = (writeIndex_ + 1) % lineLength;
    }

    stats.gainReductionDb = maxReduction;
    return stats;
}

void MagdaLimiterCompiledPlugin::onPrepare(double sampleRate, int maximumBlockSize) {
    limiter_.prepare(sampleRate, maximumBlockSize, 2);
}

void MagdaLimiterCompiledPlugin::onRelease() {
    limiter_.reset();
}

void MagdaLimiterCompiledPlugin::onReset() {
    limiter_.reset();
}

void MagdaLimiterCompiledPlugin::processAudio(DeviceProcessContext& context) {
    // No Faust engine: the lookahead line and its gain follower are MAGDA's own,
    // so the base's zone-writing pass has nothing to write and this is the whole
    // block.
    const MagdaLimiterDspCore::Settings settings{
        .thresholdDb = slotDisplayValue(kThresholdSlot),
        .attackMs = slotDisplayValue(kAttackSlot),
        .releaseMs = slotDisplayValue(kReleaseSlot),
        .outputDb = slotDisplayValue(kOutputSlot),
    };

    const auto stats =
        limiter_.process(*context.audio, context.startSample, context.numSamples, settings);

    inputPeakDb_.store(ampToDb(stats.inputPeak), std::memory_order_relaxed);
    outputPeakDb_.store(ampToDb(stats.outputPeak), std::memory_order_relaxed);
    gainReductionDb_.store(stats.gainReductionDb, std::memory_order_relaxed);
}

constexpr AliasSpec kAliases[] = {
    {"threshold", 0, "Threshold"},
    {"attack", 1, "Attack"},
    {"release", 2, "Release"},
    {"output", 3, "Output"},
};

const CompiledPluginSpec& getMagdaLimiterSpec() {
    static const CompiledPluginSpec kSpec{
        .pluginId = MagdaLimiterCompiledPlugin::xmlTypeName,
        .displayName = "Limiter",
        .browserCategory = "Dynamics",
        .description = "Native stereo lookahead limiter / autonormalizer. "
                       "Threshold drives the signal into a fixed 0 dB ceiling, "
                       "Attack and Release shape gain recovery, and Output is a "
                       "post-limiter trim limited to negative gain.",
        .createDevice = [](const DevicePluginCreationContext&) -> std::unique_ptr<MagdaDevice> {
            return std::make_unique<MagdaLimiterCompiledPlugin>();
        },
        .aliases = kAliases,
        .aliasCount = static_cast<int>(sizeof(kAliases) / sizeof(kAliases[0])),
    };
    return kSpec;
}

}  // namespace magda::daw::audio::compiled
