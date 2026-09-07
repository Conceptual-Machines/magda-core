#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "../audio/midi/RecordingNoteQueue.hpp"
#include "../core/ClipTypes.hpp"
#include "../core/ParameterDetector.hpp"
#include "../core/TempoMap.hpp"
#include "../core/TimeTypes.hpp"
#include "AudioEngineListener.hpp"
#include "PluginExclusions.hpp"

namespace juce {
class AudioDeviceManager;
}

namespace magda {

class AudioBridge;
class InsertRenderCaptureService;
class MagdaApi;
class MidiBridge;
class PluginWindowManager;
class UndoableCommand;

enum class PluginScanPhase {
    Discovering,
    UpToDate,
    Scanning,
};

struct GrooveTemplateData {
    juce::String name;
    int notesPerBeat = 2;
    bool parameterized = true;
    std::vector<float> latenessProportions;
};

struct ScannedPluginParameter {
    juce::String name;
    float defaultValue = 0.5f;
    juce::String unit;
    float rangeMin = 0.0f;
    float rangeMax = 1.0f;
    float rangeCenter = 0.5f;
    ParameterScale scale = ParameterScale::Linear;
    std::vector<juce::String> valueTable;
    ParameterScanInput scanInput;
};

enum class OfflineRenderFormat {
    Wav,
    Flac,
};

enum class TempoSequenceRippleMode {
    Insert,
    Delete,
    Duplicate,
};

struct OfflineRenderRequest {
    juce::File destination;
    OfflineRenderFormat format = OfflineRenderFormat::Wav;
    int bitDepth = 24;
    double sampleRate = 44100.0;
    int blockSize = 512;
    bool shouldNormalise = false;
    float normaliseToLevelDb = 0.0f;
    bool useMasterPlugins = true;
    bool usePlugins = true;
    bool checkNodesForAudio = false;
    bool realTimeRender = false;
    RenderTimeRange range;
    std::vector<TrackId> trackIds;
    std::vector<TrackId> excludedTrackIds;
    std::vector<ClipId> clipIds;
};

struct OfflineRenderResult {
    bool success = false;
    juce::String error;
};

struct SamplerMediaReference {
    juce::File source;
    std::function<void(const juce::File&)> replace;
};

class OfflineRenderTask {
  public:
    virtual ~OfflineRenderTask() = default;
    virtual OfflineRenderResult run(const std::function<bool()>& shouldCancel,
                                    const std::function<void(float)>& onProgress) = 0;
    OfflineRenderResult run() {
        return run({}, {});
    }
};

/**
 * Owns transport, playback-context, and plugin preparation for one logical
 * offline-render run. Multi-pass consumers create all their tasks from the
 * same session so state is restored only after the final pass.
 */
class OfflineRenderSession {
  public:
    virtual ~OfflineRenderSession() = default;
    virtual std::unique_ptr<OfflineRenderTask> createTask(const OfflineRenderRequest& request) = 0;
};

struct AudioEngineOptions {
    bool headless = false;
};

/**
 * @brief Abstract audio engine interface
 *
 * This provides a clean abstraction over the actual audio engine implementation.
 * Concrete implementations (e.g., TracktionEngineWrapper) inherit from this.
 *
 * Also inherits from AudioEngineListener so the TimelineController can notify
 * the audio engine of state changes via the observer pattern.
 */
class AudioEngine : public AudioEngineListener {
  public:
    ~AudioEngine() override = default;

    // ===== Lifecycle =====
    virtual bool initialize() = 0;
    virtual void shutdown() = 0;
    virtual bool hasActiveEdit() const = 0;
    virtual BeatDuration getEditLengthBeats() const = 0;
    virtual juce::File getEditFile() const = 0;

    // ===== Transport =====
    virtual void play() = 0;
    virtual void stop() = 0;
    virtual void pause() = 0;
    virtual void record() = 0;
    virtual void locate(double positionSeconds) = 0;
    virtual double getCurrentPosition() const = 0;
    virtual bool isPlaying() const = 0;
    virtual bool isRecording() const = 0;

