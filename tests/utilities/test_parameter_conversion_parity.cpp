#include <juce_core/juce_core.h>

#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <vector>

#include "magda/daw/audio/plugins/DeviceManifests.hpp"
#include "magda/daw/core/ParameterUtils.hpp"

// The conversion was ParameterUtils' own until it moved to the SDK (#2939). The functions below
// are that code as it was, so the SDK's version is held to its exact results.

namespace {

using magda::ParameterScale;

struct LegacyDomain {
    ParameterScale scale = ParameterScale::Linear;
    float minValue = 0.0f;
    float maxValue = 1.0f;
    float skewFactor = 1.0f;
    float scaleAnchor = 0.0f;
    int choiceCount = 0;
};

float computeSkew(float anchorPosition) {
    if (anchorPosition <= 1e-6f || anchorPosition >= 1.0f - 1e-6f)
        return 1.0f;
    if (std::abs(anchorPosition - 0.5f) < 1e-6f)
        return 1.0f;
    return std::log(anchorPosition) / std::log(0.5f);
}

bool hasScaleAnchor(const LegacyDomain& domain) {
    return domain.scaleAnchor > domain.minValue && domain.scaleAnchor < domain.maxValue;
}

float legacyNormalizedToReal(float normalized, const LegacyDomain& domain) {
    normalized = juce::jlimit(0.0f, 1.0f, normalized);

    switch (domain.scale) {
        case ParameterScale::Linear: {
            float range = domain.maxValue - domain.minValue;
            if (hasScaleAnchor(domain) && range > 0.0f) {
                float anchorPos = (domain.scaleAnchor - domain.minValue) / range;
                float skew = computeSkew(anchorPos);
                normalized = std::pow(normalized, skew);
            }
            return domain.minValue + normalized * range;
        }

        case ParameterScale::Logarithmic: {
            if (domain.minValue <= 0.0f) {
                return domain.minValue + normalized * (domain.maxValue - domain.minValue);
            }
            float logRange = std::log(domain.maxValue / domain.minValue);
            if (hasScaleAnchor(domain)) {
                float anchorLogPos = std::log(domain.scaleAnchor / domain.minValue) / logRange;
                float skew = computeSkew(anchorLogPos);
                normalized = std::pow(normalized, skew);
            }
            return domain.minValue * std::exp(normalized * logRange);
        }

        case ParameterScale::Exponential:
            return std::pow(normalized, domain.skewFactor) * (domain.maxValue - domain.minValue) +
                   domain.minValue;

        case ParameterScale::Discrete: {
            if (domain.choiceCount <= 0) {
                return 0.0f;
            }
            int index = static_cast<int>(std::round(normalized * (domain.choiceCount - 1)));
            return static_cast<float>(index);
        }

        case ParameterScale::Boolean:
            return normalized >= 0.5f ? 1.0f : 0.0f;

        case ParameterScale::FaderDB: {
            constexpr float UNITY_POS = 0.75f;
            constexpr float UNITY_DB = 0.0f;

            if (normalized <= 0.0f)
                return domain.minValue;
            if (normalized >= 1.0f)
                return domain.maxValue;

            if (normalized < UNITY_POS) {
                return domain.minValue + (normalized / UNITY_POS) * (UNITY_DB - domain.minValue);
            } else {
                return UNITY_DB + ((normalized - UNITY_POS) / (1.0f - UNITY_POS)) *
                                      (domain.maxValue - UNITY_DB);
            }
        }
    }
    return domain.minValue + normalized * (domain.maxValue - domain.minValue);
}

float legacyRealToNormalized(float real, const LegacyDomain& domain) {
    switch (domain.scale) {
        case ParameterScale::Linear: {
            float range = domain.maxValue - domain.minValue;
            if (range == 0.0f)
                return 0.0f;
            float linPos = (real - domain.minValue) / range;
            if (hasScaleAnchor(domain)) {
                float anchorPos = (domain.scaleAnchor - domain.minValue) / range;
                float skew = computeSkew(anchorPos);
                linPos = std::pow(juce::jlimit(0.0f, 1.0f, linPos), 1.0f / skew);
            }
            return juce::jlimit(0.0f, 1.0f, linPos);
        }

        case ParameterScale::Logarithmic: {
            if (domain.minValue <= 0.0f || real <= 0.0f) {
                float range = domain.maxValue - domain.minValue;
                if (range == 0.0f)
                    return 0.0f;
                return juce::jlimit(0.0f, 1.0f, (real - domain.minValue) / range);
            }
            float logRange = std::log(domain.maxValue / domain.minValue);
            if (logRange == 0.0f)
                return 0.0f;
            float logPos = std::log(real / domain.minValue) / logRange;
            if (hasScaleAnchor(domain)) {
                float anchorLogPos = std::log(domain.scaleAnchor / domain.minValue) / logRange;
                float skew = computeSkew(anchorLogPos);
                logPos = std::pow(juce::jlimit(0.0f, 1.0f, logPos), 1.0f / skew);
            }
            return juce::jlimit(0.0f, 1.0f, logPos);
        }

        case ParameterScale::Exponential: {
            float range = domain.maxValue - domain.minValue;
            if (range == 0.0f || domain.skewFactor == 0.0f)
                return 0.0f;
            float normalized = (real - domain.minValue) / range;
            return juce::jlimit(0.0f, 1.0f, std::pow(normalized, 1.0f / domain.skewFactor));
        }

        case ParameterScale::Discrete: {
            if (domain.choiceCount <= 0)
                return 0.0f;
            int index = juce::jlimit(0, domain.choiceCount - 1, static_cast<int>(std::round(real)));
            return static_cast<float>(index) / static_cast<float>(domain.choiceCount - 1);
        }

        case ParameterScale::Boolean:
            return real >= 0.5f ? 1.0f : 0.0f;

        case ParameterScale::FaderDB: {
            constexpr float UNITY_POS = 0.75f;
            constexpr float UNITY_DB = 0.0f;

            if (real <= domain.minValue)
                return 0.0f;
            if (real >= domain.maxValue)
                return 1.0f;

            if (real < UNITY_DB) {
                return UNITY_POS * (real - domain.minValue) / (UNITY_DB - domain.minValue);
            } else {
                return UNITY_POS +
                       (1.0f - UNITY_POS) * (real - UNITY_DB) / (domain.maxValue - UNITY_DB);
            }
        }
    }
    float range = domain.maxValue - domain.minValue;
    if (range == 0.0f)
        return 0.0f;
    return juce::jlimit(0.0f, 1.0f, (real - domain.minValue) / range);
}

LegacyDomain legacyDomain(const magda::sdk::ParameterDescriptor& descriptor) {
    return {descriptor.scale,       descriptor.minValue,
            descriptor.maxValue,    descriptor.exponent,
            descriptor.scaleAnchor, static_cast<int>(descriptor.choices.size())};
}

std::vector<float> normalizedProbes() {
    std::vector<float> probes{-1.0f, 0.0f, 1.0f, 2.0f, 0.5f, 0.75f, 0.499999f, 0.5f - 1e-7f};
    for (int i = 0; i <= 128; ++i)
        probes.push_back(static_cast<float>(i) / 128.0f);
    for (int i = 0; i < 97; ++i)
        probes.push_back(static_cast<float>(i) * 0.0103f);
    return probes;
}

/// Equal, or both not-a-number: the two are the same answer.
bool sameFloat(float a, float b) {
    return a == b || (std::isnan(a) && std::isnan(b));
}

void requireSameConversions(const magda::sdk::ParameterDescriptor& descriptor) {
    const auto legacy = legacyDomain(descriptor);
    const auto domain = magda::sdk::domainOf(descriptor);
    const float span = descriptor.maxValue - descriptor.minValue;

    for (const float n : normalizedProbes()) {
        const float expected = legacyNormalizedToReal(n, legacy);
        INFO(descriptor.stableId << " normalized " << n);
        REQUIRE(sameFloat(magda::sdk::normalizedToReal(n, domain), expected));

        // Real values around the range: its ends, outside it, and where the curve lands.
        for (const float real : {expected, descriptor.minValue + n * span,
                                 descriptor.minValue - 1.0f, descriptor.maxValue + 1.0f}) {
            INFO(descriptor.stableId << " real " << real);
            REQUIRE(sameFloat(magda::sdk::realToNormalized(real, domain),
                              legacyRealToNormalized(real, legacy)));
        }
    }
}

magda::sdk::ParameterDescriptor sample(ParameterScale scale, float min, float max) {
    magda::sdk::ParameterDescriptor descriptor;
    descriptor.scale = scale;
    descriptor.minValue = min;
    descriptor.maxValue = max;
    return descriptor;
}

}  // namespace

