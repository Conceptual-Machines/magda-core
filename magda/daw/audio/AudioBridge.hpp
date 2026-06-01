#pragma once

#include <tracktion_engine/tracktion_engine.h>

#include <functional>
#include <map>
#include <memory>

#include "../core/ClipManager.hpp"
#include "../core/ControlTarget.hpp"
#include "../core/DeviceInfo.hpp"
#include "../core/TrackManager.hpp"
#include "../core/TypeIds.hpp"
#include "AudioBridgeMixer.hpp"
#include "DeviceMeteringManager.hpp"
#include "MeteringBuffer.hpp"
#include "PluginWindowBridge.hpp"
#include "TrackController.hpp"
#include "WarpMarkerManager.hpp"
#include "automation/AutomationPlaybackEngine.hpp"
#include "automation/AutomationRecordingEngine.hpp"
#include "automation/ControlTargetResolver.hpp"
#include "midi/MidiActivityMonitor.hpp"
#include "midi/MidiInputRouter.hpp"
#include "params/ParameterManager.hpp"
#include "params/ParameterQueue.hpp"
#include "plugin_manager/PluginManager.hpp"
#include "processors/base/DeviceProcessor.hpp"
#include "sampling/SamplerFileLoader.hpp"
#include "session/ClipSynchronizer.hpp"
#include "session/SessionClipAudioMonitor.hpp"
#include "sidechain/SidechainRoutingManager.hpp"
#include "transport/TransportStateManager.hpp"

namespace magda {

// Forward declarations
namespace te = tracktion;
class PluginWindowManager;
class SessionMonitorPlugin;
class TracktionEngineWrapper;

/**
 * @brief Bridges TrackManager and ClipManager (UI models) to Tracktion Engine (audio processing)
 *
 * Responsibilities:
 * - Listens to TrackManager for device changes
 * - Listens to ClipManager for clip changes
 * - Maps DeviceId to tracktion::Plugin instances
 * - Maps TrackId to tracktion::AudioTrack instances
 * - Maps ClipId to tracktion::Clip instances
 * - Loads built-in and external plugins
 * - Manages metering and parameter communication
 *
 * Thread Safety:
 * - UI thread: Receives TrackManager/ClipManager notifications, updates mappings
 * - Audio thread: Reads mappings, processes parameter changes, pushes metering
 */
class AudioBridge : public TrackManagerListener, public ClipManagerListener, public juce::Timer {
  public:
    /**
     * @brief Construct AudioBridge with Tracktion Engine references
     * @param engine Reference to the Tracktion Engine instance
     * @param edit Reference to the current Edit (project)
     */
    AudioBridge(te::Engine& engine, te::Edit& edit);
    ~AudioBridge() override;

    // =========================================================================
    // TrackManagerListener implementation
    // =========================================================================

    void tracksChanged() override;
    void trackPropertyChanged(int trackId) override;
    void trackSelectionChanged(TrackId trackId) override;
    void trackDevicesChanged(TrackId trackId) override;
    void deviceModifiersChanged(TrackId trackId) override;
    void audioSidechainTriggered(TrackId sourceTrackId) override;
    void devicePropertyChanged(const ChainNodePath& devicePath) override;
    void deviceParameterChanged(const ChainNodePath& devicePath, int paramIndex,
                                float newValue) override;
    void macroValueChanged(TrackId trackId, ChainScope scope, int ownerId, int macroIndex,
                           float value) override;
    void modParameterChanged(TrackId trackId, const ChainNodePath& devicePath, ModId modId,
                             int paramIndex, float value) override;
    void masterChannelChanged() override;

    // =========================================================================
    // ClipManagerListener implementation
    // =========================================================================

    void clipsChanged() override;
    void clipPropertyChanged(ClipId clipId) override;
    void clipSelectionChanged(ClipId clipId) override;

    // =========================================================================
    // Clip Synchronization (Arrangement)
    // =========================================================================

    /**
     * @brief Sync a single arrangement clip to Tracktion Engine
     * @param clipId The MAGDA clip ID to sync
     */
    void syncClipToEngine(ClipId clipId);

    /**
     * @brief Remove an arrangement clip from Tracktion Engine
     * @param clipId The MAGDA clip ID to remove
     */
    void removeClipFromEngine(ClipId clipId);

    // =========================================================================
    // Session Clip Lifecycle (slot-based, managed by SessionClipScheduler)
    // =========================================================================

