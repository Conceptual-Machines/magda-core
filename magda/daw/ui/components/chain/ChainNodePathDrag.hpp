#pragma once

#include <juce_core/juce_core.h>

#include <optional>
#include <vector>

#include "../../../core/ChainNodePath.hpp"

namespace magda::daw::ui {

/**
 * @brief Chain-node drag payloads, encoded with the canonical path serializer.
 *
 * A chain-element drag description carries one path per dragged node plus an
 * unsuffixed copy of the first. These helpers own that layout so the drop
 * handlers do not each re-derive it: previously five near-identical decoders
 * across three files parsed the path out of flat `trackId`/`stepTypes`/`stepIds`
 * keys, reading missing properties as 0 and turning unparseable step ids into
 * `Rack(0)`.
 *
 * `index < 0` addresses the unsuffixed copy.
 */
void writeChainNodePathToDragInfo(juce::DynamicObject& obj, const ChainNodePath& path,
                                  int index = -1);

/**
 * @brief Decode one path from a drag description.
 *
 * Returns nullopt when the entry is absent, malformed, or addresses a section
 * the drop handlers cannot accept.
 */
std::optional<ChainNodePath> readChainNodePathFromDragInfo(const juce::DynamicObject& obj,
                                                           int index = -1);

/**
 * @brief Decode every path a drag description carries.
 *
 * Reads `pathCount` entries, falling back to the unsuffixed copy when none
 * decode. Malformed entries are dropped rather than failing the whole drag —
 * a partial selection still has a sensible drop, and each surviving path has
 * been validated.
 */
std::vector<ChainNodePath> readChainNodePathsFromDragInfo(const juce::DynamicObject& obj);

}  // namespace magda::daw::ui
