#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "../audio/midi/RecordingNoteQueue.hpp"
#include "../audio/plugin_manager/ExternalPluginState.hpp"
#include "../core/ChainNodePath.hpp"
#include "../core/ClipTypes.hpp"
#include "../core/HostedParameterEdit.hpp"
#include "../core/ParameterDetector.hpp"
#include "../core/TempoMap.hpp"
#include "../core/TimeTypes.hpp"
#include "AudioEngineChoice.hpp"
#include "AudioEngineListener.hpp"
#include "PluginExclusions.hpp"

namespace juce {
class AudioDeviceManager;
}

namespace magda::daw::audio {
class MagdaDevice;
}

namespace magda {

class AudioBridge;
class DeviceMeters;
class InsertRenderCaptureService;

class MagdaApi;
class MidiBridge;
class PluginWindowManager;
struct TrackMeters;
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

    /// The parameter's own id, which is what a saved config is matched by
    /// (PluginParameterConfigEntry::id). Empty for a parameter that declares
    /// none, which falls back to its position.
    juce::String stableId;

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

/** @brief What a fixed-point render is dithered with before it is rounded. */
enum class OfflineRenderDither {
    None,
    Tpdf,
    Shaped,
};

/** @brief TPDF for 16 and 24 bit, nothing for 32-bit float. */
inline OfflineRenderDither defaultOfflineRenderDither(int bitDepth) {
    return bitDepth >= 32 ? OfflineRenderDither::None : OfflineRenderDither::Tpdf;
}

enum class TempoSequenceRippleMode {
    Insert,
    Delete,
    Duplicate,
};

struct OfflineRenderRequest {
    juce::File destination;
    OfflineRenderFormat format = OfflineRenderFormat::Wav;
    int bitDepth = 24;

    /// Unset lets the bit depth decide (defaultOfflineRenderDither).
    std::optional<OfflineRenderDither> dither;

    double sampleRate = 44100.0;
    int blockSize = 512;
    bool shouldNormalise = false;
    float normaliseToLevelDb = 0.0f;
    bool useMasterPlugins = true;
    bool usePlugins = true;

    /// False renders a track's instruments without the effects after them.
    bool useTrackEffects = true;

    bool realTimeRender = false;
    BeatRange range;

    /// Rendered past the range's end, for reverb and delay tails. Unset renders
    /// the longest tail the rendered devices declare.
    std::optional<double> tailSeconds = 0.0;

    /// Silence written ahead of the range.
    double leadInSeconds = 0.0;

    std::vector<TrackId> trackIds;
    std::vector<TrackId> excludedTrackIds;
    std::vector<ClipId> clipIds;

    /// The track a freeze renders: up to its fader, unmuted, with no track soloed.
    TrackId freezeTrackId = INVALID_TRACK_ID;
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

    /// Which engine this is, for the about box (#2559). Asked of the engine
    /// that was built rather than of chosenAudioEngine(), whose answer moves
    /// the moment the setting changes and only means anything at the next
    /// start.
    virtual juce::String engineName() const {
        return nameOf(AudioEngineChoice::Tracktion);
    }

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

    /** Launch scene @p sceneIndex across @p trackIds: each track's slot, or a
        stop for a track whose slot is empty.

        One call rather than a loop of launches, because a scene is one event:
        the native engine puts every slot of it on the audio thread together,
        and slots launched one at a time can land on two sides of a boundary
        (#2552). */
    virtual void launchSessionScene(const std::vector<TrackId>& trackIds, int sceneIndex) = 0;

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
    virtual std::map<int, juce::String> getOutputDeviceNamesByChannel() const {
        return {};
    }
    virtual void setEnabledWaveChannels(bool input, const juce::BigInteger& channels) = 0;
    virtual void rescanWaveDevices(bool enableInputs, bool enableOutputs) = 0;
    virtual bool isDevicesLoading() const = 0;
    virtual void setDevicesLoadingCallback(
        std::function<void(bool, const juce::String&)> callback) = 0;

    // ===== Startup hooks =====
    //
    // What the application asks of an engine while it is coming up. On the
    // interface rather than on one implementation's public fields, because the
    // app owns the startup sequence and must not have to know which engine it
    // was given -- naming a concrete type there is what made the choice
    // unreachable from the running app (#2551).

    /** Startup plugin-detection status, for the splash screen. */
    virtual void setPluginScanStatusCallback(std::function<void(const juce::String&)> callback) = 0;

    /** Fires the first time MIDI devices become available, and on subsequent
        device-list changes. Work needing MIDI output ports open waits for this:
        a SysEx send issued before JUCE opens the port is dropped. */
    virtual void setMidiDevicesReadyCallback(std::function<void()> callback) = 0;

    // ===== Audio Management =====
    virtual AudioBridge* getAudioBridge() = 0;
    virtual const AudioBridge* getAudioBridge() const = 0;

    /// Track and master meters and MIDI activity, from an engine-neutral
    /// object both engines feed (#2579).
    virtual TrackMeters& meters() = 0;
    virtual const TrackMeters& meters() const = 0;

    /// Per-slot device and rack meters, from the same kind of neutral object
    /// as the track meters above (#2570).
    virtual DeviceMeters& deviceMeters() = 0;
    virtual const DeviceMeters& deviceMeters() const = 0;

