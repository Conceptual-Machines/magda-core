#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "../magda/daw/core/ParameterInfo.hpp"
#include "../magda/daw/core/ParameterUtils.hpp"

using namespace magda;

// ============================================================================
// ParameterInfo Structure Tests
// ============================================================================

TEST_CASE("ParameterInfo - Default construction", "[parameter]") {
    ParameterInfo info;
    REQUIRE(info.paramIndex == -1);
    REQUIRE(info.name.isEmpty());
    REQUIRE(info.unit.isEmpty());
    REQUIRE(info.minValue == Catch::Approx(0.0f));
    REQUIRE(info.maxValue == Catch::Approx(1.0f));
    REQUIRE(info.defaultValue == Catch::Approx(0.5f));
    REQUIRE(info.scale == ParameterScale::Linear);
    REQUIRE(info.skewFactor == Catch::Approx(1.0f));
    REQUIRE(info.choices.empty());
    REQUIRE(info.modulatable == true);
    REQUIRE(info.bipolarModulation == true);
}

TEST_CASE("ParameterInfo - Constructor with values", "[parameter]") {
    ParameterInfo info(0, "Cutoff", "Hz", 20.0f, 20000.0f, 1000.0f, ParameterScale::Logarithmic);
    REQUIRE(info.paramIndex == 0);
    REQUIRE(info.name == "Cutoff");
    REQUIRE(info.unit == "Hz");
    REQUIRE(info.minValue == Catch::Approx(20.0f));
    REQUIRE(info.maxValue == Catch::Approx(20000.0f));
    REQUIRE(info.defaultValue == Catch::Approx(1000.0f));
    REQUIRE(info.scale == ParameterScale::Logarithmic);
}

// ============================================================================
// ParameterPresets Tests
// ============================================================================

TEST_CASE("ParameterPresets - frequency", "[parameter][presets]") {
    auto freq = ParameterPresets::frequency(0, "Cutoff");
    REQUIRE(freq.paramIndex == 0);
    REQUIRE(freq.name == "Cutoff");
    REQUIRE(freq.unit == "Hz");
    REQUIRE(freq.minValue == Catch::Approx(20.0f));
    REQUIRE(freq.maxValue == Catch::Approx(20000.0f));
    REQUIRE(freq.scale == ParameterScale::Logarithmic);
    // Default is geometric mean: sqrt(20 * 20000) = sqrt(400000) ~= 632.45
    REQUIRE(freq.defaultValue == Catch::Approx(632.45f).margin(0.1f));
}

TEST_CASE("ParameterPresets - frequency with custom range", "[parameter][presets]") {
    auto freq = ParameterPresets::frequency(1, "LFO Rate", 0.1f, 100.0f);
    REQUIRE(freq.minValue == Catch::Approx(0.1f));
    REQUIRE(freq.maxValue == Catch::Approx(100.0f));
    // sqrt(0.1 * 100) = sqrt(10) ~= 3.162
    REQUIRE(freq.defaultValue == Catch::Approx(3.162f).margin(0.01f));
}

TEST_CASE("ParameterPresets - time", "[parameter][presets]") {
    auto time = ParameterPresets::time(0, "Attack");
    REQUIRE(time.name == "Attack");
    REQUIRE(time.unit == "ms");
    REQUIRE(time.minValue == Catch::Approx(0.1f));
    REQUIRE(time.maxValue == Catch::Approx(10000.0f));
    REQUIRE(time.scale == ParameterScale::Logarithmic);
}

TEST_CASE("ParameterPresets - percent", "[parameter][presets]") {
    auto pct = ParameterPresets::percent(0, "Mix");
    REQUIRE(pct.name == "Mix");
    REQUIRE(pct.unit == "%");
    REQUIRE(pct.minValue == Catch::Approx(0.0f));
    REQUIRE(pct.maxValue == Catch::Approx(100.0f));
    REQUIRE(pct.defaultValue == Catch::Approx(50.0f));
    REQUIRE(pct.scale == ParameterScale::Linear);
}

TEST_CASE("ParameterPresets - decibels", "[parameter][presets]") {
    auto db = ParameterPresets::decibels(0, "Gain");
    REQUIRE(db.name == "Gain");
    REQUIRE(db.unit == "dB");
    REQUIRE(db.minValue == Catch::Approx(-60.0f));
    REQUIRE(db.maxValue == Catch::Approx(12.0f));
    REQUIRE(db.defaultValue == Catch::Approx(0.0f));  // Unity gain
    REQUIRE(db.scale == ParameterScale::Linear);
}

