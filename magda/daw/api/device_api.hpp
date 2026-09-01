#pragma once

#include <juce_core/juce_core.h>

#include <optional>
#include <vector>

#include "../core/ChainNodePath.hpp"
#include "../core/DeviceInfo.hpp"
#include "../core/TypeIds.hpp"

namespace magda {

/**
 * @brief A device the user can add, described without exposing where it lives.
 *
 * `catalogId` is `DeviceInfo::pluginId` — the loader id for internal and
 * compiled devices ("4osc", "drumgrid"), and the scanned identifier for
 * external plugins. It is the only handle a caller needs to add a device.
 *
 * Deliberately carries no `fileOrIdentifier`: a remote caller must be able to
 * name a plugin without learning the filesystem layout of the machine hosting
 * it.
 */
struct DeviceCatalogEntry {
    juce::String catalogId;
    juce::String name;
    juce::String manufacturer;
    juce::String category;
    juce::String description;
    PluginFormat format = PluginFormat::Internal;
    DeviceType type = DeviceType::Effect;
    bool isInstrument = false;

    bool operator==(const DeviceCatalogEntry&) const = default;
};

/**
 * @brief One automatable parameter of a live device.
 *
 * Values are in real parameter units (Hz, dB, %), matching `ParameterInfo` —
 * never MAGDA-normalized automation units. `index` addresses the parameter
 * within its device and is what `setDeviceParameter` takes.
 */
struct DeviceParameter {
    int index = -1;
    juce::String stableId;
    juce::String name;
    juce::String unit;
    float minValue = 0.0f;
    float maxValue = 1.0f;
    float defaultValue = 0.0f;
    float currentValue = 0.0f;

    bool operator==(const DeviceParameter&) const = default;
};

/**
 * @brief Device discovery and inspection, addressed by `ChainNodePath`.
 *
 * Live devices are named by path rather than by `DeviceId`, because a device id
 * is unique only within one of a track's three sections — the main FX chain,
 * the post-fader list, and the mixer-analysis rail each allocate from their own
 * counter. A path also reaches arbitrarily deep into nested racks, which the
 * `(trackId, rackId, chainId)` triples on `TrackApi` cannot express.
 *
 * Callers never construct a `DeviceInfo`: `addDevice` takes a catalogue id and
 * a placement, so plugin internals and file paths stay private to the host.
 * Every mutation is one undo step.
 */
class DeviceApi {
  public:
    virtual ~DeviceApi() = default;

    /** Every device the user can add — internal, compiled, and external. */
    virtual std::vector<DeviceCatalogEntry> getCatalog() const = 0;

    /** One catalogue entry, or nullopt if `catalogId` names nothing. */
    virtual std::optional<DeviceCatalogEntry> findCatalogEntry(
        const juce::String& catalogId) const = 0;

    /** The device at `devicePath`, or nullptr. */
    virtual const DeviceInfo* getDevice(const ChainNodePath& devicePath) const = 0;

    /** Empty if the path does not resolve, or the device has no parameters. */
    virtual std::vector<DeviceParameter> getDeviceParameters(
        const ChainNodePath& devicePath) const = 0;

    /**
     * @brief Add a device to a track's FX chain or to a rack chain.
     *
     * `parentPath` is a track-level path for the main FX chain, or a chain path
     * at any nesting depth. `index` inserts at that position; negative appends.
     * Returns INVALID_DEVICE_ID if the path or the catalogue id does not
     * resolve.
     */
    virtual DeviceId addDevice(const ChainNodePath& parentPath, const juce::String& catalogId,
                               int index) = 0;

    /** Remove the device at `devicePath`. */
    virtual bool removeDevice(const ChainNodePath& devicePath) = 0;

    /** Move a device within the chain it already lives in. */
    virtual bool moveDevice(const ChainNodePath& devicePath, int toIndex) = 0;

    virtual bool setDeviceBypassed(const ChainNodePath& devicePath, bool bypassed) = 0;

    /**
     * @brief Write one parameter, in real parameter units.
     *
     * Values outside the parameter's range are rejected rather than clamped: a
     * silently clamped write reports success while doing something the caller
     * did not ask for.
     */
    virtual bool setDeviceParameter(const ChainNodePath& devicePath, int paramIndex,
                                    float value) = 0;

    /**
     * @brief Open the device's plugin editor window in the MAGDA UI.
     *
     * Returns whether a window is actually open afterwards — false when the
     * path does not resolve, when no engine is running (headless), or when
     * the device has no native editor to show.
     */
    virtual bool openDeviceEditor(const ChainNodePath& devicePath) = 0;
};

}  // namespace magda
