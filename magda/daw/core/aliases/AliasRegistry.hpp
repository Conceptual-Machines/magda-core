#pragma once

#include <juce_core/juce_core.h>

#include <map>
#include <optional>
#include <vector>

#include "../ParameterInfo.hpp"
#include "../SelectionManager.hpp"
#include "Target.hpp"

namespace magda {

// ============================================================================
// Layer priority (highest to lowest)
// ============================================================================

/**
 * @brief Registry layer identifiers, listed highest to lowest priority.
 *
 * When multiple layers define the same alias name, the layer with the
 * lowest numeric value wins.
 */
enum class AliasLayer {
    UserProject = 0,  // Saved inside the .mgd project file
    UserGlobal = 1,   // Saved in user config (~/.config/MAGDA/config.json)
    Curated = 2,      // Shipped default aliases (read-only at runtime)
    AutoGen = 3,      // Auto-generated aliases from plugin scan
};

// ============================================================================
// StoredAlias
// ============================================================================

/**
 * @brief A stored alias entry in the registry.
 *
 * Intentionally keeps devicePath optional: user-global aliases may not
 * have a concrete path (the path is materialised at resolution time by
 * matching the pluginTypeKey against the active chain context).
 */
struct StoredAlias {
    juce::String pluginTypeKey;         // e.g. "serum", "surge_xt"
    int paramIndex = -1;                // Index within the plugin's parameter list
    juce::String paramNameAtSetTime;    // Used for drift detection/recovery
    std::optional<ChainNodePath> path;  // Concrete path (absent for user-global aliases)

    bool isValid() const {
        return pluginTypeKey.isNotEmpty() && paramIndex >= 0;
    }
};

// ============================================================================
// ReverseMatch
// ============================================================================

/**
 * @brief Result of a reverse-lookup in the AliasRegistry.
 *
 * Returned by findByPath() when an alias maps back to a concrete
 * (devicePath, paramIndex) pair.
 */
struct ReverseMatch {
    juce::String canonicalName;  // The alias key (e.g. "@serum.filter_cutoff")
    StoredAlias alias;           // The full stored alias entry
    AliasLayer layer;            // Which layer this alias lives in
};

// ============================================================================
// AliasRegistry listener
// ============================================================================

class AliasRegistry;

class AliasRegistryListener {
  public:
    virtual ~AliasRegistryListener() = default;
    virtual void aliasRegistryChanged(AliasLayer layer) = 0;
};

// ============================================================================
// AliasRegistry
// ============================================================================

/**
 * @brief Layered registry of parameter aliases.
 *
 * Priority order (highest to lowest): UserProject, UserGlobal, Curated, AutoGen.
 *
 * Key design constraints:
 * - lookup() returns a concrete ControlTarget only when a path is already known.
 * - lookupStored() exposes the raw StoredAlias (including path-absent entries)
 *   for use by TargetResolver (PR 3).
 * - Param-index drift: on lookup, if path is present the stored paramIndex is
 *   verified against paramNameAtSetTime. If the name no longer matches the
 *   index, a name-based scan repairs the index. Returns nullopt if repair fails.
 * - Persistence: UserGlobal layer <-> Config "paramAliases" section.
 *               UserProject layer <-> ProjectInfo::paramAliases var.
 */
class AliasRegistry {
  public:
    static AliasRegistry& getInstance();

    // ========================================================================
    // Core lookup API
    // ========================================================================

    /**
     * @brief Look up an alias, returning a concrete ControlTarget when possible.
     *
     * Walks layers highest-to-lowest. Returns a value when:
     *   - A hit is found AND the stored alias has a concrete path.
     *
     * Applies drift fallback: if the device at the stored path is accessible and
     * the parameter name at paramIndex has changed, scans all parameters by name
     * and updates the stored index. Returns nullopt if name resolution also fails.
     *
     * @param canonicalName  Normalized alias name (e.g. "filter_cutoff").
     * @param pluginTypeHint Optional hint for disambiguation (may be empty).
     */
    std::optional<ControlTarget> lookup(const juce::String& canonicalName,
                                        const juce::String& pluginTypeHint = {}) const;

