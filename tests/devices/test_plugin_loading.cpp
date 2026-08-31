#include <juce_audio_processors/juce_audio_processors.h>

#include <catch2/catch_test_macros.hpp>

// ============================================================================
// Plugin Description Matching Tests
// ============================================================================
// These tests verify the logic for correctly identifying plugins in
// multi-component VST3 bundles (e.g., Serum 2 vs Serum 2 FX sharing same .vst3)

namespace {

// Helper to create a mock PluginDescription
juce::PluginDescription createMockDescription(const juce::String& name,
                                              const juce::String& fileOrId, int uniqueId,
                                              int deprecatedUid, bool isInstrument,
                                              const juce::String& format = "VST3") {
    juce::PluginDescription desc;
    desc.name = name;
    desc.fileOrIdentifier = fileOrId;
    desc.uniqueId = uniqueId;
    desc.deprecatedUid = deprecatedUid;
    desc.isInstrument = isInstrument;
    desc.pluginFormatName = format;
    desc.manufacturerName = "Test Manufacturer";
    return desc;
}

// Simulates Tracktion Engine's createIdentifierString using deprecatedUid
juce::String createTEIdentifierString(const juce::PluginDescription& d) {
    return d.pluginFormatName + "-" + d.name + "-" +
           juce::String::toHexString(d.fileOrIdentifier.hashCode()) + "-" +
           juce::String::toHexString(d.deprecatedUid);
}

// Simulates JUCE's createIdentifierString using uniqueId
juce::String createJUCEIdentifierString(const juce::PluginDescription& d) {
    return d.pluginFormatName + "-" + d.name + "-" +
           juce::String::toHexString(d.fileOrIdentifier.hashCode()) + "-" +
           juce::String::toHexString(d.uniqueId);
}

// Simulates the patched findMatchingPluginDescription logic
// Returns the matching description from knownPlugins, or nullptr if not found
const juce::PluginDescription* findMatchingByUniqueIdAndName(
    const std::vector<juce::PluginDescription>& knownPlugins,
    const juce::PluginDescription& searchDesc) {
    // Match by uniqueId + name + fileOrIdentifier (most specific)
    if (searchDesc.uniqueId != 0 && searchDesc.name.isNotEmpty()) {
        for (const auto& d : knownPlugins) {
            if (d.uniqueId == searchDesc.uniqueId && d.name == searchDesc.name &&
                (searchDesc.fileOrIdentifier.isEmpty() ||
                 searchDesc.fileOrIdentifier == d.fileOrIdentifier)) {
                return &d;
            }
        }
    }

    // Fallback: match by uniqueId only (original TE behavior - problematic for multi-component)
    if (searchDesc.uniqueId != 0) {
        for (const auto& d : knownPlugins) {
            if (d.uniqueId == searchDesc.uniqueId &&
                (searchDesc.fileOrIdentifier.isEmpty() ||
                 searchDesc.fileOrIdentifier == d.fileOrIdentifier)) {
                return &d;
            }
        }
    }

    return nullptr;
}

// Original buggy behavior: match by fileOrIdentifier only, returns first match
const juce::PluginDescription* findMatchingByFileOnly(
    const std::vector<juce::PluginDescription>& knownPlugins,
    const juce::PluginDescription& searchDesc) {
    if (searchDesc.fileOrIdentifier.isNotEmpty()) {
        for (const auto& d : knownPlugins) {
            if (d.fileOrIdentifier == searchDesc.fileOrIdentifier) {
                return &d;  // Returns FIRST match - problematic!
            }
        }
    }
    return nullptr;
}

}  // namespace

// ============================================================================
// Multi-Component VST3 Bundle Tests (Serum scenario)
// ============================================================================