    /**
     * @brief Sync a session clip to its corresponding ClipSlot in Tracktion Engine
     *
     * Creates the TE clip and moves it into the appropriate ClipSlot based on
     * the clip's trackId and sceneIndex. Idempotent — skips if slot already has a clip.
     * @param clipId The MAGDA clip ID
     * @return true if a new clip was created and moved into the slot
     */
    bool syncSessionClipToSlot(ClipId clipId);

    /**
     * @brief Remove a session clip from its ClipSlot
     * @param clipId The MAGDA clip ID
     */
    void removeSessionClipFromSlot(ClipId clipId);

    /**
     * @brief Launch a session clip via its LaunchHandle (lock-free, no graph rebuild)
     * @param clipId The MAGDA clip ID
     */
    void launchSessionClip(ClipId clipId, bool forceImmediate = false);

    /**
     * @brief Stop a session clip via its LaunchHandle (lock-free, no graph rebuild)
     * @param clipId The MAGDA clip ID
     */
    void stopSessionClip(ClipId clipId);

    /** Stop a session clip at the next quantization grid point. */
    void stopSessionClipQueued(ClipId clipId, LaunchQuantize quantize);

    /**
     * @brief Get the precise quantized launch time for a track's last-launched session clip.
     * @param trackId The track to query
     * @return Time in seconds, or 0.0 if no launch recorded
     */
    double getLastLaunchTimeForTrack(TrackId trackId) const {
        return clipSynchronizer_.getLastLaunchTimeForTrack(trackId);
    }

    /**
     * @brief Reset all synth plugins on a track to prevent stuck notes
     * @param trackId The MAGDA track ID
     *
     * Iterates through the track's plugin list and calls reset() on any
     * synth plugin, which triggers allNotesOff(). Use after stopping
     * recording or session clip playback.
     */
    void resetSynthsOnTrack(TrackId trackId);

    /**
     * @brief Get the TE clip from a session clip's ClipSlot
     * @param clipId The MAGDA clip ID
     * @return The TE Clip pointer, or nullptr if not found
     */
    te::Clip* getSessionTeClip(ClipId clipId);

    /**
     * @brief Get the TE clip for an arrangement clip
     * @param clipId The MAGDA clip ID
     * @return The TE Clip pointer, or nullptr if not found
     */
    te::Clip* getArrangementTeClip(ClipId clipId) const;

    /** Get the audio-thread session clip monitor (for SessionClipScheduler). */
    SessionClipAudioMonitor& getSessionAudioMonitor() {
        return sessionAudioMonitor_;
    }
    const SessionClipAudioMonitor& getSessionAudioMonitor() const {
        return sessionAudioMonitor_;
    }

    /** Ensure the SessionMonitorPlugin is installed on a track for audio-thread monitoring. */
    void ensureSessionMonitorPlugin();

    // =========================================================================
    // Transient Detection
    // =========================================================================

    /**
     * @brief Set transient detection sensitivity and re-run detection
     * @param clipId The MAGDA clip ID
     * @param sensitivity Sensitivity value (0.0 to 1.0)
     */
    void setTransientSensitivity(ClipId clipId, float sensitivity);

    /**
     * @brief Detect transient times for an audio clip's source file
     *
     * On first call, kicks off async transient detection via TE's WarpTimeManager.
     * Subsequent calls poll for completion. Results are cached per file path.
     *
     * @param clipId The MAGDA clip ID (must be an audio clip)
     * @return true if transients are ready (cached), false if still detecting
     */
    bool getTransientTimes(ClipId clipId);

    // =========================================================================
    // Warp Markers
    // =========================================================================

    /** Enable warping: populate WarpTimeManager with markers at detected transients */
    void enableWarp(ClipId clipId);

    /** Disable warping: remove all warp markers */
    void disableWarp(ClipId clipId);

    /** Get current warp marker positions for display */
    std::vector<WarpMarkerInfo> getWarpMarkers(ClipId clipId);

    /** Add a warp marker. Returns index of inserted marker. */
    int addWarpMarker(ClipId clipId, double sourceTime, double warpTime);

    /** Move a warp marker's warp time. Returns actual position (clamped by TE). */
    double moveWarpMarker(ClipId clipId, int index, double newWarpTime);

    /** Remove a warp marker at index. */
    void removeWarpMarker(ClipId clipId, int index);

    // =========================================================================
    // Plugin State Capture
    // =========================================================================

