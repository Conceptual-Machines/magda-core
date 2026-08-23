#pragma once

#include <juce_core/juce_core.h>
#include <juce_graphics/juce_graphics.h>

#include <optional>

#include "ClipTypes.hpp"
#include "RackInfo.hpp"
#include "TrackChain.hpp"
#include "TrackTypes.hpp"
#include "TrackViewSettings.hpp"

namespace magda {

/**
 * @brief Links a MultiOut track back to its source instrument
 */
struct MultiOutTrackLink {
    TrackId sourceTrackId = INVALID_TRACK_ID;     // Parent track hosting the instrument
    DeviceId sourceDeviceId = INVALID_DEVICE_ID;  // The multi-out instrument device
    int outputPairIndex = 0;                      // Which stereo pair (0 = main, 1 = 3-4, etc.)
};

/**
 * @brief Describes a send from this track to an aux track
 */
struct SendInfo {
    int busIndex = 0;                        // TE aux bus index
    float level = 1.0f;                      // Send level (0.0 - 1.0)
    bool preFader = false;                   // Pre/post fader
    TrackId destTrackId = INVALID_TRACK_ID;  // Target aux track (for display)
};

enum class InputMonitorMode { Off, In, Auto };

enum class TrackPlaybackMode { Arrangement, Session };

/**
 * @brief Track data structure containing all track properties
 */
struct TrackInfo {
    TrackId id = INVALID_TRACK_ID;      // Unique identifier
    TrackType type = TrackType::Audio;  // Track type
    juce::String name;                  // Track name
    juce::Colour colour;                // Track color

    // Hierarchy
    TrackId parentId = INVALID_TRACK_ID;  // Parent track (for grouped tracks)
    std::vector<TrackId> childIds;        // Child tracks (for groups)

    // Mixer state
    float volume = 1.0f;  // Volume level (0-1), default is unity gain (0dB)
    float pan = 0.0f;     // Pan position (-1 to 1)
    // User-set fader positions — updated only by explicit user gestures, never by
    // automation playback writes. Used as the recording seed so that
    // seedBaselines() captures the intended position even when automation has
    // been driving track->volume during a prior playback session.
    float manualVolume = 1.0f;
    float manualPan = 0.0f;
    bool muted = false;
    bool soloed = false;
    bool recordArmed = false;
    InputMonitorMode inputMonitor = InputMonitorMode::Off;
    bool frozen = false;  // Track is frozen (rendered to audio, plugins disabled)
    TrackPlaybackMode playbackMode = TrackPlaybackMode::Arrangement;

    // Mixer UI layout. Zero/negative channel width means "use default"; fader
    // inset is the empty space above the fader and therefore controls fader height.
    int mixerChannelWidth = 0;
    int mixerFaderTopInset = 0;

    // The session clip currently active on this track. Set on launch, cleared on
    // explicit stop.  Transport stop/start does not touch this — it is the
    // single source of truth for which clip to re-launch on transport resume.
    ClipId activeSessionClipId = INVALID_CLIP_ID;

    // Routing
    juce::String midiInputDevice;    // MIDI input device ID ("all", device ID, or empty for none)
    juce::String midiOutputDevice;   // MIDI output device ID (device ID or empty for none)
    juce::String audioInputDevice;   // Audio input device/channel (device ID or empty for none)
    juce::String audioOutputDevice;  // Audio output routing (default: "master")

    // Sends (to aux tracks)
    std::vector<SendInfo> sends;

    // Aux bus index (assigned when type == Aux, used for AuxReturn/AuxSend bus matching)
    int auxBusIndex = -1;

    // Multi-output link (set when type == MultiOut)
    std::optional<MultiOutTrackLink> multiOutLink;

    // Signal chain: main (pre-fader) FX tree + post-fader FX list. The track
    // fader (VolumeAndPan) sits structurally between the two segments.
    TrackChain chain;

    // View settings per view mode
    TrackViewSettingsMap viewSettings;

