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
// with the grid's own DeviceId, under the distinct `PadRack`/`PadChain` step
// types (`padChainPath`). Rack ids and device ids come out of counters that
// both start at 1, so an untyped `Rack(1)` would be as much rack 1 as it is
// Drum Grid 1's pads; the types keep the two apart without anyone having to
// inspect the model or count steps (#2219).
// ============================================================================

namespace {

/// Drop any selection standing on something inside @p chainPath before it goes.
///
/// Every node under it, not only the devices it holds directly: a pad's chain
/// takes racks like any other chain, and `clearSelectionForDeletedChainNode()`
/// matches an exact path or a device id rather than an ancestor, so a selection
/// below a nested rack would survive the erase pointing at freed model (#2211).
void clearSelectionsUnder(const std::vector<ChainElement>& elements,
                          const ChainNodePath& chainPath) {
    auto& selection = SelectionManager::getInstance();
    for (const auto& element : elements) {
        if (magda::isDevice(element)) {
            selection.clearSelectionForDeletedChainNode(
                chainPath.withDevice(magda::getDevice(element).id));
            continue;
        }

        if (!magda::isRack(element))
            continue;

        const auto& rack = magda::getRack(element);
        const auto rackPath = chainPath.withRack(rack.id);
        for (const auto& chain : rack.chains) {
            const auto nested = rackPath.withChain(chain.id);
            clearSelectionsUnder(chain.elements, nested);
            selection.clearSelectionForDeletedChainNode(nested);
        }
        selection.clearSelectionForDeletedChainNode(rackPath);
    }
}

}  // namespace

