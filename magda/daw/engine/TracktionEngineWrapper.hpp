#pragma once

#include <tracktion_engine/tracktion_engine.h>

#include <functional>
#include <unordered_map>

#include "../audio/RecordingNoteQueue.hpp"
#include "../command.hpp"
#include "../interfaces/clip_interface.hpp"
#include "../interfaces/mixer_interface.hpp"
#include "../interfaces/track_interface.hpp"
#include "../interfaces/transport_interface.hpp"
#include "AudioEngine.hpp"

namespace magda {

// Forward declarations
class AudioBridge;
class MidiBridge;
class PluginScanCoordinator;
class PluginWindowManager;
class SessionClipScheduler;

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

    // Metronome/click track control
    void setMetronomeEnabled(bool enabled) override;
    bool isMetronomeEnabled() const override;

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
     * Call clearPluginScanCrashFiles() to retry scanning problematic plugins.
     */
    void startPluginScan(
        std::function<void(float, const juce::String&)> progressCallback = nullptr);

    /**
     * @brief Abort an in-progress plugin scan
     */
    void abortPluginScan();

    /**
     * @brief Clear the dead man's pedal crash files to retry scanning problematic plugins
     *
     * Call this if you want to give previously-crashed plugins another chance.
     * After clearing, the next scan will attempt all plugins again.
     */
    void clearPluginScanCrashFiles();

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

    // MIDI bridge for MIDI device management and routing
    std::unique_ptr<MidiBridge> midiBridge_;

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

    // Track recording start time per track (populated in recordingAboutToStart)
    std::unordered_map<int, double> recordingStartTimes_;

    // Real-time MIDI recording preview (outside ClipManager)
    RecordingNoteQueue recordingNoteQueue_;
    std::atomic<double> transportPositionForMidi_{0.0};
    std::unordered_map<TrackId, RecordingPreview> recordingPreviews_;
    void drainRecordingNoteQueue();

    // Device loading state
    bool devicesLoading_ = true;  // Start as loading until first scan completes
    bool wasPlayingBeforeDeviceChange_ = false;

    // Plugin scanning state
    bool isScanning_ = false;
    std::function<void(float, const juce::String&)> scanProgressCallback_;
    std::unique_ptr<PluginScanCoordinator> pluginScanCoordinator_;
};

}  // namespace magda
