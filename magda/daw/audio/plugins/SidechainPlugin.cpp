#include "plugins/SidechainPlugin.hpp"

#include <cmath>

namespace magda::daw::audio {

const char* SidechainPlugin::xmlTypeName = "sidechain";

namespace {

// One-pole smoothing coefficient for a time constant in milliseconds.
// 0 ms returns 1 (instant).
float smoothingCoeff(float ms, double sampleRate) {
    if (ms <= 0.0f)
        return 1.0f;
    return 1.0f - std::exp(-1.0f / (ms * 0.001f * static_cast<float>(sampleRate)));
}

// Upper bound on the reconstructed stair width, and on the idle counter that
// measures it (which would otherwise grow without bound and overflow).
constexpr int kMaxStairSamples = 4096;

/// The slot table. The ids are pinned rather than derived from the names
/// because they key saved state, and "channelMode" is not what a name-derived
/// scheme would make of "Channel Mode".
ParameterInfo slotInfo(int index) {
    ParameterInfo info;
    info.paramIndex = index;

    switch (index) {
        case SidechainPlugin::kGainParamIndex:
            // The duck target the bundled curve modulator drives. It rests at
            // unity; users control duck intensity through the mod link's depth,
            // not by moving this parameter.
            info.stableId = "gain";
            info.name = "Gain";
            info.minValue = 0.0f;
            info.maxValue = 1.0f;
            info.defaultValue = 1.0f;
            break;

        case SidechainPlugin::kAttackParamIndex:
            info.stableId = "attack";
            info.name = "Attack";
            info.unit = "ms";
            info.minValue = 0.0f;
            info.maxValue = 50.0f;
            info.defaultValue = 1.0f;
            break;

        case SidechainPlugin::kReleaseParamIndex:
            info.stableId = "release";
            info.name = "Release";
            info.unit = "ms";
            info.minValue = 0.0f;
            info.maxValue = 500.0f;
            info.defaultValue = 15.0f;
            break;

        case SidechainPlugin::kChannelModeParamIndex:
            info.stableId = "channelMode";
            info.name = "Channel Mode";
            info.scale = ParameterScale::Discrete;
            info.minValue = 0.0f;
            info.maxValue = 1.0f;
            info.defaultValue = 0.0f;
            info.choices = {"Stereo", "Sides"};
            break;

        default:
            break;
    }

    return info;
}

}  // namespace

SidechainPlugin::SidechainPlugin() {
    for (int index = 0; index < kParamCount; ++index) {
        const auto info = slotInfo(index);
        domains_[static_cast<size_t>(index)] = ParameterUtils::domainOf(info);
        values_[static_cast<size_t>(index)] =
            ParameterUtils::realToNormalized(info.defaultValue, info);
    }
}

ParameterInfo SidechainPlugin::parameterInfo(int index) const {
    if (index < 0 || index >= kParamCount)
        return {};
    return slotInfo(index);
}

float SidechainPlugin::parameterValue(int index) const {
    if (index < 0 || index >= kParamCount)
        return 0.0f;
    return values_[static_cast<size_t>(index)];
}

void SidechainPlugin::setParameterValue(int index, float value) {
    if (index < 0 || index >= kParamCount)
        return;
    values_[static_cast<size_t>(index)] = juce::jlimit(0.0f, 1.0f, value);
}

float SidechainPlugin::displayValue(int index) const {
    return ParameterUtils::normalizedToReal(values_[static_cast<size_t>(index)],
                                            domains_[static_cast<size_t>(index)]);
}

void SidechainPlugin::prepare(const DevicePrepareContext& context) {
    sampleRate_ = context.sampleRate;
    reset();
}

void SidechainPlugin::reset() {
    currentGain_ = displayValue(kGainParamIndex);
    lastTarget_ = currentGain_;
    rampValue_ = currentGain_;
    rampStep_ = 0.0f;
    rampSamplesLeft_ = 0;
    samplesSinceChange_ = 0;
}

void SidechainPlugin::process(DeviceProcessContext& context) {
    if (context.audio == nullptr || context.numSamples <= 0)
        return;

    const float target = displayValue(kGainParamIndex);
    // Anti-click floors, not user-range mins: with 0 ms the gain trajectory
    // has raw corners - most audibly where the steep recovery ramp freezes
    // the instant it reaches full level (a slope discontinuity at maximum
    // loudness). A fraction of a millisecond keeps the duck hit effectively
    // instant; a few milliseconds of release round the arrival corner with
    // no audible loudness cost.
    const float attackCoeff =
        smoothingCoeff(juce::jmax(0.3f, displayValue(kAttackParamIndex)), sampleRate_);
    const float releaseCoeff =
        smoothingCoeff(juce::jmax(5.0f, displayValue(kReleaseParamIndex)), sampleRate_);

    const int numChannels = context.audio->getNumChannels();
    const int numSamples = context.numSamples;
    auto channels = context.audio->getArrayOfWritePointers();
    const int offset = context.startSample;

    // The modifier writes the gain target at a coarse quantum (one hop per
    // modifier update, several render blocks wide), so the drawn curve
    // arrives undersampled: the log showed hops of 0.4+ gain where the curve
    // is steep, and the one-shot latch lands as one final hop. Reconstruct:
    // ramp upward (recovery) steps over the measured stair width so the
    // recovery is continuous at source; ramp downward steps (the duck hit)
    // within one block so the onset stays punchy. The attack/release poles
    // then only shape, they no longer have to hide discontinuities.
    if (target != lastTarget_) {
        const bool goingDown = target < rampValue_;
        const int width =
            goingDown ? numSamples
                      : juce::jmax(numSamples, juce::jmin(kMaxStairSamples, samplesSinceChange_));
        rampStep_ = (target - rampValue_) / static_cast<float>(width);
        rampSamplesLeft_ = width;
        samplesSinceChange_ = 0;
        lastTarget_ = target;
    }
    samplesSinceChange_ = juce::jmin(samplesSinceChange_ + numSamples, kMaxStairSamples);

    const bool sidesOnly = juce::roundToInt(displayValue(kChannelModeParamIndex)) ==
                               static_cast<int>(ChannelMode::Sides) &&
                           numChannels >= 2;
    float gain = currentGain_;
    for (int i = 0; i < numSamples; ++i) {
        if (rampSamplesLeft_ > 0) {
            rampValue_ += rampStep_;
            --rampSamplesLeft_;
            if (rampSamplesLeft_ == 0)
                rampValue_ = lastTarget_;  // land exactly, no float drift
        }
        // Attack when ducking (gain falling), release when recovering.
        const float coeff = rampValue_ < gain ? attackCoeff : releaseCoeff;
        gain += coeff * (rampValue_ - gain);
        if (sidesOnly) {
            const float left = channels[0][offset + i];
            const float right = channels[1][offset + i];
            const float mid = 0.5f * (left + right);
            const float side = 0.5f * (left - right) * gain;
            channels[0][offset + i] = mid + side;
            channels[1][offset + i] = mid - side;
            for (int ch = 2; ch < numChannels; ++ch)
                channels[ch][offset + i] *= gain;
        } else {
            for (int ch = 0; ch < numChannels; ++ch)
                channels[ch][offset + i] *= gain;
        }
    }
    currentGain_ = gain;
}

}  // namespace magda::daw::audio