    /**
     * @brief Capture native state from all loaded external plugins into DeviceInfo
     * Call before saving a project to snapshot live plugin states.
     */
    void captureAllPluginStates();

    /**
     * @brief Capture warp marker positions from TE into ClipInfo for all warped clips.
     */
    void captureWarpMarkerStates();

    // =========================================================================
    // Plugin Loading
    // =========================================================================

    /**
     * @brief Load a built-in Tracktion plugin
     * @param trackId The MAGDA track ID
     * @param type Plugin type (e.g., "tone", "volume", "delay", "reverb")
     * @return The loaded plugin, or nullptr on failure
     */
    te::Plugin::Ptr loadBuiltInPlugin(TrackId trackId, const juce::String& type);

    /**
     * @brief Load an external plugin (VST3, AU)
     * @param trackId The MAGDA track ID
     * @param description Plugin description from plugin scan
     * @return PluginLoadResult with success status, error message, and plugin pointer
     */
    PluginLoadResult loadExternalPlugin(TrackId trackId,
                                        const juce::PluginDescription& description);

    /**
     * @brief Callback invoked when a plugin fails to load
     * Parameters: deviceId, error message
     */
    std::function<void(DeviceId, const juce::String&)> onPluginLoadFailed;

    /**
     * @brief Add a level meter plugin to a track for metering
     * @param trackId The MAGDA track ID
     * @return The level meter plugin
     */
    te::Plugin::Ptr addLevelMeterToTrack(TrackId trackId);

    /**
     * @brief Ensure VolumeAndPanPlugin is at the correct position (near end of chain)
     * @param track The Tracktion Engine audio track
     */
    void ensureVolumePluginPosition(te::AudioTrack* track) const;

    // =========================================================================
    // Track Mapping
    // =========================================================================

    /**
     * @brief Get the Tracktion AudioTrack for a MAGDA track
     * @param trackId MAGDA track ID
     * @return The AudioTrack, or nullptr if not found
     */
    te::AudioTrack* getAudioTrack(TrackId trackId) const;

    /**
     * @brief Get the PluginManager (for InstrumentRackManager access)
     */
    PluginManager& getPluginManager() {
        return pluginManager_;
    }

    /**
     * @brief Path-based lookup — preferred for new code. Resolves the live TE
     * plugin behind the device referenced by `devicePath`. Sections are
     * walked in the same way getDeviceInChainByPath() walks them.
     */
    te::Plugin::Ptr getPlugin(const ChainNodePath& devicePath) const;

    /**
     * @brief Convenience: resolve both the MAGDA DeviceInfo and the live TE
     * plugin from a single path. Avoids two independent chain walks for
     * callers (UI mainly) that need both. Either field may be nullptr if
     * the device or its plugin isn't currently materialised.
     */
    struct ResolvedDevice {
        DeviceInfo* info = nullptr;
        te::Plugin::Ptr plugin;
    };
    ResolvedDevice resolveDevice(const ChainNodePath& devicePath) const;

    // Path-based variants of the plugin-window methods.
    void showPluginWindow(const ChainNodePath& devicePath);
    void hidePluginWindow(const ChainNodePath& devicePath);
    bool isPluginWindowOpen(const ChainNodePath& devicePath) const;
    bool togglePluginWindow(const ChainNodePath& devicePath);

    /**
     * @brief Resolve any ControlTarget to its writable te::AutomatableParameter.
     *
     * Single dispatch site for the unified parameter addressing scheme — used
     * by AutomationPlaybackEngine, ModifierSync, and any other consumer that
     * needs to write into a TE-side parameter from a MAGDA-side address.
     * Returns nullptr if the target's path / index doesn't resolve to a live TE
     * parameter (device removed, plugin gone, etc.).
     */
    te::AutomatableParameter* resolveControlTarget(const ControlTarget& target) const;

    DeviceProcessor* getDeviceProcessor(const ChainNodePath& devicePath) const;

    // ------------------------------------------------------------------------
    // VST/AU plugin program (factory preset) access for hosted external plugins.
    // All return zero/empty for non-external (internal/MAGDA) devices.
    // ------------------------------------------------------------------------
    // Path-based overloads — new code uses these so the section is part of
    // the lookup.
    int getPluginNumPrograms(const ChainNodePath& devicePath) const;
    int getPluginCurrentProgram(const ChainNodePath& devicePath) const;
    juce::String getPluginProgramName(const ChainNodePath& devicePath, int programIndex) const;
    bool setPluginCurrentProgram(const ChainNodePath& devicePath, int programIndex);

