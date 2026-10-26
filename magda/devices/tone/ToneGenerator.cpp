#include "devices/tone/ToneGenerator.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace magda::devices {

namespace {

/// PolyBLEP: the correction that removes the aliasing a naive step would make, applied either
/// side of a discontinuity over one sample of phase. A test tone must not alias.
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

sdk::ParameterDescriptor slotInfo(int index) {
    sdk::ParameterDescriptor info;
    info.index = index;

    switch (index) {
        case ToneGenerator::kWaveformParamIndex:
            info.stableId = "oscType";
            info.name = "Waveform";
            info.scale = sdk::ParameterScale::Discrete;
            info.minValue = 0.0f;
            info.maxValue = static_cast<float>(ToneGenerator::kWaveformCount - 1);
            info.defaultValue = 0.0f;
            info.choices = sdk::choicesFromLabels(
                {"Sine", "Triangle", "Saw Up", "Saw Down", "Square", "Noise"});
            break;

        case ToneGenerator::kBandLimitParamIndex:
            info.stableId = "bandLimit";
            info.name = "Band Limit";
            info.scale = sdk::ParameterScale::Boolean;
            info.minValue = 0.0f;
            info.maxValue = 1.0f;
            info.defaultValue = 1.0f;
            break;

        case ToneGenerator::kFrequencyParamIndex:
            info.stableId = "frequency";
            info.name = "Frequency";
            info.unit = "Hz";
            info.scale = sdk::ParameterScale::Logarithmic;
            info.minValue = 20.0f;
            info.maxValue = 20000.0f;
            info.defaultValue = 440.0f;
            info.scaleAnchor = 1000.0f;
            break;

        case ToneGenerator::kLevelParamIndex:
            info.stableId = "level";
            info.name = "Level";
            info.unit = "dB";
            info.scale = sdk::ParameterScale::Linear;
            info.minValue = -60.0f;
            info.maxValue = 0.0f;
            info.defaultValue = -12.0f;
            break;

        case ToneGenerator::kNoiseColorParamIndex:
            info.stableId = "noiseColor";
            info.name = "Noise Color";
            info.scale = sdk::ParameterScale::Discrete;
            info.minValue = 0.0f;
            info.maxValue = static_cast<float>(ToneGenerator::kNoiseColorCount - 1);
            info.defaultValue = 0.0f;
            info.choices = sdk::choicesFromLabels({"White", "Pink", "Brown"});
            break;

        case ToneGenerator::kTransportOnlyParamIndex:
            info.stableId = "transportOnly";
            info.name = "Transport Only";
            info.scale = sdk::ParameterScale::Boolean;
            info.minValue = 0.0f;
            info.maxValue = 1.0f;
            info.defaultValue = 0.0f;
            break;

        default:
            break;
    }

    return info;
}

}  // namespace

ToneGenerator::ToneGenerator() {
    for (int index = 0; index < kParamCount; ++index) {
        const auto info = slotInfo(index);
        domains_[static_cast<size_t>(index)] = sdk::domainOf(info);
        values_[static_cast<size_t>(index)] =
            sdk::realToNormalized(info.defaultValue, sdk::domainOf(info));
    }
}

sdk::ParameterDescriptor ToneGenerator::parameterDescriptor(int index) const {
    if (index < 0 || index >= kParamCount)
        return {};
    return slotInfo(index);
}

float ToneGenerator::parameterValue(int index) const {
    if (index < 0 || index >= kParamCount)
        return 0.0f;
    return values_[static_cast<size_t>(index)];
}

void ToneGenerator::setParameterValue(int index, float value) {
    if (index < 0 || index >= kParamCount)
        return;
    values_[static_cast<size_t>(index)] = std::clamp(value, 0.0f, 1.0f);
}

float ToneGenerator::displayValue(int index) const {
    return sdk::normalizedToReal(values_[static_cast<size_t>(index)],
                                 domains_[static_cast<size_t>(index)]);
}

void ToneGenerator::prepare(const sdk::PrepareContext& context) {
    sampleRate_ = context.sampleRate > 0.0 ? context.sampleRate : 44100.0;
    reset();
}