    /** Returns the looped playhead position within the active session clip (seconds).
        Returns -1.0 if no session clips are playing. Tracks the most recently launched clip. */
    virtual double getSessionPlayheadPosition() const = 0;

    /** Returns the clip ID the session playhead currently tracks, or INVALID_CLIP_ID. */
    virtual ClipId getSessionPlayheadClipId() const = 0;

    /** Returns per-clip playhead positions for all active session clips. */
    virtual std::unordered_map<ClipId, double> getActiveClipPlayheadPositions() const = 0;

    /** Returns the play state of a session clip (Stopped/Queued/Playing). */
    virtual SessionClipPlayState getSessionClipPlayState(ClipId clipId) const = 0;

    /** Schedule a quantized stop for the active clip on a track (empty slot in scene). */
    virtual void stopSessionTrack(TrackId trackId) = 0;

    /** True while a quantized stop on this track is in flight (between
        `stopSessionTrack` and the LaunchHandle reporting Stopped). The UI
        uses this to blink the empty-slot stop affordance. */
    virtual bool isSessionTrackStopPending(TrackId trackId) const = 0;

    /** Latest transport position (seconds) sampled by the audio thread.
        Returns -1.0 if the audio thread has not run yet. Used by
        beat-aligned visuals (beat indicator, etc.) so they stay phase-locked
        with audio rather than drifting at the message-thread sampling rate. */
    virtual double getAudioThreadTransportSeconds() const = 0;

    /** Stop all session clips, clear active state, revert to arrangement. */
    virtual void deactivateAllSessionClips() = 0;

    /** Mark an empty session slot as the target for recording. */
    virtual void armSessionSlotRecording(TrackId /*trackId*/, int /*sceneIndex*/) {}

    /** Begin any armed session slot recordings. */
    virtual void beginArmedSessionSlotRecordings() {}

    /** True if the given empty session slot is armed as a recording target. */
    virtual bool isSessionSlotRecordArmed(TrackId /*trackId*/, int /*sceneIndex*/) const {
        return false;
    }

    /** True while the given session slot is actively recording. */
    virtual bool isSessionSlotRecording(TrackId /*trackId*/, int /*sceneIndex*/) const {
        return false;
    }

    // ===== Tempo =====
    virtual void setTempo(double bpm) = 0;
    virtual double getTempo() const = 0;
    virtual void setTimeSignature(int numerator, int denominator) = 0;
    virtual void getTimeSignature(int& numerator, int& denominator) const = 0;

    /** Position-aware beats<->seconds facade backed by the engine's tempo
        sequence (the single source of truth). */
    virtual const TempoMap* tempoMap() const = 0;

    // ===== Loop =====
    virtual void setLooping(bool enabled) = 0;
    virtual void setLoopRegionBeats(BeatRange range) = 0;
    virtual bool isLooping() const = 0;
    virtual BeatRange getLoopRegionBeats() const = 0;

    // ===== Metronome =====
    virtual void setMetronomeEnabled(bool enabled) = 0;
    virtual bool isMetronomeEnabled() const = 0;

    // Count-in / pre-roll (0=none, 1=1bar, 2=2bars, 3=2beats, 4=1beat)
    virtual void setCountInMode(int mode) = 0;
    virtual int getCountInMode() const = 0;

    // ===== Trigger State (for transport-synced devices) =====
    virtual void updateTriggerState() = 0;

    // ===== Session State Events (audio thread → message thread) =====
    virtual void processSessionStateEvents() = 0;

    // ===== Device Management =====
    virtual juce::AudioDeviceManager* getDeviceManager() = 0;
    virtual juce::BigInteger getEnabledWaveChannels(bool input) const = 0;
    virtual void setEnabledWaveChannels(bool input, const juce::BigInteger& channels) = 0;
    virtual void rescanWaveDevices(bool enableInputs, bool enableOutputs) = 0;
    virtual bool isDevicesLoading() const = 0;
    virtual void setDevicesLoadingCallback(
        std::function<void(bool, const juce::String&)> callback) = 0;

