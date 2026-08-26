#pragma once

#include <juce_core/juce_core.h>

#include <memory>
#include <optional>

#include "KitRow.hpp"
#include "MacroInfo.hpp"
#include "ModInfo.hpp"
#include "ParameterInfo.hpp"
#include "TypeIds.hpp"

namespace magda {

struct RackInfo;

/**
 * @brief Value-semantic owner for a device's pad chains.
 *
 * RackInfo is incomplete here -- RackInfo.hpp includes this header, so it
 * cannot be included back -- and DeviceInfo is copied by value throughout the
 * app. A bare unique_ptr would make DeviceInfo non-copyable and a shared_ptr
 * would make two copies of a device share one set of pads. This owns the rack,
 * copies it deep, and keeps its own rule of five out of line so DeviceInfo
 * needs none of its own.
 */
class PadRack {
  public:
    PadRack();
    ~PadRack();
    PadRack(const PadRack& other);
    PadRack& operator=(const PadRack& other);
    PadRack(PadRack&& other) noexcept;
    PadRack& operator=(PadRack&& other) noexcept;

    explicit operator bool() const {
        return rack_ != nullptr;
    }
    RackInfo* get() const {
        return rack_.get();
    }
    RackInfo* operator->() const {
        return rack_.get();
    }
    void reset(std::unique_ptr<RackInfo> rack);

  private:
    std::unique_ptr<RackInfo> rack_;
};

/**
 * @brief Plugin format enumeration
 *
 * Values are serialized as raw ints by TrackSerializer, so they are fixed:
 * append new formats, never renumber the existing ones.
 *
 * Slot 2 previously held VST2, which MAGDA has never hosted
 * (JUCE_PLUGINHOST_VST is not set on any target), so nothing could be scanned
 * as one and no saved project can contain a working device with that value.
 * LV2 reuses it rather than leaving a permanent hole.
 */
enum class PluginFormat {
    VST3 = 0,
    AU = 1,
    LV2 = 2,
    Internal = 3,
};

inline std::optional<PluginFormat> maybePluginFormatFromName(const juce::String& pluginFormatName) {
    if (pluginFormatName.containsIgnoreCase("VST3"))
        return PluginFormat::VST3;
    if (pluginFormatName.containsIgnoreCase("AudioUnit") || pluginFormatName.equalsIgnoreCase("AU"))
        return PluginFormat::AU;
    if (pluginFormatName.containsIgnoreCase("LV2"))
        return PluginFormat::LV2;
    return std::nullopt;
}

/** True for ints that name a live PluginFormat. Guards the deserializer
 *  against values a newer build might write. */
inline bool isKnownPluginFormatValue(int value) {
    return value >= static_cast<int>(PluginFormat::VST3) &&
           value <= static_cast<int>(PluginFormat::Internal);
}

inline PluginFormat pluginFormatFromName(const juce::String& pluginFormatName) {
    if (auto format = maybePluginFormatFromName(pluginFormatName))
        return *format;
    return PluginFormat::VST3;
}

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
 * @brief How a read-only meter asks to be drawn in a grid cell.
 */
enum class MeterStyle {
    Bar,        // filled bar across the cell
    Numerical,  // value as text, no bar
    Led,        // lamp whose brightness tracks the value
};

/**
 * @brief A read-only value a device reports back to the host.
 *
 * Deliberately not a ParameterInfo. A meter is written by the device and read
 * by the UI, so it can never be automated, modulated, macro-linked or MIDI
 * learned, and keeping it out of `DeviceInfo::parameters` is what guarantees
 * that, since every binding surface walks that list. This struct describes the
 * meter; the live reading is polled by the cell, not carried here.
 *
 * Runtime Faust devices populate this from `hbargraph` / `vbargraph`.
 */
struct MeterInfo {
    /// Index into the device's own output pool, passed back to the device
    /// when the cell polls for a reading.
    int meterIndex = -1;
    juce::String name;
    juce::String unit;
    juce::String group;    // page/tab name, same rule as ParameterInfo::group
    juce::String tooltip;  // hover help
    float minValue = 0.0f;
    float maxValue = 1.0f;
    int widthCells = 1;
    MeterStyle style = MeterStyle::Bar;
    /// Declared as a vertical bargraph. A run of these in one group can be
    /// banked side by side rather than each taking a cell of its own.
    bool vertical = false;
};

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

    // User/browser categorization override (e.g. "MIDI FX"). This is UX
    // metadata only; it must not rewrite JUCE scan facts or routing
    // capabilities such as isInstrument, canReceiveMidi, or producesMidi.
    juce::String browserCategoryOverride;

    // External plugin identity from JUCE. Populated for scanned VST/AU/etc.
    // plugins using PluginDescription::createIdentifierString(); it is not
    // guaranteed for internal MAGDA devices.
    juce::String uniqueId;
    juce::String fileOrIdentifier;  // Path to plugin file or AU identifier

