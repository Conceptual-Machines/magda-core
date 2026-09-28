#pragma once

#include <map>

#include "TypeIds.hpp"

namespace magda {

/**
 * @brief What one re-keyed chain subtree's ids were moved to.
 *
 * Filled by `TrackManager::reassignChainElementIds()`, and read by everything
 * that has to follow the subtree's internal references afterwards: macro and
 * mod link targets, automation lane targets, and the addresses stored inside
 * the subtree itself.
 *
 * `racks` carries two kinds of key. An allocated rack's id is one. The other is
 * the synthetic negative id a pad rack holds (`padRackIdFor()`), which moves
 * with the DeviceId it is derived from; a link naming a pad rack is remapped
 * through this map like any other.
 */
struct ChainIdRemap {
    std::map<DeviceId, DeviceId> devices;
    std::map<RackId, RackId> racks;
    std::map<ChainId, ChainId> chains;
};

}  // namespace magda
