#include <algorithm>

#include "DrumGridPads.hpp"
#include "RackInfo.hpp"
#include "SelectionManager.hpp"
#include "TrackManager.hpp"

namespace magda {

// ============================================================================
// Pad-per-chain devices (#2207)
//
// A Drum Grid's pads are a rack it owns, so the chain commands already on
// TrackManager do most of the work: `padChainPath()` turns a pad into an
// ordinary chain path, and adding, removing, reordering, muting, soloing and
// fading a pad go through the same calls a rack chain does. What is left here
// is the handful of things only a pad has: it is found by the note it answers
// to, it is made on demand when something is dropped on it, and two of them can
// trade places.
// ============================================================================

ChainNodePath TrackManager::padChainPath(const ChainNodePath& gridPath, ChainId padChainId) {
    ChainNodePath path;
    path.trackId = gridPath.trackId;
    path.steps = gridPath.steps;

    // The rack step names the pads, so the device step it hangs off is dropped:
    // a top-level Drum Grid has no step at all (its id lives in
    // `topLevelDeviceId`), and a nested one is reached through its chain.
    if (!path.steps.empty() && path.steps.back().type == ChainStepType::Device)
        path.steps.pop_back();

    path.steps.push_back({ChainStepType::Rack, padRackIdFor(gridPath.getDeviceId())});
    path.steps.push_back({ChainStepType::Chain, padChainId});
    return path;
}

RackInfo* TrackManager::getPads(const ChainNodePath& gridPath) {
    auto* device = getDeviceInChainByPath(gridPath);
    return device != nullptr ? device->pads.get() : nullptr;
}

const RackInfo* TrackManager::getPads(const ChainNodePath& gridPath) const {
    return const_cast<TrackManager*>(this)->getPads(gridPath);
}

const ChainInfo* TrackManager::getPad(const ChainNodePath& gridPath, int padIndex) const {
    const auto* pads = getPads(gridPath);
    return pads != nullptr ? findPadChain(*pads, padIndex) : nullptr;
}

ChainId TrackManager::ensurePad(const ChainNodePath& gridPath, int padIndex) {
    if (padIndex < 0 || padIndex >= kPadCount)
        return INVALID_CHAIN_ID;

    auto* device = getDeviceInChainByPath(gridPath);
    if (device == nullptr || !isPadRackDevice(device->pluginId))
        return INVALID_CHAIN_ID;

    auto& pads = ensurePads(*device);
    const bool existed = findPadChain(pads, padIndex) != nullptr;
    const auto id = ensurePadChain(pads, padIndex).id;

    if (!existed)
        notifyTrackDevicesChanged(gridPath.trackId);

    return id;
}

DeviceId TrackManager::setPadDevice(const ChainNodePath& gridPath, int padIndex,
                                    const DeviceInfo& device) {
    const auto padChainId = ensurePad(gridPath, padIndex);
    if (padChainId == INVALID_CHAIN_ID)
        return INVALID_DEVICE_ID;

    const auto chainPath = padChainPath(gridPath, padChainId);

    auto* pad = getChainByPath(chainPath);
    if (pad == nullptr)
        return INVALID_DEVICE_ID;

    // Dropping an instrument on a pad replaces the pad, effects and all: that
    // is what the pad's slot means. Selections pointing into what is going away
    // are cleared first, the same as any other device removal.
    for (const auto& element : pad->elements)
        if (magda::isDevice(element))
            SelectionManager::getInstance().clearSelectionForDeletedChainNode(
                chainPath.withDevice(magda::getDevice(element).id));
    pad->elements.clear();

    // Named after what is on it, which is what the grid shows on the pad.
    pad->name = device.name;

    return addDeviceToChainByPath(chainPath, device);
}

void TrackManager::clearPad(const ChainNodePath& gridPath, int padIndex) {
    auto* pads = getPads(gridPath);
    if (pads == nullptr)
        return;

    auto* pad = findPadChain(*pads, padIndex);
    if (pad == nullptr)
        return;

    // Only a pad that is this pad. A chain answering to a range wider than one
    // note is shared with its neighbours, and clearing it from one of them
    // would take the others' sound away too.
    const auto note = padNoteFor(padIndex);
    if (pad->lowNote != note || pad->highNote != note)
        return;

    const auto chainPath = padChainPath(gridPath, pad->id);
    for (const auto& element : pad->elements)
        if (magda::isDevice(element))
            SelectionManager::getInstance().clearSelectionForDeletedChainNode(
                chainPath.withDevice(magda::getDevice(element).id));
    SelectionManager::getInstance().clearSelectionForDeletedChainNode(chainPath);

    std::erase_if(pads->chains, [id = pad->id](const ChainInfo& chain) { return chain.id == id; });
    notifyTrackDevicesChanged(gridPath.trackId);
}

void TrackManager::swapPads(const ChainNodePath& gridPath, int padA, int padB) {
    if (padA == padB || padA < 0 || padA >= kPadCount || padB < 0 || padB >= kPadCount)
        return;

    auto* pads = getPads(gridPath);
    if (pads == nullptr)
        return;

    auto* chainA = findPadChain(*pads, padA);
    auto* chainB = findPadChain(*pads, padB);
    if (chainA == nullptr && chainB == nullptr)
        return;

    // The chains stay where they are and their note ranges trade: the pad is
    // the note, so moving a sound to another pad is retuning the chain that
    // makes it. Chain ids are untouched, so a macro, a mod or an automation
    // lane naming one still names the same devices afterwards.
    const auto retune = [](ChainInfo& chain, int padIndex) {
        const auto note = padNoteFor(padIndex);
        chain.lowNote = note;
        chain.highNote = note;
        chain.rootNote = note;
    };

    if (chainA != nullptr && chainB != nullptr) {
        std::swap(chainA->lowNote, chainB->lowNote);
        std::swap(chainA->highNote, chainB->highNote);
        std::swap(chainA->rootNote, chainB->rootNote);
        std::swap(chainA->name, chainB->name);
    } else if (chainA != nullptr) {
        retune(*chainA, padB);
    } else {
        retune(*chainB, padA);
    }

    notifyTrackDevicesChanged(gridPath.trackId);
}

}  // namespace magda
