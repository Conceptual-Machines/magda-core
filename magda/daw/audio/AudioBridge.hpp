#pragma once

#include <tracktion_engine/tracktion_engine.h>

#include <functional>
#include <map>
#include <memory>

#include "../core/ClipManager.hpp"
#include "../core/DeviceInfo.hpp"
#include "../core/TrackManager.hpp"
#include "../core/TypeIds.hpp"
#include "ClipSynchronizer.hpp"
#include "DeviceProcessor.hpp"
#include "MeteringBuffer.hpp"
#include "MidiActivityMonitor.hpp"
#include "ParameterManager.hpp"
#include "ParameterQueue.hpp"
#include "PluginManager.hpp"
#include "PluginWindowBridge.hpp"
#include "TrackController.hpp"
#include "TransportStateManager.hpp"
#include "WarpMarkerManager.hpp"

namespace magda {

// Forward declarations
namespace te = tracktion;
class PluginWindowManager;
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
    void trackDevicesChanged(TrackId trackId) override;
    void devicePropertyChanged(DeviceId deviceId) override;
    void deviceParameterChanged(DeviceId deviceId, int paramIndex, float newValue) override;
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
    void launchSessionClip(ClipId clipId);

    /**
     * @brief Stop a session clip via its LaunchHandle (lock-free, no graph rebuild)
     * @param clipId The MAGDA clip ID
     */
    void stopSessionClip(ClipId clipId);

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

    // =========================================================================
    // Transient Detection
    // =========================================================================

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
     * @brief Get the Tracktion Plugin for a MAGDA device
     * @param deviceId MAGDA device ID
     * @return The Plugin, or nullptr if not found
     */
    te::Plugin::Ptr getPlugin(DeviceId deviceId) const;

    /**
     * @brief Get the DeviceProcessor for a MAGDA device
     * @param deviceId MAGDA device ID
     * @return The DeviceProcessor, or nullptr if not found
     */
    DeviceProcessor* getDeviceProcessor(DeviceId deviceId) const;

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
    bool pushParameterChange(DeviceId deviceId, int paramIndex, float value);

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
     * @brief Trigger MIDI activity for a track (audio thread safe)
     * @param trackId The track that received MIDI
     */
    void triggerMidiActivity(TrackId trackId) {
        midiActivity_.triggerActivity(trackId);
    }

    /**
     * @brief Check and clear MIDI activity flag for a track (UI thread)
     * @param trackId The track to check
     * @return true if MIDI activity occurred since last check
     */
    bool consumeMidiActivity(TrackId trackId) {
        return midiActivity_.consumeActivity(trackId);
    }

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
     * @brief Get current MIDI input source for a track
     * @param trackId The MAGDA track ID
     * @return MIDI device ID, or empty if none
     */
    juce::String getTrackMidiInput(TrackId trackId) const;

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
     * @brief Show the plugin's native editor window
     * @param deviceId MAGDA device ID of the plugin
     */
    void showPluginWindow(DeviceId deviceId);

    /**
     * @brief Hide/close the plugin's native editor window
     * @param deviceId MAGDA device ID of the plugin
     */
    void hidePluginWindow(DeviceId deviceId);

    /**
     * @brief Check if a plugin window is currently open
     * @param deviceId MAGDA device ID of the plugin
     * @return true if the plugin window is visible
     */
    bool isPluginWindowOpen(DeviceId deviceId) const;

    /**
     * @brief Toggle the plugin's native editor window (open if closed, close if open)
     * @param deviceId MAGDA device ID of the plugin
     * @return true if the window is now open, false if now closed
     */
    bool togglePluginWindow(DeviceId deviceId);

  private:
    // Timer callback for metering updates (runs on message thread)
    void timerCallback() override;

    // Create track mapping
    void ensureTrackMapping(TrackId trackId);

    // Plugin creation helpers
    te::Plugin::Ptr createToneGenerator(te::AudioTrack* track);
    // Note: createVolumeAndPan removed - track volume is separate infrastructure
    te::Plugin::Ptr createLevelMeter(te::AudioTrack* track);
    te::Plugin::Ptr createFourOscSynth(te::AudioTrack* track);

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
    ClipSynchronizer clipSynchronizer_;

    // Master channel metering (lock-free atomics for thread safety)
    std::atomic<float> masterPeakL_{0.0f};
    std::atomic<float> masterPeakR_{0.0f};
    te::LevelMeasurer::Client masterMeterClient_;
    bool masterMeterRegistered_{false};  // Whether master meter client is registered

    // Synchronization
    mutable juce::CriticalSection
        mappingLock_;  // Protects mapping updates (mutable for const getters)

    // Pending MIDI routes (applied when playback context becomes available)
    std::vector<std::pair<TrackId, juce::String>> pendingMidiRoutes_;
    void applyPendingMidiRoutes();

    // Engine wrapper (owns this AudioBridge, used for ClipInterface access)
    TracktionEngineWrapper* engineWrapper_ = nullptr;

    // Shutdown flag to prevent operations during cleanup
    std::atomic<bool> isShuttingDown_{false};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioBridge)
};

}  // namespace magda
