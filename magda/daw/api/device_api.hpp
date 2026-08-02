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
 * Read surface only for now. The mutation half — add by catalogue id, remove,
 * move, bypass, and parameter writes — needs path-based undoable commands that
 * do not exist yet (`TrackCommands` has add/remove at track level only), and
 * lands separately so that work gets reviewed on its own terms.
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
};

}  // namespace magda