    // ------------------------------------------------------------------------
    // Disk-based plugin preset loading / saving (.vstpreset / .aupreset).
    //
    // VST3: routed through JUCE's ExtensionsVisitor::VST3Client which feeds
    //       raw .vstpreset bytes to IComponent::setState / IEditController::setState.
    // AU:   uses AudioPluginInstance::setCurrentProgramStateInformation, which
    //       parses the .aupreset plist and calls
    //       AudioUnitSetProperty(kAudioUnitProperty_ClassInfo).
    // ------------------------------------------------------------------------
    bool loadPluginPresetFile(const ChainNodePath& devicePath, const juce::File& presetFile);
    bool savePluginPresetFile(const ChainNodePath& devicePath, const juce::File& presetFile);

    /**
     * @brief Get (or lazily create) the virtual MIDI input device used by
     *        the QWERTY keyboard. Returns nullptr if creation fails.
     */
    te::VirtualMidiInputDevice* getQwertyMidiDevice();

    /**
     * @brief Create a Tracktion AudioTrack for a MAGDA track
     * @param trackId MAGDA track ID
     * @param name Track name
     * @return The created AudioTrack
     */
    te::AudioTrack* createAudioTrack(TrackId trackId, const juce::String& name);

    /**
     * @brief Remove a Tracktion track
     * @param trackId MAGDA track ID
     */
    void removeAudioTrack(TrackId trackId);

    // =========================================================================
    // Metering
    // =========================================================================

    /**
     * @brief Get the metering buffer for reading levels in UI
     */
    MeteringBuffer& getMeteringBuffer() {
        return meteringBuffer_;
    }
    const MeteringBuffer& getMeteringBuffer() const {
        return meteringBuffer_;
    }

    /**
     * @brief Get the dedicated recording metering buffer
     *
     * Separate from the main metering buffer so that UI meter consumers
     * (TrackHeadersPanel, MixerView) don't drain data before the recording
     * preview can read it.
     */
    MeteringBuffer& getRecordingMeteringBuffer() {
        return recordingMeteringBuffer_;
    }

    // =========================================================================
    // Parameter Queue
    // =========================================================================

    /**
     * @brief Get the parameter queue for pushing changes from UI
     */
    ParameterQueue& getParameterQueue() {
        return parameterManager_.getQueue();
    }

    /**
     * @brief Push a parameter change to the audio thread
     */
    bool pushParameterChange(const ChainNodePath& devicePath, int paramIndex, float value);

    // =========================================================================
    // Synchronization
    // =========================================================================

    /**
     * @brief Sync all tracks and devices to Tracktion Engine
     * Call this after initial setup or major state changes
     */
    void syncAll();

    /**
     * @brief Sync a single track's devices to Tracktion Engine
     */
    void syncTrackPlugins(TrackId trackId);

    /**
     * @brief Push TrackInfo::recordArmed onto TE's InputDeviceInstance destinations.
     *
     * Called from trackPropertyChanged (arm toggle) and from tracksChanged
     * (post-load, add/remove/reorder). Post-load is the critical path: a
     * project saved with recordArmed=true restores TrackInfo with the flag
     * already true, so trackPropertyChanged doesn't fire — and without this
     * call TE's destinations stay at recordEnabled=N, silently swallowing
     * record attempts.
     */
    void syncRecordArmedToTE(TrackId trackId);

    /**
     * @brief Sync every armed track to TE.
     *
     * Belt-and-braces companion to syncRecordArmedToTE — called right before
     * transport.record() to guarantee destinations are current regardless of
     * when the playback context became available during project load.
     */
    void syncAllArmedTracksToTE();

    // =========================================================================
    // Audio Callback Support
    // =========================================================================

    /**
     * @brief Process pending parameter changes (call from audio thread)
     */
    void processParameterChanges();

    /**
     * @brief Update metering from level measurers (call from audio thread)
     */
    void updateMetering();

    // =========================================================================
    // Transport State (for trigger sync)
    // =========================================================================

    /**
     * @brief Update transport state from UI thread (called by TracktionEngineWrapper)
     * @param isPlaying Current transport playing state
     * @param justStarted True if transport just started this frame
     * @param justLooped True if transport just looped this frame
     */
    void updateTransportState(bool isPlaying, bool justStarted, bool justLooped);

