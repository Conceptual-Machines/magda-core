#include <catch2/catch_test_macros.hpp>

#include "magda/daw/engine/PluginMetadataStore.hpp"
#include "magda/daw/ui/panels/content/PluginBrowserMetadataMerge.hpp"

namespace {

juce::PluginDescription description(const juce::String& name, const juce::String& path,
                                    int uniqueId, int deprecatedUid) {
    juce::PluginDescription result;
    result.name = name;
    result.manufacturerName = "Live Vendor";
    result.category = "Effect";
    result.pluginFormatName = "VST3";
    result.fileOrIdentifier = path;
    result.uniqueId = uniqueId;
    result.deprecatedUid = deprecatedUid;
    return result;
}

magda::PluginMetadataRecord metadata(const juce::PluginDescription& plugin,
                                     const juce::String& alias) {
    return {
        .key = plugin.createIdentifierString(),
        .name = plugin.name,
        .format = plugin.pluginFormatName,
        .category = plugin.category,
        .manufacturer = "Stored Vendor",
        .fileOrIdentifier = plugin.fileOrIdentifier,
        .alias = alias,
        .isInstrument = plugin.isInstrument,
        .isFavorite = true,
    };
}

}  // namespace

TEST_CASE("plugin browser merges partial metadata per preferred plugin",
          "[plugin][browser][metadata-store]") {
    const auto stored = description("Stored", "/plugins/Stored.vst3", 1001, 1001);
    const auto liveOnly = description("Live Only", "/plugins/LiveOnly.vst3", 1002, 1002);
    juce::Array<juce::PluginDescription> preferred{stored, liveOnly};

    const auto merged =
        magda::daw::ui::mergeExternalPluginMetadata(preferred, {metadata(stored, "stored_alias")});

    REQUIRE(merged.size() == 2);
    CHECK(merged[0].alias == "stored_alias");
    CHECK(merged[0].manufacturer == "Stored Vendor");
    CHECK(merged[0].isFavorite);
    CHECK(merged[1].name == liveOnly.name);
    CHECK(merged[1].manufacturer == liveOnly.manufacturerName);
    CHECK_FALSE(merged[1].isFavorite);
}

TEST_CASE("plugin browser keeps preferred entries whose metadata keys collide",
          "[plugin][browser][metadata-store][duplicates]") {
    const auto legacy = description("Migrated", "/plugins/Migrated.vst3", 0, 2001);
    const auto current = description("Migrated", "/plugins/Migrated.vst3", 2001, 2001);
    REQUIRE(legacy.createIdentifierString() == current.createIdentifierString());

    juce::KnownPluginList preferred;
    preferred.addType(legacy);
    preferred.addType(current);
    REQUIRE(preferred.getNumTypes() == 2);

    const auto merged = magda::daw::ui::mergeExternalPluginMetadata(
        preferred.getTypes(), {metadata(current, "migrated_alias")});

    REQUIRE(merged.size() == 2);
    CHECK(merged[0].alias == "migrated_alias");
    CHECK(merged[1].alias == "migrated_alias");
}