    bool bypassed = false;   // Device bypass state
    bool deltaSolo = false;  // Hear the processed signal minus its latency-aligned dry input
    bool expanded = true;    // UI expanded state

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

    // Serialized llm::Conversation (JSON) for the device's AI panel, so multi-
    // turn refinement ("make it brighter") survives slot rebuilds. Like
    // aiPanelOutput this is transient runtime state, owned by the agent: read
    // before a generation and written back after, on the message thread.
    juce::String aiConversation;

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

    // Read-only values the device reports back (Faust bargraphs). Kept out of
    // `parameters` on purpose; see MeterInfo. The grid gives each one a cell
    // and polls the device for its reading.
    std::vector<MeterInfo> meters;

    // User-selected visible parameters (indices into plugin parameter list)
    // If empty, show first N parameters; otherwise show these specific indices
    std::vector<int> visibleParameters;

    // User-selected parameters surfaced in the mixer mini-chain row (indices into
    // the plugin parameter list). If empty, the mini row falls back to the first
    // non-hidden parameters in device order.
    std::vector<int> miniMixerParameters;

    // User-selected parameters exposed to the generic AI sound designer
    // (indices into the plugin parameter list). External plugins require an
    // explicit non-empty selection; internal generic sound generators ignore
    // this filter and expose their full automatable parameter set.
    std::vector<int> aiSoundDesignerParameters;

    // Optional user-authored, plugin-specific instructions appended to the
    // generic AI sound designer's system prompt for external plugins.
    juce::String aiSoundDesignerPrompt;

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

    /**
     * A pad-per-chain device's pads, as chains keyed by note range (#2207).
     *
     * Null for every device that is not one. This is the device's pads: the
     * project file saves them, every pad edit writes them, the plan compiler
     * expands them, and `DrumGridPlugin` is filled from them. Nothing reads
     * them back out of the plugin, so nothing can drift.
     *
     * They are chains because that is what they are. ChainInfo already carries
     * a pad's level, pan, mute, solo, bypass and output; the note range it
     * gained alongside this is the only thing a pad has that a rack chain does
     * not.
     */
    PadRack pads;

    // Sidechain configuration (e.g., compressor key input)
    SidechainConfig sidechain;
    bool canSidechain = false;    // true if TE plugin supports audio sidechain input
    bool canReceiveMidi = false;  // true if TE plugin accepts MIDI input (for cross-track MIDI)
    bool producesMidi = false;    // true if the live plugin can output MIDI

    // Audio channels the plugin reported (1 = mono, 2 = stereo), read from the
    // live plugin via getChannelNames. Stereo until it is asked. The chain
    // model wires from these: no audio input means the device is not connected
    // to the bus, no audio output means nothing reaches what follows, and a
    // mono device is given and read as mono. An instrument's input count is
    // unused, since an instrument is never fed chain audio.
    int audioInputChannels = 2;
    int audioOutputChannels = 2;

    // "MIDI in thru": pass the chain's raw MIDI input past a MIDI-producing
    // plugin so downstream devices can receive both the original input and the
    // plugin's generated MIDI. Off means plugin MIDI output only; on means merge
    // raw input plus plugin output. Defaults on to preserve historic passthrough.
    bool midiInThru = true;

    // Multi-output configuration (for instruments with >2 output channels)
    MultiOutConfig multiOut;

    // Plugin native state (base64-encoded binary blob from TE ExternalPlugin)
    juce::String pluginState;

    // VST3 class id (32-char hex FUID) for hosted VST3 plugins, captured once
    // from the .vstpreset header. This is the portable identity other hosts match
    // on (DAWproject deviceID); unlike uniqueId it is not lossily hashed. Empty
    // for non-VST3 devices. Runtime/interchange only - not part of native state.
    juce::String vst3ClassId;

    // Base64 of the plugin's current .vstpreset (Steinberg preset format), captured
    // for hosted VST3 plugins. Used as the portable DAWproject <State> so other
    // hosts (and MAGDA on import) can restore the patch via setPreset(). Refreshed
    // on capture; interchange only - native state uses pluginState.
    juce::String vst3Preset;

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
            case PluginFormat::LV2:
                return "LV2";
            case PluginFormat::Internal:
                return "Internal";
            default:
                return "Unknown";
        }
    }

    // Whether this device opens a separate floating editor window. External
    // plugins (VST/AU) carry their own editor; internal MAGDA devices render
    // their UI inline in the chain and have no floating window. The engine's
    // plugin->hasEditor() is the final guard when the window is actually opened.
    bool hasEditorWindow() const {
        return format != PluginFormat::Internal;
    }
};

}  // namespace magda