    /**
     * @brief Get current transport playing state (audio thread safe)
     */
    bool isTransportPlaying() const {
        return transportState_.isPlaying();
    }

    /**
     * @brief Get just-started flag (audio thread safe)
     */
    bool didJustStart() const {
        return transportState_.didJustStart();
    }

    /**
     * @brief Get just-looped flag (audio thread safe)
     */
    bool didJustLoop() const {
        return transportState_.didJustLoop();
    }

    // =========================================================================
    // MIDI Activity Monitoring
    // =========================================================================

    /**
     * @brief Trigger MIDI activity for a track (MIDI thread safe)
     * @param trackId The track that received MIDI
     */
    void triggerMidiActivity(TrackId trackId) {
        midiActivity_.triggerActivity(trackId);
        // Write to sidechain trigger bus so updateAllMods() picks up live MIDI too.
        sidechainRouting_.triggerMidiActivity(trackId);
        // LFO retrigger is handled on the audio thread by SidechainMonitorPlugin
        // (which calls PluginManager::triggerSidechainNoteOn). Calling it here
        // from the MIDI thread would double-trigger and race with the audio
        // thread's ramp state (non-atomic floats in TE's Ramp).
    }

    /**
     * @brief Get the monotonic MIDI activity counter for a track (UI thread)
     * @param trackId The track to check
     * @return Counter value — compare with previous to detect new activity
     */
    uint32_t getMidiActivityCounter(TrackId trackId) const {
        return midiActivity_.getActivityCounter(trackId);
    }

    // =========================================================================
    // Automation Recording
    // =========================================================================

    /**
     * @brief Enable/disable global automation write mode
     * @param enabled When true, parameter changes during playback are recorded to armed lanes
     */
    void setAutomationWriteEnabled(bool enabled);

    /**
     * @brief Check if automation write mode is enabled
     */
    bool isAutomationWriteEnabled() const;

    /**
     * @brief Set the active automation mode (Off / Write / Touch / Latch).
     *
     * Off disarms recording. Write records any user-driven change while transport
     * rolls. Touch records only while a control is held. Latch records while held
     * and continues writing the held value after release until the transport stops.
     */
    void setAutomationMode(AutomationMode mode);
    AutomationMode getAutomationMode() const;

    // =========================================================================
    // Mixer Controls
    // =========================================================================

    /**
     * @brief Set track volume (linear gain, 0.0-2.0)
     * @param trackId MAGDA track ID
     * @param volume Linear gain (0.0 = silence, 1.0 = unity, 2.0 = +6dB)
     */
    void setTrackVolume(TrackId trackId, float volume);

    /**
     * @brief Get track volume (linear gain)
     * @param trackId MAGDA track ID
     * @return Linear gain value
     */
    float getTrackVolume(TrackId trackId) const;

    /**
     * @brief Set track pan
     * @param trackId MAGDA track ID
     * @param pan Pan position (-1.0 = full left, 0.0 = center, 1.0 = full right)
     */
    void setTrackPan(TrackId trackId, float pan);

    /**
     * @brief Get track pan
     * @param trackId MAGDA track ID
     * @return Pan position
     */
    float getTrackPan(TrackId trackId) const;

    /**
     * @brief Set master volume (linear gain)
     * @param volume Linear gain (0.0 = silence, 1.0 = unity, 2.0 = +6dB)
     */
    void setMasterVolume(float volume);

    /**
     * @brief Get master volume (linear gain)
     * @return Linear gain value
     */
    float getMasterVolume() const;

    /**
     * @brief Set master pan
     * @param pan Pan position (-1.0 to 1.0)
     */
    void setMasterPan(float pan);

    /**
     * @brief Get master pan
     * @return Pan position
     */
    float getMasterPan() const;

    // =========================================================================
    // Master Metering
    // =========================================================================

    /**
     * @brief Get the per-device metering manager
     */
    DeviceMeteringManager& getDeviceMetering() {
        return deviceMetering_;
    }
    const DeviceMeteringManager& getDeviceMetering() const {
        return deviceMetering_;
    }

    /**
     * @brief Get master channel peak level (left)
     * @return Peak level as linear gain
     */
    float getMasterPeakL() const {
        return masterPeakL_.load(std::memory_order_relaxed);
    }