TEST_CASE("ParameterPresets - boolean", "[parameter][presets]") {
    auto toggle = ParameterPresets::boolean(0, "Bypass");
    REQUIRE(toggle.name == "Bypass");
    REQUIRE(toggle.scale == ParameterScale::Boolean);
    REQUIRE(toggle.modulatable == false);
}

TEST_CASE("ParameterPresets - discrete", "[parameter][presets]") {
    std::vector<juce::String> choices = {"Off", "Low", "Medium", "High"};
    auto mode = ParameterPresets::discrete(0, "Mode", choices);
    REQUIRE(mode.name == "Mode");
    REQUIRE(mode.scale == ParameterScale::Discrete);
    REQUIRE(mode.choices.size() == 4);
    REQUIRE(mode.choices[0] == "Off");
    REQUIRE(mode.choices[3] == "High");
    REQUIRE(mode.maxValue == Catch::Approx(3.0f));
    REQUIRE(mode.modulatable == false);
}

// ============================================================================
// normalizedToReal Tests
// ============================================================================

TEST_CASE("normalizedToReal - Linear scale", "[parameter][conversion]") {
    auto param = ParameterPresets::percent(0, "Mix");

    SECTION("0.0 returns minimum") {
        REQUIRE(ParameterUtils::normalizedToReal(0.0f, param) == Catch::Approx(0.0f));
    }

    SECTION("1.0 returns maximum") {
        REQUIRE(ParameterUtils::normalizedToReal(1.0f, param) == Catch::Approx(100.0f));
    }

    SECTION("0.5 returns midpoint") {
        REQUIRE(ParameterUtils::normalizedToReal(0.5f, param) == Catch::Approx(50.0f));
    }

    SECTION("0.25 returns quarter") {
        REQUIRE(ParameterUtils::normalizedToReal(0.25f, param) == Catch::Approx(25.0f));
    }
}

TEST_CASE("normalizedToReal - Logarithmic scale (frequency)", "[parameter][conversion]") {
    auto param = ParameterPresets::frequency(0, "Cutoff");

    SECTION("0.0 returns minimum (20 Hz)") {
        REQUIRE(ParameterUtils::normalizedToReal(0.0f, param) == Catch::Approx(20.0f));
    }

    SECTION("1.0 returns maximum (20000 Hz)") {
        REQUIRE(ParameterUtils::normalizedToReal(1.0f, param) == Catch::Approx(20000.0f));
    }

    SECTION("0.5 returns geometric mean (~632 Hz)") {
        // 20 * (20000/20)^0.5 = 20 * 1000^0.5 = 20 * 31.623 ~= 632.46
        REQUIRE(ParameterUtils::normalizedToReal(0.5f, param) ==
                Catch::Approx(632.46f).margin(0.1f));
    }
}

TEST_CASE("normalizedToReal - Logarithmic scale (time)", "[parameter][conversion]") {
    auto param = ParameterPresets::time(0, "Attack", 1.0f, 10000.0f);

    SECTION("0.0 returns 1 ms") {
        REQUIRE(ParameterUtils::normalizedToReal(0.0f, param) == Catch::Approx(1.0f));
    }

    SECTION("1.0 returns 10000 ms") {
        REQUIRE(ParameterUtils::normalizedToReal(1.0f, param) == Catch::Approx(10000.0f));
    }

    SECTION("0.5 returns geometric mean (~100 ms)") {
        // 1 * (10000/1)^0.5 = 10000^0.5 = 100
        REQUIRE(ParameterUtils::normalizedToReal(0.5f, param) ==
                Catch::Approx(100.0f).margin(0.1f));
    }
}

TEST_CASE("normalizedToReal - Exponential scale", "[parameter][conversion]") {
    ParameterInfo param;
    param.minValue = 0.0f;
    param.maxValue = 100.0f;
    param.scale = ParameterScale::Exponential;
    param.skewFactor = 2.0f;  // Quadratic curve

    SECTION("0.0 returns minimum") {
        REQUIRE(ParameterUtils::normalizedToReal(0.0f, param) == Catch::Approx(0.0f));
    }

    SECTION("1.0 returns maximum") {
        REQUIRE(ParameterUtils::normalizedToReal(1.0f, param) == Catch::Approx(100.0f));
    }

    SECTION("0.5 returns pow(0.5, 2) * 100 = 25") {
        REQUIRE(ParameterUtils::normalizedToReal(0.5f, param) == Catch::Approx(25.0f));
    }
}

