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

/// Which way the two representations need moving, for one device.
enum class InsertSyncDirection {
    ToPlugin,  ///< the project carries the config; the fork is made to match it
    ToModel,   ///< only the restored plugin carries it; the model is filled from it
    Neither,   ///< neither end knows anything, which is an insert nobody configured
};

/**
 * @brief Which direction one insert needs, stated once (#2245).
 *
 * The rule is small and the cost of getting it backwards is not, so it is a
 * function rather than a condition written out at the one call site.
 *
 * A project that carries the config wins, because the model is what the native
 * engine compiles a send op and a return op from, and what the fork plays has
 * to be the same insert.
 *
 * A project saved before the model carried any of this is the other way round,
 * and this is the case that matters: its routing exists only in the ValueTree
 * the plugin just restored, so a model left inactive would make the compiler
 * emit an ordinary Device op for an insert -- for every project anybody already
 * has.
 *
 * A plugin that resolved to nothing is not read back. An insert whose ports
 * this machine does not have has no device at either end, and writing that into
 * the model would erase what the project actually says the next time it is
 * saved.
 */
InsertSyncDirection insertSyncDirectionFor(const magda::InsertConfig& model,
                                           const magda::InsertConfig& plugin);

}  // namespace magda::daw::audio