TEST_CASE("Multi-component VST3 - Same bundle, different plugins", "[plugin][vst3]") {
    // Simulate Serum 2 bundle with instrument and FX versions
    const juce::String serumPath = "/Library/Audio/Plug-Ins/VST3/Serum2.vst3";

    // Note: In real VST3, each component has different uniqueId but same fileOrIdentifier
    // deprecatedUid is 0 for VST3 plugins
    auto serumFX = createMockDescription("Serum 2 FX", serumPath, -1002064652, 0, false);
    auto serumInst = createMockDescription("Serum 2", serumPath, -1002318962, 0, true);

    // KnownPluginList order matters - FX comes first (alphabetically or scan order)
    std::vector<juce::PluginDescription> knownPlugins = {serumFX, serumInst};

    SECTION("Different uniqueIds for instrument vs effect") {
        REQUIRE(serumFX.uniqueId != serumInst.uniqueId);
        REQUIRE(serumFX.fileOrIdentifier == serumInst.fileOrIdentifier);
        REQUIRE(serumFX.name != serumInst.name);
    }

    SECTION("Identifier strings are different") {
        auto fxId = createJUCEIdentifierString(serumFX);
        auto instId = createJUCEIdentifierString(serumInst);

        REQUIRE(fxId != instId);
        REQUIRE(fxId.contains("Serum 2 FX"));
        REQUIRE(instId.contains("Serum 2"));
        REQUIRE_FALSE(instId.contains("Serum 2 FX"));
    }

    SECTION("File hash is same for both") {
        auto fxHash = juce::String::toHexString(serumFX.fileOrIdentifier.hashCode());
        auto instHash = juce::String::toHexString(serumInst.fileOrIdentifier.hashCode());
        REQUIRE(fxHash == instHash);
    }
}

TEST_CASE("Plugin lookup - Patched matching by uniqueId + name", "[plugin][matching]") {
    const juce::String serumPath = "/Library/Audio/Plug-Ins/VST3/Serum2.vst3";

    auto serumFX = createMockDescription("Serum 2 FX", serumPath, -1002064652, 0, false);
    auto serumInst = createMockDescription("Serum 2", serumPath, -1002318962, 0, true);

    // FX comes first in list (simulating alphabetical order)
    std::vector<juce::PluginDescription> knownPlugins = {serumFX, serumInst};

    SECTION("Search for instrument returns instrument, not FX") {
        juce::PluginDescription searchDesc;
        searchDesc.name = "Serum 2";
        searchDesc.fileOrIdentifier = serumPath;
        searchDesc.uniqueId = -1002318962;
        searchDesc.deprecatedUid = 0;

        auto result = findMatchingByUniqueIdAndName(knownPlugins, searchDesc);

        REQUIRE(result != nullptr);
        REQUIRE(result->name == "Serum 2");
        REQUIRE(result->isInstrument == true);
        REQUIRE(result->uniqueId == -1002318962);
    }

    SECTION("Search for FX returns FX, not instrument") {
        juce::PluginDescription searchDesc;
        searchDesc.name = "Serum 2 FX";
        searchDesc.fileOrIdentifier = serumPath;
        searchDesc.uniqueId = -1002064652;
        searchDesc.deprecatedUid = 0;

        auto result = findMatchingByUniqueIdAndName(knownPlugins, searchDesc);

        REQUIRE(result != nullptr);
        REQUIRE(result->name == "Serum 2 FX");
        REQUIRE(result->isInstrument == false);
    }
}

TEST_CASE("Plugin lookup - Original buggy file-only matching", "[plugin][matching][bug]") {
    const juce::String serumPath = "/Library/Audio/Plug-Ins/VST3/Serum2.vst3";

    auto serumFX = createMockDescription("Serum 2 FX", serumPath, -1002064652, 0, false);
    auto serumInst = createMockDescription("Serum 2", serumPath, -1002318962, 0, true);

    // FX comes first in list
    std::vector<juce::PluginDescription> knownPlugins = {serumFX, serumInst};

    SECTION("Bug: file-only match returns WRONG plugin (first match)") {
        juce::PluginDescription searchDesc;
        searchDesc.name = "Serum 2";  // We want the instrument
        searchDesc.fileOrIdentifier = serumPath;
        searchDesc.uniqueId = -1002318962;

        // This is the buggy behavior - matching by file only
        auto result = findMatchingByFileOnly(knownPlugins, searchDesc);

        REQUIRE(result != nullptr);
        // BUG: We asked for "Serum 2" but got "Serum 2 FX" because it's first!
        REQUIRE(result->name == "Serum 2 FX");     // Wrong plugin!
        REQUIRE(result->name != searchDesc.name);  // Mismatch!
    }
}

