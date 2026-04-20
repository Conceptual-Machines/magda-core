#include <catch2/catch_test_macros.hpp>

#include "../magda/daw/core/DeviceInfo.hpp"
#include "../magda/daw/core/aliases/AliasRegistry.hpp"
#include "../magda/daw/core/aliases/AutoAliasGenerator.hpp"

using namespace magda;

// ============================================================================
// Helpers
// ============================================================================

static ChainNodePath makePath(int trackId, int deviceId) {
    return ChainNodePath::topLevelDevice(trackId, deviceId);
}

static DeviceInfo makeDevice(DeviceId id, const juce::String& name,
                             const std::vector<juce::String>& paramNames) {
    DeviceInfo dev;
    dev.id = id;
    dev.name = name;
    for (int i = 0; i < static_cast<int>(paramNames.size()); ++i) {
        ParameterInfo p;
        p.paramIndex = i;
        p.name = paramNames[static_cast<size_t>(i)];
        dev.parameters.push_back(p);
    }
    return dev;
}

// ============================================================================
// computeForDevice - key generation
// ============================================================================

TEST_CASE("AutoAliasGenerator::computeForDevice - basic key generation", "[aliases][autogen]") {
    auto dev = makeDevice(10, "Serum 2", {"Filter 1 Cutoff", "Filter 1 Resonance", "Env Attack"});
    auto path = makePath(1, 10);

    auto entries = AutoAliasGenerator::computeForDevice(dev, path);

    REQUIRE(entries.count("serum_2.filter_1_cutoff") == 1);
    REQUIRE(entries.count("serum_2.filter_1_resonance") == 1);
    REQUIRE(entries.count("serum_2.env_attack") == 1);

    // Check param indices
    REQUIRE(entries.at("serum_2.filter_1_cutoff").paramIndex == 0);
    REQUIRE(entries.at("serum_2.filter_1_resonance").paramIndex == 1);
    REQUIRE(entries.at("serum_2.env_attack").paramIndex == 2);
}

TEST_CASE("AutoAliasGenerator::computeForDevice - pluginKey matches normalizeParamName",
          "[aliases][autogen]") {
    auto dev = makeDevice(11, "Pro-Q 3", {"Low Freq", "High Freq"});
    auto path = makePath(2, 11);

    auto entries = AutoAliasGenerator::computeForDevice(dev, path);
    REQUIRE(entries.count("pro_q_3.low_freq") == 1);
    REQUIRE(entries.count("pro_q_3.high_freq") == 1);
}

TEST_CASE("AutoAliasGenerator::computeForDevice - duplicate param names get uniquified",
          "[aliases][autogen]") {
    // Two params with same normalized name -> _2 suffix for second
    auto dev = makeDevice(12, "MySynth", {"Cutoff", "Cutoff", "Cutoff"});
    auto path = makePath(3, 12);

    auto entries = AutoAliasGenerator::computeForDevice(dev, path);
    REQUIRE(entries.count("mysynth.cutoff") == 1);
    REQUIRE(entries.count("mysynth.cutoff_2") == 1);
    REQUIRE(entries.count("mysynth.cutoff_3") == 1);

    REQUIRE(entries.at("mysynth.cutoff").paramIndex == 0);
    REQUIRE(entries.at("mysynth.cutoff_2").paramIndex == 1);
    REQUIRE(entries.at("mysynth.cutoff_3").paramIndex == 2);
}

TEST_CASE("AutoAliasGenerator::computeForDevice - empty parameter list returns empty map",
          "[aliases][autogen]") {
    auto dev = makeDevice(13, "Empty Plugin", {});
    auto path = makePath(4, 13);

    auto entries = AutoAliasGenerator::computeForDevice(dev, path);
    REQUIRE(entries.empty());
}

TEST_CASE("AutoAliasGenerator::computeForDevice - StoredAlias carries correct metadata",
          "[aliases][autogen]") {
    auto dev = makeDevice(14, "Compressor", {"Threshold", "Ratio", "Attack", "Release"});
    auto path = makePath(5, 14);

    auto entries = AutoAliasGenerator::computeForDevice(dev, path);
    REQUIRE(entries.count("compressor.threshold") == 1);

    const auto& a = entries.at("compressor.threshold");
    REQUIRE(a.pluginTypeKey == "compressor");
    REQUIRE(a.paramIndex == 0);
    REQUIRE(a.paramNameAtSetTime == "Threshold");
    REQUIRE(a.path.has_value());
    REQUIRE(*a.path == path);
}

// ============================================================================
// Round-trip: merge into AliasRegistry then lookup
// ============================================================================

TEST_CASE("AutoAliasGenerator - round-trip via AliasRegistry lookup", "[aliases][autogen]") {
    auto& reg = AliasRegistry::getInstance();
    reg.clearLayer(AliasLayer::AutoGen);

    auto dev = makeDevice(20, "Serum 2", {"Filter 1 Cutoff", "Filter 1 Resonance", "Env Attack"});
    auto path = makePath(10, 20);

    auto entries = AutoAliasGenerator::computeForDevice(dev, path);
    reg.replaceAutoForDevice(path, entries);

    // lookup() should return StaticTarget since path is present
    auto result = reg.lookup("serum_2.filter_1_cutoff");
    REQUIRE(result.has_value());
    REQUIRE(result->paramIndex == 0);
    REQUIRE(result->devicePath == path);

    auto result2 = reg.lookup("serum_2.filter_1_resonance");
    REQUIRE(result2.has_value());
    REQUIRE(result2->paramIndex == 1);

    reg.clearLayer(AliasLayer::AutoGen);
}

TEST_CASE("AutoAliasGenerator - replaceAutoForDevice isolates per-device entries",
          "[aliases][autogen]") {
    auto& reg = AliasRegistry::getInstance();
    reg.clearLayer(AliasLayer::AutoGen);

    auto path1 = makePath(1, 10);
    auto path2 = makePath(2, 20);

    // Load two devices into AutoGen
    auto dev1 = makeDevice(10, "Serum 2", {"Cutoff"});
    auto dev2 = makeDevice(20, "Compressor", {"Threshold"});

    auto entries1 = AutoAliasGenerator::computeForDevice(dev1, path1);
    auto entries2 = AutoAliasGenerator::computeForDevice(dev2, path2);

    reg.replaceAutoForDevice(path1, entries1);
    reg.replaceAutoForDevice(path2, entries2);

    REQUIRE(reg.lookup("serum_2.cutoff").has_value());
    REQUIRE(reg.lookup("compressor.threshold").has_value());

    // Re-generate dev1 with a different param list (simulating plugin version change)
    auto dev1v2 = makeDevice(10, "Serum 2", {"Cutoff", "Resonance"});
    auto entries1v2 = AutoAliasGenerator::computeForDevice(dev1v2, path1);
    reg.replaceAutoForDevice(path1, entries1v2);

    // dev1 should have both params; dev2 should be unaffected
    REQUIRE(reg.lookup("serum_2.cutoff").has_value());
    REQUIRE(reg.lookup("serum_2.resonance").has_value());
    REQUIRE(reg.lookup("compressor.threshold").has_value());

    reg.clearLayer(AliasLayer::AutoGen);
}
