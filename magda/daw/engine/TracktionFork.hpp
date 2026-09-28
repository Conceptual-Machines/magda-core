#pragma once

#include <tracktion_engine/tracktion_engine.h>

#include "../core/ChainNodePath.hpp"
#include "../core/ClipTypes.hpp"

namespace magda {
struct ControlTarget;
}

/**
 * @file TracktionFork.hpp
 * @brief The fork's own objects, for the features only it renders (#2760).
 *
 * 4OSC, the external insert panel, the Drum Grid's plugin, a parameter's
 * modifier-free base value and transient detection are still Tracktion's. Every
 * lookup here answers null under any other engine, and the file goes with the
 * fork (#2557).
 */
namespace magda::tracktion_fork {

/// Whether the fork is the engine rendering.
bool isRendering();

/// The fork's plugin at @p devicePath, or null.
tracktion::engine::Plugin::Ptr pluginAt(const ChainNodePath& devicePath);

/// The fork's parameter that @p target controls, or null.
tracktion::engine::AutomatableParameter* parameterFor(const ControlTarget& target);

/// Whether @p clipId's transients are cached, starting their detection when not.
bool detectTransients(ClipId clipId);

/// Detect @p clipId's transients again at @p sensitivity, once the value settles.
void setTransientSensitivity(ClipId clipId, float sensitivity);

}  // namespace magda::tracktion_fork
