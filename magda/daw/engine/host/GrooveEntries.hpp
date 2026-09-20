#pragma once

/**
 * @file GrooveEntries.hpp
 * @brief The app's groove entries in the engine's own shape (#2757).
 *
 * Its own header because EngineHost.hpp reaches none of magda/engine's, which is what
 * keeps the app's end of the boundary free of them.
 */

#include <vector>

#include "EngineHost.hpp"
#include "clip/GrooveTemplate.hpp"

namespace magda::daw::engine_host {

/** @brief @p entries compiled into the set a clip snapshot is compiled against. */
engine::GrooveTemplateSet grooveSetFrom(std::vector<EngineHost::GrooveEntry> entries);

}  // namespace magda::daw::engine_host