    // Track-level modulators and macros (global: can target any device in the chain)
    ModArray mods = createDefaultMods(0);
    MacroArray macros = createDefaultMacros();

    // Track-level panel UI state
    bool globalModsPanelOpen = false;
    bool globalMacrosPanelOpen = false;
    int selectedGlobalModIndex = -1;
    int selectedGlobalMacroIndex = -1;

    // Default constructor
    TrackInfo() = default;

    // Move operations (default is fine)
    TrackInfo(TrackInfo&&) = default;
    TrackInfo& operator=(TrackInfo&&) = default;

    // Copy constructor - deep copies chainElements
    TrackInfo(const TrackInfo& other)
        : id(other.id),
          type(other.type),
          name(other.name),
          colour(other.colour),
          parentId(other.parentId),
          childIds(other.childIds),
          volume(other.volume),
          pan(other.pan),
          manualVolume(other.manualVolume),
          manualPan(other.manualPan),
          muted(other.muted),
          soloed(other.soloed),
          recordArmed(other.recordArmed),
          inputMonitor(other.inputMonitor),
          frozen(other.frozen),
          playbackMode(other.playbackMode),
          mixerChannelWidth(other.mixerChannelWidth),
          mixerFaderTopInset(other.mixerFaderTopInset),
          activeSessionClipId(other.activeSessionClipId),
          midiInputDevice(other.midiInputDevice),
          midiOutputDevice(other.midiOutputDevice),
          audioInputDevice(other.audioInputDevice),
          audioOutputDevice(other.audioOutputDevice),
          sends(other.sends),
          auxBusIndex(other.auxBusIndex),
          multiOutLink(other.multiOutLink),
          chain(other.chain),
          viewSettings(other.viewSettings),
          mods(other.mods),
          macros(other.macros),
          globalModsPanelOpen(other.globalModsPanelOpen),
          globalMacrosPanelOpen(other.globalMacrosPanelOpen),
          selectedGlobalModIndex(other.selectedGlobalModIndex),
          selectedGlobalMacroIndex(other.selectedGlobalMacroIndex) {}

    // Copy assignment - deep copies chainElements
    TrackInfo& operator=(const TrackInfo& other) {
        if (this != &other) {
            id = other.id;
            type = other.type;
            name = other.name;
            colour = other.colour;
            parentId = other.parentId;
            childIds = other.childIds;
            volume = other.volume;
            pan = other.pan;
            manualVolume = other.manualVolume;
            manualPan = other.manualPan;
            muted = other.muted;
            soloed = other.soloed;
            recordArmed = other.recordArmed;
            inputMonitor = other.inputMonitor;
            frozen = other.frozen;
            playbackMode = other.playbackMode;
            mixerChannelWidth = other.mixerChannelWidth;
            mixerFaderTopInset = other.mixerFaderTopInset;
            activeSessionClipId = other.activeSessionClipId;
            midiInputDevice = other.midiInputDevice;
            midiOutputDevice = other.midiOutputDevice;
            audioInputDevice = other.audioInputDevice;
            audioOutputDevice = other.audioOutputDevice;
            sends = other.sends;
            auxBusIndex = other.auxBusIndex;
            multiOutLink = other.multiOutLink;
            viewSettings = other.viewSettings;
            mods = other.mods;
            macros = other.macros;
            globalModsPanelOpen = other.globalModsPanelOpen;
            globalMacrosPanelOpen = other.globalMacrosPanelOpen;
            selectedGlobalModIndex = other.selectedGlobalModIndex;
            selectedGlobalMacroIndex = other.selectedGlobalMacroIndex;
            chain = other.chain;
        }
        return *this;
    }