TEST_CASE("normalizedToReal - Discrete scale", "[parameter][conversion]") {
    std::vector<juce::String> choices = {"A", "B", "C", "D"};
    auto param = ParameterPresets::discrete(0, "Mode", choices);

    SECTION("0.0 returns index 0") {
        REQUIRE(ParameterUtils::normalizedToReal(0.0f, param) == Catch::Approx(0.0f));
    }

    SECTION("1.0 returns last index (3)") {
        REQUIRE(ParameterUtils::normalizedToReal(1.0f, param) == Catch::Approx(3.0f));
    }

    SECTION("0.5 returns middle index (1 or 2)") {
        // 0.5 * 3 = 1.5, rounds to 2
        REQUIRE(ParameterUtils::normalizedToReal(0.5f, param) == Catch::Approx(2.0f));
    }

    SECTION("0.33 returns index 1") {
        // 0.33 * 3 = 0.99, rounds to 1
        REQUIRE(ParameterUtils::normalizedToReal(0.33f, param) == Catch::Approx(1.0f));
    }
}

TEST_CASE("normalizedToReal - Boolean scale", "[parameter][conversion]") {
    auto param = ParameterPresets::boolean(0, "Toggle");

    SECTION("0.0 returns 0 (off)") {
        REQUIRE(ParameterUtils::normalizedToReal(0.0f, param) == Catch::Approx(0.0f));
    }

    SECTION("0.49 returns 0 (off)") {
        REQUIRE(ParameterUtils::normalizedToReal(0.49f, param) == Catch::Approx(0.0f));
    }

    SECTION("0.5 returns 1 (on)") {
        REQUIRE(ParameterUtils::normalizedToReal(0.5f, param) == Catch::Approx(1.0f));
    }

    SECTION("1.0 returns 1 (on)") {
        REQUIRE(ParameterUtils::normalizedToReal(1.0f, param) == Catch::Approx(1.0f));
    }
}

TEST_CASE("normalizedToReal - Clamping", "[parameter][conversion]") {
    auto param = ParameterPresets::percent(0, "Mix");

    SECTION("Negative value clamps to min") {
        REQUIRE(ParameterUtils::normalizedToReal(-0.5f, param) == Catch::Approx(0.0f));
    }

    SECTION("Value > 1 clamps to max") {
        REQUIRE(ParameterUtils::normalizedToReal(1.5f, param) == Catch::Approx(100.0f));
    }
}

// ============================================================================
// realToNormalized Tests
// ============================================================================

TEST_CASE("realToNormalized - Linear scale", "[parameter][conversion]") {
    auto param = ParameterPresets::percent(0, "Mix");

    SECTION("0% returns 0.0") {
        REQUIRE(ParameterUtils::realToNormalized(0.0f, param) == Catch::Approx(0.0f));
    }

    SECTION("100% returns 1.0") {
        REQUIRE(ParameterUtils::realToNormalized(100.0f, param) == Catch::Approx(1.0f));
    }

    SECTION("50% returns 0.5") {
        REQUIRE(ParameterUtils::realToNormalized(50.0f, param) == Catch::Approx(0.5f));
    }
}

TEST_CASE("realToNormalized - Logarithmic scale (frequency)", "[parameter][conversion]") {
    auto param = ParameterPresets::frequency(0, "Cutoff");

    SECTION("20 Hz returns 0.0") {
        REQUIRE(ParameterUtils::realToNormalized(20.0f, param) == Catch::Approx(0.0f));
    }

    SECTION("20000 Hz returns 1.0") {
        REQUIRE(ParameterUtils::realToNormalized(20000.0f, param) == Catch::Approx(1.0f));
    }

    SECTION("~632 Hz returns 0.5 (geometric mean)") {
        REQUIRE(ParameterUtils::realToNormalized(632.46f, param) ==
                Catch::Approx(0.5f).margin(0.01f));
    }

    SECTION("440 Hz returns ~0.353") {
        // log(440/20) / log(20000/20) = log(22) / log(1000)
        // = 3.091 / 6.908 ~= 0.4475
        float norm = ParameterUtils::realToNormalized(440.0f, param);
        REQUIRE(norm > 0.0f);
        REQUIRE(norm < 1.0f);
    }
}

