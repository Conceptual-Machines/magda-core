#include "plugins/ToneGeneratorPlugin.hpp"

#include <cmath>

namespace magda::daw::audio {

// The id the retired device used, because that is what saved projects name.
const char* ToneGeneratorPlugin::xmlTypeName = "toneGenerator";

namespace {

/// PolyBLEP: the correction that removes the aliasing a naive step would make.
///
/// Applied either side of a discontinuity, over one sample of phase. Without it
/// a saw or square at any frequency that does not divide the sample rate folds
/// harmonics back down the spectrum, which is exactly what a test tone must not
/// do when it is being used to check a signal path.
float polyBlep(float phase, float phaseIncrement) {
    if (phaseIncrement <= 0.0f)
        return 0.0f;

    if (phase < phaseIncrement) {
        const float t = phase / phaseIncrement;
        return (t + t) - (t * t) - 1.0f;
    }
    if (phase > 1.0f - phaseIncrement) {
        const float t = (phase - 1.0f) / phaseIncrement;
        return (t * t) + (t + t) + 1.0f;
    }
    return 0.0f;
}

ParameterInfo slotInfo(int index) {
    ParameterInfo info;
    info.paramIndex = index;

    switch (index) {
        case ToneGeneratorPlugin::kWaveformParamIndex:
            info.stableId = "oscType";
            info.name = "Waveform";
            info.scale = ParameterScale::Discrete;
            info.minValue = 0.0f;
            info.maxValue = static_cast<float>(ToneGeneratorPlugin::kWaveformCount - 1);
            info.defaultValue = 0.0f;
            info.choices = {"Sine", "Triangle", "Saw Up", "Saw Down", "Square", "Noise"};
            break;

        case ToneGeneratorPlugin::kBandLimitParamIndex:
            info.stableId = "bandLimit";
            info.name = "Band Limit";
            info.scale = ParameterScale::Boolean;
            info.minValue = 0.0f;
            info.maxValue = 1.0f;
            info.defaultValue = 1.0f;
            break;

        case ToneGeneratorPlugin::kFrequencyParamIndex:
            info.stableId = "frequency";
            info.name = "Frequency";
            info.unit = technicalText(TechnicalTextToken::Hertz);
            info.scale = ParameterScale::Logarithmic;
            info.minValue = 20.0f;
            info.maxValue = 20000.0f;
            info.defaultValue = 440.0f;
            info.scaleAnchor = 1000.0f;
            break;

        case ToneGeneratorPlugin::kLevelParamIndex:
            info.stableId = "level";
            info.name = "Level";
            info.unit = technicalText(TechnicalTextToken::Decibels);
            info.scale = ParameterScale::Linear;
            info.minValue = -60.0f;
            info.maxValue = 0.0f;
            info.defaultValue = -12.0f;
            break;

        default:
            break;
    }

    return info;
}

}  // namespace

ToneGeneratorPlugin::ToneGeneratorPlugin() {
    for (int index = 0; index < kParamCount; ++index) {
        const auto info = slotInfo(index);
        domains_[static_cast<size_t>(index)] = ParameterUtils::domainOf(info);
        values_[static_cast<size_t>(index)] =
            ParameterUtils::realToNormalized(info.defaultValue, info);
    }
}

ParameterInfo ToneGeneratorPlugin::parameterInfo(int index) const {
    if (index < 0 || index >= kParamCount)
        return {};
    return slotInfo(index);
}

float ToneGeneratorPlugin::parameterValue(int index) const {
    if (index < 0 || index >= kParamCount)
        return 0.0f;
    return values_[static_cast<size_t>(index)];
}

void ToneGeneratorPlugin::setParameterValue(int index, float value) {
    if (index < 0 || index >= kParamCount)
        return;
    values_[static_cast<size_t>(index)] = juce::jlimit(0.0f, 1.0f, value);
}

float ToneGeneratorPlugin::displayValue(int index) const {
    return ParameterUtils::normalizedToReal(values_[static_cast<size_t>(index)],
                                            domains_[static_cast<size_t>(index)]);
}

void ToneGeneratorPlugin::prepare(const DevicePrepareContext& context) {
    sampleRate_ = context.sampleRate > 0.0 ? context.sampleRate : 44100.0;
    reset();
}

void ToneGeneratorPlugin::reset() {
    phase_ = 0.0f;
}

float ToneGeneratorPlugin::oscillate(Waveform waveform, float phase, float phaseIncrement,
                                     bool bandLimit) {
    switch (waveform) {
        case Waveform::Sine:
            // Already band limited: one partial, nowhere to alias to.
            return std::sin(phase * juce::MathConstants<float>::twoPi);

        case Waveform::Triangle: {
            // No step to correct -- the slope breaks, the value does not.
            const float rising = 4.0f * phase - 1.0f;
            return phase < 0.5f ? rising : 3.0f - 4.0f * phase;
        }

        case Waveform::SawUp: {
            float value = 2.0f * phase - 1.0f;
            if (bandLimit)
                value -= polyBlep(phase, phaseIncrement);
            return value;
        }

        case Waveform::SawDown: {
            float value = 1.0f - 2.0f * phase;
            if (bandLimit)
                value += polyBlep(phase, phaseIncrement);
            return value;
        }

        case Waveform::Square: {
            float value = phase < 0.5f ? 1.0f : -1.0f;
            if (bandLimit) {
                // Two discontinuities per cycle, corrected in opposite
                // directions: the rising edge at zero and the falling edge at
                // the half cycle.
                value += polyBlep(phase, phaseIncrement);
                value -= polyBlep(std::fmod(phase + 0.5f, 1.0f), phaseIncrement);
            }
            return value;
        }

        case Waveform::Noise: {
            constexpr float kScale = 2.0f / static_cast<float>(std::minstd_rand::max());
            return static_cast<float>(noise_()) * kScale - 1.0f;
        }
    }

    return 0.0f;
}

void ToneGeneratorPlugin::process(DeviceProcessContext& context) {
    if (context.audio == nullptr || context.numSamples <= 0)
        return;

    const int numChannels = context.audio->getNumChannels();
    if (numChannels <= 0)
        return;

    const auto waveform = static_cast<Waveform>(juce::jlimit(
        0, kWaveformCount - 1, static_cast<int>(std::lround(displayValue(kWaveformParamIndex)))));
    const bool bandLimit = displayValue(kBandLimitParamIndex) >= 0.5f;
    const float levelDb = displayValue(kLevelParamIndex);
    // The bottom of the range is off rather than very quiet, which is what a
    // fader at its floor means everywhere else in MAGDA.
    const float gain = levelDb <= -59.99f ? 0.0f : juce::Decibels::decibelsToGain(levelDb);

    const float frequency = juce::jlimit(20.0f, 20000.0f, displayValue(kFrequencyParamIndex));
    const float phaseIncrement = frequency / static_cast<float>(sampleRate_);

    // The generator replaces whatever it was handed: it takes no audio input,
    // so anything already in the buffer is not part of its signal.
    float* first = context.audio->getWritePointer(0, context.startSample);
    for (int i = 0; i < context.numSamples; ++i) {
        const float sample = oscillate(waveform, phase_, phaseIncrement, bandLimit) * gain;
        first[i] = std::isfinite(sample) ? juce::jlimit(-1.0f, 1.0f, sample) : 0.0f;

        phase_ += phaseIncrement;
        if (phase_ >= 1.0f)
            phase_ -= 1.0f;
    }

    for (int channel = 1; channel < numChannels; ++channel)
        context.audio->copyFrom(channel, context.startSample, *context.audio, 0,
                                context.startSample, context.numSamples);
}

}  // namespace magda::daw::audio
