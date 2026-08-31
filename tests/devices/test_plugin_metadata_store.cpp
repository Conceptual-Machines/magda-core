#include <juce_audio_processors/juce_audio_processors.h>

#include <catch2/catch_test_macros.hpp>
#include <exception>
#include <string>
#include <thread>

#include "magda/daw/engine/PluginMetadataStore.hpp"

namespace {

struct TempDirectory {
    TempDirectory()
        : directory(juce::File::getSpecialLocation(juce::File::tempDirectory)
                        .getChildFile("magda-plugin-metadata-" + juce::Uuid().toString())) {
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

void executeSql(const juce::File& database, const char* sql) {
    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open(database.getFullPathName().toRawUTF8(), &db) == SQLITE_OK);
    char* message = nullptr;
    const auto result = sqlite3_exec(db, sql, nullptr, nullptr, &message);
    INFO((message ? message : ""));
    sqlite3_free(message);
    REQUIRE(result == SQLITE_OK);
    REQUIRE(sqlite3_close(db) == SQLITE_OK);
}

int scalarInt(const juce::File& database, const char* sql) {
    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open(database.getFullPathName().toRawUTF8(), &db) == SQLITE_OK);
    sqlite3_stmt* statement = nullptr;
    REQUIRE(sqlite3_prepare_v2(db, sql, -1, &statement, nullptr) == SQLITE_OK);
    REQUIRE(sqlite3_step(statement) == SQLITE_ROW);
    const auto result = sqlite3_column_int(statement, 0);
    REQUIRE(sqlite3_finalize(statement) == SQLITE_OK);
    REQUIRE(sqlite3_close(db) == SQLITE_OK);
    return result;
}

std::string exceptionMessage(const std::exception_ptr& error) {
    if (!error)
        return {};
    try {
        std::rethrow_exception(error);
    } catch (const std::exception& e) {
        return e.what();
    } catch (...) {
        return "unknown non-standard exception";
    }
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
    CHECK(scalarInt(database, "PRAGMA user_version") == magda::kPluginMetadataSchemaVersion);
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

TEST_CASE("PluginMetadataStore keeps sparse user metadata when plugins disappear after a rescan",
          "[plugin][metadata-store]") {
    TempDirectory temp;
    magda::PluginMetadataStore store(temp.directory.getChildFile("plugin_metadata.db"));

    const auto favorite = description("Missing Favorite", "/plugins/Favorite.vst3", 2101, true);
    const auto aliased = description("Missing Alias", "/plugins/Aliased.vst3", 2102, false);
    juce::KnownPluginList initial;
    initial.addType(favorite);
    initial.addType(aliased);
    store.saveKnownPlugins(initial);
    store.setFavorite(favorite.createIdentifierString(), favorite.name, true);
    store.setAlias(aliased.createIdentifierString(), "missing_alias");

    juce::KnownPluginList emptyRescan;
    store.saveKnownPlugins(emptyRescan);

    CHECK(store.favoriteKeys().contains(favorite.createIdentifierString()));
    REQUIRE(store.aliases().count(aliased.createIdentifierString()) == 1);
    CHECK(store.aliases().at(aliased.createIdentifierString()) == "missing_alias");
    CHECK(store.query().empty());
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

TEST_CASE("PluginMetadataStore rolls back an interrupted legacy import",
          "[plugin][metadata-store][migration]") {
    TempDirectory temp;
    const auto database = temp.directory.getChildFile("plugin_metadata.db");

    // Create the schema, then recreate the first-import state with a trigger
    // that simulates an interruption during the final legacy dataset.
    { magda::PluginMetadataStore schema(database); }
    executeSql(database, R"SQL(
        DELETE FROM plugin_store_meta WHERE key='legacy_import_complete';
        CREATE TRIGGER fail_legacy_exclusion
        BEFORE INSERT ON plugin_exclusion
        BEGIN
            SELECT RAISE(ABORT, 'simulated legacy import failure');
        END;
    )SQL");

    const auto legacyPlugin = description("Atomic Legacy", "/plugins/Atomic.vst3", 3101, true);
    juce::KnownPluginList legacyList;
    legacyList.addType(legacyPlugin);
    REQUIRE(legacyList.createXml()->writeTo(temp.directory.getChildFile("PluginList.xml")));
    REQUIRE(temp.directory.getChildFile("plugin_favorites.xml")
                .replaceWithText("<PluginFavorites><Plugin key=\"" +
                                 legacyPlugin.createIdentifierString() +
                                 "\" name=\"Atomic Legacy\"/></PluginFavorites>"));
    REQUIRE(temp.directory.getChildFile("plugin_aliases.xml")
                .replaceWithText("<PluginAliases><Alias key=\"" +
                                 legacyPlugin.createIdentifierString() +
                                 "\" alias=\"atomic_legacy\"/></PluginAliases>"));
    REQUIRE(temp.directory.getChildFile("plugin_exclusions.txt")
                .replaceWithText("/plugins/BadAtomic.vst3\tcrash\t2026-07-27T12:00:00Z\n"));

    CHECK_THROWS_AS(magda::PluginMetadataStore(database), magda::sqlite::Error);
    CHECK(scalarInt(database, "SELECT COUNT(*) FROM plugin") == 0);
    CHECK(scalarInt(database, "SELECT COUNT(*) FROM plugin_store_meta "
                              "WHERE key='legacy_import_complete'") == 0);

    executeSql(database, "DROP TRIGGER fail_legacy_exclusion");
    magda::PluginMetadataStore recovered(database);
    CHECK(recovered.query().size() == 1);
    CHECK(recovered.favoriteKeys().contains(legacyPlugin.createIdentifierString()));
    CHECK(recovered.aliases().at(legacyPlugin.createIdentifierString()) == "atomic_legacy");
    CHECK(recovered.loadExclusions().size() == 1);
}

TEST_CASE("PluginMetadataStore serializes concurrent first opens",
          "[plugin][metadata-store][migration]") {
    TempDirectory temp;
    const auto database = temp.directory.getChildFile("plugin_metadata.db");
    const auto legacyPlugin =
        description("Concurrent Legacy", "/plugins/Concurrent.vst3", 3201, true);
    juce::KnownPluginList legacyList;
    legacyList.addType(legacyPlugin);
    REQUIRE(legacyList.createXml()->writeTo(temp.directory.getChildFile("PluginList.xml")));

    std::exception_ptr firstError;
    std::exception_ptr secondError;
    auto open = [&database](std::exception_ptr& error) {
        try {
            magda::PluginMetadataStore store(database);
        } catch (...) {
            error = std::current_exception();
        }
    };
    std::thread first(open, std::ref(firstError));
    std::thread second(open, std::ref(secondError));
    first.join();
    second.join();

    INFO("first open: " + exceptionMessage(firstError));
    INFO("second open: " + exceptionMessage(secondError));
    CHECK_FALSE(firstError);
    CHECK_FALSE(secondError);
    magda::PluginMetadataStore reopened(database);
    CHECK(reopened.query().size() == 1);
}
