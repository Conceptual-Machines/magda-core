#pragma once

#include <type_traits>
#include <vector>

#include "ChainNodePath.hpp"
#include "RackInfo.hpp"

/**
 * @file
 * @brief The one descent through a track's chain tree (#2204).
 *
 * "The devices in this chain" was implemented separately in a dozen places
 * under a dozen names, so adding a container to the model meant finding each
 * one by hand, with no compiler error and no test that caught a miss. Every
 * finding on #2200 was a walk that had not been updated, and each was a silent
 * wrong render rather than a failure.
 *
 * Two things come with the descent, and they are the reason it is worth
 * sharing rather than just repeating:
 *
 * **The path.** A top-level device is addressed by `topLevelDevice()`, which
 * puts the id in its own field rather than in a step, so a walk that reaches
 * for `withDevice()` at the top level builds a path that names the same device
 * and does not compare equal to the one everything else stored. Six sites
 * carried the `isTrackLevel` ternary for this by hand.
 *
 * **The pads.** A pad-per-chain device owns chains of its own (#2207), so a
 * device is not always a leaf. Whether to descend into them is a real question
 * with different right answers -- a plan expands them, gain staging does not --
 * so it is asked explicitly rather than defaulted, and the answer is visible at
 * the call site.
 *
 * Not everything should be here. `PlanCompiler::emitElements()` and
 * `ParamTableCompiler::walkElements()` thread state through the descent -- a
 * signal, a nesting stack -- and are compilations rather than traversals. They
 * opt out, and say so where they do.
 */

