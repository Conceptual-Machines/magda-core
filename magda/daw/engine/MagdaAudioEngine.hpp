#pragma once

#include <functional>
#include <map>
#include <memory>

#include "../audio/DeviceMeters.hpp"
#include "../audio/MidiBridge.hpp"
#include "../audio/TrackMeters.hpp"
#include "../audio/io/AudioIOService.hpp"
#include "AudioEngine.hpp"
#include "AudioEngineChoice.hpp"
#include "PluginService.hpp"

namespace magda::daw::engine_host {
class EngineHost;
}

namespace magda {
class GrooveStore;
class MagdaApiLive;
}  // namespace magda

/**
 * @file MagdaAudioEngine.hpp
 * @brief The app's second AudioEngine, backed by magda::engine (#2551).
 *
 * The engine has seven finished subsystems and a corpus of 74 cases, and until
 * this class existed none of it had ever been driven by an audio device: there
 * was no caller of EngineSession outside the test binaries. What is left to
 * settle -- monitor round trip, input latency, how a plugin behaves under a
 * real host -- cannot be settled offline, so the engine has to be reachable
 * from a running app before the rest of #1897 is worth doing.
 *
 * Selected in createDefaultAudioEngine rather than chosen at build time, so
 * both engines ship in one binary and switching them is a setting.
 *
 * ## What magda::engine answers
 *
 * Transport, tempo, loop and metronome are held here, published to an
 * EngineSession and rendered from the audio device callback: what fills the
 * output buffer is magda::engine and nothing else, and what the ruler converts
 * through is the map it renders with. Tempo automation is #2554.
 *
 * Recording and session launch are not wired yet (#2552, #2553) and say so
 * once in the log rather than answering silently.
 *
 * No Tracktion object sits under it (#2761). What is not an engine question --
 * plugin lists, grooves, MIDI, the project save hooks -- belongs to the app's
 * services, which this lends what they need and takes back at shutdown.
 *
 * ## What here is temporary
 *
 * The engine-selection vocabulary -- this class being a choice at all, the
 * environment variable, the setting -- goes at #2557, when there is nothing left
 * to choose between.
 */

