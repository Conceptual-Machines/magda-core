#include "PluginMetadataStore.hpp"

#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <utility>

#include "core/AppPaths.hpp"

namespace magda {
namespace {

using sqlite::Statement;
using sqlite::Transaction;

std::mutex initializationMutex;

std::string utf8(const juce::String& text) {
    return text.toStdString();
}

void bindText(sqlite3_stmt* stmt, int index, const juce::String& text) {
    const auto value = utf8(text);
    sqlite3_bind_text(stmt, index, value.c_str(), static_cast<int>(value.size()), SQLITE_TRANSIENT);
}

juce::String columnText(sqlite3_stmt* stmt, int column) {
    const auto* text = sqlite3_column_text(stmt, column);
    return text ? juce::String::fromUTF8(reinterpret_cast<const char*>(text)) : juce::String();
}

void upsertSparsePlugin(sqlite3* db, const juce::String& key, const juce::String& name) {
    Statement stmt(db, R"SQL(
        INSERT INTO plugin (plugin_key, name) VALUES (?, ?)
        ON CONFLICT(plugin_key) DO UPDATE SET
            name = CASE WHEN plugin.name = '' THEN excluded.name ELSE plugin.name END
    )SQL");
    bindText(stmt.get(), 1, key);
    bindText(stmt.get(), 2, name);
    stmt.stepDone();
}

int readUserVersion(sqlite3* db) {
    Statement stmt(db, "PRAGMA user_version");
    if (sqlite3_step(stmt.get()) == SQLITE_ROW)
        return sqlite3_column_int(stmt.get(), 0);
    throw sqlite::Error("read plugin metadata schema version: " + sqlite::lastError(db));
}

void requireReadComplete(sqlite3* db, int result) {
    if (result != SQLITE_DONE)
        throw sqlite::Error(sqlite::lastError(db));
}

}  // namespace

PluginMetadataStore::PluginMetadataStore(const juce::File& databaseFile)
    : PluginMetadataStore(
          databaseFile, {databaseFile.getParentDirectory().getChildFile("PluginList.xml"),
                         databaseFile.getParentDirectory().getChildFile("plugin_favorites.xml"),
                         databaseFile.getParentDirectory().getChildFile("plugin_aliases.xml"),
                         databaseFile.getParentDirectory().getChildFile("plugin_exclusions.txt")}) {
}

PluginMetadataStore::PluginMetadataStore(const juce::File& databaseFile, LegacyFiles legacyFiles)
    : file_(databaseFile), legacyFiles_(std::move(legacyFiles)) {
    (void)file_.getParentDirectory().createDirectory();
    db_.open(utf8(file_.getFullPathName()), "open plugin metadata database");

    try {
        sqlite3_busy_timeout(db_.get(), 60000);
        // Changing the journal mode can return SQLITE_BUSY without invoking the
        // busy handler when two connections open a fresh database together.
        // Cover that step as well as the transactional setup for concurrent
        // first opens within the application.
        const std::lock_guard initializationLock(initializationMutex);
        sqlite::exec(db_.get(), "PRAGMA journal_mode=WAL", "set plugin metadata journal mode");
        sqlite::exec(db_.get(), "PRAGMA synchronous=NORMAL",
                     "set plugin metadata synchronous mode");
        Transaction initialization(db_.get(), sqlite::TransactionMode::Immediate);
        createSchema();
        importLegacyFilesOnce();
        initialization.commit();
    } catch (...) {
        db_.close();
        throw;
    }
}

PluginMetadataStore::~PluginMetadataStore() = default;

PluginMetadataStore PluginMetadataStore::openDefault() {
    return PluginMetadataStore(paths::pluginMetadataFile(),
                               {paths::pluginListFile(), paths::pluginFavoritesFile(),
                                paths::pluginAliasesFile(), paths::pluginExclusionsFile()});
}

PluginMetadataStore& PluginMetadataStore::defaultForCurrentThread() {
    thread_local std::unique_ptr<PluginMetadataStore> store;
    const auto expectedFile = paths::pluginMetadataFile();
    if (!store || store->file() != expectedFile)
        store = std::make_unique<PluginMetadataStore>(openDefault());
    return *store;
}

void PluginMetadataStore::createSchema() {
    const auto version = readUserVersion(db_.get());
    if (version > kPluginMetadataSchemaVersion)
        throw sqlite::Error("plugin metadata database schema is newer than this build");

    // Version 1 is fully described by the idempotent DDL below. Before
    // increasing kPluginMetadataSchemaVersion, add an ordered migration step
    // here for every prior version, then update this assertion. CREATE TABLE
    // IF NOT EXISTS alone cannot upgrade an existing table.
    static_assert(kPluginMetadataSchemaVersion == 1,
                  "Add a plugin metadata schema migration before bumping the version");
    sqlite::exec(db_.get(), R"SQL(
        CREATE TABLE IF NOT EXISTS plugin (
            plugin_key         TEXT PRIMARY KEY,
            name               TEXT NOT NULL DEFAULT '',
            description_xml    TEXT,
            format             TEXT,
            category           TEXT,
            manufacturer       TEXT,
            file_or_identifier TEXT,
            is_instrument      INTEGER NOT NULL DEFAULT 0 CHECK (is_instrument IN (0, 1)),
            favorite           INTEGER NOT NULL DEFAULT 0 CHECK (favorite IN (0, 1)),
            alias              TEXT
        );
        CREATE TABLE IF NOT EXISTS plugin_exclusion (
            path        TEXT PRIMARY KEY,
            reason      TEXT NOT NULL DEFAULT 'unknown',
            excluded_at TEXT NOT NULL DEFAULT ''
        );
        CREATE TABLE IF NOT EXISTS plugin_store_meta (
            key   TEXT PRIMARY KEY,
            value TEXT NOT NULL
        );
        CREATE INDEX IF NOT EXISTS idx_plugin_format_instrument_favorite
            ON plugin (format, is_instrument, favorite);
        CREATE INDEX IF NOT EXISTS idx_plugin_file_or_identifier
            ON plugin (file_or_identifier);
        CREATE INDEX IF NOT EXISTS idx_plugin_alias ON plugin (alias);
    )SQL");
    if (version < kPluginMetadataSchemaVersion) {
        const auto stamp = "PRAGMA user_version = " + std::to_string(kPluginMetadataSchemaVersion);
        sqlite::exec(db_.get(), stamp.c_str());
    }
}

void PluginMetadataStore::importLegacyFilesOnce() {
    Transaction transaction(db_.get(), sqlite::TransactionMode::Immediate);
    Statement migrated(
        db_.get(), "SELECT 1 FROM plugin_store_meta WHERE key='legacy_import_complete' LIMIT 1");
    const auto migrationStatus = sqlite3_step(migrated.get());
    if (migrationStatus == SQLITE_ROW) {
        transaction.commit();
        return;
    }
    requireReadComplete(db_.get(), migrationStatus);

    if (legacyFiles_.pluginList.existsAsFile()) {
        if (auto xml = juce::XmlDocument::parse(legacyFiles_.pluginList)) {
            juce::KnownPluginList plugins;
            plugins.recreateFromXml(*xml);
            saveKnownPlugins(plugins);
        }
    }

    if (auto xml = juce::parseXML(legacyFiles_.favorites)) {
        for (auto* child : xml->getChildIterator()) {
            const auto key = child->getStringAttribute("key");
            if (key.isNotEmpty())
                setFavorite(key, child->getStringAttribute("name"), true);
        }
    }

    if (auto xml = juce::parseXML(legacyFiles_.aliases)) {
        for (auto* child : xml->getChildIterator()) {
            const auto key = child->getStringAttribute("key");
            if (key.isNotEmpty())
                setAlias(key, child->getStringAttribute("alias"));
        }
    }

    saveExclusions(loadExclusionList(legacyFiles_.exclusions));
    sqlite::exec(db_.get(), "INSERT OR IGNORE INTO plugin_store_meta (key, value) "
                            "VALUES ('legacy_import_complete', '1')");
    transaction.commit();
}

void PluginMetadataStore::saveKnownPlugins(const juce::KnownPluginList& plugins) {
    Transaction transaction(db_.get(), sqlite::TransactionMode::Immediate);
    sqlite::exec(db_.get(),
                 "CREATE TEMP TABLE IF NOT EXISTS plugin_seen (plugin_key TEXT PRIMARY KEY)");
    sqlite::exec(db_.get(), "DELETE FROM plugin_seen");

    Statement upsert(db_.get(), R"SQL(
        INSERT INTO plugin (
            plugin_key, name, description_xml, format, category, manufacturer,
            file_or_identifier, is_instrument
        ) VALUES (?, ?, ?, ?, ?, ?, ?, ?)
        ON CONFLICT(plugin_key) DO UPDATE SET
            name=excluded.name,
            description_xml=excluded.description_xml,
            format=excluded.format,
            category=excluded.category,
            manufacturer=excluded.manufacturer,
            file_or_identifier=excluded.file_or_identifier,
            is_instrument=excluded.is_instrument
    )SQL");
    Statement seen(db_.get(), "INSERT OR IGNORE INTO plugin_seen (plugin_key) VALUES (?)");

    for (const auto& description : plugins.getTypes()) {
        const auto key = description.createIdentifierString();
        auto xml = description.createXml();
        const auto xmlText = xml ? xml->toString() : juce::String();

        bindText(upsert.get(), 1, key);
        bindText(upsert.get(), 2, description.name);
        bindText(upsert.get(), 3, xmlText);
        bindText(upsert.get(), 4, description.pluginFormatName);
        bindText(upsert.get(), 5, description.category);
        bindText(upsert.get(), 6, description.manufacturerName);
        bindText(upsert.get(), 7, description.fileOrIdentifier);
        sqlite3_bind_int(upsert.get(), 8, description.isInstrument ? 1 : 0);
        upsert.stepDone();
        upsert.reset();

        bindText(seen.get(), 1, key);
        seen.stepDone();
        seen.reset();
    }

    sqlite::exec(db_.get(), R"SQL(
        DELETE FROM plugin
        WHERE description_xml IS NOT NULL
          AND plugin_key NOT IN (SELECT plugin_key FROM plugin_seen)
          AND favorite = 0
          AND alias IS NULL
    )SQL");
    sqlite::exec(db_.get(), R"SQL(
        UPDATE plugin
        SET description_xml=NULL, format=NULL, category=NULL, manufacturer=NULL,
            file_or_identifier=NULL, is_instrument=0
        WHERE description_xml IS NOT NULL
          AND plugin_key NOT IN (SELECT plugin_key FROM plugin_seen)
    )SQL");
    transaction.commit();
}

void PluginMetadataStore::loadKnownPlugins(juce::KnownPluginList& plugins) const {
    std::vector<juce::PluginDescription> loaded;
    Statement stmt(db_.get(), "SELECT description_xml FROM plugin "
                              "WHERE description_xml IS NOT NULL ORDER BY rowid");
    int result = SQLITE_OK;
    while ((result = sqlite3_step(stmt.get())) == SQLITE_ROW) {
        if (auto xml = juce::parseXML(columnText(stmt.get(), 0))) {
            juce::PluginDescription description;
            if (description.loadFromXml(*xml))
                loaded.push_back(std::move(description));
        }
    }
    requireReadComplete(db_.get(), result);

    plugins.clear();
    for (const auto& description : loaded)
        plugins.addType(description);
}

void PluginMetadataStore::clearKnownPlugins() {
    Transaction transaction(db_.get(), sqlite::TransactionMode::Immediate);
    sqlite::exec(db_.get(), "DELETE FROM plugin WHERE favorite=0 AND alias IS NULL");
    sqlite::exec(db_.get(), "UPDATE plugin SET description_xml=NULL, format=NULL, category=NULL, "
                            "manufacturer=NULL, file_or_identifier=NULL, is_instrument=0");
    transaction.commit();
}

void PluginMetadataStore::setFavorite(const juce::String& key, const juce::String& name,
                                      bool favorite) {
    saveFavorites({{key, name, favorite}});
}

void PluginMetadataStore::saveFavorites(const std::vector<PluginFavoriteUpdate>& updates) {
    Transaction transaction(db_.get(), sqlite::TransactionMode::Immediate);
    Statement stmt(db_.get(), "UPDATE plugin SET favorite=? WHERE plugin_key=?");
    for (const auto& update : updates) {
        upsertSparsePlugin(db_.get(), update.key, update.name);
        sqlite3_bind_int(stmt.get(), 1, update.favorite ? 1 : 0);
        bindText(stmt.get(), 2, update.key);
        stmt.stepDone();
        stmt.reset();
    }
    transaction.commit();
}

juce::StringArray PluginMetadataStore::favoriteKeys() const {
    juce::StringArray result;
    Statement stmt(db_.get(), "SELECT plugin_key FROM plugin WHERE favorite=1 ORDER BY plugin_key");
    int status = SQLITE_OK;
    while ((status = sqlite3_step(stmt.get())) == SQLITE_ROW)
        result.add(columnText(stmt.get(), 0));
    requireReadComplete(db_.get(), status);
    return result;
}

void PluginMetadataStore::setAlias(const juce::String& key, const juce::String& alias) {
    saveAliases({{key, alias}});
}

void PluginMetadataStore::saveAliases(const std::map<juce::String, juce::String>& aliasesToSave) {
    Transaction transaction(db_.get(), sqlite::TransactionMode::Immediate);
    Statement stmt(db_.get(), "UPDATE plugin SET alias=? WHERE plugin_key=?");
    for (const auto& [key, alias] : aliasesToSave) {
        upsertSparsePlugin(db_.get(), key, {});
        if (alias.isEmpty())
            sqlite3_bind_null(stmt.get(), 1);
        else
            bindText(stmt.get(), 1, alias);
        bindText(stmt.get(), 2, key);
        stmt.stepDone();
        stmt.reset();
    }
    transaction.commit();
}

std::map<juce::String, juce::String> PluginMetadataStore::aliases() const {
    std::map<juce::String, juce::String> result;
    Statement stmt(db_.get(), "SELECT plugin_key, alias FROM plugin "
                              "WHERE alias IS NOT NULL ORDER BY plugin_key");
    int status = SQLITE_OK;
    while ((status = sqlite3_step(stmt.get())) == SQLITE_ROW)
        result[columnText(stmt.get(), 0)] = columnText(stmt.get(), 1);
    requireReadComplete(db_.get(), status);
    return result;
}

void PluginMetadataStore::saveExclusions(const std::vector<ExcludedPlugin>& entries) {
    Transaction transaction(db_.get(), sqlite::TransactionMode::Immediate);
    sqlite::exec(db_.get(), "DELETE FROM plugin_exclusion");
    Statement stmt(db_.get(),
                   "INSERT INTO plugin_exclusion (path, reason, excluded_at) VALUES (?, ?, ?)");
    for (const auto& entry : entries) {
        bindText(stmt.get(), 1, entry.path);
        bindText(stmt.get(), 2, entry.reason);
        bindText(stmt.get(), 3, entry.timestamp);
        stmt.stepDone();
        stmt.reset();
    }
    transaction.commit();
}

std::vector<ExcludedPlugin> PluginMetadataStore::loadExclusions() const {
    std::vector<ExcludedPlugin> result;
    Statement stmt(db_.get(),
                   "SELECT path, reason, excluded_at FROM plugin_exclusion ORDER BY rowid");
    int status = SQLITE_OK;
    while ((status = sqlite3_step(stmt.get())) == SQLITE_ROW)
        result.push_back(
            {columnText(stmt.get(), 0), columnText(stmt.get(), 1), columnText(stmt.get(), 2)});
    requireReadComplete(db_.get(), status);
    return result;
}

std::vector<PluginMetadataRecord> PluginMetadataStore::query(
    const PluginMetadataQuery& query) const {
    std::string sql =
        "SELECT plugin_key, name, format, category, manufacturer, file_or_identifier, "
        "COALESCE(alias, ''), is_instrument, favorite FROM plugin WHERE description_xml IS NOT "
        "NULL";
    if (query.favorite)
        sql += " AND favorite=?";
    if (query.instrument)
        sql += " AND is_instrument=?";
    if (query.format.isNotEmpty())
        sql += " AND format=?";
    if (query.excludeExcluded)
        sql += " AND NOT EXISTS (SELECT 1 FROM plugin_exclusion e "
               "WHERE e.path=plugin.file_or_identifier)";
    sql += " ORDER BY name COLLATE NOCASE, plugin_key";

    Statement stmt(db_.get(), sql.c_str());
    int bind = 1;
    if (query.favorite)
        sqlite3_bind_int(stmt.get(), bind++, *query.favorite ? 1 : 0);
    if (query.instrument)
        sqlite3_bind_int(stmt.get(), bind++, *query.instrument ? 1 : 0);
    if (query.format.isNotEmpty())
        bindText(stmt.get(), bind++, query.format);

    std::vector<PluginMetadataRecord> result;
    int status = SQLITE_OK;
    while ((status = sqlite3_step(stmt.get())) == SQLITE_ROW) {
        result.push_back({columnText(stmt.get(), 0), columnText(stmt.get(), 1),
                          columnText(stmt.get(), 2), columnText(stmt.get(), 3),
                          columnText(stmt.get(), 4), columnText(stmt.get(), 5),
                          columnText(stmt.get(), 6), sqlite3_column_int(stmt.get(), 7) != 0,
                          sqlite3_column_int(stmt.get(), 8) != 0});
    }
    requireReadComplete(db_.get(), status);
    return result;
}

}  // namespace magda
