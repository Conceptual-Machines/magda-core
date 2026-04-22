#pragma once

#include <optional>
#include <vector>

#include "AliasRegistry.hpp"

namespace magda {

// ============================================================================
// Free functions for alias reverse lookup
// ============================================================================

/**
 * @brief Find all aliases that map to a specific (devicePath, paramIndex) pair.
 *
 * Delegates to AliasRegistry::findByPath().
 *
 * @param registry     The registry to search.
 * @param devicePath   Concrete device path.
 * @param paramIndex   Parameter index within the device.
 * @param autoGenOnly  When true (default), only the AutoGen layer is searched.
 *                     When false, all layers are searched.
 * @return Vector of ReverseMatch entries (may be empty).
 */
std::vector<ReverseMatch> findAliasesByPath(const AliasRegistry& registry,
                                            const ChainNodePath& devicePath, int paramIndex,
                                            bool autoGenOnly = true);

/**
 * @brief Return the canonical alias name for a (devicePath, paramIndex) pair.
 *
 * Prefers the highest-priority layer hit. If autoGenOnly is true, only the
 * AutoGen layer is searched.
 *
 * @return The canonical alias name, or nullopt when no alias is found.
 */
std::optional<juce::String> bestAliasForPath(const AliasRegistry& registry,
                                             const ChainNodePath& devicePath, int paramIndex,
                                             bool autoGenOnly = true);

}  // namespace magda
