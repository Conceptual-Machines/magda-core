#include "PluginMetadataStore.hpp"

#include <sqlite3.h>

#include <stdexcept>
#include <string>

#include "core/AppPaths.hpp"

namespace magda {
namespace {

class Statement {
  public:
    Statement(sqlite3* db, const char* sql) {
        if (sqlite3_prepare_v2(db, sql, -1, &stmt_, nullptr) != SQLITE_OK)
            throw std::runtime_error(sqlite3_errmsg(db));
    }
    ~Statement() {
        sqlite3_finalize(stmt_);
    }
    sqlite3_stmt* get() const {
        return stmt_;
    }
    void reset() {
        sqlite3_reset(stmt_);
        sqlite3_clear_bindings(stmt_);
    }

  private:
    sqlite3_stmt* stmt_ = nullptr;
};

void exec(sqlite3* db, const char* sql) {
    char* message = nullptr;
    if (sqlite3_exec(db, sql, nullptr, nullptr, &message) != SQLITE_OK) {
        std::string error = message ? message : sqlite3_errmsg(db);
        sqlite3_free(message);
        throw std::runtime_error(error);
    }
}

class Transaction {
  public:
    explicit Transaction(sqlite3* db) : db_(db) {
        exec(db_, "BEGIN IMMEDIATE");
    }
    ~Transaction() {
        if (!committed_)
            sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
    }
    void commit() {
        exec(db_, "COMMIT");
        committed_ = true;
    }

