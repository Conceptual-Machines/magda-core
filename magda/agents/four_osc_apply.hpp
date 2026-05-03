#pragma once

#include <juce_core/juce_core.h>

#include "core/ChainNodePath.hpp"
#include "four_osc_agent.hpp"

namespace magda {

/**
 * @brief Apply a parsed FourOsc preset to a specific device path.
 *
 * Writes parameter values, wave shapes, filter type, voice mode, and FX
 * gates onto the resolved device. Best-effort: silently skips params
 * whose name doesn't match a device parameter, but reports counts in
 * the returned status string.
 *
 * Returns a one-line status (e.g. "applied 24 param(s) + 4 wave(s) + …")
 * suitable for display in chat or panel output. If `path` doesn't
 * resolve to a 4OSC device, returns an error string starting with "(".
 */
juce::String applyFourOscPresetToPath(const FourOscAgent::Preset& preset,
                                      const ChainNodePath& path);

}  // namespace magda