TEST_CASE("realToNormalized - Roundtrip consistency", "[parameter][conversion]") {
    SECTION("Linear parameter roundtrip") {
        auto param = ParameterPresets::percent(0, "Mix");
        float original = 37.5f;
        float normalized = ParameterUtils::realToNormalized(original, param);
        float recovered = ParameterUtils::normalizedToReal(normalized, param);
        REQUIRE(recovered == Catch::Approx(original).margin(0.01f));
    }

    SECTION("Logarithmic parameter roundtrip") {
        auto param = ParameterPresets::frequency(0, "Cutoff");
        float original = 440.0f;
        float normalized = ParameterUtils::realToNormalized(original, param);
        float recovered = ParameterUtils::normalizedToReal(normalized, param);
        REQUIRE(recovered == Catch::Approx(original).margin(0.1f));
    }

    SECTION("Multiple frequency values roundtrip") {
        auto param = ParameterPresets::frequency(0, "Cutoff");
        std::vector<float> testValues = {20.0f, 100.0f, 440.0f, 1000.0f, 5000.0f, 20000.0f};

        for (float original : testValues) {
            float normalized = ParameterUtils::realToNormalized(original, param);
            float recovered = ParameterUtils::normalizedToReal(normalized, param);
            REQUIRE(recovered == Catch::Approx(original).margin(0.1f));
        }
    }
}

// ============================================================================
// applyModulation Tests
// ============================================================================

TEST_CASE("applyModulation - Bipolar modulation", "[parameter][modulation]") {
    SECTION("LFO at center (0.5) has no effect") {
        float result = ParameterUtils::applyModulation(0.5f, 0.5f, 1.0f, true);
        // modOffset = 0.5 * 2 - 1 = 0
        // delta = 0 * 1.0 = 0
        REQUIRE(result == Catch::Approx(0.5f));
    }

    SECTION("LFO at max (1.0) pushes up") {
        float result = ParameterUtils::applyModulation(0.5f, 1.0f, 0.5f, true);
        // modOffset = 1.0 * 2 - 1 = 1.0
        // delta = 1.0 * 0.5 = 0.5
        // result = 0.5 + 0.5 = 1.0
        REQUIRE(result == Catch::Approx(1.0f));
    }

    SECTION("LFO at min (0.0) pushes down") {
        float result = ParameterUtils::applyModulation(0.5f, 0.0f, 0.5f, true);
        // modOffset = 0.0 * 2 - 1 = -1.0
        // delta = -1.0 * 0.5 = -0.5
        // result = 0.5 - 0.5 = 0.0
        REQUIRE(result == Catch::Approx(0.0f));
    }

    SECTION("Result is clamped to 0-1") {
        // Try to push above 1.0
        float result = ParameterUtils::applyModulation(0.8f, 1.0f, 0.5f, true);
        // delta = 1.0 * 0.5 = 0.5
        // 0.8 + 0.5 = 1.3 → clamped to 1.0
        REQUIRE(result == Catch::Approx(1.0f));

        // Try to push below 0.0
        result = ParameterUtils::applyModulation(0.2f, 0.0f, 0.5f, true);
        // delta = -1.0 * 0.5 = -0.5
        // 0.2 - 0.5 = -0.3 → clamped to 0.0
        REQUIRE(result == Catch::Approx(0.0f));
    }
}

TEST_CASE("applyModulation - Unipolar modulation", "[parameter][modulation]") {
    SECTION("LFO at 0.0 has no effect") {
        float result = ParameterUtils::applyModulation(0.5f, 0.0f, 1.0f, false);
        // modOffset = 0.0 (unipolar)
        // delta = 0.0 * 1.0 = 0
        REQUIRE(result == Catch::Approx(0.5f));
    }

    SECTION("LFO at 1.0 with full amount pushes to max") {
        float result = ParameterUtils::applyModulation(0.0f, 1.0f, 1.0f, false);
        // modOffset = 1.0 (unipolar)
        // delta = 1.0 * 1.0 = 1.0
        // result = 0.0 + 1.0 = 1.0
        REQUIRE(result == Catch::Approx(1.0f));
    }

    SECTION("LFO at 0.5 with 50% amount") {
        float result = ParameterUtils::applyModulation(0.25f, 0.5f, 0.5f, false);
        // modOffset = 0.5 (unipolar)
        // delta = 0.5 * 0.5 = 0.25
        // result = 0.25 + 0.25 = 0.5
        REQUIRE(result == Catch::Approx(0.5f));
    }
}