    // ===== Audio Management =====
    virtual AudioBridge* getAudioBridge() = 0;
    virtual const AudioBridge* getAudioBridge() const = 0;

    // ===== MIDI Management =====
    virtual MidiBridge* getMidiBridge() = 0;
    virtual const MidiBridge* getMidiBridge() const = 0;

    // ===== Application Services =====
    virtual MagdaApi& getMagdaApi() = 0;
    virtual PluginWindowManager* getPluginWindowManager() = 0;
    virtual const PluginWindowManager* getPluginWindowManager() const = 0;
    virtual InsertRenderCaptureService* getInsertRenderCaptureService() = 0;

    // ===== Plugin Discovery =====
    virtual juce::Array<juce::PluginDescription> getKnownPluginTypes() const = 0;
    virtual juce::Array<juce::PluginDescription> getPreferredPluginTypes() const = 0;
    virtual void addPluginListChangeListener(juce::ChangeListener* listener) = 0;
    virtual void removePluginListChangeListener(juce::ChangeListener* listener) = 0;
    virtual void startPluginScan(
        std::function<void(float, const juce::String&)> progressCallback) = 0;
    virtual void abortPluginScan() = 0;
    virtual void detectNewPlugins(
        std::function<void(PluginScanPhase, const juce::String&)> statusCallback,
        std::function<void(bool, int, int, const juce::StringArray&)> completionCallback) = 0;
    virtual void setPluginScanCompletionCallback(
        std::function<void(bool, int, const juce::StringArray&)> callback) = 0;
    virtual bool isPluginScanRunning() const = 0;
    virtual std::vector<ExcludedPlugin> getExcludedPlugins() const = 0;
    virtual void setExcludedPlugins(const std::vector<ExcludedPlugin>& excludedPlugins) = 0;
    virtual juce::File getPluginScanReportFile() const = 0;
    virtual std::vector<std::string> getSystemPluginSearchPaths() const = 0;

    // ===== Plugin Parameter Discovery =====
    virtual std::vector<ScannedPluginParameter> scanPluginParameters(const juce::String& pluginId,
                                                                     bool internalPlugin) = 0;

    // ===== Groove Templates =====
    virtual bool upsertGrooveTemplate(const GrooveTemplateData& groove) = 0;
    virtual juce::StringArray getGrooveTemplateNames() const = 0;

    // ===== Offline Rendering =====
    virtual std::unique_ptr<OfflineRenderSession> createOfflineRenderSession(
        bool resumePlaybackWhenFinished) = 0;

    // ===== Project Media =====
    virtual std::vector<SamplerMediaReference> getSamplerMediaReferences() = 0;

    // ===== Edit-Wide Tempo Sequences =====
    virtual std::unique_ptr<UndoableCommand> createTempoSequenceRippleCommand(
        TempoSequenceRippleMode mode, BeatPosition start, BeatPosition end) = 0;

    // ===== MIDI Preview =====
    /**
     * @brief Preview a MIDI note on a track (for keyboard audition)
     * @param track_id Track ID to send note to
     * @param noteNumber MIDI note number (0-127)
     * @param velocity Velocity (0-127), 0 for note-off
     * @param isNoteOn True for note-on, false for note-off
     */
    virtual void previewNoteOnTrack(const std::string& track_id, int noteNumber, int velocity,
                                    bool isNoteOn) = 0;

    // ===== Recording Preview =====
    /**
     * @brief Get active recording previews for real-time MIDI note display
     * Returns transient preview data that exists only during recording.
     * No ClipManager clips are involved — this is paint-only overlay data.
     */
    virtual const std::unordered_map<TrackId, RecordingPreview>& getRecordingPreviews() const {
        static const std::unordered_map<TrackId, RecordingPreview> empty;
        return empty;
    }
};

/** Construct the production audio-engine backend without exposing its concrete type. */
std::unique_ptr<AudioEngine> createDefaultAudioEngine(AudioEngineOptions options = {});

}  // namespace magda
