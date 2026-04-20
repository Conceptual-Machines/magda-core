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