TEST_CASE("The SDK conversion matches the code it replaced on every scale",
          "[parameter-utils][2939]") {
    auto anchoredLinear = sample(ParameterScale::Linear, -24.0f, 24.0f);
    anchoredLinear.scaleAnchor = -6.0f;
    auto anchoredLog = sample(ParameterScale::Logarithmic, 20.0f, 20000.0f);
    anchoredLog.scaleAnchor = 1000.0f;
    auto exponential = sample(ParameterScale::Exponential, -12.0f, 6.0f);
    exponential.exponent = 0.5849625f;
    auto discrete = sample(ParameterScale::Discrete, 0.0f, 5.0f);
    discrete.choices = magda::sdk::choicesFromLabels({"a", "b", "c", "d", "e", "f"});
    auto singleChoice = sample(ParameterScale::Discrete, 0.0f, 0.0f);
    singleChoice.choices = magda::sdk::choicesFromLabels({"only"});

    for (const auto& descriptor :
         {sample(ParameterScale::Linear, 0.0f, 1.0f), sample(ParameterScale::Linear, 5.0f, 5.0f),
          anchoredLinear, sample(ParameterScale::Logarithmic, 0.1f, 10000.0f),
          sample(ParameterScale::Logarithmic, 0.0f, 100.0f), anchoredLog, exponential,
          sample(ParameterScale::Exponential, 0.0f, 0.0f), discrete, singleChoice,
          sample(ParameterScale::Discrete, 0, 3), sample(ParameterScale::Boolean, 0.0f, 1.0f),
          sample(ParameterScale::FaderDB, -60.0f, 6.0f),
          sample(ParameterScale::FaderDB, -90.0f, 12.0f)})
        requireSameConversions(descriptor);
}

TEST_CASE("The SDK conversion matches the code it replaced on every device parameter",
          "[parameter-utils][2939]") {
    int checked = 0;
    for (const auto& entry : magda::daw::audio::buildBasePackManifests()) {
        if (!entry.manifest)
            continue;
        for (const auto& descriptor : entry.manifest->parameters) {
            requireSameConversions(descriptor);
            ++checked;
        }
    }
    CHECK(checked > 300);
}

TEST_CASE("ParameterUtils converts through the SDK's domain", "[parameter-utils][2939]") {
    auto info = magda::ParameterPresets::frequency(0, "Cutoff");
    info.scaleAnchor = 1000.0f;
    const auto domain = magda::ParameterUtils::domainOf(info);

    CHECK(magda::ParameterUtils::normalizedToReal(0.3f, info) ==
          magda::sdk::normalizedToReal(0.3f, domain));
    CHECK(magda::ParameterUtils::realToNormalized(440.0f, info) ==
          magda::sdk::realToNormalized(440.0f, domain));
}
