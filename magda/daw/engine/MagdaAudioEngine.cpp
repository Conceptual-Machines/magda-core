#include "MagdaAudioEngine.hpp"

#include <set>
#include <string>

#include "../audio/AudioBridge.hpp"
#include "../core/UndoManager.hpp"  // complete type for the unique_ptr this forwards
#include "TracktionEngineWrapper.hpp"
#include "host/EngineHost.hpp"

namespace magda {

MagdaAudioEngine::MagdaAudioEngine(AudioEngineOptions options) {
    // The option has to reach the wrapper before initialize(), or a headless
    // caller that picked this engine opens devices, builds GUI services and
    // starts a plugin scan. The CLI is such a caller.
    auto wrapper = std::make_unique<TracktionEngineWrapper>();
    wrapper->setForceHeadless(options.headless);
    fork_ = wrapper.get();
    tracktion_ = std::move(wrapper);

    // Here rather than in initialize(), so that everything below can ask it
    // things without first asking whether it exists. It renders nothing until
    // start() puts it on a device.
    host_ = std::make_unique<daw::engine_host::EngineHost>();
}

/**
 * @brief Point the host's meters at the fork's rings (#2570).
 *
 * The only side holding both an engine that measures and a bridge that
 * publishes. Three rings because they are three readers of one measurement,
 * which is why the fork pushes to all three too.
 */
void MagdaAudioEngine::meterInto(AudioBridge* bridge) {
    if (bridge == nullptr)
        return;

    bridge->setMeteringFedElsewhere(true);

    host_->meterInto([bridge](TrackId trackId, float peakL, float peakR) {
        // No ring can hold it: MASTER_TRACK_ID is negative, and the master
        // strip reads the bridge directly.
        if (trackId == MASTER_TRACK_ID) {
            bridge->setMasterPeak(peakL, peakR);
            return;
        }

        const MeterData data{.peakL = peakL, .peakR = peakR};
        bridge->getMeteringBuffer().pushLevels(trackId, data);
        bridge->getRecordingMeteringBuffer().pushLevels(trackId, data);
        bridge->getRemoteMeteringBuffer().pushLevels(trackId, data);
    });
}

void MagdaAudioEngine::reportUnwired(const char* method, const char* issue) const {
    static std::set<std::string> said;
    if (!said.insert(method).second)
        return;

    juce::Logger::writeToLog(juce::String("[engine] ") + method +
                             " is not wired on magda::engine yet (" + issue + ")");
}

MagdaAudioEngine::~MagdaAudioEngine() = default;

// --- what magda::engine answers ----------------------------------------------
//
// Transport and what is published with it. Everything below this section still
// forwards, and each method moves up here as its subsystem is wired.

bool MagdaAudioEngine::initialize() {
    if (!tracktion_->initialize())
        return false;

    // Before the device, so the first publish can already load the plugins a
    // project names rather than going without them until the second (#2566).
    host_->setPluginServices(fork_->getPluginFormatManager(), fork_->getKnownPluginList());

    meterInto(tracktion_->getAudioBridge());

    // After the fork, because the device is its to open: the settings UI, the
    // channel lists and the driver choice are all still on that side, and two
    // device managers over one interface is the failure this class exists not
    // to have. What changes is who fills the buffer.
    if (auto* devices = tracktion_->getDeviceManager())
        host_->start(*devices);

    // Seeded from the fork rather than left at the engine's own 120 in 4/4. A
    // project whose tempo never changes after it loads would otherwise place
    // every clip at a tempo nobody chose.
    host_->setTempo(tracktion_->getTempo());

    int numerator = 4;
    int denominator = 4;
    tracktion_->getTimeSignature(numerator, denominator);
    host_->setTimeSignature(numerator, denominator);
    publishLoop();

    return true;
}
void MagdaAudioEngine::shutdown() {
    // Before the fork's, which closes the device this is rendering into.
    host_->stop();
    tracktion_->shutdown();
}
bool MagdaAudioEngine::hasActiveEdit() const {
    return tracktion_->hasActiveEdit();
}
BeatDuration MagdaAudioEngine::getEditLengthBeats() const {
    return tracktion_->getEditLengthBeats();
}
juce::File MagdaAudioEngine::getEditFile() const {
    return tracktion_->getEditFile();
}
void MagdaAudioEngine::play() {
    // The fork's guard, and it is about the device rather than about the
    // engine: both render through the one the fork opens, and starting into a
    // device that is still being enumerated is a glitch either way.
    if (tracktion_->isDevicesLoading())
        return;

    host_->play();
}
void MagdaAudioEngine::stop() {
    host_->stopPlaying();
}
void MagdaAudioEngine::pause() {
    // The engine has no pause: a stop that does not move the cursor is the same
    // thing, and is what this asks for.
    host_->stopPlaying();
}
void MagdaAudioEngine::record() {
    reportUnwired("record", "#2553");
}
void MagdaAudioEngine::locate(double positionSeconds) {
    host_->locateSeconds(positionSeconds);

    // The fork keeps the position too, because half the app still asks it
    // things about the edit. Harmless while its transport is stopped, which is
    // the one thing this class guarantees about it.
    tracktion_->locate(positionSeconds);
}
double MagdaAudioEngine::getCurrentPosition() const {
    return host_->positionSeconds();
}
bool MagdaAudioEngine::isPlaying() const {
    return host_->isPlaying();
}
bool MagdaAudioEngine::isRecording() const {
    return tracktion_->isRecording();
}
double MagdaAudioEngine::getSessionPlayheadPosition() const {
    return tracktion_->getSessionPlayheadPosition();
}
ClipId MagdaAudioEngine::getSessionPlayheadClipId() const {
    return tracktion_->getSessionPlayheadClipId();
}
std::unordered_map<ClipId, double> MagdaAudioEngine::getActiveClipPlayheadPositions() const {
    return tracktion_->getActiveClipPlayheadPositions();
}
SessionClipPlayState MagdaAudioEngine::getSessionClipPlayState(ClipId clipId) const {
    return tracktion_->getSessionClipPlayState(clipId);
}
void MagdaAudioEngine::stopSessionTrack(TrackId trackId) {
    tracktion_->stopSessionTrack(trackId);
}
bool MagdaAudioEngine::isSessionTrackStopPending(TrackId trackId) const {
    return tracktion_->isSessionTrackStopPending(trackId);
}
double MagdaAudioEngine::getAudioThreadTransportSeconds() const {
    return host_->positionSeconds();
}
void MagdaAudioEngine::deactivateAllSessionClips() {
    tracktion_->deactivateAllSessionClips();
}
// Tempo, time signature and loop reach both engines. The fork still owns the
// model -- its tempo sequence is what the ruler, the grid and every
// beats<->seconds conversion in the UI read through tempoMap() -- and the
// engine needs the same numbers to render with (#2554 is where that authority
// moves).
void MagdaAudioEngine::setTempo(double bpm) {
    tracktion_->setTempo(bpm);
    host_->setTempo(bpm);
}
double MagdaAudioEngine::getTempo() const {
    return tracktion_->getTempo();
}
void MagdaAudioEngine::setTimeSignature(int numerator, int denominator) {
    tracktion_->setTimeSignature(numerator, denominator);
    host_->setTimeSignature(numerator, denominator);
}
void MagdaAudioEngine::getTimeSignature(int& numerator, int& denominator) const {
    tracktion_->getTimeSignature(numerator, denominator);
}
const TempoMap* MagdaAudioEngine::tempoMap() const {
    return tracktion_->tempoMap();
}
void MagdaAudioEngine::setLooping(bool enabled) {
    tracktion_->setLooping(enabled);
    publishLoop();
}
void MagdaAudioEngine::setLoopRegionBeats(BeatRange range) {
    tracktion_->setLoopRegionBeats(range);
    publishLoop();
}

/// Both halves of a loop from the one place that holds them: the enable and
/// the range arrive separately and the engine takes them as one value.
void MagdaAudioEngine::publishLoop() {
    const auto range = tracktion_->getLoopRegionBeats();
    host_->setLoop(tracktion_->isLooping(), range.start.value, range.end.value);
}
bool MagdaAudioEngine::isLooping() const {
    return tracktion_->isLooping();
}
BeatRange MagdaAudioEngine::getLoopRegionBeats() const {
    return tracktion_->getLoopRegionBeats();
}
void MagdaAudioEngine::setMetronomeEnabled(bool enabled) {
    tracktion_->setMetronomeEnabled(enabled);
    host_->setMetronomeEnabled(enabled);
}
bool MagdaAudioEngine::isMetronomeEnabled() const {
    return tracktion_->isMetronomeEnabled();
}
void MagdaAudioEngine::setCountInMode(int mode) {
    tracktion_->setCountInMode(mode);
}
int MagdaAudioEngine::getCountInMode() const {
    return tracktion_->getCountInMode();
}
void MagdaAudioEngine::updateTriggerState() {
    tracktion_->updateTriggerState();
}
void MagdaAudioEngine::processSessionStateEvents() {
    tracktion_->processSessionStateEvents();
}
juce::AudioDeviceManager* MagdaAudioEngine::getDeviceManager() {
    return tracktion_->getDeviceManager();
}
juce::BigInteger MagdaAudioEngine::getEnabledWaveChannels(bool input) const {
    return tracktion_->getEnabledWaveChannels(input);
}
void MagdaAudioEngine::setEnabledWaveChannels(bool input, const juce::BigInteger& channels) {
    tracktion_->setEnabledWaveChannels(input, channels);
}
void MagdaAudioEngine::rescanWaveDevices(bool enableInputs, bool enableOutputs) {
    tracktion_->rescanWaveDevices(enableInputs, enableOutputs);
}
bool MagdaAudioEngine::isDevicesLoading() const {
    return tracktion_->isDevicesLoading();
}
void MagdaAudioEngine::setDevicesLoadingCallback(
    std::function<void(bool, const juce::String&)> callback) {
    tracktion_->setDevicesLoadingCallback(callback);
}
void MagdaAudioEngine::setPluginScanStatusCallback(
    std::function<void(const juce::String&)> callback) {
    tracktion_->setPluginScanStatusCallback(std::move(callback));
}
void MagdaAudioEngine::setMidiDevicesReadyCallback(std::function<void()> callback) {
    tracktion_->setMidiDevicesReadyCallback(std::move(callback));
}
AudioBridge* MagdaAudioEngine::getAudioBridge() {
    return tracktion_->getAudioBridge();
}
const AudioBridge* MagdaAudioEngine::getAudioBridge() const {
    return tracktion_->getAudioBridge();
}
MidiBridge* MagdaAudioEngine::getMidiBridge() {
    return tracktion_->getMidiBridge();
}
const MidiBridge* MagdaAudioEngine::getMidiBridge() const {
    return tracktion_->getMidiBridge();
}
MagdaApi& MagdaAudioEngine::getMagdaApi() {
    return tracktion_->getMagdaApi();
}
PluginWindowManager* MagdaAudioEngine::getPluginWindowManager() {
    return tracktion_->getPluginWindowManager();
}
const PluginWindowManager* MagdaAudioEngine::getPluginWindowManager() const {
    return tracktion_->getPluginWindowManager();
}
InsertRenderCaptureService* MagdaAudioEngine::getInsertRenderCaptureService() {
    return tracktion_->getInsertRenderCaptureService();
}
juce::Array<juce::PluginDescription> MagdaAudioEngine::getKnownPluginTypes() const {
    return tracktion_->getKnownPluginTypes();
}
juce::Array<juce::PluginDescription> MagdaAudioEngine::getPreferredPluginTypes() const {
    return tracktion_->getPreferredPluginTypes();
}
void MagdaAudioEngine::addPluginListChangeListener(juce::ChangeListener* listener) {
    tracktion_->addPluginListChangeListener(listener);
}
void MagdaAudioEngine::removePluginListChangeListener(juce::ChangeListener* listener) {
    tracktion_->removePluginListChangeListener(listener);
}
void MagdaAudioEngine::startPluginScan(
    std::function<void(float, const juce::String&)> progressCallback) {
    tracktion_->startPluginScan(progressCallback);
}
void MagdaAudioEngine::abortPluginScan() {
    tracktion_->abortPluginScan();
}
void MagdaAudioEngine::detectNewPlugins(
    std::function<void(PluginScanPhase, const juce::String&)> statusCallback,
    std::function<void(bool, int, int, const juce::StringArray&)> completionCallback) {
    tracktion_->detectNewPlugins(statusCallback, completionCallback);
}
void MagdaAudioEngine::setPluginScanCompletionCallback(
    std::function<void(bool, int, const juce::StringArray&)> callback) {
    tracktion_->setPluginScanCompletionCallback(callback);
}
bool MagdaAudioEngine::isPluginScanRunning() const {
    return tracktion_->isPluginScanRunning();
}
std::vector<ExcludedPlugin> MagdaAudioEngine::getExcludedPlugins() const {
    return tracktion_->getExcludedPlugins();
}
void MagdaAudioEngine::setExcludedPlugins(const std::vector<ExcludedPlugin>& excludedPlugins) {
    tracktion_->setExcludedPlugins(excludedPlugins);
}
juce::File MagdaAudioEngine::getPluginScanReportFile() const {
    return tracktion_->getPluginScanReportFile();
}
std::vector<std::string> MagdaAudioEngine::getSystemPluginSearchPaths() const {
    return tracktion_->getSystemPluginSearchPaths();
}
std::vector<ScannedPluginParameter> MagdaAudioEngine::scanPluginParameters(
    const juce::String& pluginId, bool internalPlugin) {
    return tracktion_->scanPluginParameters(pluginId, internalPlugin);
}
bool MagdaAudioEngine::upsertGrooveTemplate(const GrooveTemplateData& groove) {
    return tracktion_->upsertGrooveTemplate(groove);
}
juce::StringArray MagdaAudioEngine::getGrooveTemplateNames() const {
    return tracktion_->getGrooveTemplateNames();
}
std::unique_ptr<OfflineRenderSession> MagdaAudioEngine::createOfflineRenderSession(
    bool resumePlaybackWhenFinished) {
    return tracktion_->createOfflineRenderSession(resumePlaybackWhenFinished);
}
std::vector<SamplerMediaReference> MagdaAudioEngine::getSamplerMediaReferences() {
    return tracktion_->getSamplerMediaReferences();
}
std::unique_ptr<UndoableCommand> MagdaAudioEngine::createTempoSequenceRippleCommand(
    TempoSequenceRippleMode mode, BeatPosition start, BeatPosition end) {
    return tracktion_->createTempoSequenceRippleCommand(mode, start, end);
}
void MagdaAudioEngine::previewNoteOnTrack(const std::string& track_id, int noteNumber, int velocity,
                                          bool isNoteOn) {
    tracktion_->previewNoteOnTrack(track_id, noteNumber, velocity, isNoteOn);
}
// The UI's own transport, which TimelineController drives. Deliberately not
// forwarded: the fork's versions of these are locate-and-play, and starting its
// transport is the one thing this class must never do.
void MagdaAudioEngine::onTransportPlay(double positionSeconds) {
    locate(positionSeconds);
    play();
}
void MagdaAudioEngine::onTransportStop(double returnPositionSeconds) {
    stop();
    locate(returnPositionSeconds);
}
void MagdaAudioEngine::onTransportPause() {
    pause();
}
void MagdaAudioEngine::onTransportRecord(double positionSeconds) {
    juce::ignoreUnused(positionSeconds);
    reportUnwired("onTransportRecord", "#2553");
}
void MagdaAudioEngine::onTransportStopRecording() {
    reportUnwired("onTransportStopRecording", "#2553");
}
void MagdaAudioEngine::onEditPositionChanged(double positionSeconds) {
    // Only while stopped, which is the fork's rule and the right one: this
    // fires whenever the edit cursor moves, and clicking in the piano roll to
    // place a note moves it. Seeking on that would drag the transport out from
    // under whoever is listening.
    if (!isPlaying())
        locate(positionSeconds);
}
void MagdaAudioEngine::onTempoChanged(double bpm) {
    tracktion_->onTempoChanged(bpm);
    host_->setTempo(bpm);
}
void MagdaAudioEngine::onTimeSignatureChanged(int numerator, int denominator) {
    tracktion_->onTimeSignatureChanged(numerator, denominator);
    host_->setTimeSignature(numerator, denominator);
}
void MagdaAudioEngine::onLoopRegionChanged(double startSeconds, double endSeconds, bool enabled) {
    tracktion_->onLoopRegionChanged(startSeconds, endSeconds, enabled);
    publishLoop();
}
void MagdaAudioEngine::onLoopEnabledChanged(bool enabled) {
    tracktion_->onLoopEnabledChanged(enabled);
    publishLoop();
}

// --- the bases' defaulted virtuals -------------------------------------------

void MagdaAudioEngine::armSessionSlotRecording(TrackId trackId, int sceneIndex) {
    tracktion_->armSessionSlotRecording(trackId, sceneIndex);
}

void MagdaAudioEngine::beginArmedSessionSlotRecordings() {
    tracktion_->beginArmedSessionSlotRecordings();
}

bool MagdaAudioEngine::isSessionSlotRecordArmed(TrackId trackId, int sceneIndex) const {
    return tracktion_->isSessionSlotRecordArmed(trackId, sceneIndex);
}

bool MagdaAudioEngine::isSessionSlotRecording(TrackId trackId, int sceneIndex) const {
    return tracktion_->isSessionSlotRecording(trackId, sceneIndex);
}

const std::unordered_map<TrackId, RecordingPreview>& MagdaAudioEngine::getRecordingPreviews()
    const {
    return tracktion_->getRecordingPreviews();
}

void MagdaAudioEngine::onPunchRegionChanged(double startSeconds, double endSeconds,
                                            bool punchInEnabled, bool punchOutEnabled) {
    tracktion_->onPunchRegionChanged(startSeconds, endSeconds, punchInEnabled, punchOutEnabled);
}

void MagdaAudioEngine::onPunchEnabledChanged(bool punchInEnabled, bool punchOutEnabled) {
    tracktion_->onPunchEnabledChanged(punchInEnabled, punchOutEnabled);
}

}  // namespace magda