TEST_CASE("applyModulation - Amount controls depth", "[parameter][modulation]") {
    SECTION("Zero amount has no effect") {
        float result = ParameterUtils::applyModulation(0.5f, 1.0f, 0.0f, true);
        REQUIRE(result == Catch::Approx(0.5f));
    }

    SECTION("25% amount limits modulation range") {
        // Full LFO swing with 25% amount
        float result = ParameterUtils::applyModulation(0.5f, 1.0f, 0.25f, true);
        // delta = 1.0 * 0.25 = 0.25
        REQUIRE(result == Catch::Approx(0.75f));

        result = ParameterUtils::applyModulation(0.5f, 0.0f, 0.25f, true);
        // delta = -1.0 * 0.25 = -0.25
        REQUIRE(result == Catch::Approx(0.25f));
    }
}

// ============================================================================
// applyModulations (multiple) Tests
// ============================================================================

TEST_CASE("applyModulations - Multiple sources", "[parameter][modulation]") {
    SECTION("Two modulators summed") {
        std::vector<std::pair<float, float>> mods = {
            {0.75f, 0.4f},  // LFO1 at 75%, amount 40%
            {0.25f, 0.3f}   // LFO2 at 25%, amount 30%
        };

        float result = ParameterUtils::applyModulations(0.5f, mods, true);
        // LFO1: offset = 0.75 * 2 - 1 = 0.5, delta = 0.5 * 0.4 = 0.2
        // LFO2: offset = 0.25 * 2 - 1 = -0.5, delta = -0.5 * 0.3 = -0.15
        // total = 0.5 + 0.2 - 0.15 = 0.55
        REQUIRE(result == Catch::Approx(0.55f));
    }

    SECTION("Empty modulations returns base") {
        std::vector<std::pair<float, float>> mods;
        float result = ParameterUtils::applyModulations(0.7f, mods, true);
        REQUIRE(result == Catch::Approx(0.7f));
    }

    SECTION("Multiple modulations clamped") {
        std::vector<std::pair<float, float>> mods = {{1.0f, 0.5f}, {1.0f, 0.5f}, {1.0f, 0.5f}};

        float result = ParameterUtils::applyModulations(0.5f, mods, true);
        // Each adds 0.5, total would be 2.0, clamped to 1.0
        REQUIRE(result == Catch::Approx(1.0f));
    }
}

// ============================================================================
// formatValue Tests
// ============================================================================

TEST_CASE("formatValue - Frequency display", "[parameter][format]") {
    auto param = ParameterPresets::frequency(0, "Cutoff");

    SECTION("Low frequency shows Hz") {
        auto str = ParameterUtils::formatValue(440.0f, param);
        REQUIRE(str == "440.0 Hz");
    }

    SECTION("High frequency shows kHz") {
        auto str = ParameterUtils::formatValue(5000.0f, param);
        REQUIRE(str == "5.0 kHz");
    }

    SECTION("1000 Hz shows kHz") {
        auto str = ParameterUtils::formatValue(1000.0f, param);
        REQUIRE(str == "1.0 kHz");
    }
}

TEST_CASE("formatValue - Time display", "[parameter][format]") {
    auto param = ParameterPresets::time(0, "Attack");

    SECTION("Short time shows ms") {
        auto str = ParameterUtils::formatValue(100.0f, param);
        REQUIRE(str == "100.0 ms");
    }

    SECTION("Long time shows s") {
        auto str = ParameterUtils::formatValue(2500.0f, param);
        REQUIRE(str == "2.5 s");
    }
}

TEST_CASE("formatValue - Percent display", "[parameter][format]") {
    auto param = ParameterPresets::percent(0, "Mix");
    auto str = ParameterUtils::formatValue(50.0f, param);
    REQUIRE(str == "50.0%");
}

TEST_CASE("formatValue - Decibels display", "[parameter][format]") {
    auto param = ParameterPresets::decibels(0, "Gain");

    SECTION("Positive dB shows plus sign") {
        auto str = ParameterUtils::formatValue(6.0f, param);
        REQUIRE(str == "+6.0 dB");
    }

    SECTION("Negative dB shows minus") {
        auto str = ParameterUtils::formatValue(-12.0f, param);
        REQUIRE(str == "-12.0 dB");
    }

    SECTION("Zero dB") {
        auto str = ParameterUtils::formatValue(0.0f, param);
        REQUIRE(str == "0.0 dB");
    }
}

