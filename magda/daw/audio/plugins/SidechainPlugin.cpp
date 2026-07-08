#include "plugins/SidechainPlugin.hpp"

#include "modifiers/ADSRDebugLog.hpp"

namespace magda::daw::audio {

const char* SidechainPlugin::xmlTypeName = "sidechain";

namespace {

float clampToParamRange(const te::AutomatableParameter::Ptr& param, float value) {
    return param->getValueRange().clipValue(value);
}

// One-pole smoothing coefficient for a time constant in milliseconds.
// 0 ms returns 1 (instant).
float smoothingCoeff(float ms, double sampleRate) {
    if (ms <= 0.0f)
        return 1.0f;
    return 1.0f - std::exp(-1.0f / (ms * 0.001f * static_cast<float>(sampleRate)));
}

}  // namespace

SidechainPlugin::SidechainPlugin(const te::PluginCreationInfo& info) : te::Plugin(info) {
    auto um = getUndoManager();

    // `gain` is the duck target the bundled curve modulator drives. It rests
    // at unity; users control duck intensity through the mod link's depth,
    // not by moving this parameter.
    static const juce::Identifier gainId("gain");
    gainValue.referTo(state, gainId, um, 1.0f);
    gainParam = addParam(
        "gain", "Gain", {0.0f, 1.0f, 0.0f},
        [](float v) { return juce::String(juce::roundToInt(v * 100.0f)) + " %"; },
        [](const juce::String& s) {
            return s.upToFirstOccurrenceOf(" ", false, false).getFloatValue() / 100.0f;
        });

    static const juce::Identifier attackId("attack");
    attackValue.referTo(state, attackId, um, 1.0f);
    attackParam = addParam(
        "attack", "Attack", {0.0f, 50.0f, 0.0f}, [](float v) { return juce::String(v, 1) + " ms"; },
        [](const juce::String& s) {
            return s.upToFirstOccurrenceOf(" ", false, false).getFloatValue();
        });

    static const juce::Identifier releaseId("release");
    releaseValue.referTo(state, releaseId, um, 15.0f);
    releaseParam = addParam(
        "release", "Release", {0.0f, 500.0f, 0.0f},
        [](float v) { return juce::String(v, 1) + " ms"; },
        [](const juce::String& s) {
            return s.upToFirstOccurrenceOf(" ", false, false).getFloatValue();
        });

    gainParam->attachToCurrentValue(gainValue);
    attackParam->attachToCurrentValue(attackValue);
    releaseParam->attachToCurrentValue(releaseValue);
}

SidechainPlugin::~SidechainPlugin() {
    notifyListenersOfDeletion();
    gainParam->detachFromCurrentValue();
    attackParam->detachFromCurrentValue();
    releaseParam->detachFromCurrentValue();
}

void SidechainPlugin::initialise(const te::PluginInitialisationInfo& info) {
    sampleRate_ = info.sampleRate;
    currentGain_ = gainParam->getCurrentValue();
    lastTarget_ = currentGain_;
    rampValue_ = currentGain_;
    rampStep_ = 0.0f;
    rampSamplesLeft_ = 0;
    samplesSinceChange_ = 0;
}

void SidechainPlugin::reset() {
    currentGain_ = gainParam->getCurrentValue();
    lastTarget_ = currentGain_;
    rampValue_ = currentGain_;
    rampStep_ = 0.0f;
    rampSamplesLeft_ = 0;
    samplesSinceChange_ = 0;
}

void SidechainPlugin::applyToBuffer(const te::PluginRenderContext& fc) {
    if (fc.destBuffer == nullptr || fc.bufferNumSamples <= 0)
        return;

    const float target = gainParam->getCurrentValue();
    const float attackCoeff = smoothingCoeff(attackParam->getCurrentValue(), sampleRate_);
    const float releaseCoeff = smoothingCoeff(releaseParam->getCurrentValue(), sampleRate_);

    // Click probe: a large block-boundary jump of the modulated gain target is
    // where any click originates. Rare (a few per bar at most), so the log
    // call is acceptable while we chase this.
    if (std::abs(target - currentGain_) > 0.2f)
        MAGDA_ADSR_AUDIO_LOG("SC-GAIN jump plug="
                             << juce::String::toHexString(
                                    reinterpret_cast<juce::pointer_sized_int>(this))
                             << " target=" << target << " current=" << currentGain_
                             << " atkMs=" << attackParam->getCurrentValue()
                             << " relMs=" << releaseParam->getCurrentValue());

    const int numChannels = fc.destBuffer->getNumChannels();
    float* channels[8] = {};
    const int usedChannels = juce::jmin(numChannels, 8);
    for (int ch = 0; ch < usedChannels; ++ch)
        channels[ch] = fc.destBuffer->getWritePointer(ch, fc.bufferStartSample);

    // The modifier writes the gain target at a coarse quantum (one hop per
    // modifier update, several render blocks wide), so the drawn curve
    // arrives undersampled: the log showed hops of 0.4+ gain where the curve
    // is steep, and the one-shot latch lands as one final hop. Reconstruct:
    // ramp upward (recovery) steps over the measured stair width so the
    // recovery is continuous at source; ramp downward steps (the duck hit)
    // within one block so the onset stays punchy. The attack/release poles
    // then only shape, they no longer have to hide discontinuities.
    if (target != lastTarget_) {
        constexpr int kMaxStairSamples = 4096;
        const bool goingDown = target < rampValue_;
        const int width =
            goingDown ? fc.bufferNumSamples
                      : juce::jlimit(fc.bufferNumSamples, kMaxStairSamples, samplesSinceChange_);
        rampStep_ = (target - rampValue_) / static_cast<float>(width);
        rampSamplesLeft_ = width;
        samplesSinceChange_ = 0;
        lastTarget_ = target;
    }
    samplesSinceChange_ += fc.bufferNumSamples;

    float gain = currentGain_;
    for (int i = 0; i < fc.bufferNumSamples; ++i) {
        if (rampSamplesLeft_ > 0) {
            rampValue_ += rampStep_;
            --rampSamplesLeft_;
            if (rampSamplesLeft_ == 0)
                rampValue_ = lastTarget_;  // land exactly, no float drift
        }
        // Attack when ducking (gain falling), release when recovering.
        const float coeff = rampValue_ < gain ? attackCoeff : releaseCoeff;
        gain += coeff * (rampValue_ - gain);
        for (int ch = 0; ch < usedChannels; ++ch)
            channels[ch][i] *= gain;
    }
    currentGain_ = gain;
}

void SidechainPlugin::restorePluginStateFromValueTree(const juce::ValueTree& v) {
    te::copyPropertiesToCachedValues(v, gainValue, attackValue, releaseValue);

    gainValue = clampToParamRange(gainParam, gainValue.get());
    attackValue = clampToParamRange(attackParam, attackValue.get());
    releaseValue = clampToParamRange(releaseParam, releaseValue.get());

    for (auto p : getAutomatableParameters())
        p->updateFromAttachedValue();
}

}  // namespace magda::daw::audio
