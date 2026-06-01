#pragma once

#include <juce_core/juce_core.h>

#include "KitRow.hpp"
#include "MacroInfo.hpp"
#include "ModInfo.hpp"
#include "ParameterInfo.hpp"
#include "TypeIds.hpp"

namespace magda {

/**
 * @brief Plugin format enumeration
 */
enum class PluginFormat { VST3, AU, VST, Internal };

/**
 * @brief Device type classification
 *
 * Instruments generate audio/MIDI, effects process audio, MIDI devices process
 * or analyse MIDI without audio I/O, and Analysis devices are transparent
 * passthroughs (oscilloscope, spectrum) that visualise the signal and expose
 * no macros or mods.
 */
enum class DeviceType { Instrument, Effect, MIDI, Analysis };

/**
 * @brief Describes a single stereo output pair from a multi-output plugin
 */
struct MultiOutOutputPair {
    int outputIndex = 0;                 // 0-based pair index (0 = main 1,2)
    juce::String name;                   // From plugin channel names, e.g. "St.3-4"
    bool active = false;                 // User activated this pair
    TrackId trackId = INVALID_TRACK_ID;  // Output track created for this pair
    int firstPin = 1;                    // 1-based rack output pin for left channel
    int numChannels = 2;                 // 1=mono, 2=stereo
};

/**
 * @brief Multi-output configuration for instruments with >2 output channels
 */
struct MultiOutConfig {
    bool isMultiOut = false;
    int totalOutputChannels = 0;
    std::vector<MultiOutOutputPair> outputPairs;
};

/**
 * @brief Sidechain routing configuration for a plugin
 *
 * Allows a plugin (e.g., compressor) to receive audio or MIDI from another track
 * as a sidechain/key input.
 */
struct SidechainConfig {
    enum class Type { None, Audio, MIDI };
    Type type = Type::None;
    TrackId sourceTrackId = INVALID_TRACK_ID;

    bool isActive() const {
        return type != Type::None && sourceTrackId != INVALID_TRACK_ID;
    }
};

/**
 * @brief Loading state for a device's underlying plugin
 */
enum class DeviceLoadState { Loaded, Loading, Failed };

/**
 * @brief Device/plugin information stored on a track
 */
struct DeviceInfo {
    DeviceId id = INVALID_DEVICE_ID;
    juce::String name;  // Display name (e.g., "Pro-Q 3")

    // MAGDA's loader/model id for this device. For internal devices this is
    // the canonical id ("4osc", "drumgrid"). For external plugins it is
    // usually initialised from the scanned plugin identifier, but call sites
    // that need a user-global plugin key should use
    // PluginPreferences::identifierForDevice() rather than choosing between
    // pluginId and uniqueId themselves.
    juce::String pluginId;

    juce::String manufacturer;  // Plugin vendor
    PluginFormat format = PluginFormat::VST3;
    bool isInstrument = false;  // true for instruments (synths, samplers), false for effects
    DeviceType deviceType = DeviceType::Effect;  // Instrument, Effect, or MIDI

    // External plugin identity from JUCE. Populated for scanned VST/AU/etc.
    // plugins using PluginDescription::createIdentifierString(); it is not
    // guaranteed for internal MAGDA devices.
    juce::String uniqueId;
    juce::String fileOrIdentifier;  // Path to plugin file or AU identifier

    bool bypassed = false;  // Device bypass state
    bool expanded = true;   // UI expanded state

    // UI panel visibility states
    bool modPanelOpen = false;    // Modulator panel visible
    bool gainPanelOpen = false;   // Gain panel visible
    bool paramPanelOpen = false;  // Parameter panel visible
    bool aiPanelOpen = false;     // AI sound-design panel visible

    // AI panel output text — transient runtime state, NOT serialized to disk.
    // Lives on DeviceInfo so the streamed prompt/result history survives slot
    // rebuilds (TrackChainContent::rebuildNodeComponents tears down the
    // owning DeviceSlotComponent on any notifyTrackDevicesChanged).
    juce::String aiPanelOutput;

    // Device parameters (populated by DeviceProcessor) — the plugin's own
    // automatable parameters. Wrapper-injected slot params (e.g. TE's
    // PluginWetDryAutomatableParam pair) belong in `wrapperParameters` and
    // must not be mixed in here.
    std::vector<ParameterInfo> parameters;

    // Wrapper-owned slot parameters. These come from the host wrapper, not
    // the plugin itself — TE's slot-level dry/wet on external plugins, and
    // anywhere else a wrapper synthesises parameters that the plugin author
    // never declared. Rendered by device-header chrome, never by the
    // parameter grid. `paramIndex` still addresses the underlying TE slot so
    // host writes, automation and aliases work the same as for plugin params.
    std::vector<ParameterInfo> wrapperParameters;

    // User-selected visible parameters (indices into plugin parameter list)
    // If empty, show first N parameters; otherwise show these specific indices
    std::vector<int> visibleParameters;

    // User-selected parameters surfaced in the mixer mini-chain row (indices into
    // the plugin parameter list). If empty, the mini row falls back to the first
    // non-hidden parameters in device order.
    std::vector<int> miniMixerParameters;

    // Device volume (gain knob on each device slot)
    float gainValue = 1.0f;  // Current gain value (linear)
    float gainDb = 0.0f;     // Current gain in dB for UI

    // Macro controls for device-level parameter mapping
    MacroArray macros = createDefaultMacros();

    // Modulators for device-level modulation
    ModArray mods = createDefaultMods(0);

    // Drum kit (note number -> label + role) for this specific instance.
    // Authoritative source for the drum grid editor's row metadata on any
    // clip routed through this device. Editing rows in the drum grid writes
    // here; PluginPreferences keeps a user-global mirror that's stamped onto
    // new instances when they're created. See KitRow.hpp.
    std::vector<KitRow> kitRows;

    // Sidechain configuration (e.g., compressor key input)
    SidechainConfig sidechain;
    bool canSidechain = false;    // true if TE plugin supports audio sidechain input
    bool canReceiveMidi = false;  // true if TE plugin accepts MIDI input (for cross-track MIDI)

    // Multi-output configuration (for instruments with >2 output channels)
    MultiOutConfig multiOut;

    // Plugin native state (base64-encoded binary blob from TE ExternalPlugin)
    juce::String pluginState;

    // Plugin loading state (Loading while async load is in-flight)
    DeviceLoadState loadState = DeviceLoadState::Loaded;

    // UI state
    int currentParameterPage = 0;  // Current parameter page (for multi-page param display)

    // Resolve a TE-relative paramIndex to the matching ParameterInfo in either
    // bucket. The argument is ALWAYS a TE index (ParameterInfo::paramIndex),
    // never an array position. Returns nullptr if no entry matches.
    ParameterInfo* findParameterByIndex(int paramIndex) {
        if (paramIndex < 0)
            return nullptr;
        for (auto& p : parameters)
            if (p.paramIndex == paramIndex)
                return &p;
        for (auto& p : wrapperParameters)
            if (p.paramIndex == paramIndex)
                return &p;
        return nullptr;
    }
    const ParameterInfo* findParameterByIndex(int paramIndex) const {
        return const_cast<DeviceInfo*>(this)->findParameterByIndex(paramIndex);
    }

    juce::String getFormatString() const {
        switch (format) {
            case PluginFormat::VST3:
                return "VST3";
            case PluginFormat::AU:
                return "AU";
            case PluginFormat::VST:
                return "VST";
            case PluginFormat::Internal:
                return "Internal";
            default:
                return "Unknown";
        }
    }
};

}  // namespace magda
