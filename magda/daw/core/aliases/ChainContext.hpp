#pragma once

#include <optional>
#include <vector>

#include "../DeviceInfo.hpp"
#include "../SelectionManager.hpp"
#include "../TypeIds.hpp"

namespace magda {

// ============================================================================
// ChainContext
// ============================================================================

/**
 * @brief Abstract interface for querying the current chain/track state.
 *
 * Used by TargetResolver to answer questions like "which chain is focused?"
 * and "is there a device of this plugin type in the current chain?".
 *
 * Separating this behind an interface allows tests to inject a fixed context
 * (FixedChainContext) without requiring a live DAW engine.
 */
class ChainContext {
  public:
    virtual ~ChainContext() = default;

    /**
     * @brief Get the path to the first focused rack (if any).
     *
     * The "focused chain" is the chain whose rack is currently selected in
     * the chain inspector. Returns an invalid path if nothing is focused.
     */
    virtual ChainNodePath focusedChain() const = 0;

    /**
     * @brief Get the currently selected track ID.
     *
     * Returns INVALID_TRACK_ID if no track is selected.
     */
    virtual TrackId selectedTrack() const = 0;

    /**
     * @brief Get the path to the currently focused device.
     *
     * The focused device is typically the last device selected in the
     * chain inspector. Returns an invalid path if none.
     */
    virtual ChainNodePath focusedDevice() const = 0;

    /**
     * @brief Look up a DeviceInfo at the given path.
     *
     * Returns nullptr if the path is invalid or no device lives there.
     * The pointer is valid only as long as the TrackManager data is not
     * mutated (suitable for the duration of a single resolution call).
     */
    virtual const DeviceInfo* deviceAt(const ChainNodePath& path) const = 0;

    /**
     * @brief Get all devices in the focused chain, in chain order.
     *
     * Returns device infos for every device element in the first chain of
     * the focused rack. Returns empty when no chain is focused.
     * Devices are returned with their paths populated.
     */
    struct DeviceWithPath {
        const DeviceInfo* device = nullptr;
        ChainNodePath path;
    };

    virtual std::vector<DeviceWithPath> devicesInFocusedChain() const = 0;

    /**
     * @brief Get all top-level devices on the given track, in chain order.
     *
     * Returns empty when the track does not exist or has no devices.
     * Used by TargetResolver to prefer devices in the selected-track chain
     * when resolving '@name.param' with a track selected.
     */
    virtual std::vector<DeviceWithPath> devicesForTrack(TrackId trackId) const = 0;
};

// ============================================================================
// DefaultChainContext
// ============================================================================

/**
 * @brief Live production context backed by SelectionManager and TrackManager.
 */
class DefaultChainContext : public ChainContext {
  public:
    ChainNodePath focusedChain() const override;
    TrackId selectedTrack() const override;
    ChainNodePath focusedDevice() const override;
    const DeviceInfo* deviceAt(const ChainNodePath& path) const override;
    std::vector<DeviceWithPath> devicesInFocusedChain() const override;
    std::vector<DeviceWithPath> devicesForTrack(TrackId trackId) const override;
};

// ============================================================================
// FixedChainContext (test double)
// ============================================================================

/**
 * @brief Test double with fixed/injectable state.
 *
 * Tests populate this struct and pass it to TargetResolver instead of
 * DefaultChainContext. No real managers are required.
 */
class FixedChainContext : public ChainContext {
  public:
    // Setters for test fixture setup
    void setFocusedChain(const ChainNodePath& path) {
        focusedChain_ = path;
    }
    void setSelectedTrack(TrackId id) {
        selectedTrack_ = id;
    }
    void setFocusedDevice(const ChainNodePath& path) {
        focusedDevice_ = path;
    }

    /**
     * @brief Register a device with the context.
     *
     * The DeviceInfo must remain valid as long as this context is in use
     * (the context stores a pointer to the caller's storage).
     */
    void addDevice(const ChainNodePath& path, const DeviceInfo& device) {
        devices_.push_back({&device, path});
    }

    // ChainContext implementation
    ChainNodePath focusedChain() const override {
        return focusedChain_;
    }
    TrackId selectedTrack() const override {
        return selectedTrack_;
    }
    ChainNodePath focusedDevice() const override {
        return focusedDevice_;
    }
    const DeviceInfo* deviceAt(const ChainNodePath& path) const override {
        for (const auto& entry : devices_)
            if (entry.path == path)
                return entry.device;
        return nullptr;
    }
    std::vector<DeviceWithPath> devicesInFocusedChain() const override {
        return devices_;
    }
    std::vector<DeviceWithPath> devicesForTrack(TrackId trackId) const override {
        std::vector<DeviceWithPath> result;
        for (const auto& dw : devices_) {
            if (dw.path.trackId == trackId)
                result.push_back(dw);
        }
        return result;
    }

  private:
    ChainNodePath focusedChain_;
    TrackId selectedTrack_ = INVALID_TRACK_ID;
    ChainNodePath focusedDevice_;
    std::vector<DeviceWithPath> devices_;
};

}  // namespace magda