    // Check if this track has an instrument device in its chain
    bool hasInstrument() const {
        for (const auto& element : chain.fxChainElements) {
            if (isDevice(element) && getDevice(element).isInstrument)
                return true;
            if (isRack(element)) {
                const auto& rack = getRack(element);
                for (const auto& chain : rack.chains) {
                    for (const auto& e : chain.elements) {
                        if (isDevice(e) && getDevice(e).isInstrument)
                            return true;
                    }
                }
            }
        }
        return false;
    }

    // Hierarchy helpers
    bool hasParent() const {
        return parentId != INVALID_TRACK_ID;
    }
    bool hasChildren() const {
        return !childIds.empty();
    }
    bool isGroup() const {
        return type == TrackType::Group;
    }
    bool isTopLevel() const {
        return parentId == INVALID_TRACK_ID;
    }

    // True for tracks that take external audio/MIDI input and can be recorded /
    // monitored. Aux send buses and Group summing tracks only pass signal from
    // elsewhere, so they never take external input. Single source of truth for
    // the input/record/monitor guards across TrackManager and MidiInputRouter.
    bool takesExternalInput() const {
        return type != TrackType::Aux && type != TrackType::Group;
    }

    // True for tracks that can host an instrument device. Aux/Group summing
    // buses and the Master track only process signal from elsewhere, so they
    // never host instruments. Single source of truth for the instrument-add
    // guards across TrackManager (track and rack-chain add paths).
    bool canHostInstrument() const {
        return type != TrackType::Aux && type != TrackType::Group && type != TrackType::Master;
    }

    // Whether clips can live on this track's timeline. Single source of truth
    // for every path that puts one there: a file drop, a clip drag, a keyboard
    // nudge.
    //
    // Asked of the track rather than of what is being placed, because a track
    // is hybrid -- TrackType::Audio is the regular track and it hosts audio
    // clips and MIDI clips alike. Nothing about a clip's kind decides where it
    // may land, and a call site that tested for one was how a Drum Grid track
    // came to refuse the .mid files it was the likeliest track to want
    // (#2172), while the identical clip could be dragged onto it from a
    // neighbour.
    //
    // What cannot host clips is structural. Group and Aux are buses with no
    // timeline of their own, Master has no lane, Chord is a singleton whose
    // clips are progressions rather than parts, and a MultiOut lane is owned by
    // a device's output pair -- deactivating that pair erases the track without
    // touching its clips, so anything parked there is orphaned.
    bool canHostClips() const {
        return type != TrackType::Group && type != TrackType::Aux && type != TrackType::Master &&
               type != TrackType::Chord && type != TrackType::MultiOut;
    }

    // Enforce the track-type invariants on this struct's own fields. Input-less
    // tracks (Aux send buses, Group summing tracks) only pass signal from
    // elsewhere: they take no external audio/MIDI input and so cannot be
    // monitored or record-armed. This is the single normalization boundary for
    // those rules. Aggregate entry points that accept or mutate whole track
    // state (create, restore, deserialize) call it so persistence and importers
    // stay mechanical and cannot let stale on-disk input state become live.
    void normalizeForType() {
        if (!takesExternalInput()) {
            recordArmed = false;
            inputMonitor = InputMonitorMode::Off;
            midiInputDevice = "";
            audioInputDevice = "";
        }
    }

    // MIDI input helpers
    //
    // Single source of truth for "does this track listen to live MIDI input".
    // Gated purely on the track's own monitor/arm state - NOT on selection - so
    // the TE routing (MidiInputRouter) and the UI activity light agree. A track
    // listens when input monitoring is enabled (In/Auto) or it is record-armed.
    bool receivesLiveMidiInput() const {
        return inputMonitor != InputMonitorMode::Off || recordArmed;
    }

    // View settings helpers
    bool isVisibleIn(ViewMode mode) const {
        return viewSettings.isVisible(mode);
    }
    bool isLockedIn(ViewMode mode) const {
        return viewSettings.isLocked(mode);
    }
    bool isCollapsedIn(ViewMode mode) const {
        return viewSettings.isCollapsed(mode);
    }
};

}  // namespace magda
