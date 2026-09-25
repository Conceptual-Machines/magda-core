#pragma once

#include <juce_core/juce_core.h>

#include <optional>
#include <vector>

#include "../core/ChainNodePath.hpp"
#include "../core/DeviceInfo.hpp"
#include "../core/ReferenceImpact.hpp"
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

/** One path-free preset address for a particular live device. */
struct DevicePresetEntry {
    juce::String id;
    juce::String name;
    juce::String category;
    /** "magda" for a saved device state, "plugin" for VST3/AU preset files. */
    juce::String source;

    bool operator==(const DevicePresetEntry&) const = default;
};

enum class ApplyDevicePresetStatus {
    Applied,
    Unchanged,
    DeviceNotFound,
    PresetNotFound,
    Incompatible,
    ReferenceConflict,
    LoadFailed,
};

/** Result of resolving, preflighting, and atomically applying one opaque preset id. */
struct ApplyDevicePresetResult {
    ApplyDevicePresetStatus status = ApplyDevicePresetStatus::LoadFailed;
    ReferenceImpactPlan referenceImpact;
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
 * @brief Detection-data overrides for one parameter.
 *
 * The same fields Configure Parameters' AI-Detect writes: display unit,
 * scale, display range, and discrete choice labels. Present fields replace
 * the saved value; absent fields keep it. `index` is a position in
 * `DeviceInfo::parameters`.
 */
struct DeviceParameterOverride {
    int index = -1;
    std::optional<juce::String> unit;
    std::optional<ParameterScale> scale;
    std::optional<float> minValue;
    std::optional<float> maxValue;
    std::optional<std::vector<juce::String>> choices;
};

/**
 * @brief A partial update to a device's saved parameter customization.
 *
 * Each present field replaces that selection wholesale — an empty vector
 * clears it; an absent field leaves the saved selection alone. Indices are
 * positions in `DeviceInfo::parameters`.
 */
struct DeviceParameterConfigUpdate {
    std::optional<std::vector<int>> visibleParameters;
    std::optional<std::vector<int>> miniMixerParameters;
    std::optional<std::vector<int>> aiAgentParameters;
    std::optional<juce::String> aiPrompt;
    std::optional<std::vector<DeviceParameterOverride>> parameterOverrides;
};

/** Fields an agent may change on a device modulator. Missing fields are retained. */
struct DeviceModUpdate {
    std::optional<juce::String> name;
    std::optional<ModType> type;
    std::optional<LFOWaveform> waveform;
    std::optional<float> rate;
    std::optional<bool> enabled;
    std::optional<bool> tempoSync;
    std::optional<SyncDivision> syncDivision;
    std::optional<bool> oneShot;
    std::optional<float> attackMs;
    std::optional<float> decayMs;
    std::optional<float> sustain;
    std::optional<float> releaseMs;
};

/** A partial update to one Drum Grid pad chain. */
struct PadUpdate {
    std::optional<int> lowNote;
    std::optional<int> highNote;
    std::optional<int> rootNote;
    std::optional<float> levelDb;
    std::optional<float> pan;
    std::optional<bool> muted;
    std::optional<bool> solo;
    std::optional<bool> bypassed;
    std::optional<int> outputBus;
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

    /** Presets applicable to this device, with opaque ids and no file paths. */
    virtual std::vector<DevicePresetEntry> getDevicePresets(
        const ChainNodePath& devicePath) const = 0;

    /** Apply an id returned by getDevicePresets() without replacing the device slot. */
    virtual ApplyDevicePresetResult applyPreset(const ChainNodePath& devicePath,
                                                const juce::String& presetId) = 0;

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

    /** Drum Grid kit edits. Paths identify the owning grid, never a rack with a colliding id. */
    virtual ChainId createPad(const ChainNodePath&, int) = 0;
    virtual DeviceId setPadVoice(const ChainNodePath&, int, const juce::String&) = 0;
    /** A host-local sample path is accepted only as write input and is never projected. */
    virtual DeviceId setPadSample(const ChainNodePath&, int, const juce::String&) = 0;
    virtual bool clearPad(const ChainNodePath&, int) = 0;
    virtual bool swapPads(const ChainNodePath&, int, int) = 0;
    virtual bool updatePad(const ChainNodePath&, int, const PadUpdate&) = 0;

    virtual bool setDeviceBypassed(const ChainNodePath& devicePath, bool bypassed) = 0;

    /**
     * @brief Write one parameter, in real parameter units.
     *
     * Values outside the parameter's range are rejected rather than clamped: a
     * silently clamped write reports success while doing something the caller
     * did not ask for.
     *
     * External-plugin parameters the user has not opted in under Configure
     * Parameters are rejected: this facade is the programmatic surface, and
     * the per-parameter opt-in is enforced here so no consumer routes around
     * it. Internal devices accept writes on every parameter.
     */
    virtual bool setDeviceParameter(const ChainNodePath& devicePath, int paramIndex,
                                    float value) = 0;

    /**
     * @brief Update the plugin's saved parameter customization and persist it.
     *
     * Writes the same per-plugin store Configure Parameters writes, then
     * re-applies it to every live instance, so the change outlives the
     * session. External plugins only: internal devices have no saved
     * customization and their parameters already accept agent writes. False
     * when the path does not resolve, the device is internal or carries no
     * config id, or an index is out of range.
     */
    virtual bool setDeviceParameterConfig(const ChainNodePath& devicePath,
                                          const DeviceParameterConfigUpdate& update) = 0;

    /**
     * @brief Open the device's plugin editor window in the MAGDA UI.
     *
     * Returns whether a window is actually open afterwards — false when the
     * path does not resolve, when no engine is running (headless), or when
     * the device has no native editor to show.
     */
    virtual bool openDeviceEditor(const ChainNodePath& devicePath) = 0;

    /** Device-owned modulation. Link targets are parameters on the same device.
     * External targets require the same AI opt-in as setDeviceParameter. */
    virtual std::vector<ModInfo> getDeviceMods(const ChainNodePath& devicePath) const = 0;
    virtual std::vector<MacroInfo> getDeviceMacros(const ChainNodePath& devicePath) const = 0;
    virtual ModId createDeviceMod(const ChainNodePath& devicePath, ModType type,
                                  LFOWaveform waveform) = 0;
    virtual bool updateDeviceMod(const ChainNodePath& devicePath, ModId modId,
                                 const DeviceModUpdate& update) = 0;
    virtual bool removeDeviceMod(const ChainNodePath& devicePath, ModId modId) = 0;
    virtual bool linkDeviceMod(const ChainNodePath& devicePath, ModId modId, int parameterIndex,
                               float amount, bool bipolar) = 0;
    virtual bool unlinkDeviceMod(const ChainNodePath& devicePath, ModId modId,
                                 int parameterIndex) = 0;
    virtual bool setDeviceMacroValue(const ChainNodePath& devicePath, int macroIndex,
                                     float value) = 0;
    virtual bool linkDeviceMacro(const ChainNodePath& devicePath, int macroIndex,
                                 int parameterIndex, float amount, bool bipolar) = 0;
    virtual bool unlinkDeviceMacro(const ChainNodePath& devicePath, int macroIndex,
                                   int parameterIndex) = 0;
};

}  // namespace magda
