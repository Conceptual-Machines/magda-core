#pragma once

#include <tracktion_engine/tracktion_engine.h>

#include <functional>
#include <map>
#include <thread>
#include <unordered_map>
#include <unordered_set>

#include "../audio/DeviceMeters.hpp"
#include "../audio/TrackMeters.hpp"
#include "../audio/midi/RecordingNoteQueue.hpp"
#include "../command.hpp"
#include "../interfaces/clip_interface.hpp"
#include "../interfaces/mixer_interface.hpp"
#include "../interfaces/track_interface.hpp"
#include "../interfaces/transport_interface.hpp"
#include "AudioEngine.hpp"
#include "PluginService.hpp"
#include "TracktionAudioIO.hpp"

namespace magda {

// Forward declarations
class AudioBridge;
class InsertRenderCaptureService;
class MagdaApi;
class MidiBridge;
class PluginWindowManager;
class SessionClipScheduler;
class SessionRecorder;
class TracktionTempoMap;
class TempoLaneSync;
struct ProjectInfo;

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
    ~TracktionEngineWrapper() override;

    /**
     * @brief Force the wrapper to skip UI/timer-backed runtime helpers.
     *
     * Intended for console tools and e2e tests that boot the engine without a GUI.
     * The MAGDA_HEADLESS environment variable provides the same behavior.
     */
    void setForceHeadless(bool forceHeadless) {
        forceHeadless_ = forceHeadless;
    }

    bool isHeadlessRuntime() const;

    /**
     * @brief Whether Tracktion opens the audio interface, or leaves it to another owner (#2747).
     *
     * Before initialiseServices(). False keeps plugin formats and MIDI but gives Tracktion no
     * audio backends, so the native engine's AudioIOService is the only one open.
     */
    void setOpensAudioInterface(bool opensAudioInterface) {
        opensAudioInterface_ = opensAudioInterface;
    }

    // Initialize the engine
    bool initialize() override;

    /**
     * @brief Bring up the half that is not playback: the engine, plugin formats,
     *        devices, the MidiBridge and the project save hooks (#2579).
     * @return Whether the Tracktion engine was created.
     */
    bool initialiseServices();

    /**
     * @brief Bring up the Edit and everything that renders through it (#2579).
     *
     * Never called under the magda engine, which renders for itself.
     * @return Whether an Edit was created.
     */
    bool initialisePlayback();

    void shutdown() final;
    bool hasActiveEdit() const override {
        return currentEdit_ != nullptr;
    }
    BeatDuration getEditLengthBeats() const override;
    juce::File getEditFile() const override;

    // Process commands from MCP agents
    CommandResponse processCommand(const Command& command);

    // TransportInterface implementation
    void play() override;
    void stop() override;
    void pause() override;
    void record() override;

    /**
     * @brief Flush all-notes-off through every External Instrument insert's
     *        MIDI send so hardware synths never hang after a transport stop.
     */
    void sendAllNotesOffToExternalInserts();
    void locate(double position_seconds) override;
    void locateMusical(int bar, int beat, int tick) override;
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
    void launchSessionScene(const std::vector<TrackId>& trackIds, int sceneIndex) override;
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
    const TempoMap* tempoMap() const override;
    void setMidiDevicesReadyCallback(std::function<void()> callback) override {
        onMidiDevicesReady = std::move(callback);
    }
    void setTimeSignature(int numerator, int denominator) override;
    void getTimeSignature(int& numerator, int& denominator) const override;
    void setLooping(bool enabled) override;
    void setLoopRegionBeats(BeatRange range) override;
    BeatRange getLoopRegionBeats() const override;
    // Seconds-domain implementation required by the legacy TransportInterface.
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
    AudioIOControl* getAudioIO() override;

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

    TrackMeters& meters() override {
        return meters_;
    }
    const TrackMeters& meters() const override {
        return meters_;
    }

    DeviceMeters& deviceMeters() override {
        return deviceMeters_;
    }
    const DeviceMeters& deviceMeters() const override {
        return deviceMeters_;
    }

    /** @brief Read the bridge's plugins back into the model. They are what this renders. */
    void captureAllPluginStates() override;
    void capturePluginStateAt(const ChainNodePath& devicePath) override;
    void applyPluginStateAt(const ChainNodePath& devicePath) override;

    /** @brief The windows onto those same plugins (#2580). */
    std::optional<PluginPrograms> getPluginPrograms(const ChainNodePath& devicePath) override;
    bool setPluginCurrentProgram(const ChainNodePath& devicePath, int programIndex) override;
    bool loadPluginPresetFile(const ChainNodePath& devicePath, const juce::File& file) override;
    bool savePluginPresetFile(const ChainNodePath& devicePath, const juce::File& file) override;

    bool showDeviceEditor(const ChainNodePath& devicePath) override;
    bool hideDeviceEditor(const ChainNodePath& devicePath) override;
    bool toggleDeviceEditor(const ChainNodePath& devicePath) override;
    bool isDeviceEditorOpen(const ChainNodePath& devicePath) const override;

    /** @brief The fork's processor formats the value (#2600). */
    juce::String formatDeviceParameter(const ChainNodePath& devicePath, int paramIndex,
                                       float normalised) const override;

    /** @brief The MAGDA device inside the fork's plugin at @p devicePath (#2585). */
    std::shared_ptr<daw::audio::MagdaDevice> renderedDevice(
        const ChainNodePath& devicePath) const override;

