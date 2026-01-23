#include "ParameterUtils.hpp"

#include <cmath>

namespace magda {
namespace ParameterUtils {

float normalizedToReal(float normalized, const ParameterInfo& info) {
    // Clamp input to valid range
    normalized = juce::jlimit(0.0f, 1.0f, normalized);

    switch (info.scale) {
        case ParameterScale::Linear:
            return info.minValue + normalized * (info.maxValue - info.minValue);

        case ParameterScale::Logarithmic: {
            // Handle edge case where min is zero or negative
            if (info.minValue <= 0.0f) {
                return info.minValue + normalized * (info.maxValue - info.minValue);
            }
            // Exponential interpolation: min * (max/min)^normalized
            return info.minValue * std::pow(info.maxValue / info.minValue, normalized);
        }

        case ParameterScale::Exponential:
            return std::pow(normalized, info.skewFactor) * (info.maxValue - info.minValue) +
                   info.minValue;

        case ParameterScale::Discrete: {
            if (info.choices.empty()) {
                return 0.0f;
            }
            int index = static_cast<int>(std::round(normalized * (info.choices.size() - 1)));
            return static_cast<float>(index);
        }

        case ParameterScale::Boolean:
            return normalized >= 0.5f ? 1.0f : 0.0f;

        default:
            return info.minValue + normalized * (info.maxValue - info.minValue);
    }
}

float realToNormalized(float real, const ParameterInfo& info) {
    switch (info.scale) {
        case ParameterScale::Linear: {
            float range = info.maxValue - info.minValue;
            if (range == 0.0f)
                return 0.0f;
            return juce::jlimit(0.0f, 1.0f, (real - info.minValue) / range);
        }

        case ParameterScale::Logarithmic: {
            // Handle edge cases
            if (info.minValue <= 0.0f || real <= 0.0f) {
                float range = info.maxValue - info.minValue;
                if (range == 0.0f)
                    return 0.0f;
                return juce::jlimit(0.0f, 1.0f, (real - info.minValue) / range);
            }
            // Inverse of exponential: log(real/min) / log(max/min)
            float logRatio = std::log(info.maxValue / info.minValue);
            if (logRatio == 0.0f)
                return 0.0f;
            return juce::jlimit(0.0f, 1.0f, std::log(real / info.minValue) / logRatio);
        }

        case ParameterScale::Exponential: {
            float range = info.maxValue - info.minValue;
            if (range == 0.0f || info.skewFactor == 0.0f)
                return 0.0f;
            float normalized = (real - info.minValue) / range;
            return juce::jlimit(0.0f, 1.0f, std::pow(normalized, 1.0f / info.skewFactor));
        }

        case ParameterScale::Discrete: {
            if (info.choices.empty())
                return 0.0f;
            int index = juce::jlimit(0, static_cast<int>(info.choices.size() - 1),
                                     static_cast<int>(std::round(real)));
            return static_cast<float>(index) / static_cast<float>(info.choices.size() - 1);
        }

        case ParameterScale::Boolean:
            return real >= 0.5f ? 1.0f : 0.0f;

        default: {
            float range = info.maxValue - info.minValue;
            if (range == 0.0f)
                return 0.0f;
            return juce::jlimit(0.0f, 1.0f, (real - info.minValue) / range);
        }
    }
}

float applyModulation(float baseNormalized, float modValue, float amount, bool bipolar) {
    // modValue is 0-1, convert to -1 to +1 if bipolar
    float modOffset = bipolar ? (modValue * 2.0f - 1.0f) : modValue;

    // amount is depth: how much of the mod affects the base
    float delta = modOffset * amount;

    return juce::jlimit(0.0f, 1.0f, baseNormalized + delta);
}

float applyModulations(float baseNormalized,
                       const std::vector<std::pair<float, float>>& modsAndAmounts, bool bipolar) {
    float result = baseNormalized;

    for (const auto& [modValue, amount] : modsAndAmounts) {
        float modOffset = bipolar ? (modValue * 2.0f - 1.0f) : modValue;
        result += modOffset * amount;
    }

    return juce::jlimit(0.0f, 1.0f, result);
}

juce::String formatValue(float realValue, const ParameterInfo& info, int decimalPlaces) {
    switch (info.scale) {
        case ParameterScale::Discrete: {
            int index = static_cast<int>(std::round(realValue));
            return getChoiceString(index, info);
        }

        case ParameterScale::Boolean:
            return realValue >= 0.5f ? "On" : "Off";

        default:
            break;
    }

    // Format based on unit
    if (info.unit == "Hz") {
        // Show kHz for values >= 1000
        if (realValue >= 1000.0f) {
            return juce::String(realValue / 1000.0f, decimalPlaces) + " kHz";
        }
        return juce::String(realValue, decimalPlaces) + " Hz";
    }

    if (info.unit == "ms") {
        // Show s for values >= 1000
        if (realValue >= 1000.0f) {
            return juce::String(realValue / 1000.0f, decimalPlaces) + " s";
        }
        return juce::String(realValue, decimalPlaces) + " ms";
    }

    if (info.unit == "%") {
        return juce::String(realValue, decimalPlaces) + "%";
    }

    if (info.unit == "dB") {
        // Show + sign for positive values
        if (realValue > 0.0f) {
            return "+" + juce::String(realValue, decimalPlaces) + " dB";
        }
        return juce::String(realValue, decimalPlaces) + " dB";
    }

    // Default: just value with unit
    if (info.unit.isNotEmpty()) {
        return juce::String(realValue, decimalPlaces) + " " + info.unit;
    }

    return juce::String(realValue, decimalPlaces);
}

juce::String getChoiceString(int index, const ParameterInfo& info) {
    if (info.choices.empty()) {
        return juce::String(index);
    }

    if (index >= 0 && index < static_cast<int>(info.choices.size())) {
        return info.choices[static_cast<size_t>(index)];
    }

    return juce::String(index);
}

}  // namespace ParameterUtils
}  // namespace magda