ChainNodePath TrackManager::padChainPath(const ChainNodePath& gridPath, ChainId padChainId) {
    // Flat, and named by the DEVICE's own id: `PadRack(gridDeviceId) >
    // PadChain(pad)`, whatever the grid itself is nested in.
    //
    // The step types say which id is which, so a resolver or a remapper never
    // has to infer pad ownership from the path's shape and can never confuse a
    // grid's DeviceId with the RackId of an allocated rack sharing the number
    // (#2219).
    //
    // The synthetic negative id is still what `RackInfo::id` holds, because the
    // plan keys its ops on it and `isPadRackId()` is how the executor tells a
    // pad rack from an allocated one.
    //
    // `getRackByPath()` does not resolve this: a pad rack is a field on a
    // device rather than a chain element. `getDeviceInChainByPath()` follows it
    // through `getDeviceInPadByPath()`, which now dispatches on the step type
    // rather than trying the pad route as a fallback.
    return ChainNodePath::padChain(gridPath.trackId, gridPath.getDeviceId(), padChainId);
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
    clearSelectionsUnder(pad->elements, padChainPath(gridPath, padChainId));
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
    clearSelectionsUnder(pad->elements, chainPath);
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

bool TrackManager::padBusesAvailable(const ChainNodePath& gridPath) const {
    // A multi-out child track is fed by the output instance
    // InstrumentRackManager makes when it wraps a top-level instrument. A grid
    // inside a MAGDA rack is loaded by RackSyncManager instead and has no entry
    // there, so nothing would carry a bus off it: the pads on that bus would
    // simply go silent. Refused until that route exists (#2211).
    return gridPath.getType() == ChainNodeType::TopLevelDevice;
}

bool TrackManager::resetPadBuses(const ChainNodePath& gridPath) {
    auto* pads = getPads(gridPath);
    if (pads == nullptr)
        return false;

    bool moved = false;
    for (auto& pad : pads->chains) {
        if (pad.outputIndex == 0)
            continue;
        pad.outputIndex = 0;
        moved = true;
    }

    if (moved)
        notifyTrackDevicesChanged(gridPath.trackId);

    return moved;
}

bool TrackManager::setPadOutput(const ChainNodePath& gridPath, int padIndex, int outputIndex) {
    // The bus has to be one that exists. The live plugin clamps what it is
    // given and the plan compiler takes the model's value as it finds it, so an
    // out-of-range index makes the two engines disagree: the plan reports a bus
    // that reaches no track and the pads on it go silent (#2211).
    if (outputIndex < 0 || outputIndex >= kPadBusCount)
        return false;

    if (outputIndex != 0 && !padBusesAvailable(gridPath))
        return false;

    auto* pad = mutablePad(gridPath, padIndex);
    if (pad == nullptr)
        return false;
    if (pad->outputIndex == outputIndex)
        return true;

    pad->outputIndex = outputIndex;

    // trackDevicesChanged, not trackPropertyChanged: a pad's bus decides
    // whether the grid needs a multi-out child track for it, and that is
    // settled by the device sync (PluginManager::syncDrumGridMultiOutTracks).
    notifyTrackDevicesChanged(gridPath.trackId);
    return true;
}

bool TrackManager::padNoteRangeIsFree(const ChainNodePath& gridPath, int padIndex, int lowNote,
                                      int highNote) const {
    const auto* pads = getPads(gridPath);
    if (pads == nullptr)
        return false;

    const auto* pad = findPadChain(*pads, padIndex);
    if (pad == nullptr)
        return false;

    const auto low = juce::jlimit(0, 127, std::min(lowNote, highNote));
    const auto high = juce::jlimit(0, 127, std::max(lowNote, highNote));

    // Reachable, at both ends. The grid shows kPadCount pads from kPadBaseNote
    // and builds its rows from those notes, so a chain reaching outside them
    // keeps playing and cannot be seen; and `padParameterSlot()` takes the pad's
    // slot from its bottom note, so a low end below the grid returns -1 and the
    // plan stops binding the chain's fader and pan to the grid's parameters.
    // The endpoint sliders offer only these notes, so this is also what keeps
    // the row's reading of a range honest.
    if (low < kPadBaseNote || high > kPadBaseNote + kPadCount - 1)
        return false;

    // One note, one owner. Every chain whose range covers an incoming note
    // plays it, so an overlap layers two sounds the UI has no way to tell
    // apart: the rows show the last chain to claim a note and the setters
    // reach the first, so a row would edit a chain other than the one it
    // shows. Refused rather than silently normalised, so the row snaps back
    // to what the model kept.
    for (const auto& other : pads->chains)
        if (other.id != pad->id && !other.answersToEveryNote() && low <= other.highNote &&
            high >= other.lowNote)
            return false;

    return true;
}

bool TrackManager::setPadNoteRange(const ChainNodePath& gridPath, int padIndex, int lowNote,
                                   int highNote, int rootNote) {
    auto* pads = getPads(gridPath);
    if (pads == nullptr)
        return false;

    auto* pad = findPadChain(*pads, padIndex);
    if (pad == nullptr)
        return false;

    const auto low = juce::jlimit(0, 127, std::min(lowNote, highNote));
    const auto high = juce::jlimit(0, 127, std::max(lowNote, highNote));
    const auto root = juce::jlimit(0, 127, rootNote);

    if (pad->lowNote == low && pad->highNote == high && pad->rootNote == root)
        return true;

    if (!padNoteRangeIsFree(gridPath, padIndex, low, high))
        return false;

    pad->lowNote = low;
    pad->highNote = high;
    pad->rootNote = root;
    notifyTrackDevicesChanged(gridPath.trackId);
    return true;
}

void TrackManager::removePadChain(const ChainNodePath& gridPath, ChainId padChainId) {
    auto* pads = getPads(gridPath);
    if (pads == nullptr)
        return;

    const auto found = std::ranges::find_if(
        pads->chains, [padChainId](const ChainInfo& chain) { return chain.id == padChainId; });
    if (found == pads->chains.end())
        return;

    const auto chainPath = padChainPath(gridPath, padChainId);
    clearSelectionsUnder(found->elements, chainPath);
    SelectionManager::getInstance().clearSelectionForDeletedChainNode(chainPath);

    pads->chains.erase(found);
    notifyTrackDevicesChanged(gridPath.trackId);
}

ChainInfo* TrackManager::mutablePad(const ChainNodePath& gridPath, int padIndex) {
    auto* pads = getPads(gridPath);
    return pads != nullptr ? findPadChain(*pads, padIndex) : nullptr;
}

}  // namespace magda