    /**
     * @brief Get master channel peak level (right)
     * @return Peak level as linear gain
     */
    float getMasterPeakR() const {
        return masterPeakR_.load(std::memory_order_relaxed);
    }

    // =========================================================================
    // Audio Routing
    // =========================================================================

    /**
     * @brief Get a bitmask of user-enabled input channels from TE WaveInputDevices
     *
     * JUCE device->getActiveInputChannels() always returns all channels (because
     * TracktionEngineWrapper enables all at JUCE level). User preferences are applied
     * at the TE WaveInputDevice level. This method reads those TE-level enabled states.
     *
     * @return BigInteger with bits set for each enabled input channel
     */
    juce::BigInteger getEnabledInputChannels() const;

    /** Map from hardware channel index to TE WaveInputDevice name (e.g. "Input 1"). */
    std::map<int, juce::String> getInputDeviceNamesByChannel() const;

    /**
     * @brief Get a bitmask of user-enabled output channels from TE WaveOutputDevices
     * @return BigInteger with bits set for each enabled output channel
     */
    juce::BigInteger getEnabledOutputChannels() const;

    /**
     * @brief Set audio output destination for a track
     * @param trackId The MAGDA track ID
     * @param destination Output destination: "master" for default, deviceID for specific output,
     * empty to disable
     */
    void setTrackAudioOutput(TrackId trackId, const juce::String& destination);

    /**
     * @brief Set audio input source for a track
     * @param trackId The MAGDA track ID
     * @param deviceId Input device ID, "default" for default input, empty to disable
     */
    void setTrackAudioInput(TrackId trackId, const juce::String& deviceId);

    /**
     * @brief Get current audio output destination for a track
     * @return Output destination string
     */
    juce::String getTrackAudioOutput(TrackId trackId) const;

    /**
     * @brief Get current audio input source for a track
     * @return Input device ID
     */
    juce::String getTrackAudioInput(TrackId trackId) const;

    bool setSessionSlotAudioRecordingTarget(TrackId trackId, int sceneIndex, bool enabled);

    // =========================================================================
    // MIDI Routing (for live instrument playback)
    // =========================================================================

    /**
     * @brief Set MIDI input source for a track (routes through Tracktion Engine)
     * @param trackId The MAGDA track ID
     * @param midiDeviceId MIDI device identifier, "all" for all inputs, empty to disable
     *
     * This routes MIDI through Tracktion Engine's input device system,
     * allowing instrument plugins to receive live MIDI input.
     */
    void setTrackMidiInput(TrackId trackId, const juce::String& midiDeviceId);

    /**
     * @brief Mark one MIDI input as control-surface-only.
     *
     * Surface-only inputs remain available to raw MIDI listeners (Lua scripts,
     * controller routing, monitors) but are excluded from Tracktion Engine live
     * track input routing, including "all" routing. Empty clears the current
     * surface-only input.
     */
    void setSurfaceOnlyMidiInputPort(const juce::String& midiDeviceIdOrName);
    void clearSurfaceOnlyMidiInputPorts();

    /**
     * @brief Get current MIDI input source for a track
     * @param trackId The MAGDA track ID
     * @return MIDI device ID, or empty if none
     */
    juce::String getTrackMidiInput(TrackId trackId) const;

    bool setSessionSlotMidiRecordingTarget(TrackId trackId, int sceneIndex, bool enabled);

    /**
     * @brief Set record arm state on the TE InputDeviceInstance for a track
     * @param trackId The MAGDA track ID
     * @param armed True to enable recording, false to disable
     */
    void setTrackRecordArmed(TrackId trackId, bool armed);

    /**
     * @brief Reverse-lookup MAGDA TrackId from a TE track's EditItemID
     * @param itemId The TE EditItemID
     * @return The MAGDA TrackId, or INVALID_TRACK_ID if not found
     */
    TrackId getTrackIdForTeTrack(te::EditItemID itemId) const;

    /**
     * @brief Enable all MIDI input devices in Tracktion Engine's DeviceManager
     *
     * Must be called before MIDI routing will work. This enables the devices
     * at the engine level - track routing is done via setTrackMidiInput().
     */
    void enableAllMidiInputDevices();

    /**
     * @brief Called when MIDI input devices become available
     *
     * This is called by TracktionEngineWrapper when the DeviceManager
     * creates MIDI input device wrappers (which happens asynchronously).
     * Any pending MIDI routes will be applied.
     */
    void onMidiDevicesAvailable();