  private:
    sqlite3* db_;
    bool committed_ = false;
};

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

void stepDone(sqlite3* db, sqlite3_stmt* stmt) {
    if (sqlite3_step(stmt) != SQLITE_DONE)
        throw std::runtime_error(sqlite3_errmsg(db));
}

void upsertSparsePlugin(sqlite3* db, const juce::String& key, const juce::String& name) {
    Statement stmt(db, R"SQL(
        INSERT INTO plugin (plugin_key, name) VALUES (?, ?)
        ON CONFLICT(plugin_key) DO UPDATE SET
            name = CASE WHEN plugin.name = '' THEN excluded.name ELSE plugin.name END
    )SQL");
    bindText(stmt.get(), 1, key);
    bindText(stmt.get(), 2, name);
    stepDone(db, stmt.get());
}

}  // namespace

PluginMetadataStore::PluginMetadataStore(const juce::File& databaseFile) : file_(databaseFile) {
    (void)file_.getParentDirectory().createDirectory();
    if (sqlite3_open(utf8(file_.getFullPathName()).c_str(), &db_) != SQLITE_OK) {
        const std::string error = db_ ? sqlite3_errmsg(db_) : "unknown SQLite error";
        sqlite3_close(db_);
        db_ = nullptr;
        throw std::runtime_error("open plugin metadata database: " + error);
    }

    try {
        sqlite3_busy_timeout(db_, 5000);
        exec(db_, "PRAGMA journal_mode=WAL");
        exec(db_, "PRAGMA synchronous=NORMAL");
        createSchema();
        importLegacyFilesOnce();
    } catch (...) {
        sqlite3_close(db_);
        db_ = nullptr;
        throw;
    }
}

PluginMetadataStore::~PluginMetadataStore() {
    sqlite3_close(db_);
}

PluginMetadataStore PluginMetadataStore::openDefault() {
    return PluginMetadataStore(paths::pluginMetadataFile());
}

void PluginMetadataStore::createSchema() {
    exec(db_, R"SQL(
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
}

void PluginMetadataStore::importLegacyFilesOnce() {
    Statement migrated(
        db_, "SELECT 1 FROM plugin_store_meta WHERE key='legacy_import_complete' LIMIT 1");
    if (sqlite3_step(migrated.get()) == SQLITE_ROW)
        return;

    const auto legacyDirectory = file_.getParentDirectory();
    const auto pluginListFile = legacyDirectory.getChildFile("PluginList.xml");
    if (pluginListFile.existsAsFile()) {
        if (auto xml = juce::XmlDocument::parse(pluginListFile)) {
            juce::KnownPluginList plugins;
            plugins.recreateFromXml(*xml);
            saveKnownPlugins(plugins);
        }
    }

    const auto favoritesFile = legacyDirectory.getChildFile("plugin_favorites.xml");
    if (auto xml = juce::parseXML(favoritesFile)) {
        for (auto* child : xml->getChildIterator()) {
            const auto key = child->getStringAttribute("key");
            if (key.isNotEmpty())
                setFavorite(key, child->getStringAttribute("name"), true);
        }
    }

    const auto aliasesFile = legacyDirectory.getChildFile("plugin_aliases.xml");
    if (auto xml = juce::parseXML(aliasesFile)) {
        for (auto* child : xml->getChildIterator()) {
            const auto key = child->getStringAttribute("key");
            if (key.isNotEmpty())
                setAlias(key, child->getStringAttribute("alias"));
        }
    }

    saveExclusions(loadExclusionList(legacyDirectory.getChildFile("plugin_exclusions.txt")));
    exec(db_, "INSERT INTO plugin_store_meta (key, value) "
              "VALUES ('legacy_import_complete', '1')");
}

void PluginMetadataStore::saveKnownPlugins(const juce::KnownPluginList& plugins) {
    Transaction transaction(db_);
    exec(db_, "CREATE TEMP TABLE IF NOT EXISTS plugin_seen (plugin_key TEXT PRIMARY KEY)");
    exec(db_, "DELETE FROM plugin_seen");

    Statement upsert(db_, R"SQL(
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
    Statement seen(db_, "INSERT OR IGNORE INTO plugin_seen (plugin_key) VALUES (?)");

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
        stepDone(db_, upsert.get());
        upsert.reset();

        bindText(seen.get(), 1, key);
        stepDone(db_, seen.get());
        seen.reset();
    }

    exec(db_, R"SQL(
        DELETE FROM plugin
        WHERE description_xml IS NOT NULL
          AND plugin_key NOT IN (SELECT plugin_key FROM plugin_seen)
          AND favorite = 0
          AND alias IS NULL
    )SQL");
    exec(db_, R"SQL(
        UPDATE plugin
        SET description_xml=NULL, format=NULL, category=NULL, manufacturer=NULL,
            file_or_identifier=NULL, is_instrument=0
        WHERE description_xml IS NOT NULL
          AND plugin_key NOT IN (SELECT plugin_key FROM plugin_seen)
    )SQL");
    transaction.commit();
}

void PluginMetadataStore::loadKnownPlugins(juce::KnownPluginList& plugins) const {
    plugins.clear();
    Statement stmt(db_, "SELECT description_xml FROM plugin "
                        "WHERE description_xml IS NOT NULL ORDER BY rowid");
    while (sqlite3_step(stmt.get()) == SQLITE_ROW) {
        if (auto xml = juce::parseXML(columnText(stmt.get(), 0))) {
            juce::PluginDescription description;
            if (description.loadFromXml(*xml))
                plugins.addType(description);
        }
    }
}

void PluginMetadataStore::clearKnownPlugins() {
    Transaction transaction(db_);
    exec(db_, "DELETE FROM plugin WHERE favorite=0 AND alias IS NULL");
    exec(db_, "UPDATE plugin SET description_xml=NULL, format=NULL, category=NULL, "
              "manufacturer=NULL, file_or_identifier=NULL, is_instrument=0");
    transaction.commit();
}

void PluginMetadataStore::setFavorite(const juce::String& key, const juce::String& name,
                                      bool favorite) {
    saveFavorites({{key, name, favorite}});
}

void PluginMetadataStore::saveFavorites(const std::vector<PluginFavoriteUpdate>& updates) {
    Transaction transaction(db_);
    Statement stmt(db_, "UPDATE plugin SET favorite=? WHERE plugin_key=?");
    for (const auto& update : updates) {
        upsertSparsePlugin(db_, update.key, update.name);
        sqlite3_bind_int(stmt.get(), 1, update.favorite ? 1 : 0);
        bindText(stmt.get(), 2, update.key);
        stepDone(db_, stmt.get());
        stmt.reset();
    }
    transaction.commit();
}

juce::StringArray PluginMetadataStore::favoriteKeys() const {
    juce::StringArray result;
    Statement stmt(db_, "SELECT plugin_key FROM plugin WHERE favorite=1 ORDER BY plugin_key");
    while (sqlite3_step(stmt.get()) == SQLITE_ROW)
        result.add(columnText(stmt.get(), 0));
    return result;
}

void PluginMetadataStore::setAlias(const juce::String& key, const juce::String& alias) {
    saveAliases({{key, alias}});
}

void PluginMetadataStore::saveAliases(const std::map<juce::String, juce::String>& aliasesToSave) {
    Transaction transaction(db_);
    Statement stmt(db_, "UPDATE plugin SET alias=? WHERE plugin_key=?");
    for (const auto& [key, alias] : aliasesToSave) {
        upsertSparsePlugin(db_, key, {});
        if (alias.isEmpty())
            sqlite3_bind_null(stmt.get(), 1);
        else
            bindText(stmt.get(), 1, alias);
        bindText(stmt.get(), 2, key);
        stepDone(db_, stmt.get());
        stmt.reset();
    }
    transaction.commit();
}

std::map<juce::String, juce::String> PluginMetadataStore::aliases() const {
    std::map<juce::String, juce::String> result;
    Statement stmt(db_, "SELECT plugin_key, alias FROM plugin "
                        "WHERE alias IS NOT NULL ORDER BY plugin_key");
    while (sqlite3_step(stmt.get()) == SQLITE_ROW)
        result[columnText(stmt.get(), 0)] = columnText(stmt.get(), 1);
    return result;
}

void PluginMetadataStore::saveExclusions(const std::vector<ExcludedPlugin>& entries) {
    Transaction transaction(db_);
    exec(db_, "DELETE FROM plugin_exclusion");
    Statement stmt(db_,
                   "INSERT INTO plugin_exclusion (path, reason, excluded_at) VALUES (?, ?, ?)");
    for (const auto& entry : entries) {
        bindText(stmt.get(), 1, entry.path);
        bindText(stmt.get(), 2, entry.reason);
        bindText(stmt.get(), 3, entry.timestamp);
        stepDone(db_, stmt.get());
        stmt.reset();
    }
    transaction.commit();
}

std::vector<ExcludedPlugin> PluginMetadataStore::loadExclusions() const {
    std::vector<ExcludedPlugin> result;
    Statement stmt(db_, "SELECT path, reason, excluded_at FROM plugin_exclusion ORDER BY rowid");
    while (sqlite3_step(stmt.get()) == SQLITE_ROW)
        result.push_back(
            {columnText(stmt.get(), 0), columnText(stmt.get(), 1), columnText(stmt.get(), 2)});
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

    Statement stmt(db_, sql.c_str());
    int bind = 1;
    if (query.favorite)
        sqlite3_bind_int(stmt.get(), bind++, *query.favorite ? 1 : 0);
    if (query.instrument)
        sqlite3_bind_int(stmt.get(), bind++, *query.instrument ? 1 : 0);
    if (query.format.isNotEmpty())
        bindText(stmt.get(), bind++, query.format);

    std::vector<PluginMetadataRecord> result;
    while (sqlite3_step(stmt.get()) == SQLITE_ROW) {
        result.push_back({columnText(stmt.get(), 0), columnText(stmt.get(), 1),
                          columnText(stmt.get(), 2), columnText(stmt.get(), 3),
                          columnText(stmt.get(), 4), columnText(stmt.get(), 5),
                          columnText(stmt.get(), 6), sqlite3_column_int(stmt.get(), 7) != 0,
                          sqlite3_column_int(stmt.get(), 8) != 0});
    }
    return result;
}

}  // namespace magda
