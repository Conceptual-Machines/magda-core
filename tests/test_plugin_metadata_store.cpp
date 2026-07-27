#include <juce_audio_processors/juce_audio_processors.h>

#include <catch2/catch_test_macros.hpp>

#include "magda/daw/engine/PluginMetadataStore.hpp"

namespace {

struct TempDirectory {
    TempDirectory()
        : directory(juce::File::getCurrentWorkingDirectory().getChildFile(
              "magda-plugin-metadata-" + juce::Uuid().toString())) {
        const auto result = directory.createDirectory();
        INFO(result.getErrorMessage().toStdString());
        REQUIRE(result.wasOk());
    }
    ~TempDirectory() {
        directory.deleteRecursively();
    }
    juce::File directory;
};

juce::PluginDescription description(const juce::String& name, const juce::String& path,
                                    int uniqueId, bool instrument,
                                    const juce::String& format = "VST3") {
    juce::PluginDescription result;
    result.name = name;
    result.descriptiveName = name;
    result.pluginFormatName = format;
    result.category = instrument ? "Synth" : "Effect";
    result.manufacturerName = "Test Vendor";
    result.fileOrIdentifier = path;
    result.uniqueId = uniqueId;
    result.deprecatedUid = uniqueId;
    result.isInstrument = instrument;
    return result;
}

}  // namespace

TEST_CASE("PluginMetadataStore round-trips descriptions and user metadata",
          "[plugin][metadata-store]") {
    TempDirectory temp;
    const auto database = temp.directory.getChildFile("plugin_metadata.db");

    juce::KnownPluginList input;
    const auto synth = description("Test Synth", "/plugins/TestSynth.vst3", 1001, true);
    const auto effect = description("Test Effect", "/plugins/TestEffect.vst3", 1002, false);
    input.addType(synth);
    input.addType(effect);

    {
        magda::PluginMetadataStore store(database);
        store.saveKnownPlugins(input);
        store.setFavorite(synth.createIdentifierString(), synth.name, true);
        store.setAlias(synth.createIdentifierString(), "test_synth_custom");
        store.saveExclusions({{effect.fileOrIdentifier, "crash", "2026-07-27T12:00:00Z"}});
    }

    magda::PluginMetadataStore reopened(database);
    juce::KnownPluginList output;
    reopened.loadKnownPlugins(output);
    REQUIRE(output.getNumTypes() == 2);
    REQUIRE(reopened.favoriteKeys().contains(synth.createIdentifierString()));
    REQUIRE(reopened.aliases().at(synth.createIdentifierString()) == "test_synth_custom");

    const auto exclusions = reopened.loadExclusions();
    REQUIRE(exclusions.size() == 1);
    CHECK(exclusions.front().path == effect.fileOrIdentifier);
    CHECK(exclusions.front().reason == "crash");
}

TEST_CASE("PluginMetadataStore directly queries across metadata concerns",
          "[plugin][metadata-store]") {
    TempDirectory temp;
    magda::PluginMetadataStore store(temp.directory.getChildFile("plugin_metadata.db"));

    const auto included = description("Included Synth", "/plugins/Included.vst3", 2001, true);
    const auto excluded = description("Excluded Synth", "/plugins/Excluded.vst3", 2002, true);
    const auto effect = description("Favorite Effect", "/plugins/Effect.vst3", 2003, false);
    const auto au = description("AU Synth", "/plugins/AUSynth.component", 2004, true, "AudioUnit");

    juce::KnownPluginList plugins;
    plugins.addType(included);
    plugins.addType(excluded);
    plugins.addType(effect);
    plugins.addType(au);
    store.saveKnownPlugins(plugins);

    for (const auto& plugin : {included, excluded, effect, au})
        store.setFavorite(plugin.createIdentifierString(), plugin.name, true);
    store.saveExclusions({{excluded.fileOrIdentifier, "timeout", ""}});

    magda::PluginMetadataQuery query;
    query.favorite = true;
    query.instrument = true;
    query.format = "VST3";
    query.excludeExcluded = true;

    const auto matches = store.query(query);
    REQUIRE(matches.size() == 1);
    CHECK(matches.front().name == included.name);
    CHECK(matches.front().isFavorite);
    CHECK(matches.front().isInstrument);
}

TEST_CASE("PluginMetadataStore imports legacy files once", "[plugin][metadata-store][migration]") {
    TempDirectory temp;
    const auto legacyPlugin = description("Legacy Synth", "/plugins/Legacy.vst3", 3001, true);

    juce::KnownPluginList legacyList;
    legacyList.addType(legacyPlugin);
    REQUIRE(legacyList.createXml()->writeTo(temp.directory.getChildFile("PluginList.xml")));
    REQUIRE(temp.directory.getChildFile("plugin_favorites.xml")
                .replaceWithText("<PluginFavorites><Plugin key=\"" +
                                 legacyPlugin.createIdentifierString() +
                                 "\" name=\"Legacy Synth\"/></PluginFavorites>"));
    REQUIRE(temp.directory.getChildFile("plugin_aliases.xml")
                .replaceWithText("<PluginAliases><Alias key=\"" +
                                 legacyPlugin.createIdentifierString() +
                                 "\" alias=\"legacy_custom\"/></PluginAliases>"));
    REQUIRE(temp.directory.getChildFile("plugin_exclusions.txt")
                .replaceWithText("/plugins/Bad.vst3\tcrash\t2026-07-27T12:00:00Z\n"));

    const auto database = temp.directory.getChildFile("plugin_metadata.db");
    {
        magda::PluginMetadataStore store(database);
        REQUIRE(store.query().size() == 1);
        CHECK(store.favoriteKeys().contains(legacyPlugin.createIdentifierString()));
        CHECK(store.aliases().at(legacyPlugin.createIdentifierString()) == "legacy_custom");
        CHECK(store.loadExclusions().size() == 1);
    }

    // A later edit to a legacy input must not overwrite the migrated database.
    REQUIRE(temp.directory.getChildFile("plugin_aliases.xml").replaceWithText("<PluginAliases/>"));
    magda::PluginMetadataStore reopened(database);
    CHECK(reopened.aliases().at(legacyPlugin.createIdentifierString()) == "legacy_custom");
}
