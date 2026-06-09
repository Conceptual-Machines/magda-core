#pragma once

#include <tracktion_engine/tracktion_engine.h>

#include <functional>
#include <thread>
#include <unordered_map>
#include <unordered_set>

#include "../audio/midi/RecordingNoteQueue.hpp"
#include "../command.hpp"
#include "../interfaces/clip_interface.hpp"
#include "../interfaces/mixer_interface.hpp"
#include "../interfaces/track_interface.hpp"
#include "../interfaces/transport_interface.hpp"
#include "AudioEngine.hpp"

namespace magda {

// Forward declarations
class AudioBridge;
class MagdaApi;
class MidiBridge;
class PluginScanCoordinator;
class PluginWindowManager;
class SessionClipScheduler;
class SessionRecorder;

/**
 * @brief Tracktion Engine implementation of AudioEngine
 *
 * This class bridges our command-based interface with the actual Tracktion Engine,
 * providing real audio functionality to our multi-agent DAW system.
 *
 * Inherits from AudioEngine (which includes AudioEngineListener) so it can:
 * - Be used as a generic audio engine
 * - Receive state change notifications from TimelineController
 */
class TracktionEngineWrapper : public AudioEngine,
                               public TransportInterface,
                               public TrackInterface,
                               public ClipInterface,
                               public MixerInterface,
                               public tracktion::TransportControl::Listener,
                               private juce::ChangeListener {
    struct SessionSlotRecordingTarget {
        int sceneIndex = -1;
        bool active = false;
    };

  public:
    // Constants for audio device health checking
    static constexpr int AUDIO_DEVICE_CHECK_SLEEP_MS = 50;
    static constexpr int AUDIO_DEVICE_CHECK_RETRIES = 2;
    static constexpr int AUDIO_DEVICE_CHECK_THRESHOLD = 3;

    TracktionEngineWrapper();
    ~TracktionEngineWrapper();

    // Initialize the engine
    bool initialize() override;
    void shutdown() override;

    // Process commands from MCP agents
    CommandResponse processCommand(const Command& command);

    // TransportInterface implementation
    void play() override;
    void stop() override;
    void pause() override;
    void record() override;
    void locate(double position_seconds) override;
    void locateMusical(int bar, int beat, int tick = 0) override;
    double getCurrentPosition() const override;
    void getCurrentMusicalPosition(int& bar, int& beat, int& tick) const override;
    bool isPlaying() const override;
    bool isRecording() const override;
    double getSessionPlayheadPosition() const override;
    ClipId getSessionPlayheadClipId() const override;
    std::unordered_map<ClipId, double> getActiveClipPlayheadPositions() const override;
    SessionClipPlayState getSessionClipPlayState(ClipId clipId) const override;
    void stopSessionTrack(TrackId trackId) override;
    bool isSessionTrackStopPending(TrackId trackId) const override;
    double getAudioThreadTransportSeconds() const override;
    void deactivateAllSessionClips() override;
    void armSessionSlotRecording(TrackId trackId, int sceneIndex) override;
    void beginArmedSessionSlotRecordings() override;
    bool isSessionSlotRecordArmed(TrackId trackId, int sceneIndex) const override;
    bool isSessionSlotRecording(TrackId trackId, int sceneIndex) const override;
#ifdef MAGDA_ENABLE_TEST_HOOKS
    void testSetSessionSlotRecordingActive(TrackId trackId, int sceneIndex) {
        SessionSlotRecordingTarget target;
        target.sceneIndex = sceneIndex;
        target.active = true;
        sessionSlotRecordingTargets_[trackId] = target;
        createSessionSlotPreview(trackId, sceneIndex);
    }

    bool testFinalizeSessionSlotAudioRecording(TrackId trackId,
                                               tracktion::WaveAudioClip& audioClip) {
        return finalizeSessionSlotAudioRecording(trackId, audioClip);
    }

    bool testFinalizeSessionSlotMidiRecording(TrackId trackId, tracktion::MidiClip& midiClip) {
        return finalizeSessionSlotMidiRecording(trackId, midiClip);
    }

    void testFinishSessionSlotRecordings() {
        finishSessionSlotRecordings();
    }
#endif
    void setTempo(double bpm) override;
    double getTempo() const override;
    void setTimeSignature(int numerator, int denominator) override;
    void getTimeSignature(int& numerator, int& denominator) const override;
    void setLooping(bool enabled) override;
    void setLoopRegion(double start_seconds, double end_seconds) override;
    bool isLooping() const override;
    bool justStarted() const override;
    bool justLooped() const override;

    // Call this each frame to update trigger state (call before updateAllMods)
    void updateTriggerState() override;

    // Drain audio-thread session clip state events
    void processSessionStateEvents() override;

    // Metronome/click track control
    void setMetronomeEnabled(bool enabled) override;
    bool isMetronomeEnabled() const override;
    void setCountInMode(int mode) override;
    int getCountInMode() const override;

    // Device management
    juce::AudioDeviceManager* getDeviceManager() override;

    // AudioEngineListener implementation (receives state changes from UI)
    void onTransportPlay(double position) override;
    void onTransportStop(double returnPosition) override;
    void onTransportPause() override;
    void onTransportRecord(double position) override;
    void onTransportStopRecording() override;
    void onEditPositionChanged(double position) override;
    void onTempoChanged(double bpm) override;
    void onTimeSignatureChanged(int numerator, int denominator) override;
    void onLoopRegionChanged(double startTime, double endTime, bool enabled) override;
    void onLoopEnabledChanged(bool enabled) override;

    // TrackInterface implementation
    std::string createAudioTrack(const std::string& name) override;
    std::string createMidiTrack(const std::string& name) override;
    void deleteTrack(const std::string& track_id) override;
    void setTrackName(const std::string& track_id, const std::string& name) override;
    std::string getTrackName(const std::string& track_id) const override;
    void setTrackMuted(const std::string& track_id, bool muted) override;
    bool isTrackMuted(const std::string& track_id) const override;
    void setTrackSolo(const std::string& track_id, bool solo) override;
    bool isTrackSolo(const std::string& track_id) const override;
    void setTrackArmed(const std::string& track_id, bool armed) override;
    bool isTrackArmed(const std::string& track_id) const override;
    void setTrackColor(const std::string& track_id, int r, int g, int b) override;
    std::vector<std::string> getAllTrackIds() const override;
    bool trackExists(const std::string& track_id) const override;

    /**
     * @brief Preview a MIDI note on a track (for keyboard audition)
     * @param track_id Track ID to send note to
     * @param noteNumber MIDI note number (0-127)
     * @param velocity Velocity (0-127), 0 for note-off
     * @param isNoteOn True for note-on, false for note-off
     */
    void previewNoteOnTrack(const std::string& track_id, int noteNumber, int velocity,
                            bool isNoteOn) override;

    // ClipInterface implementation - fixed method signatures
    std::string addMidiClip(const std::string& track_id, double start_time, double length,
                            const std::vector<MidiNote>& notes) override;
    std::string addAudioClip(const std::string& track_id, double start_time,
                             const std::string& audio_file_path) override;
    void deleteClip(const std::string& clip_id) override;
    void moveClip(const std::string& clip_id, double new_start_time) override;
    void resizeClip(const std::string& clip_id, double new_length) override;
    double getClipStartTime(const std::string& clip_id) const override;
    double getClipLength(const std::string& clip_id) const override;
    void addNoteToMidiClip(const std::string& clip_id, const MidiNote& note) override;
    void removeNotesFromMidiClip(const std::string& clip_id, double start_time,
                                 double end_time) override;
    std::vector<MidiNote> getMidiClipNotes(const std::string& clip_id) const override;
    std::vector<std::string> getTrackClips(const std::string& track_id) const override;
    bool clipExists(const std::string& clip_id) const override;

    // MixerInterface implementation - fixed to use double instead of float
    void setTrackVolume(const std::string& track_id, double volume) override;
    double getTrackVolume(const std::string& track_id) const override;
    void setTrackPan(const std::string& track_id, double pan) override;
    double getTrackPan(const std::string& track_id) const override;
    void setMasterVolume(double volume) override;
    double getMasterVolume() const override;
    std::string addEffect(const std::string& track_id, const std::string& effect_name) override;
    void removeEffect(const std::string& effect_id) override;
    void setEffectParameter(const std::string& effect_id, const std::string& parameter_name,
                            double value) override;
    double getEffectParameter(const std::string& effect_id,
                              const std::string& parameter_name) const override;
    void setEffectEnabled(const std::string& effect_id, bool enabled) override;
    bool isEffectEnabled(const std::string& effect_id) const override;
    std::vector<std::string> getAvailableEffects() const override;
    std::vector<std::string> getTrackEffects(const std::string& track_id) const override;

    // =========================================================================
    // Audio Bridge Access
    // =========================================================================

    /**
     * @brief Get the AudioBridge for TrackManager-to-Tracktion synchronization
     * @return Pointer to AudioBridge, or nullptr if not initialized
     */
    AudioBridge* getAudioBridge() override {
        return audioBridge_.get();
    }
    const AudioBridge* getAudioBridge() const override {
        return audioBridge_.get();
    }

    /**
     * @brief Get the MidiBridge for MIDI device management and routing
     * @return Pointer to MidiBridge, or nullptr if not initialized
     */
    MidiBridge* getMidiBridge() override {
        return midiBridge_.get();
    }
    const MidiBridge* getMidiBridge() const override {
        return midiBridge_.get();
    }

    /**
     * @brief Get active recording previews for real-time MIDI display
     * @return Map of trackId to preview data (empty if not recording)
     */
    const std::unordered_map<TrackId, RecordingPreview>& getRecordingPreviews() const override {
        return recordingPreviews_;
    }

    /**
     * @brief Get the PluginWindowManager for safe plugin window lifecycle management
     * @return Pointer to PluginWindowManager, or nullptr if not initialized
     */
    PluginWindowManager* getPluginWindowManager() {
        return pluginWindowManager_.get();
    }
    const PluginWindowManager* getPluginWindowManager() const {
        return pluginWindowManager_.get();
    }

    /**
     * @brief Get the Tracktion Engine instance
     */
    tracktion::Engine* getEngine() {
        return engine_.get();
    }
    const tracktion::Engine* getEngine() const {
        return engine_.get();
    }

    /**
     * @brief Get the current Edit (project)
     */
    tracktion::Edit* getEdit() {
        return currentEdit_.get();
    }
    const tracktion::Edit* getEdit() const {
        return currentEdit_.get();
    }

    /** Programmatic facade onto MAGDA's DAW state. Owned by the wrapper and
     *  shared across consumers (AI Chat panel, Lua controller, future CLI).
     *  The reference is valid for the lifetime of the wrapper. */
    MagdaApi& getMagdaApi() {
        return *magdaApi_;
    }

    // =========================================================================
    // Device Loading State
    // =========================================================================

    /**
     * @brief Check if devices are currently being initialized
     * @return true if MIDI/audio devices are being scanned/opened
     */
    bool isDevicesLoading() const {
        return devicesLoading_;
    }

    /**
     * @brief Gate playback during an offline render.
     *
     * An offline analysis/export render commandeers the live edit (it frees the
     * playback context); starting playback then corrupts the node graph
     * (NodeRenderContext asserts). OfflineMixAnalysis sets this for the render's
     * lifetime so play() refuses while a render is in flight.
     */
    void setOfflineRenderActive(bool active) {
        offlineRenderActive_ = active;
    }
    bool isOfflineRenderActive() const {
        return offlineRenderActive_;
    }

    /**
     * @brief Callback when device loading state changes
     * Called with (isLoading, message) - message describes what's happening
     */
    std::function<void(bool, const juce::String&)> onDevicesLoadingChanged;

    // =========================================================================
    // Plugin Scanning
    // =========================================================================

    /**
     * @brief Start scanning for VST3/AU plugins on the system
     * @param progressCallback Called with (progress 0-1, current plugin name) during scan
     *
     * NOTE: Plugin scanning happens in-process. If a plugin crashes during scanning,
     * it will crash the application. The "dead man's pedal" file tracks which plugin
     * was being scanned, so it will be skipped on the next scan attempt.
     *
     * Crash files are stored in: ~/Library/Application Support/MAGDA/
     * Call clearPluginExclusions() to retry scanning problematic plugins.
     */
    void startPluginScan(
        std::function<void(float, const juce::String&)> progressCallback = nullptr);

    /**
     * @brief Abort an in-progress plugin scan
     */
    void abortPluginScan();

    /**
     * @brief Clear the plugin exclusion list to retry scanning problematic plugins
     *
     * Call this if you want to give previously-excluded plugins another chance.
     * After clearing, the next scan will attempt all plugins again.
     */
    void clearPluginExclusions();

    /**
     * @brief Get the plugin scan coordinator for accessing exclusion data
     */
    PluginScanCoordinator* getPluginScanCoordinator();

    /**
     * @brief Check if a plugin scan is currently in progress
     */
    bool isScanning() const {
        return isScanning_;
    }

    /**
     * @brief Get the list of known/discovered plugins
     * @return Reference to the KnownPluginList
     */
    juce::KnownPluginList& getKnownPluginList();
    const juce::KnownPluginList& getKnownPluginList() const;

    /**
     * @brief Save the plugin list to persistent storage
     * Called after plugin scanning completes
     */
    void savePluginList();

    /**
     * @brief Load the plugin list from persistent storage
     * Called on startup to restore previously scanned plugins
     */
    void loadPluginList();

    /**
     * @brief Phases reported by detectNewPlugins's status callback. Callers
     * format their own user-facing strings so the engine doesn't bake in
     * English text that bypasses localization.
     */
    enum class IncrementalScanPhase {
        Discovering,  ///< Walking plugin directories. currentPlugin is empty.
        UpToDate,     ///< No new plugins found; no scan will run. currentPlugin is empty.
        Scanning,     ///< A worker is scanning currentPlugin (full path).
    };

    /**
     * @brief Detect and scan newly installed plugins.
     *
     * Compares plugin directories against the cached list and incrementally
     * scans any new files not already known. Skips plugins on the exclusion
     * list (use startPluginScan for an exhaustive rescan).
     *
     * @param statusCallback Optional progress reporter; receives a phase enum
     * and the plugin path being scanned (empty for non-Scanning phases).
     * @param completionCallback Optional one-shot callback fired when the
     * operation finishes. addedCount is the number of new plugins added
     * this run (zero in the up-to-date case); totalCount is the size of the
     * known plugin list after the run.
     */
    void detectNewPlugins(
        std::function<void(IncrementalScanPhase phase, const juce::String& currentPlugin)>
            statusCallback = nullptr,
        std::function<void(bool success, int addedCount, int totalCount,
                           const juce::StringArray& failedPlugins)>
            completionCallback = nullptr);

    /**
     * @brief Remove entries from the given known-plugin list whose plugins
     * are no longer installed. Delegates per-entry to
     * AudioPluginFormatManager::doesPluginStillExist so each format decides
     * correctly — VST/VST3 stat the file path, AU queries AudioComponentFindNext
     * on the identifier (a plain File::exists() skips every AU entry because
     * their fileOrIdentifier is "AudioUnit:..." and not an absolute path).
     * Returns the number of entries removed.
     */
    static int pruneMissingPlugins(juce::KnownPluginList& knownPlugins,
                                   juce::AudioPluginFormatManager& formatManager);

    /**
     * @brief Remove entries that share (path, format) with a freshly-scanned
     * descriptor but whose uid is not in the fresh scan's results.
     *
     * Vendors bump VST3 uniqueIds across major versions; JUCE keys
     * KnownPluginList by (path, deprecatedUid, uniqueId) so the old and new
     * entries coexist on the same .vst3 file and the user sees a duplicate
     * row even though only one binary is installed (#1005). Multi-component
     * VST3s (Vital, Kontakt, MeldaProduction bundles) legitimately expose
     * several uids per path, so we keep every uid the current scan returned
     * and only drop ones that didn't.
     *
     * Entries whose (path, format) wasn't seen this scan (excluded plugins,
     * formats not enabled this run) are left untouched. Returns the number
     * of entries removed.
     */
    static int removeSupersededEntries(juce::KnownPluginList& knownPlugins,
                                       const juce::Array<juce::PluginDescription>& freshScan);

    /**
     * @brief Clear the plugin list and delete the saved file
     * Use this before a fresh rescan
     */
    void clearPluginList();

    /**
     * @brief Get the file path where plugin list is stored
     * @return Path to the plugin list XML file
     */
    juce::File getPluginListFile() const;

    // =========================================================================
    // PDC (Plugin Delay Compensation) Query
    // =========================================================================

    /**
     * @brief Get the latency of a specific plugin in seconds
     * @param effect_id The effect/plugin ID
     * @return Latency in seconds, or 0 if plugin not found
     */
    double getPluginLatencySeconds(const std::string& effect_id) const;

    /**
     * @brief Get the maximum latency across all tracks in the playback graph
     * This is the total PDC that Tracktion Engine compensates for
     * @return Maximum latency in seconds
     */
    double getGlobalLatencySeconds() const;

    /**
     * @brief Callback when plugin scan completes
     * Called with (success, number of plugins found, failed plugins)
     */
    std::function<void(bool, int, const juce::StringArray&)> onPluginScanComplete;

    /** Callback for startup plugin detection status (shown on splash screen). */
    std::function<void(const juce::String&)> onPluginScanStatus;

    /** Fires the first time MIDI devices become available (and on subsequent
     *  device-list changes). Use this to defer work that needs MIDI output
     *  ports to be open — e.g. controller scripts whose `on_load` sends
     *  SysEx, since sends issued before JUCE opens the port are dropped. */
    std::function<void()> onMidiDevicesReady;

    // =========================================================================
    // TransportControl::Listener implementation
    // =========================================================================

    void recordingAboutToStart(tracktion::InputDeviceInstance& instance,
                               tracktion::EditItemID targetID) override;
    void recordingFinished(
        tracktion::InputDeviceInstance& instance, tracktion::EditItemID targetID,
        const juce::ReferenceCountedArray<tracktion::Clip>& recordedClips) override;

  private:
    // juce::ChangeListener implementation
    void changeListenerCallback(juce::ChangeBroadcaster* source) override;

    // Initialization helper methods
    void initializePluginFormats();
    void initializeDeviceManager();
    void configureAudioDevices();
    void setupMidiDevices();
    void createEditAndBridges();

    // Change listener helper methods
    void handleMidiDeviceChanges(tracktion::DeviceManager& dm);
    void handlePlaybackContextReallocation(tracktion::DeviceManager& dm);
    void notifyDeviceLoadingComplete(const juce::String& message);

    // Tracktion Engine components
    std::unique_ptr<tracktion::Engine> engine_;
    std::unique_ptr<tracktion::Edit> currentEdit_;

    // Audio bridge for TrackManager synchronization
    std::unique_ptr<AudioBridge> audioBridge_;

    // Session clip scheduler for session view clip playback
    std::unique_ptr<SessionClipScheduler> sessionScheduler_;

    // Session recorder for recording session performances to arrangement
    std::unique_ptr<SessionRecorder> sessionRecorder_;

    // MIDI bridge for MIDI device management and routing
    std::unique_ptr<MidiBridge> midiBridge_;

    // Programmatic facade onto MAGDA's DAW state. Owned here and shared
    // with consumers (AI Chat, Lua controller, future CLI) via getMagdaApi().
    // No Lua / scripting types referenced from this lib — the Lua controller
    // lives in the magda_daw_app layer to avoid a circular link with
    // magda_scripting.
    std::unique_ptr<MagdaApi> magdaApi_;

    // Plugin window manager for safe window lifecycle
    std::unique_ptr<PluginWindowManager> pluginWindowManager_;

    // Test tone generator (for Phase 1 testing)
    tracktion::Plugin::Ptr testTonePlugin_;

    // Transport trigger state tracking
    bool wasPlaying_ = false;    // Previous frame's playing state
    double lastPosition_ = 0.0;  // Previous frame's position (for loop detection)
    bool justStarted_ = false;   // True for one frame after play starts
    bool justLooped_ = false;    // True for one frame after loop

    // Device change tracking
    int lastKnownDeviceCount_ = 0;
    juce::String lastKnownAudioDeviceName_;

    // Helper methods
    tracktion::Track* findTrackById(const std::string& track_id) const;
    tracktion::Clip* findClipById(const std::string& clip_id) const;
    std::string generateTrackId();
    std::string generateClipId();
    std::string generateEffectId();

    // State tracking
    std::map<std::string, tracktion::Track::Ptr> trackMap_;
    std::map<std::string, tracktion::Clip::Ptr> clipMap_;
    std::map<std::string, void*> effectMap_;  // For tracking effects
    int nextTrackId_ = 1;
    int nextClipId_ = 1;
    int nextEffectId_ = 1;

    // Per-track dedup during recordingFinished (multiple devices per track).
    // Populated in recordingFinished, cleared after transport stop.
    std::unordered_map<int, int> activeRecordingClips_;

    // Deferred MIDI recording finalization. TE can produce up to N clips per
    // track (one per input device) during stopRecording; with mergeRecordings=
    // true each subsequent device's events are merged into the first clip via
    // track->mergeInMidiSequence. We track the first TE MidiClip we see per
    // trackId and, after all synchronous recordingFinished callbacks settle,
    // extract its final (merged) state once via callAsync. Remove-on-the-spot
    // caused QWERTY's merge target to vanish, dropping its notes.
    std::unordered_map<TrackId, tracktion::MidiClip::Ptr> pendingMidiRecordings_;
    std::unordered_set<TrackId> pendingFinalizeMidi_;
    void finalizeMidiRecording(TrackId trackId);

    // Track recording start time per track (populated in recordingAboutToStart)
    std::unordered_map<int, double> recordingStartTimes_;
    double intendedRecordPosition_ = 0.0;  // Position before count-in offset

    // Real-time MIDI recording preview (outside ClipManager)
    RecordingNoteQueue recordingNoteQueue_;
    std::atomic<double> transportPositionForMidi_{0.0};
    std::unordered_map<TrackId, RecordingPreview> recordingPreviews_;
    void drainRecordingNoteQueue();

    std::unordered_map<TrackId, SessionSlotRecordingTarget> sessionSlotRecordingTargets_;
    bool hasActiveSessionSlotRecordings() const;
    void finishSessionSlotRecordings();
    bool finalizeSessionSlotAudioRecording(TrackId trackId, tracktion::WaveAudioClip& audioClip);
    bool finalizeSessionSlotMidiRecording(TrackId trackId, tracktion::MidiClip& midiClip);
    ClipId createEmptySessionSlotRecordingClip(TrackId trackId, int sceneIndex);
    // Creates the transient active-recording-pass preview for a session slot.
    void createSessionSlotPreview(TrackId trackId, int sceneIndex);

    // Device loading state
    bool devicesLoading_ = true;                    // Start as loading until first scan completes
    std::atomic<bool> offlineRenderActive_{false};  // an offline render owns the edit
    bool wasPlayingBeforeDeviceChange_ = false;

    // Plugin scanning state
    bool isScanning_ = false;
    std::function<void(float, const juce::String&)> scanProgressCallback_;
    std::unique_ptr<PluginScanCoordinator> pluginScanCoordinator_;
    std::thread pluginDiscoveryThread_;
    std::shared_ptr<std::atomic<bool>> aliveFlag_ = std::make_shared<std::atomic<bool>>(true);

    JUCE_DECLARE_WEAK_REFERENCEABLE(TracktionEngineWrapper)
};

}  // namespace magda