    /**
     * @brief The MAGDA device rendering at @p devicePath, or null (#2585).
     *
     * What a faceplate reads its telemetry off: the oscilloscope's ring, the
     * sequencer's playing step, a compiled device's own DSP figures. The
     * instance is the one filling those, so it comes from whichever engine is
     * rendering -- the fork's plugin under Tracktion, the plan's device under
     * magda -- and never from a parallel instance nothing renders.
     *
     * Held open for as long as the handle lives, so a UI reading a ring cannot
     * be left on an instance a rebuild freed. Message thread. Null for a path
     * nothing renders yet, and for a device that is not one of MAGDA's own.
     */
    virtual std::shared_ptr<daw::audio::MagdaDevice> renderedDevice(
        const ChainNodePath& /*devicePath*/) const {
        return {};
    }

    // ===== Plugin state =====
    //
    // Only the rendering instance has an up to date state chunk, so these go
    // to whichever engine is rendering (#2581). All three are synchronous:
    // callers save the project or copy the device as soon as they return.

    /** @brief Read every live plugin's state back into the model. */
    virtual void captureAllPluginStates() = 0;

    /** @brief The same for the one device at @p devicePath. */
    virtual void capturePluginStateAt(const ChainNodePath& devicePath) = 0;

    /** @brief Write the model's state for @p devicePath into the plugin (#2573). */
    virtual void applyPluginStateAt(const ChainNodePath& devicePath) = 0;

    /**
     * @brief The plugin's own text for a parameter value, or empty (#2600).
     *
     * @p paramIndex is the plan/TE slot ParameterInfo carries and
     * @p normalised the position the live parameter holds. Empty means "this
     * engine cannot say", and every caller formats from the parameter's range
     * instead (ParameterUtils::formatValue), so an engine that answers nothing
     * degrades rather than breaks.
     *
     * Whichever engine renders the device is the one that can answer, the same
     * split as the state and the editor above.
     */
    virtual juce::String formatDeviceParameter(const ChainNodePath& /*devicePath*/,
                                               int /*paramIndex*/, float /*normalised*/) const {
        return {};
    }

    /**
     * @brief Every parameter the plugin at @p devicePath reports (#2629).
     *
     * Message thread. Empty for a path this engine holds no instance for.
     */
    virtual HostParameters describeDeviceParameters(const ChainNodePath& /*devicePath*/) const {
        return {};
    }

    /// What the plugin last reported for this parameter, if anything. A value
    /// the document holds nothing for still has to be drawable.
    virtual std::optional<float> observedParameter(const ChainNodePath& /*devicePath*/,
                                                   int /*paramIndex*/) const {
        return std::nullopt;
    }

    /// Whether an edit to this parameter was accepted and has not completed.
    /// Its completion publishes a reading of its own.
    virtual bool hostedEditPending(const ChainNodePath& /*devicePath*/, int /*paramIndex*/) const {
        return false;
    }

    /**
     * @brief Deliver a one-off @p normalised position to a hosted parameter.
     *
     * The plugin owns its ordinary parameters, so this is a command to it
     * rather than a document edit: it needs no DeviceInfo entry, no table
     * entry and no plan rebuild
     * (docs/specs/hosted-plugin-parameter-control.md). Message thread.
     *
     * An engine that cannot deliver says so in the receipt, which is what
     * keeps a knob from silently doing nothing.
     */
    /// @p completed says whether the adapter took the write and what the
    /// parameter read after it, on the message thread and later than this returns.
    virtual EditReceipt editHostedParameter(
        const ChainNodePath& /*devicePath*/, int /*paramIndex*/, float normalised,
        EditOrigin /*origin*/, std::function<void(EditCompletion)> /*completed*/ = {}) {
        return {.status = EditStatus::Unavailable, .requested = normalised};
    }

    // ===== The plugins' own windows (#2580) =====
    //
    // The editor belongs to the instance that renders, so the same split as the
    // state above. Message thread; each answers whether the window is showing
    // afterwards, which is what the slot draws.

    /// Plugin programs and preset files belong to the instance that renders.
    virtual std::optional<PluginPrograms> getPluginPrograms(const ChainNodePath&) {
        return std::nullopt;
    }
    virtual bool setPluginCurrentProgram(const ChainNodePath&, int) {
        return false;
    }
    virtual bool loadPluginPresetFile(const ChainNodePath&, const juce::File&) {
        return false;
    }
    virtual bool savePluginPresetFile(const ChainNodePath&, const juce::File&) {
        return false;
    }

    virtual bool showDeviceEditor(const ChainNodePath& devicePath) = 0;
    virtual bool hideDeviceEditor(const ChainNodePath& devicePath) = 0;
    virtual bool toggleDeviceEditor(const ChainNodePath& devicePath) = 0;
    virtual bool isDeviceEditorOpen(const ChainNodePath& devicePath) const = 0;

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

    /**
     * @brief Freeze or unfreeze @p trackId. Message thread.
     *
     * A freeze renders the track before its flag flips, modally with progress,
     * and leaves it unfrozen when there is nothing to render or the render fails.
     */
    virtual void setTrackFrozen(TrackId trackId, bool frozen) = 0;

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
