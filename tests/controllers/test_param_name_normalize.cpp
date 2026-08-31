#include <catch2/catch_test_macros.hpp>

#include "../magda/daw/core/aliases/ParamNameNormalize.hpp"

using namespace magda;

// ============================================================================
// normalizeParamName
// ============================================================================

TEST_CASE("normalizeParamName - basic cases", "[aliases][normalize]") {
    REQUIRE(normalizeParamName("Filter Cutoff") == "filter_cutoff");
    REQUIRE(normalizeParamName("Resonance") == "resonance");
    REQUIRE(normalizeParamName("osc 1 pitch") == "osc_1_pitch");
    REQUIRE(normalizeParamName("Reverb Size (%)") == "reverb_size");
    REQUIRE(normalizeParamName("Freq") == "freq");
}

TEST_CASE("normalizeParamName - digits kept", "[aliases][normalize]") {
    REQUIRE(normalizeParamName("Cutoff 1") == "cutoff_1");
    REQUIRE(normalizeParamName("OSC2 Detune") == "osc_2_detune");
    REQUIRE(normalizeParamName("Filter 1 Cutoff") == "filter_1_cutoff");
}

TEST_CASE("normalizeParamName - diacritics stripped", "[aliases][normalize]") {
    REQUIRE(normalizeParamName("Frequenz") == "frequenz");
    // accented e -> e
    juce::String withAccent;
    withAccent += juce::juce_wchar(0x00E9);  // e-acute
    withAccent += "q";
    REQUIRE(normalizeParamName(withAccent) == "eq");
}

TEST_CASE("normalizeParamName - leading/trailing separators stripped", "[aliases][normalize]") {
    REQUIRE(normalizeParamName("  Gain  ") == "gain");
    REQUIRE(normalizeParamName("--Gain--") == "gain");
}

TEST_CASE("normalizeParamName - multiple separators collapsed", "[aliases][normalize]") {
    REQUIRE(normalizeParamName("A  B") == "a_b");
    REQUIRE(normalizeParamName("A--B") == "a_b");
    REQUIRE(normalizeParamName("A-_-B") == "a_b");
}

TEST_CASE("normalizeParamName - empty input", "[aliases][normalize]") {
    REQUIRE(normalizeParamName("").isEmpty());
}

TEST_CASE("normalizeParamName - already snake_case", "[aliases][normalize]") {
    REQUIRE(normalizeParamName("filter_cutoff") == "filter_cutoff");
}

// ============================================================================
// uniquify
// ============================================================================

TEST_CASE("uniquify - no duplicates unchanged", "[aliases][uniquify]") {
    std::vector<juce::String> names{"cutoff", "gain", "pan"};
    auto result = uniquify(names);
    REQUIRE(result.size() == 3);
    REQUIRE(result[0] == "cutoff");
    REQUIRE(result[1] == "gain");
    REQUIRE(result[2] == "pan");
}

TEST_CASE("uniquify - duplicates get _2, _3 suffix", "[aliases][uniquify]") {
    std::vector<juce::String> names{"cutoff", "cutoff", "cutoff"};
    auto result = uniquify(names);
    REQUIRE(result.size() == 3);
    REQUIRE(result[0] == "cutoff");
    REQUIRE(result[1] == "cutoff_2");
    REQUIRE(result[2] == "cutoff_3");
}

TEST_CASE("uniquify - non-adjacent duplicates", "[aliases][uniquify]") {
    std::vector<juce::String> names{"freq", "gain", "freq"};
    auto result = uniquify(names);
    REQUIRE(result.size() == 3);
    REQUIRE(result[0] == "freq");
    REQUIRE(result[1] == "gain");
    REQUIRE(result[2] == "freq_2");
}

TEST_CASE("uniquify - empty list", "[aliases][uniquify]") {
    std::vector<juce::String> names;
    auto result = uniquify(names);
    REQUIRE(result.empty());
}

TEST_CASE("uniquify - single item", "[aliases][uniquify]") {
    std::vector<juce::String> names{"cutoff"};
    auto result = uniquify(names);
    REQUIRE(result.size() == 1);
    REQUIRE(result[0] == "cutoff");
}
