#pragma once

#include <juce_core/juce_core.h>

namespace magda {

// ============================================================================
// CuratedAliasLoader
// ============================================================================

/**
 * @brief Loads curated per-plugin alias dictionaries into AliasRegistry.
 *
 * Curated aliases ship as embedded JSON inside the binary (via juce BinaryData).
 * They populate AliasLayer::Curated at startup and are read-only at runtime.
 *
 * Index JSON shape (curated_index.json):
 * @code
 * {
 *   "version": 1,
 *   "plugins": [
 *     { "match": { "name_equals": "4-Band Equaliser", "format": "Internal" },
 *       "key": "eq", "file": "curated_eq.json" }
 *   ]
 * }
 * @endcode
 *
 * Per-plugin JSON shape (curated_eq.json):
 * @code
 * {
 *   "version": 1,
 *   "pluginKey": "eq",
 *   "aliases": { "low_shelf_freq": { "paramIndex": 0 } },
 *   "aliasesByName": { "low_shelf_freq": ["Low-shelf freq", "Low-pass freq"] }
 * }
 * @endcode
 *
 * StoredAlias.paramNameAtSetTime is set to the first element of aliasesByName
 * for each alias so that drift-fallback works when plugin versions change param
 * order.
 *
 * StoredAlias.path is always absent (nullopt) for curated aliases -- the path
 * is materialised at resolution time against the current chain context.
 */
class CuratedAliasLoader {
  public:
    /**
     * @brief Load curated aliases from embedded BinaryData into AliasRegistry.
     *
     * Parses curated_index.json, then loads each referenced per-plugin file.
     * Replaces the entire Curated layer atomically.
     *
     * Call once at startup, after Config::load().
     */
    static void loadFromBinary();

    /**
     * @brief Parse curated aliases from an in-memory JSON string.
     *
     * The indexJson string must be the full contents of curated_index.json.
     * fileResolver is called with a filename and should return the JSON
     * content for that file (or an empty string on failure).
     *
     * This overload is exposed for testing: tests can inject fixture JSON
     * without requiring BinaryData.
     */
    static void loadFromString(
        const juce::String& indexJson,
        const std::function<juce::String(const juce::String& filename)>& fileResolver);
};

}  // namespace magda
