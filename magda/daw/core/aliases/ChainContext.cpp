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

    // Walk steps from innermost outward and return the first rack ancestor
    // (or the focused rack itself). Truncating the path to the rack step
    // yields a Rack-typed ChainNodePath whose getDeviceMacro target points
    // at the rack's macro array.
    //
    // User-created racks live in path.steps; instrument-wrapper racks
    // (InstrumentRackManager) are flattened into TopLevelDevice and never
    // appear here, so a focused top-level instrument still automaps to its
    // own device macros — preserving the "every device exposes macros
    // uniformly" contract.
    for (int i = static_cast<int>(path.steps.size()) - 1; i >= 0; --i) {
        if (path.steps[i].type == ChainStepType::Rack) {
            ChainNodePath rackPath;
            rackPath.trackId = path.trackId;
            rackPath.steps.assign(path.steps.begin(), path.steps.begin() + i + 1);
            return rackPath;
        }
    }

    // No rack ancestor — fall back to the focused device.
    if (path.getType() == ChainNodeType::TopLevelDevice || path.getType() == ChainNodeType::Device)
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