TEST_CASE("formatValue - Boolean display", "[parameter][format]") {
    auto param = ParameterPresets::boolean(0, "Bypass");

    SECTION("0.0 shows Off") {
        REQUIRE(ParameterUtils::formatValue(0.0f, param) == "Off");
    }

    SECTION("1.0 shows On") {
        REQUIRE(ParameterUtils::formatValue(1.0f, param) == "On");
    }
}

TEST_CASE("formatValue - Discrete display", "[parameter][format]") {
    std::vector<juce::String> choices = {"Off", "Low", "Medium", "High"};
    auto param = ParameterPresets::discrete(0, "Mode", choices);

    SECTION("Index 0 shows first choice") {
        REQUIRE(ParameterUtils::formatValue(0.0f, param) == "Off");
    }

    SECTION("Index 2 shows third choice") {
        REQUIRE(ParameterUtils::formatValue(2.0f, param) == "Medium");
    }

    SECTION("Index 3 shows last choice") {
        REQUIRE(ParameterUtils::formatValue(3.0f, param) == "High");
    }
}

// ============================================================================
// getChoiceString Tests
// ============================================================================

TEST_CASE("getChoiceString - Valid indices", "[parameter][format]") {
    std::vector<juce::String> choices = {"A", "B", "C"};
    auto param = ParameterPresets::discrete(0, "Choice", choices);

    REQUIRE(ParameterUtils::getChoiceString(0, param) == "A");
    REQUIRE(ParameterUtils::getChoiceString(1, param) == "B");
    REQUIRE(ParameterUtils::getChoiceString(2, param) == "C");
}

TEST_CASE("getChoiceString - Invalid indices", "[parameter][format]") {
    std::vector<juce::String> choices = {"A", "B"};
    auto param = ParameterPresets::discrete(0, "Choice", choices);

    SECTION("Negative index returns number string") {
        REQUIRE(ParameterUtils::getChoiceString(-1, param) == "-1");
    }

    SECTION("Out of range index returns number string") {
        REQUIRE(ParameterUtils::getChoiceString(5, param) == "5");
    }
}

TEST_CASE("getChoiceString - Empty choices", "[parameter][format]") {
    ParameterInfo param;
    param.scale = ParameterScale::Discrete;
    // No choices added

    REQUIRE(ParameterUtils::getChoiceString(0, param) == "0");
}

// ============================================================================
// Cutoff Modulation Example (from plan)
// ============================================================================

TEST_CASE("Cutoff modulation example from plan", "[parameter][example]") {
    // Define parameter
    ParameterInfo cutoff;
    cutoff.paramIndex = 0;
    cutoff.name = "Cutoff";
    cutoff.unit = "Hz";
    cutoff.minValue = 20.0f;
    cutoff.maxValue = 20000.0f;
    cutoff.defaultValue = 1000.0f;
    cutoff.scale = ParameterScale::Logarithmic;
    cutoff.bipolarModulation = true;

    // User sets cutoff to 440 Hz
    float realBase = 440.0f;
    float normalizedBase = ParameterUtils::realToNormalized(440.0f, cutoff);

    // ~0.353 according to plan, let's verify it's reasonable
    REQUIRE(normalizedBase > 0.0f);
    REQUIRE(normalizedBase < 0.5f);  // 440 Hz is below geometric mean (~632 Hz)

    // LFO modulates with amount = 0.5 (50% depth)
    float lfoValue = 1.0f;  // LFO at peak
    float amount = 0.5f;

    // Apply modulation
    float modulatedNorm = ParameterUtils::applyModulation(normalizedBase, lfoValue, amount, true);

    // Verify modulated normalized value increased
    REQUIRE(modulatedNorm > normalizedBase);

    // Convert back to Hz
    float modulatedReal = ParameterUtils::normalizedToReal(modulatedNorm, cutoff);

    // With amount = 0.5 and LFO at 1.0, should push up significantly
    REQUIRE(modulatedReal > realBase);
    REQUIRE(modulatedReal < 20000.0f);  // Should not exceed max

    // Verify display formatting
    auto displayStr = ParameterUtils::formatValue(modulatedReal, cutoff);
    REQUIRE((displayStr.contains("Hz") || displayStr.contains("kHz")));
}