// ============================================================================
// PluginDescription Field Preservation Tests
// ============================================================================

TEST_CASE("PluginDescription - ValueTree round-trip simulation", "[plugin][valuetree]") {
    // Simulate what ExternalPlugin::create stores and constructor reads back

    auto original = createMockDescription("Serum 2", "/Library/Audio/Plug-Ins/VST3/Serum2.vst3",
                                          -1002318962, 0, true, "VST3");

    SECTION("All critical fields preserved after simulated save/load") {
        // Simulate ValueTree storage (what ExternalPlugin::create does)
        juce::ValueTree state("PLUGIN");
        state.setProperty("uniqueId", juce::String::toHexString(original.uniqueId), nullptr);
        state.setProperty("uid", juce::String::toHexString(original.deprecatedUid), nullptr);
        state.setProperty("filename", original.fileOrIdentifier, nullptr);
        state.setProperty("name", original.name, nullptr);
        state.setProperty("manufacturer", original.manufacturerName, nullptr);
        state.setProperty("format", original.pluginFormatName, nullptr);  // NEW: format stored

        // Simulate constructor reading back
        juce::PluginDescription loaded;
        loaded.uniqueId = (int)state["uniqueId"].toString().getHexValue64();
        loaded.deprecatedUid = (int)state["uid"].toString().getHexValue64();
        loaded.fileOrIdentifier = state["filename"].toString();
        loaded.name = state["name"].toString();
        loaded.manufacturerName = state["manufacturer"].toString();
        loaded.pluginFormatName = state.getProperty("format", juce::String()).toString();

        // Verify all fields match
        REQUIRE(loaded.uniqueId == original.uniqueId);
        REQUIRE(loaded.deprecatedUid == original.deprecatedUid);
        REQUIRE(loaded.fileOrIdentifier == original.fileOrIdentifier);
        REQUIRE(loaded.name == original.name);
        REQUIRE(loaded.manufacturerName == original.manufacturerName);
        REQUIRE(loaded.pluginFormatName == original.pluginFormatName);  // Critical!
    }

    SECTION("pluginFormatName was NOT stored before fix") {
        // Simulate old behavior (before fix)
        juce::ValueTree state("PLUGIN");
        state.setProperty("uniqueId", juce::String::toHexString(original.uniqueId), nullptr);
        state.setProperty("uid", juce::String::toHexString(original.deprecatedUid), nullptr);
        state.setProperty("filename", original.fileOrIdentifier, nullptr);
        state.setProperty("name", original.name, nullptr);
        // Note: format NOT stored (old behavior)

        juce::PluginDescription loaded;
        loaded.uniqueId = (int)state["uniqueId"].toString().getHexValue64();
        loaded.deprecatedUid = (int)state["uid"].toString().getHexValue64();
        loaded.fileOrIdentifier = state["filename"].toString();
        loaded.name = state["name"].toString();
        loaded.pluginFormatName = state.getProperty("format", juce::String()).toString();

        // This was the bug: pluginFormatName is empty
        REQUIRE(loaded.pluginFormatName.isEmpty());

        // This caused TE's identifier string to lack the format prefix
        auto teId = createTEIdentifierString(loaded);
        REQUIRE(teId.startsWith("-Serum 2-"));  // Missing "VST3" prefix!
    }
}

// ============================================================================
// Identifier String Format Tests
// ============================================================================

