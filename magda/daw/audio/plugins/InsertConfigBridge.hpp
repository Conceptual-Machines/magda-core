#pragma once

#include <tracktion_engine/tracktion_engine.h>

#include "core/DeviceInfo.hpp"

namespace magda::daw::audio {

namespace te = tracktion::engine;

/**
 * @file InsertConfigBridge.hpp
 * @brief What a hardware insert is, moved between the model and the fork
 *        (#2245).
 *
 * The incumbent keeps an insert's configuration inside a te::InsertPlugin's
 * ValueTree, which made "what does this insert send to" a question only the
 * fork could answer. The native engine compiles an insert into a send op and a
 * return op, and it compiles from the model: so the model carries the same
 * facts (magda::InsertConfig), and these two functions are the only place the
 * two representations meet.
 *
 * One direction each, and both are needed while the two engines run side by
 * side. Reading is what makes a project saved by any released build produce a
 * model the native engine can compile; writing is what keeps the fork playing
 * what the model says after a load or an edit.
 *
 * The device types are derived by the fork from which ends name a device
 * (te::InsertPlugin::updateDeviceTypes), so reading takes them from there
 * rather than guessing, and writing sets the names and lets the fork derive
 * them again. That is one authority for the derivation rather than two that
 * agree until they do not.
 */

/// What @p plugin is configured as, as model values.
magda::InsertConfig insertConfigOf(const te::InsertPlugin& plugin);

/// Point @p plugin at what @p config names. Message thread: it writes the
/// plugin's ValueTree.
void applyInsertConfig(te::InsertPlugin& plugin, const magda::InsertConfig& config);

/// The model's endpoint for one of the fork's device types, and back.
magda::InsertConfig::Endpoint endpointOf(te::InsertPlugin::DeviceType type);

}  // namespace magda::daw::audio
