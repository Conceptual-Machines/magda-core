#pragma once

#include <unordered_map>

#include "PluginBrowserContent.hpp"
#include "core/PluginAlias.hpp"
#include "engine/PluginMetadataStore.hpp"

namespace magda::daw::ui {

/**
 * Applies persisted metadata per key while preserving every description in
 * the current preferred list. Missing records fall back to live description
 * fields; colliding description keys intentionally share one metadata record.
 */
inline std::vector<PluginBrowserInfo> mergeExternalPluginMetadata(
    const juce::Array<juce::PluginDescription>& descriptions,
    const std::vector<magda::PluginMetadataRecord>& records) {
    std::unordered_map<juce::String, const magda::PluginMetadataRecord*> recordsByKey;
    recordsByKey.reserve(records.size());
    for (const auto& record : records)
        recordsByKey[record.key] = &record;

    std::vector<PluginBrowserInfo> merged;
    merged.reserve(static_cast<std::size_t>(descriptions.size()));
    for (const auto& description : descriptions) {
        PluginBrowserInfo plugin;
        const auto record = recordsByKey.find(description.createIdentifierString());
        if (record == recordsByKey.end()) {
            plugin.name = description.name;
            plugin.manufacturer = description.manufacturerName;
            plugin.category = description.isInstrument ? "Instrument" : "Effect";
            plugin.format = description.pluginFormatName;
            plugin.subcategory = description.category.isNotEmpty() ? description.category : "Other";
            plugin.alias = magda::pluginNameToAlias(description.name);
            plugin.isExternal = true;
            plugin.uniqueId = description.createIdentifierString();
            plugin.fileOrIdentifier = description.fileOrIdentifier;
        } else {
            plugin.name = record->second->name;
            plugin.manufacturer = record->second->manufacturer;
            plugin.category = record->second->isInstrument ? "Instrument" : "Effect";
            plugin.format = record->second->format;
            plugin.subcategory =
                record->second->category.isNotEmpty() ? record->second->category : "Other";
            plugin.alias = record->second->alias.isNotEmpty()
                               ? record->second->alias
                               : magda::pluginNameToAlias(record->second->name);
            plugin.isFavorite = record->second->isFavorite;
            plugin.isExternal = true;
            plugin.uniqueId = record->second->key;
            plugin.fileOrIdentifier = record->second->fileOrIdentifier;
        }
        merged.push_back(std::move(plugin));
    }
    return merged;
}

}  // namespace magda::daw::ui
