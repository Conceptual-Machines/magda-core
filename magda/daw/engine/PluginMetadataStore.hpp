#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_core/juce_core.h>

#include <map>
#include <optional>
#include <vector>

#include "PluginExclusions.hpp"
#include "core/SqliteUtils.hpp"

namespace magda {

inline constexpr int kPluginMetadataSchemaVersion = 1;

struct PluginMetadataRecord {
    juce::String key;
    juce::String name;
    juce::String format;
    juce::String category;
    juce::String manufacturer;
    juce::String fileOrIdentifier;
    juce::String alias;
    bool isInstrument = false;
    bool isFavorite = false;
};

struct PluginMetadataQuery {
    std::optional<bool> favorite;
    std::optional<bool> instrument;
    juce::String format;
    bool excludeExcluded = false;
};

struct PluginFavoriteUpdate {
    juce::String key;
    juce::String name;
    bool favorite = false;
};

/**
 * Transactional SQLite store for scanned plugins and user plugin metadata.
 *
 * The first open imports PluginList.xml, plugin_favorites.xml,
 * plugin_aliases.xml, and plugin_exclusions.txt. Those files are retained as
 * rollback-friendly migration inputs but are never written again.
 */
class PluginMetadataStore {
  public:
    explicit PluginMetadataStore(const juce::File& databaseFile);
    ~PluginMetadataStore();

    PluginMetadataStore(const PluginMetadataStore&) = delete;
    PluginMetadataStore& operator=(const PluginMetadataStore&) = delete;
    PluginMetadataStore(PluginMetadataStore&&) noexcept = default;
    PluginMetadataStore& operator=(PluginMetadataStore&&) noexcept = default;

    static PluginMetadataStore openDefault();
    static PluginMetadataStore& defaultForCurrentThread();

    void saveKnownPlugins(const juce::KnownPluginList& plugins);
    void loadKnownPlugins(juce::KnownPluginList& plugins) const;
    void clearKnownPlugins();

    void setFavorite(const juce::String& key, const juce::String& name, bool favorite);
    void saveFavorites(const std::vector<PluginFavoriteUpdate>& updates);
    [[nodiscard]] juce::StringArray favoriteKeys() const;

    void setAlias(const juce::String& key, const juce::String& alias);
    void saveAliases(const std::map<juce::String, juce::String>& aliases);
    [[nodiscard]] std::map<juce::String, juce::String> aliases() const;

    void saveExclusions(const std::vector<ExcludedPlugin>& entries);
    [[nodiscard]] std::vector<ExcludedPlugin> loadExclusions() const;

    [[nodiscard]] std::vector<PluginMetadataRecord> query(
        const PluginMetadataQuery& query = {}) const;

    [[nodiscard]] const juce::File& file() const noexcept {
        return file_;
    }

  private:
    struct LegacyFiles {
        juce::File pluginList;
        juce::File favorites;
        juce::File aliases;
        juce::File exclusions;
    };

    PluginMetadataStore(const juce::File& databaseFile, LegacyFiles legacyFiles);
    void createSchema();
    void importLegacyFilesOnce();

    sqlite::Connection db_;
    juce::File file_;
    LegacyFiles legacyFiles_;
};

}  // namespace magda