namespace magda {

class MagdaAudioEngine final : public AudioEngine,
                               public PluginStateProvider,
                               public LiveMidiSink,
                               private HardwareChannels::Listener,
                               private MidiBridge::Listener {
  public:
    explicit MagdaAudioEngine(AudioEngineOptions options);
    ~MagdaAudioEngine() override;

    juce::String engineName() const override {
        return nameOf(AudioEngineChoice::Magda);
    }

    bool initialize() override;
    void shutdown() override;
    bool hasActiveEdit() const override;
    BeatDuration getEditLengthBeats() const override;
    juce::File getEditFile() const override;
    void play() override;
    void stop() override;
    void pause() override;
    void record() override;
    void locate(double positionSeconds) override;
    double getCurrentPosition() const override;
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
    void setTempo(double bpm) override;
    double getTempo() const override;
    void setTimeSignature(int numerator, int denominator) override;
    void getTimeSignature(int& numerator, int& denominator) const override;
    const TempoMap* tempoMap() const override;
    void setLooping(bool enabled) override;
    void setLoopRegionBeats(BeatRange range) override;
    bool isLooping() const override;
    BeatRange getLoopRegionBeats() const override;
    void setMetronomeEnabled(bool enabled) override;
    bool isMetronomeEnabled() const override;
    void setCountInMode(int mode) override;
    int getCountInMode() const override;
    void updateTriggerState() override;
    void processSessionStateEvents() override;
    AudioIOControl* getAudioIO() override;
    void setMidiDevicesReadyCallback(std::function<void()> callback) override;
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
    void captureAllPluginStates() override;
    void capturePluginStateAt(const ChainNodePath& devicePath) override;
    void applyPluginStateAt(const ChainNodePath& devicePath) override;
    void projectAuthoredStateAt(const ChainNodePath& devicePath) override;
    std::optional<PluginPrograms> getPluginPrograms(const ChainNodePath& devicePath) override;
    bool setPluginCurrentProgram(const ChainNodePath& devicePath, int programIndex) override;
    bool loadPluginPresetFile(const ChainNodePath& devicePath, const juce::File& file) override;
    bool savePluginPresetFile(const ChainNodePath& devicePath, const juce::File& file) override;

    bool showDeviceEditor(const ChainNodePath& devicePath) override;
    bool hideDeviceEditor(const ChainNodePath& devicePath) override;
    bool toggleDeviceEditor(const ChainNodePath& devicePath) override;
    bool isDeviceEditorOpen(const ChainNodePath& devicePath) const override;

    std::shared_ptr<daw::audio::MagdaDevice> renderedDevice(
        const ChainNodePath& devicePath) const override;
    HostParameters describeDeviceParameters(const ChainNodePath& devicePath) const override;
    std::optional<float> observedParameter(const ChainNodePath& devicePath,
                                           int paramIndex) const override;
    bool hostedEditPending(const ChainNodePath& devicePath, int paramIndex) const override;
    EditReceipt editHostedParameter(const ChainNodePath& devicePath, int paramIndex,
                                    float normalised, EditOrigin origin,
                                    std::function<void(EditCompletion)> completed = {}) override;
    MagdaApi& getMagdaApi() override;
    InsertRenderCapture* getInsertRenderCapture() override;
    std::unique_ptr<OfflineRenderSession> createOfflineRenderSession(
        bool resumePlaybackWhenFinished) override;
    void setTrackFrozen(TrackId trackId, bool frozen) override;
    void previewNoteOnTrack(const std::string& track_id, int noteNumber, int velocity,
                            bool isNoteOn) override;
    void onTransportPlay(double positionSeconds) override;
    void onTransportStop(double returnPositionSeconds) override;
    void onTransportPause() override;
    void onTransportRecord(double positionSeconds) override;
    void onTransportStopRecording() override;
    void onEditPositionChanged(double positionSeconds) override;
    void onTempoChanged(double bpm) override;
    void onTimeSignatureChanged(int numerator, int denominator) override;
    void onLoopRegionChanged(double startSeconds, double endSeconds, bool enabled) override;
    void onLoopEnabledChanged(bool enabled) override;

    /** @brief A message from @p deviceId, for whichever tracks are routed to it. */
    void pushMidi(const juce::String& deviceId, const juce::MidiMessage& message) override;

    /** @brief A note played at @p trackId itself: the piano roll, pads and chords. */
    void audition(TrackId trackId, const juce::MidiMessage& message) override;

#ifdef MAGDA_ENABLE_TEST_HOOKS
    /** @brief Every method that has named itself unwired, in the order it did. */
    static juce::StringArray unwiredMethods();

    /** @brief What the parity bench waits on after a load and reads latency from (#2082). */
    daw::engine_host::EngineHost& hostForTesting() {
        return *host_;
    }
#endif

    // Overridden because AudioEngine and AudioEngineListener give these default
    // bodies rather than leaving them pure. Not forwarding them compiles
    // perfectly and then answers no-op, false and empty for the rest of the
    // run, which is the failure a generated surface cannot see.
    void armSessionSlotRecording(TrackId trackId, int sceneIndex) override;
    void beginArmedSessionSlotRecordings() override;
    bool isSessionSlotRecordArmed(TrackId trackId, int sceneIndex) const override;
    bool isSessionSlotRecording(TrackId trackId, int sceneIndex) const override;
    const std::unordered_map<TrackId, RecordingPreview>& getRecordingPreviews() const override;
    void onPunchRegionChanged(double startSeconds, double endSeconds, bool punchInEnabled,
                              bool punchOutEnabled) override;
    void onPunchEnabledChanged(bool punchInEnabled, bool punchOutEnabled) override;
    bool hasSampleAccuratePunch() const override {
        return true;
    }

  private:
    /** @brief Wire the host's levels into @ref meters_. Once, in initialize(). */
    void meterInto();

    /** @brief Say once that @p method has not moved to magda::engine yet. */
    void reportUnwired(const char* method, const char* issue) const;

    void hardwareChannelsChanged() override;
    void midiDeviceListChanged() override;

    /// Track and master meters, fed by the host (#2579).
    TrackMeters meters_;

    /// Per-slot device meters, fed by the host from the tap behind each
    /// slot's Meter op (#2570).
    DeviceMeters deviceMeters_;

    /// Whether initialize() brought the services up. What "there is a project"
    /// means with no Edit to ask.
    bool initialised_ = false;

    /// Asked to run without devices or a UI (app_services::isHeadless).
    bool headless_ = false;

    /// Last frame's transport, for the play-start and loop edges modulators
    /// retrigger on.
    bool wasPlaying_ = false;
    double lastPosition_ = 0.0;

    /// Told on a MIDI device-list change, when a device is listed.
    std::function<void()> midiDevicesReady_;

    /// The groove library's persistence while this engine is up.
    std::unique_ptr<GrooveStore> grooveStore_;

    /// The one audio interface (#2747).
    std::unique_ptr<AudioIOService> audioIO_;

    /// What actually renders. Declared after the interface so it is destroyed
    /// first, being a callback on it.
    std::unique_ptr<daw::engine_host::EngineHost> host_;

    /// This engine's own facade onto the model.
    std::unique_ptr<MagdaApiLive> api_;
};

}  // namespace magda
