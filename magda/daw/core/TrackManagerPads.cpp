#include <algorithm>
#include <cmath>
#include <map>

#include "DrumGridPads.hpp"
#include "RackInfo.hpp"
#include "SelectionManager.hpp"
#include "TrackManager.hpp"

namespace magda {

// ============================================================================
// Pad-per-chain devices (#2207)
//
// A Drum Grid's pads are a rack the device owns, and every operation here
// reaches them through that device: `getPads()` resolves the grid by its own
// path and takes `DeviceInfo::pads`.
//
// Never through a Rack step. A pad's engine address spells the rack component
// with the grid's own DeviceId (`padChainPath`), and rack ids and device ids
// come out of counters that both start at 1, so `Rack(1)` is as much rack 1 as
// it is Drum Grid 1's pads. Resolving a pad edit that way would send it to an
// unrelated rack whenever the two numbers met. The address is kept for what it
// has always been used for, an exact-match key and a stored link target, and
// the model is reached the unambiguous way.
// ============================================================================

ChainNodePath TrackManager::padChainPath(const ChainNodePath& gridPath, ChainId padChainId) {
    // Flat, and named by the DEVICE's own id: `Rack(gridDeviceId) > Chain(pad)`,
    // whatever the grid itself is nested in.
    //
    // This is the shape a pad device's address has always had, and three other
    // things build or store it: the ADSR macro links the slicer generates
    // (ClipCommands), the addresses the native parameter table compiles
    // (ParamTableCompiler), and every link a project has already saved. A
    // plugin lookup is an exact path match, so a different spelling here would
    // leave all of them pointing at nothing (#2207).
    //
    // The synthetic negative id is still what `RackInfo::id` holds, because the
    // plan keys its ops on it and `isPadRackId()` is how the executor tells a
    // pad rack from an allocated one. `getRackByPath()` resolves either.
    return ChainNodePath::chain(gridPath.trackId, gridPath.getDeviceId(), padChainId);
}

RackInfo* TrackManager::getPads(const ChainNodePath& gridPath) {
    auto* device = getDeviceInChainByPath(gridPath);
    return device != nullptr ? device->pads.get() : nullptr;
}

const RackInfo* TrackManager::getPads(const ChainNodePath& gridPath) const {
    return const_cast<TrackManager*>(this)->getPads(gridPath);
}

void TrackManager::setPads(const ChainNodePath& gridPath, const PadRack& pads) {
    auto* device = getDeviceInChainByPath(gridPath);
    if (device == nullptr)
        return;

    device->pads = pads;

    // The rack carries the device's id, and a restored copy has to carry the
    // id the device has now rather than the one it had when the snapshot was
    // taken -- a device restored by an undo of its own removal comes back under
    // the same id, but nothing here depends on that being true.
    stampPadRackId(*device);

    notifyTrackDevicesChanged(gridPath.trackId);
}

ChainInfo* TrackManager::getPadChain(const ChainNodePath& gridPath, ChainId padChainId) {
    auto* pads = getPads(gridPath);
    if (pads == nullptr)
        return nullptr;

    const auto found = std::ranges::find_if(
        pads->chains, [padChainId](const ChainInfo& chain) { return chain.id == padChainId; });
    return found == pads->chains.end() ? nullptr : &*found;
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

DeviceId TrackManager::addDeviceToPad(const ChainNodePath& gridPath, ChainId padChainId,
                                      const DeviceInfo& device, int insertIndex) {
    auto* pad = getPadChain(gridPath, padChainId);
    if (pad == nullptr)
        return INVALID_DEVICE_ID;

    if (auto* track = getTrack(gridPath.trackId);
        track != nullptr && !track->canHostInstrument() && device.isInstrument)
        return INVALID_DEVICE_ID;

    auto newDevice = prepareNewDevice(device);
    const auto devicePath = padChainPath(gridPath, padChainId).withDevice(newDevice.id);
    seedSidechainModIfMissing(newDevice, devicePath);

    const auto at = insertIndex < 0
                        ? static_cast<int>(pad->elements.size())
                        : std::clamp(insertIndex, 0, static_cast<int>(pad->elements.size()));
    pad->elements.insert(pad->elements.begin() + at, makeDeviceElement(newDevice));

    notifyTrackDevicesChanged(gridPath.trackId);
    notifyDeviceAdded(devicePath, newDevice);
    return newDevice.id;
}

void TrackManager::removeDeviceFromPad(const ChainNodePath& gridPath, ChainId padChainId,
                                       DeviceId deviceId) {
    auto* pad = getPadChain(gridPath, padChainId);
    if (pad == nullptr)
        return;

    const auto found = std::ranges::find_if(pad->elements, [deviceId](const ChainElement& element) {
        return magda::isDevice(element) && magda::getDevice(element).id == deviceId;
    });
    if (found == pad->elements.end())
        return;

    SelectionManager::getInstance().clearSelectionForDeletedChainNode(
        padChainPath(gridPath, padChainId).withDevice(deviceId));
    pad->elements.erase(found);
    notifyTrackDevicesChanged(gridPath.trackId);
}

void TrackManager::setPadDeviceBypassed(const ChainNodePath& gridPath, ChainId padChainId,
                                        DeviceId deviceId, bool bypassed) {
    auto* pad = getPadChain(gridPath, padChainId);
    if (pad == nullptr)
        return;

    for (auto& element : pad->elements) {
        if (!magda::isDevice(element) || magda::getDevice(element).id != deviceId)
            continue;

        auto& device = magda::getDevice(element);
        if (device.bypassed == bypassed)
            return;

        device.bypassed = bypassed;
        notifyTrackDevicesChanged(gridPath.trackId);
        return;
    }
}

void TrackManager::setPadDeviceGainDb(const ChainNodePath& gridPath, ChainId padChainId,
                                      DeviceId deviceId, float gainDb) {
    auto* pad = getPadChain(gridPath, padChainId);
    if (pad == nullptr)
        return;

    for (auto& element : pad->elements) {
        if (!magda::isDevice(element) || magda::getDevice(element).id != deviceId)
            continue;

        auto& device = magda::getDevice(element);
        device.gainDb = gainDb;
        device.gainValue = std::pow(10.0f, gainDb / 20.0f);

        // trackDevicesChanged, not devicePropertyChanged: the grid is filled
        // from the model by the track sync, and that is what carries a pad
        // device's gain onto the plugin it runs.
        notifyTrackDevicesChanged(gridPath.trackId);
        return;
    }
}

void TrackManager::moveDeviceInPad(const ChainNodePath& gridPath, ChainId padChainId, int fromIndex,
                                   int toIndex) {
    auto* pad = getPadChain(gridPath, padChainId);
    if (pad == nullptr)
        return;

    const auto count = static_cast<int>(pad->elements.size());
    if (fromIndex < 0 || fromIndex >= count || toIndex < 0 || toIndex >= count ||
        fromIndex == toIndex)
        return;

    auto moved = std::move(pad->elements[static_cast<std::size_t>(fromIndex)]);
    pad->elements.erase(pad->elements.begin() + fromIndex);
    pad->elements.insert(pad->elements.begin() + toIndex, std::move(moved));
    notifyTrackDevicesChanged(gridPath.trackId);
}

DeviceId TrackManager::setPadDevice(const ChainNodePath& gridPath, int padIndex,
                                    const DeviceInfo& device) {
    const auto padChainId = ensurePad(gridPath, padIndex);
    if (padChainId == INVALID_CHAIN_ID)
        return INVALID_DEVICE_ID;

    auto* pad = getPadChain(gridPath, padChainId);
    if (pad == nullptr)
        return INVALID_DEVICE_ID;

    // Dropping an instrument on a pad replaces the pad, effects and all: that
    // is what the pad's slot means. Selections pointing into what is going away
    // are cleared first, the same as any other device removal.
    const auto chainPath = padChainPath(gridPath, padChainId);
    for (const auto& element : pad->elements)
        if (magda::isDevice(element))
            SelectionManager::getInstance().clearSelectionForDeletedChainNode(
                chainPath.withDevice(magda::getDevice(element).id));
    pad->elements.clear();

    // Named after what is on it, which is what the grid shows on the pad.
    pad->name = device.name;

    return addDeviceToPad(gridPath, padChainId, device);
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

/// A pad's fader, pan and switches are its chain's, set the unambiguous way.
void TrackManager::setPadVolume(const ChainNodePath& gridPath, int padIndex, float volume) {
    if (auto* pad = mutablePad(gridPath, padIndex)) {
        pad->volume = juce::jlimit(-60.0f, 6.0f, volume);
        notifyTrackPropertyChanged(gridPath.trackId);
    }
}

void TrackManager::setPadPan(const ChainNodePath& gridPath, int padIndex, float pan) {
    if (auto* pad = mutablePad(gridPath, padIndex)) {
        pad->pan = juce::jlimit(-1.0f, 1.0f, pan);
        notifyTrackPropertyChanged(gridPath.trackId);
    }
}

void TrackManager::setPadMuted(const ChainNodePath& gridPath, int padIndex, bool muted) {
    if (auto* pad = mutablePad(gridPath, padIndex)) {
        pad->muted = muted;
        notifyTrackDevicesChanged(gridPath.trackId);
    }
}

void TrackManager::setPadSolo(const ChainNodePath& gridPath, int padIndex, bool solo) {
    if (auto* pad = mutablePad(gridPath, padIndex)) {
        pad->solo = solo;
        notifyTrackDevicesChanged(gridPath.trackId);
    }
}

void TrackManager::setPadBypassed(const ChainNodePath& gridPath, int padIndex, bool bypassed) {
    if (auto* pad = mutablePad(gridPath, padIndex)) {
        pad->bypassed = bypassed;
        notifyTrackDevicesChanged(gridPath.trackId);
    }
}

void TrackManager::setPadOutput(const ChainNodePath& gridPath, int padIndex, int outputIndex) {
    auto* pad = mutablePad(gridPath, padIndex);
    if (pad == nullptr || pad->outputIndex == outputIndex)
        return;

    pad->outputIndex = outputIndex;

    // trackDevicesChanged, not trackPropertyChanged: a pad's bus decides
    // whether the grid needs a multi-out child track for it, and that is
    // settled by the device sync (PluginManager::syncDrumGridMultiOutTracks).
    notifyTrackDevicesChanged(gridPath.trackId);
}

void TrackManager::setPadNoteRange(const ChainNodePath& gridPath, int padIndex, int lowNote,
                                   int highNote, int rootNote) {
    auto* pad = mutablePad(gridPath, padIndex);
    if (pad == nullptr)
        return;

    const auto low = juce::jlimit(0, 127, std::min(lowNote, highNote));
    const auto high = juce::jlimit(0, 127, std::max(lowNote, highNote));
    const auto root = juce::jlimit(0, 127, rootNote);

    if (pad->lowNote == low && pad->highNote == high && pad->rootNote == root)
        return;

    pad->lowNote = low;
    pad->highNote = high;
    pad->rootNote = root;
    notifyTrackDevicesChanged(gridPath.trackId);
}

ChainInfo* TrackManager::mutablePad(const ChainNodePath& gridPath, int padIndex) {
    auto* pads = getPads(gridPath);
    return pads != nullptr ? findPadChain(*pads, padIndex) : nullptr;
}

}  // namespace magda
