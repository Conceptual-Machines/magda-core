#pragma once

#include <memory>

#include "AudioEngine.hpp"

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
 * ## Why it holds a Tracktion engine
 *
 * AudioEngine is 80 pure virtuals and roughly 25 of them are not engine
 * questions: plugin scanning and exclusion lists, groove templates, MagdaApi,
 * PluginWindowManager, the sampler media list, the tempo-ripple command. Two
 * more, getAudioBridge and getMidiBridge, hand back types built around
 * te::Edit and cannot be answered by a non-TE engine at all.
 *
 * Narrowing that interface first would have made this a refactor with nothing
 * audible at the end of it, so instead this owns a TracktionEngineWrapper and
 * delegates that half to it. Scanning a plugin list is not audio. #2554 is
 * where the delegation is cut and the sync layer becomes compiler passes.
 *
 * Written down rather than left implicit, because a delegation nobody declared
 * becomes the architecture.

 * ## What here is temporary
 *
 * The delegation and the member behind it go at #2554. The engine-selection
 * vocabulary -- this class being a choice at all, the environment variable, the
 * setting -- goes at #2557, when there is nothing left to choose between.
 *
 * Said here because scaffolding that nobody scheduled is how a cutover leaves a
 * permanent seam behind it.
 *
 * ## What the Tracktion engine is not allowed to do
 *
 * Render. Its transport is never started and its edit never gets a
 * playback context, so the only thing filling an output buffer is this class.
 * Both engines holding the device at once is the one failure that would sound
 * like an engine bug rather than a wiring mistake.
 */

namespace magda {

class MagdaAudioEngine final : public AudioEngine {
  public:
    explicit MagdaAudioEngine(AudioEngineOptions options);
    ~MagdaAudioEngine() override;

    /// Whether the app was asked for this engine. MAGDA_AUDIO_ENGINE takes
    /// "magda" or "tracktion" and wins over the setting (#2559), so a run can
    /// be switched without a rebuild and without touching preferences.
    static bool requested(const AudioEngineOptions& options);

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
    juce::AudioDeviceManager* getDeviceManager() override;
    juce::BigInteger getEnabledWaveChannels(bool input) const override;
    void setEnabledWaveChannels(bool input, const juce::BigInteger& channels) override;
    void rescanWaveDevices(bool enableInputs, bool enableOutputs) override;
    bool isDevicesLoading() const override;
    void setDevicesLoadingCallback(
        std::function<void(bool, const juce::String&)> callback) override;
    AudioBridge* getAudioBridge() override;
    const AudioBridge* getAudioBridge() const override;
    MidiBridge* getMidiBridge() override;
    const MidiBridge* getMidiBridge() const override;
    MagdaApi& getMagdaApi() override;
    PluginWindowManager* getPluginWindowManager() override;
    const PluginWindowManager* getPluginWindowManager() const override;
    InsertRenderCaptureService* getInsertRenderCaptureService() override;
    juce::Array<juce::PluginDescription> getKnownPluginTypes() const override;
    juce::Array<juce::PluginDescription> getPreferredPluginTypes() const override;
    void addPluginListChangeListener(juce::ChangeListener* listener) override;
    void removePluginListChangeListener(juce::ChangeListener* listener) override;
    void startPluginScan(std::function<void(float, const juce::String&)> progressCallback) override;
    void abortPluginScan() override;
    void detectNewPlugins(
        std::function<void(PluginScanPhase, const juce::String&)> statusCallback,
        std::function<void(bool, int, int, const juce::StringArray&)> completionCallback) override;
    void setPluginScanCompletionCallback(
        std::function<void(bool, int, const juce::StringArray&)> callback) override;
    bool isPluginScanRunning() const override;
    std::vector<ExcludedPlugin> getExcludedPlugins() const override;
    void setExcludedPlugins(const std::vector<ExcludedPlugin>& excludedPlugins) override;
    juce::File getPluginScanReportFile() const override;
    std::vector<std::string> getSystemPluginSearchPaths() const override;
    std::vector<ScannedPluginParameter> scanPluginParameters(const juce::String& pluginId,
                                                             bool internalPlugin) override;
    bool upsertGrooveTemplate(const GrooveTemplateData& groove) override;
    juce::StringArray getGrooveTemplateNames() const override;
    std::unique_ptr<OfflineRenderSession> createOfflineRenderSession(
        bool resumePlaybackWhenFinished) override;
    std::vector<SamplerMediaReference> getSamplerMediaReferences() override;
    std::unique_ptr<UndoableCommand> createTempoSequenceRippleCommand(TempoSequenceRippleMode mode,
                                                                      BeatPosition start,
                                                                      BeatPosition end) override;
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

  private:
    /// The half of the interface that is not an engine question. See the file
    /// comment: this goes away with #2554.
    std::unique_ptr<AudioEngine> tracktion_;
};

}  // namespace magda