    // =========================================================================
    // Plugin Window Manager
    // =========================================================================

    /**
     * @brief Set the plugin window manager (for delegation)
     * @param manager Pointer to PluginWindowManager (owned by TracktionEngineWrapper)
     */
    void setPluginWindowManager(PluginWindowManager* manager) {
        pluginWindowBridge_.setPluginWindowManager(manager);
    }

    /**
     * @brief Set the engine wrapper (for accessing ClipInterface methods)
     * @param wrapper Pointer to TracktionEngineWrapper (owns this AudioBridge)
     */
    void setEngineWrapper(TracktionEngineWrapper* wrapper) {
        engineWrapper_ = wrapper;
    }

    // =========================================================================
    // Plugin Editor Windows (delegates to PluginWindowManager)
    // =========================================================================

    /**
     * @brief Load a sample file into a MagdaSamplerPlugin device
     * @param devicePath MAGDA device path of the sampler plugin
     * @param file Audio file to load
     * @return true if sample was loaded successfully
     */
    bool loadSamplerSample(const ChainNodePath& devicePath, const juce::File& file);

  private:
    // Timer callback for metering updates (runs on message thread)
    void timerCallback() override;
    void refreshInputMeterClients(const std::map<TrackId, te::AudioTrack*>& trackMapping);

    // Create track mapping
    void ensureTrackMapping(TrackId trackId);

    // Convert DeviceInfo to plugin
    te::Plugin::Ptr loadDeviceAsPlugin(TrackId trackId, const DeviceInfo& device);

    // References to Tracktion Engine (not owned)
    te::Engine& engine_;
    te::Edit& edit_;

    // Bidirectional mappings
    std::map<TrackId, std::string> trackIdToEngineId_;  // MAGDA TrackId → Engine string ID

    // (Session clips use ClipSlot-based mapping via trackId + sceneIndex — no ID maps needed)

    // Lock-free communication buffers
    MeteringBuffer meteringBuffer_;
    MeteringBuffer recordingMeteringBuffer_;

    // Phase 1 refactoring: Pure data managers (extracted from AudioBridge)
    TransportStateManager transportState_;
    MidiActivityMonitor midiActivity_;
    ParameterManager parameterManager_;

    // Phase 2 refactoring: Independent features (extracted from AudioBridge)
    PluginWindowBridge pluginWindowBridge_;
    WarpMarkerManager warpMarkerManager_;

    // Phase 3 refactoring: Core controllers (extracted from AudioBridge)
    TrackController trackController_;
    PluginManager pluginManager_;
    AudioBridgeMixer mixer_;
    MidiInputRouter midiInputRouter_;
    ControlTargetResolver controlTargetResolver_;
    SidechainRoutingManager sidechainRouting_;
    SamplerFileLoader samplerFileLoader_;
    ClipSynchronizer clipSynchronizer_;
    SessionClipAudioMonitor sessionAudioMonitor_;
    SessionMonitorPlugin* sessionMonitorPlugin_ = nullptr;

    // Automation playback (samples curves at 30Hz, applies to parameters)
    AutomationPlaybackEngine automationPlayback_;

    // Automation recording (writes parameter changes to armed lanes during playback)
    AutomationRecordingEngine automationRecording_;

    // Per-device metering (LevelMeasurer per device, polled on timer)
    DeviceMeteringManager deviceMetering_;

    // Master channel metering (lock-free atomics for thread safety)
    std::atomic<float> masterPeakL_{0.0f};
    std::atomic<float> masterPeakR_{0.0f};
    te::LevelMeasurer::Client masterMeterClient_;
    bool masterMeterRegistered_{false};  // Whether master meter client is registered

    struct InputMeterClientEntry {
        te::LevelMeasurer::Client client;
        te::LevelMeasurer* measurer = nullptr;
    };
    std::map<TrackId, InputMeterClientEntry> inputMeterClients_;

    // Synchronization
    mutable juce::CriticalSection
        mappingLock_;  // Protects mapping updates (mutable for const getters)

    void updateMidiRoutingForSelection();
    void resyncAllInputMonitors();

    void applyPendingMidiRoutes();

    // Engine wrapper (owns this AudioBridge, used for ClipInterface access)
    TracktionEngineWrapper* engineWrapper_ = nullptr;

    // Shutdown flag to prevent operations during cleanup
    std::atomic<bool> isShuttingDown_{false};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioBridge)
};

}  // namespace magda
