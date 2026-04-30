#include "ChainContext.hpp"

#include "../SelectionManager.hpp"
#include "../TrackManager.hpp"

namespace magda {

// ============================================================================
// DefaultChainContext
// ============================================================================

ChainNodePath DefaultChainContext::focusedChain() const {
    auto& sel = SelectionManager::getInstance();
    if (sel.hasChainNodeSelection())
        return sel.getSelectedChainNode();
    return {};
}

TrackId DefaultChainContext::selectedTrack() const {
    return SelectionManager::getInstance().getSelectedTrack();
}

ChainNodePath DefaultChainContext::focusedDevice() const {
    auto& sel = SelectionManager::getInstance();
    if (!sel.hasChainNodeSelection())
        return {};

    const auto& path = sel.getSelectedChainNode();
    if (path.getType() == ChainNodeType::Device || path.getType() == ChainNodeType::TopLevelDevice)
        return path;

    return {};
}

ChainNodePath DefaultChainContext::focusedMacroOwner() const {
    auto& sel = SelectionManager::getInstance();
    if (!sel.hasChainNodeSelection())
        return {};

    const auto& path = sel.getSelectedChainNode();
    if (!path.isValid())
        return {};

    // Map the focused node to the macro-owning scope used for automap:
    //
    //   TopLevelDevice  → the device itself (instrument-wrapper racks are
    //                     flattened, so there's no rack to bubble up to).
    //   Rack            → the rack itself.
    //   Chain           → the parent rack (chains don't own macros, but
    //                     focusing a chain implies "operating on this rack").
    //   Device-in-chain → invalid. Focusing a device INSIDE a user rack is
    //                     not enough to engage automap — the user must click
    //                     the rack header explicitly. Avoids two surprises:
    //                     (a) inner-device macros being mapped just because
    //                     the user opened a device to inspect parameters;
    //                     (b) rack macros silently lighting up green when
    //                     the user clicked through into the rack's chain.
    //   Track / None    → invalid (track macros don't go through
    //                     focused.macro today).
    switch (path.getType()) {
        case ChainNodeType::TopLevelDevice:
        case ChainNodeType::Rack:
            return path;
        case ChainNodeType::Chain: {
            for (int i = static_cast<int>(path.steps.size()) - 1; i >= 0; --i) {
                if (path.steps[i].type == ChainStepType::Rack) {
                    ChainNodePath rackPath;
                    rackPath.trackId = path.trackId;
                    rackPath.steps.assign(path.steps.begin(), path.steps.begin() + i + 1);
                    return rackPath;
                }
            }
            return {};
        }
        case ChainNodeType::Device:
        case ChainNodeType::Track:
        case ChainNodeType::None:
            return {};
    }
    return {};
}

const DeviceInfo* DefaultChainContext::deviceAt(const ChainNodePath& path) const {
    return TrackManager::getInstance().getDeviceInChainByPath(path);
}

std::vector<ChainContext::DeviceWithPath> DefaultChainContext::devicesInFocusedChain() const {
    std::vector<DeviceWithPath> result;

    auto& sel = SelectionManager::getInstance();
    if (!sel.hasChainNodeSelection())
        return result;

    const auto& chainPath = sel.getSelectedChainNode();
    if (!chainPath.isValid())
        return result;

    TrackId trackId = chainPath.trackId;
    auto& tm = TrackManager::getInstance();
    const auto* track = tm.getTrack(trackId);
    if (track == nullptr)
        return result;

    // Walk the chain elements in the focused chain.
    // For simplicity: top-level chain elements on the track.
    // A more complete implementation would drill into the selected rack/chain.
    for (const auto& element : track->chainElements) {
        if (isDevice(element)) {
            const auto& dev = getDevice(element);
            ChainNodePath devPath = ChainNodePath::topLevelDevice(trackId, dev.id);
            result.push_back({&dev, devPath});
        }
    }

    return result;
}

std::vector<ChainContext::DeviceWithPath> DefaultChainContext::devicesForTrack(
    TrackId trackId) const {
    std::vector<DeviceWithPath> result;

    auto& tm = TrackManager::getInstance();
    const auto* track = tm.getTrack(trackId);
    if (track == nullptr)
        return result;

    for (const auto& element : track->chainElements) {
        if (isDevice(element)) {
            const auto& dev = getDevice(element);
            ChainNodePath devPath = ChainNodePath::topLevelDevice(trackId, dev.id);
            result.push_back({&dev, devPath});
        }
    }

    return result;
}

}  // namespace magda