    /**
     * @brief Look up the raw StoredAlias (for TargetResolver in PR 3).
     *
     * Same layer-walking logic as lookup(), but returns the stored alias
     * regardless of whether path is present.
     */
    std::optional<StoredAlias> lookupStored(const juce::String& canonicalName,
                                            const juce::String& pluginTypeHint = {}) const;

    // ========================================================================
    // Mutation API
    // ========================================================================

    void set(AliasLayer layer, const juce::String& canonicalName, const StoredAlias& alias);
    void clear(AliasLayer layer, const juce::String& canonicalName);
    void clearLayer(AliasLayer layer);
    void replaceLayer(AliasLayer layer, const std::map<juce::String, StoredAlias>& entries);

    /**
     * @brief Replace all AutoGen entries for a specific device path.
     *
     * Removes every existing AutoGen entry whose StoredAlias.path matches
     * devicePath, then inserts the new entries. This keeps per-device
     * auto-generated aliases isolated so re-scanning one plugin does not
     * clobber aliases from other plugins.
     */
    void replaceAutoForDevice(const ChainNodePath& devicePath,
                              std::map<juce::String, StoredAlias> newEntries);

    /** Read-only view of a layer (for serialisation helpers). */
    const std::map<juce::String, StoredAlias>& layerEntries(AliasLayer layer) const;

    // ========================================================================
    // Reverse lookup
    // ========================================================================

    /**
     * @brief Find all aliases that resolve to a given (devicePath, paramIndex).
     *
     * Walks all four layers and returns every entry whose StoredAlias.path
     * matches devicePath and whose paramIndex matches.
     *
     * @param devicePath   Concrete device path to search for.
     * @param paramIndex   Parameter index within the device.
     * @param autoGenOnly  When true (default) only the AutoGen layer is searched.
     *                     When false, all layers are searched.
     */
    std::vector<ReverseMatch> findByPath(const ChainNodePath& devicePath, int paramIndex,
                                         bool autoGenOnly = true) const;

    // ========================================================================
    // Persistence
    // ========================================================================

    /**
     * @brief Load UserGlobal layer from Config::paramAliases JSON var.
     *
     * Called by Config::load() after config.json is parsed.
     */
    void loadUserGlobal(const juce::var& json);

    /**
     * @brief Serialize UserGlobal layer to a juce::var for Config::save().
     */
    juce::var saveUserGlobal() const;

    /**
     * @brief Load UserProject layer from a project-level juce::var.
     *
     * Called by ProjectSerializer after the "paramAliases" key is read.
     */
    void loadFromProjectJson(const juce::var& json);

    /**
     * @brief Serialize UserProject layer to a juce::var for project JSON.
     */
    juce::var toProjectJson() const;

    // ========================================================================
    // Listener management
    // ========================================================================

    void addListener(AliasRegistryListener* l);
    void removeListener(AliasRegistryListener* l);

  private:
    AliasRegistry() = default;

    std::map<juce::String, StoredAlias>& layerMap(AliasLayer layer);
    const std::map<juce::String, StoredAlias>& layerMap(AliasLayer layer) const;

    void notifyListeners(AliasLayer layer);

    // ---- drift fallback helper ----
    // Given a device parameter list, try to find 'name' and return its index.
    static std::optional<int> findParamIndexByName(const std::vector<ParameterInfo>& params,
                                                   const juce::String& name);

    // ---- storage ----
    std::map<juce::String, StoredAlias> userProjectLayer_;
    std::map<juce::String, StoredAlias> userGlobalLayer_;
    std::map<juce::String, StoredAlias> curatedLayer_;
    std::map<juce::String, StoredAlias> autoGenLayer_;

    std::vector<AliasRegistryListener*> listeners_;
};

// ============================================================================
// JSON helpers (free functions, used by AliasRegistry and tests)
// ============================================================================

/** Serialize a single StoredAlias to a juce::var object. */
juce::var serializeStoredAlias(const StoredAlias& alias);

/** Deserialize a StoredAlias from a juce::var object. Returns false on failure. */
bool deserializeStoredAlias(const juce::var& v, StoredAlias& out);

}  // namespace magda