void ToneGenerator::reset() {
    phase_ = 0.0f;
    pink_.fill(0.0f);
    brown_ = 0.0f;
}

float ToneGenerator::noise(NoiseColor color) {
    constexpr float kScale = 2.0f / static_cast<float>(std::minstd_rand::max());
    const float white = static_cast<float>(noise_()) * kScale - 1.0f;
    switch (color) {
        case NoiseColor::White:
            return white;

        case NoiseColor::Pink: {
            // Paul Kellet's refined pink filter, scaled back to about white's level.
            auto& b = pink_;
            b[0] = 0.99886f * b[0] + white * 0.0555179f;
            b[1] = 0.99332f * b[1] + white * 0.0750759f;
            b[2] = 0.96900f * b[2] + white * 0.1538520f;
            b[3] = 0.86650f * b[3] + white * 0.3104856f;
            b[4] = 0.55000f * b[4] + white * 0.5329522f;
            b[5] = -0.7616f * b[5] - white * 0.0168980f;
            const float pink = b[0] + b[1] + b[2] + b[3] + b[4] + b[5] + b[6] + white * 0.5362f;
            b[6] = white * 0.115926f;
            return pink * 0.11f;
        }

        case NoiseColor::Brown:
            // A leaky integrator, so it wanders without drifting off.
            brown_ = (brown_ + 0.02f * white) / 1.02f;
            return brown_ * 3.5f;
    }
    return white;
}

float ToneGenerator::oscillate(Waveform waveform, float phase, float phaseIncrement,
                               bool bandLimit) {
    switch (waveform) {
        case Waveform::Sine:
            return std::sin(phase * 2.0f * std::numbers::pi_v<float>);

        case Waveform::Triangle: {
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
                // The rising edge at zero and the falling edge at the half cycle.
                value += polyBlep(phase, phaseIncrement);
                value -= polyBlep(std::fmod(phase + 0.5f, 1.0f), phaseIncrement);
            }
            return value;
        }

        case Waveform::Noise:
            return noise(noiseColor_);
    }

    return 0.0f;
}

void ToneGenerator::process(sdk::ProcessContext& context) {
    const int numSamples = context.numSamples();
    const int numChannels = context.audio.numChannels();
    if (numSamples <= 0 || numChannels <= 0)
        return;

    // Transport Only keeps the tone for when the song plays, silent while it is stopped.
    if (displayValue(kTransportOnlyParamIndex) >= 0.5f && !context.isPlaying) {
        for (int channel = 0; channel < numChannels; ++channel)
            std::fill_n(context.audio.channel(channel), numSamples, 0.0f);
        return;
    }

    const auto waveform = static_cast<Waveform>(std::clamp(
        static_cast<int>(std::lround(displayValue(kWaveformParamIndex))), 0, kWaveformCount - 1));
    const bool bandLimit = displayValue(kBandLimitParamIndex) >= 0.5f;
    noiseColor_ = static_cast<NoiseColor>(
        std::clamp(static_cast<int>(std::lround(displayValue(kNoiseColorParamIndex))), 0,
                   kNoiseColorCount - 1));
    const float levelDb = displayValue(kLevelParamIndex);
    // The bottom of the range is off, as a fader at its floor is everywhere else in MAGDA.
    const float gain = levelDb <= -59.99f ? 0.0f : std::pow(10.0f, levelDb * 0.05f);

    const float frequency = std::clamp(displayValue(kFrequencyParamIndex), 20.0f, 20000.0f);
    const float phaseIncrement = frequency / static_cast<float>(sampleRate_);

    // The generator takes no audio input: it replaces whatever it was handed.
    float* first = context.audio.channel(0);
    for (int i = 0; i < numSamples; ++i) {
        const float sample = oscillate(waveform, phase_, phaseIncrement, bandLimit) * gain;
        first[i] = std::isfinite(sample) ? std::clamp(sample, -1.0f, 1.0f) : 0.0f;

        phase_ += phaseIncrement;
        if (phase_ >= 1.0f)
            phase_ -= 1.0f;
    }

    for (int channel = 1; channel < numChannels; ++channel)
        std::copy(first, first + numSamples, context.audio.channel(channel));
}

}  // namespace magda::devices