namespace magda::chain_walk {

/// Whether a device's own pad chains are part of the walk.
///
/// `Skip` is right whenever the thing being collected is something a pad device
/// cannot own or cannot answer for; `Enter` whenever a pad device counts as one
/// of the user's devices. Neither is a safe default, hence no default.
enum class Pads { Skip, Enter };

/// The address of a device directly inside @p parentPath.
///
/// The whole reason this is a function: at the top level a device's id lives in
/// `topLevelDeviceId` rather than in a step, and `withDevice()` would build the
/// other spelling.
inline ChainNodePath deviceIn(const ChainNodePath& parentPath, DeviceId deviceId) {
    return parentPath.isTrackLevel ? ChainNodePath::topLevelDevice(parentPath.trackId, deviceId)
                                   : parentPath.withDevice(deviceId);
}

/// The address of a rack directly inside @p parentPath.
///
/// `withRack()` already answers this correctly at every depth, because
/// extending clears the track-level flag (#2230). Here for symmetry with
/// `deviceIn()`, so a caller never has to know that only one of the two needed
/// the special case.
inline ChainNodePath rackIn(const ChainNodePath& parentPath, RackId rackId) {
    return parentPath.withRack(rackId);
}

/// Whether a rack's contents are part of the walk.
///
/// Returned by a rack visitor to prune: the parameter table stops at a rack
/// already open above it, because a second instance claims every address the
/// first one has.
enum class Descend { Into, Skip };

/// The id a device's macros, modifiers and links are addressed under.
///
/// The innermost rack it stands in, or the Drum Grid whose pad holds it -- a
/// stored link to a pad device names the grid, not the synthetic pad rack. The
/// plan, the parameter table and the runtime store each derived this from their
/// own walk; deriving it once is what makes a disagreement between them
/// impossible rather than untested (#2204).
inline RackId owningRackId(const ChainNodePath& path) {
    for (auto it = path.steps.rbegin(); it != path.steps.rend(); ++it)
        if (it->type == ChainStepType::Rack || it->type == ChainStepType::PadRack)
            return it->id;
    return INVALID_RACK_ID;
}

/// The one descent. Devices and racks in a single pre-order pass, each with its
/// address, depth-first in element order.
///
/// @p onDevice is called with `(device, devicePath)`; returning `bool` prunes
/// the whole walk on `false`, and a `void` visitor always continues.
/// @p onRack is called with `(rack, rackPath)` before the rack's chains and
/// returns `Descend`, so a visitor can skip a subtree without stopping.
/// @p onRackExit is called with the same arguments once the rack's chains are
/// done, for a visitor keeping a stack of what is open around it.
///
/// The exit is run from a destructor rather than after the loop, so a walk that
/// prunes on the way out still unwinds: `RackNesting` refuses a rack that is
/// already open, and one left open by an early return would refuse every later
/// sibling that reuses the id.
///
/// A device's pads come after the device and before whatever stands next to it,
/// the way a rack's chains do.
template <typename Elements, typename OnDevice, typename OnRack, typename OnRackExit>
bool forEachNode(Elements& elements, const ChainNodePath& parentPath, Pads pads,
                 OnDevice&& onDevice, OnRack&& onRack, OnRackExit&& onRackExit) {
    for (auto& element : elements) {
        if (isDevice(element)) {
            auto& device = getDevice(element);
            const auto devicePath = deviceIn(parentPath, device.id);

            if constexpr (std::is_same_v<decltype(onDevice(device, devicePath)), bool>) {
                if (!onDevice(device, devicePath))
                    return false;
            } else {
                onDevice(device, devicePath);
            }

            if (pads == Pads::Enter && device.pads) {
                for (auto& pad : device.pads->chains) {
                    const auto padPath =
                        ChainNodePath::padChain(parentPath.trackId, device.id, pad.id);
                    if (!forEachNode(pad.elements, padPath, pads, onDevice, onRack))
                        return false;
                }
            }
            continue;
        }

        if (!isRack(element))
            continue;

        auto& rack = getRack(element);
        const auto rackPath = rackIn(parentPath, rack.id);
        if (onRack(rack, rackPath) == Descend::Skip)
            continue;

        struct Exit {
            OnRackExit& run;
            decltype(rack)& rack;
            const ChainNodePath& path;
            ~Exit() {
                run(rack, path);
            }
        } exit{onRackExit, rack, rackPath};

        for (auto& chain : rack.chains)
            if (!forEachNode(chain.elements, rackPath.withChain(chain.id), pads, onDevice, onRack,
                             onRackExit))
                return false;
    }

    return true;
}

/// The three-argument form, for a walk with nothing to unwind.
template <typename Elements, typename OnDevice, typename OnRack>
bool forEachNode(Elements& elements, const ChainNodePath& parentPath, Pads pads,
                 OnDevice&& onDevice, OnRack&& onRack) {
    return forEachNode(elements, parentPath, pads, onDevice, onRack,
                       [](auto&, const ChainNodePath&) {});
}

/// Every device, with its address. The common face of the walk.
template <typename Elements, typename Visit>
bool forEachDevice(Elements& elements, const ChainNodePath& parentPath, Pads pads, Visit&& visit) {
    return forEachNode(elements, parentPath, pads, visit,
                       [](auto&, const ChainNodePath&) { return Descend::Into; });
}

/// Every rack, outermost first, with its address.
template <typename Elements, typename Visit>
void forEachRack(Elements& elements, const ChainNodePath& parentPath, Pads pads, Visit&& visit) {
    forEachNode(
        elements, parentPath, pads, [](auto&, const ChainNodePath&) {},
        [&visit](auto& rack, const ChainNodePath& rackPath) {
            visit(rack, rackPath);
            return Descend::Into;
        });
}

/// Every chain with its address: a rack's chains, and a pad-per-chain device's
/// pads when asked for.
template <typename Elements, typename Visit>
void forEachChain(Elements& elements, const ChainNodePath& parentPath, Pads pads, Visit&& visit) {
    for (auto& element : elements) {
        if (isDevice(element)) {
            if (pads == Pads::Enter) {
                auto& device = getDevice(element);
                if (device.pads) {
                    for (auto& pad : device.pads->chains) {
                        const auto padPath =
                            ChainNodePath::padChain(parentPath.trackId, device.id, pad.id);
                        visit(pad, padPath);
                        forEachChain(pad.elements, padPath, pads, visit);
                    }
                }
            }
            continue;
        }

        if (!isRack(element))
            continue;

        auto& rack = getRack(element);
        const auto rackPath = rackIn(parentPath, rack.id);
        for (auto& chain : rack.chains) {
            const auto chainPath = rackPath.withChain(chain.id);
            visit(chain, chainPath);
            forEachChain(chain.elements, chainPath, pads, visit);
        }
    }
}

}  // namespace magda::chain_walk