TEST_CASE("Identifier string - TE vs JUCE format mismatch", "[plugin][identifier]") {
    auto desc = createMockDescription("Serum 2", "/Library/Audio/Plug-Ins/VST3/Serum2.vst3",
                                      -1002318962, 0, true, "VST3");

    SECTION("TE uses deprecatedUid (0 for VST3), JUCE uses uniqueId") {
        auto teId = createTEIdentifierString(desc);
        auto juceId = createJUCEIdentifierString(desc);

        // They're different because suffix uses different ID
        REQUIRE(teId != juceId);

        // TE ends with deprecatedUid (0 for VST3)
        REQUIRE(teId.endsWith("-0"));

        // JUCE ends with uniqueId (the actual VST3 component ID)
        REQUIRE_FALSE(juceId.endsWith("-0"));
        REQUIRE(juceId.contains(juce::String::toHexString(desc.uniqueId)));
    }

    SECTION("With empty pluginFormatName, TE identifier lacks format prefix") {
        juce::PluginDescription emptyFormat = desc;
        emptyFormat.pluginFormatName = "";  // Bug condition

        auto teId = createTEIdentifierString(emptyFormat);

        // Starts with hyphen instead of format name
        REQUIRE(teId.startsWith("-Serum 2-"));
        REQUIRE_FALSE(teId.startsWith("VST3-"));
    }
}

// ============================================================================
// isInstrument Matching Tests
// ============================================================================

TEST_CASE("Plugin lookup by isInstrument flag", "[plugin][instrument]") {
    const juce::String serumPath = "/Library/Audio/Plug-Ins/VST3/Serum2.vst3";

    auto serumFX = createMockDescription("Serum 2 FX", serumPath, -1002064652, 0, false);
    auto serumInst = createMockDescription("Serum 2", serumPath, -1002318962, 0, true);

    std::vector<juce::PluginDescription> knownPlugins = {serumFX, serumInst};

    SECTION("Can distinguish by isInstrument + fileOrIdentifier") {
        // Search for instrument in bundle
        const juce::PluginDescription* instrumentMatch = nullptr;
        for (const auto& d : knownPlugins) {
            if (d.fileOrIdentifier == serumPath && d.isInstrument) {
                instrumentMatch = &d;
                break;
            }
        }

        REQUIRE(instrumentMatch != nullptr);
        REQUIRE(instrumentMatch->name == "Serum 2");
        REQUIRE(instrumentMatch->isInstrument == true);
    }

    SECTION("Can distinguish by isInstrument + name pattern") {
        // FX plugins typically have "FX" in name
        const juce::PluginDescription* fxMatch = nullptr;
        for (const auto& d : knownPlugins) {
            if (d.fileOrIdentifier == serumPath && !d.isInstrument) {
                fxMatch = &d;
                break;
            }
        }

        REQUIRE(fxMatch != nullptr);
        REQUIRE(fxMatch->name == "Serum 2 FX");
        REQUIRE(fxMatch->isInstrument == false);
    }
}

// ============================================================================
// Edge Cases
// ============================================================================

TEST_CASE("Plugin lookup - Edge cases", "[plugin][edge]") {
    SECTION("Empty knownPlugins list returns nullptr") {
        std::vector<juce::PluginDescription> emptyList;
        juce::PluginDescription search;
        search.name = "Test";
        search.uniqueId = 12345;

        REQUIRE(findMatchingByUniqueIdAndName(emptyList, search) == nullptr);
    }

    SECTION("Zero uniqueId skips uniqueId matching") {
        auto plugin = createMockDescription("Test", "/path/test.vst3", 0, 0, false);
        std::vector<juce::PluginDescription> knownPlugins = {plugin};

        juce::PluginDescription search;
        search.name = "Test";
        search.uniqueId = 0;  // Zero uniqueId
        search.fileOrIdentifier = "/path/test.vst3";

        // With uniqueId = 0, both conditions in findMatchingByUniqueIdAndName fail
        REQUIRE(findMatchingByUniqueIdAndName(knownPlugins, search) == nullptr);
    }

    SECTION("Empty name with valid uniqueId falls through to uniqueId-only match") {
        auto plugin = createMockDescription("Test Plugin", "/path/test.vst3", 12345, 0, false);
        std::vector<juce::PluginDescription> knownPlugins = {plugin};

        juce::PluginDescription search;
        search.name = "";  // Empty name
        search.uniqueId = 12345;
        search.fileOrIdentifier = "/path/test.vst3";

        // First condition fails (name empty), falls through to uniqueId-only
        auto result = findMatchingByUniqueIdAndName(knownPlugins, search);
        REQUIRE(result != nullptr);
        REQUIRE(result->name == "Test Plugin");
    }
}