    /**
     * @brief Export capture pass for External FX / Instrument devices (#1623)
     * @return Pointer to the service, or nullptr when unavailable (headless)
     */
    InsertRenderCaptureService* getInsertRenderCaptureService() override {
        return insertRenderCapture_.get();
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
    PluginWindowManager* getPluginWindowManager() override {
        return pluginWindowManager_.get();
    }
    const PluginWindowManager* getPluginWindowManager() const override {
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
    MagdaApi& getMagdaApi() override {
        jassert(magdaApi_ != nullptr);
        return *magdaApi_;
    }

    // =========================================================================
    // Device Loading State
    // =========================================================================

    /**
     * @brief Check if devices are currently being initialized
     * @return true if MIDI/audio devices are being scanned/opened
     */
    /** Gate playback while an offline render owns the edit. */
    void setOfflineRenderActive(bool active) {
        offlineRenderActive_ = active;
    }
    bool isOfflineRenderActive() const {
        return offlineRenderActive_;
    }

    // =========================================================================
    // Plugins
    // =========================================================================

    /**
     * @brief The pair Tracktion's own hosting reads, which PluginService answers off
     * until the fork goes (#2557), and magda::engine creates a plan's plugins with (#2566).
     */
    juce::KnownPluginList& getKnownPluginList();
    const juce::KnownPluginList& getKnownPluginList() const;
    juce::AudioPluginFormatManager& getPluginFormatManager();

    /**
     * @brief Every parameter a Tracktion internal plugin declares, built in the current Edit.
     *
     * Installed on PluginService as the fallback for a plugin its catalog does not know:
     * 4OSC and the rest of Tracktion's own need an Edit to exist in (#2601).
     */
    std::vector<ScannedPluginParameter> scanInternalParametersInEdit(const juce::String& pluginId);

    bool upsertGrooveTemplate(const GrooveTemplateData& data) override;
    juce::StringArray getGrooveTemplateNames() const override;
    std::unique_ptr<OfflineRenderSession> createOfflineRenderSession(
        bool resumePlaybackWhenFinished) override;
    void setTrackFrozen(TrackId trackId, bool frozen) override;
    std::vector<SamplerMediaReference> getSamplerMediaReferences() override;
    std::unique_ptr<UndoableCommand> createTempoSequenceRippleCommand(TempoSequenceRippleMode mode,
                                                                      BeatPosition start,
                                                                      BeatPosition end) override;

    // =========================================================================
    // PDC (Plugin Delay Compensation) Query
    // =========================================================================

    /**
     * @brief Get the latency of a specific plugin in seconds
     * @param effect_id The effect/plugin ID
     * @return Latency in seconds, or 0 if plugin not found
     */
    static double getPluginLatencySeconds(const std::string& effect_id);

    /**
     * @brief Get the maximum latency across all tracks in the playback graph
     * This is the total PDC that Tracktion Engine compensates for
     * @return Maximum latency in seconds
     */
    double getGlobalLatencySeconds() const;

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

    /** @brief Install ProjectManager's save and load hooks. The plugin-state
     *  half is guarded at save time, since there may be no AudioBridge (#2579). */
    void installProjectStateHooks();

    // Change listener helper methods
    void handleMidiDeviceChanges(tracktion::DeviceManager& dm);
    void handlePlaybackContextReallocation(tracktion::DeviceManager& dm);

    // Tracktion Engine components
    std::unique_ptr<tracktion::Engine> engine_;
    std::unique_ptr<TracktionAudioIO> audioIO_;
    std::unique_ptr<tracktion::Edit> currentEdit_;

    // Position-aware beats<->seconds facade over currentEdit_->tempoSequence.
    // Resolves the current Edit lazily so it survives Edit recreation.
    std::unique_ptr<TracktionTempoMap> tempoMap_;

    // Keeps the edit-scoped Tempo automation lane in sync with
    // currentEdit_->tempoSequence (both directions). Recreated per Edit.
    std::unique_ptr<TempoLaneSync> tempoLaneSync_;

    // Declared before audioBridge_ so they outlive it: AudioBridge holds a
    // reference to both (#2579, #2570).
    TrackMeters meters_;
    DeviceMeters deviceMeters_;

    // Audio bridge for TrackManager synchronization
    std::unique_ptr<AudioBridge> audioBridge_;

    // Session clip scheduler for session view clip playback
    std::unique_ptr<SessionClipScheduler> sessionScheduler_;

    // Session recorder for recording session performances to arrangement
    std::unique_ptr<SessionRecorder> sessionRecorder_;

    // Export capture pass for External FX / Instrument devices (#1623)
    std::unique_ptr<InsertRenderCaptureService> insertRenderCapture_;

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
    bool forceHeadless_ = false;
    bool opensAudioInterface_ = true;

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

    // Real-time MIDI recording preview (outside ClipManager).
    // Two queues because RecordingNoteQueue is single-producer/single-consumer:
    // recordingNoteQueue_ is fed by MidiBridge (hardware/QWERTY MIDI callback
    // threads) and trackMidiRecordingNoteQueue_ by MidiInputRouter's TE input
    // consumers (audio thread) for "track:N"-routed MIDI. Both drain on the
    // message thread in drainRecordingNoteQueue().
    RecordingNoteQueue recordingNoteQueue_;
    RecordingNoteQueue trackMidiRecordingNoteQueue_;
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

    std::atomic<bool> offlineRenderActive_{false};  // an offline render owns the edit

    std::shared_ptr<std::atomic<bool>> aliveFlag_ = std::make_shared<std::atomic<bool>>(true);

    /// The save and load hooks as they were before this installed its own, put
    /// back at shutdown so a second wrapper in a process does not strip the first's.
    std::function<void()> previousBeforeSave_;
    std::function<void(const ProjectInfo&)> previousAfterLoad_;

    JUCE_DECLARE_WEAK_REFERENCEABLE(TracktionEngineWrapper)
};

}  // namespace magda
