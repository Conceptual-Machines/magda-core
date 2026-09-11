#include "MagdaAudioEngine.hpp"

#include <set>
#include <string>

#include "../api/magda_api_live.hpp"
#include "../core/TrackManager.hpp"
#include "../core/UndoManager.hpp"  // complete type for the unique_ptr this forwards
#include "TracktionEngineWrapper.hpp"
#include "host/EngineHost.hpp"

namespace magda::daw::engine_host {
/// Declared rather than included: EngineProject.hpp reaches magda/engine's own
/// headers, which magda_daw does not see (EngineHost.hpp says why).
double projectEndBeat();
}  // namespace magda::daw::engine_host

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

    // No edit accessor: the half of the API that reads a te::Edit is #2554's.
    // The MidiBridge arrives in initialize(), once the fork has built it.
    api_ = std::make_unique<MagdaApiLive>();
    api_->setProjectTempoWriter([this](double bpm) { setTempo(bpm); });
    api_->setProjectTimeSignatureWriter(
        [this](int numerator, int denominator) { setTimeSignature(numerator, denominator); });
}

/**
 * @brief Point the host's meters at meters_ (#2579).
 *
 * Three rings because they are three readers of one measurement.
 */
void MagdaAudioEngine::meterInto() {
    host_->meterInto([this](TrackId trackId, float peakL, float peakR) {
        // No ring can hold it: MASTER_TRACK_ID is negative.
        if (trackId == MASTER_TRACK_ID) {
            meters_.setMasterPeak(peakL, peakR);
            return;
        }

        const MeterData data{.peakL = peakL, .peakR = peakR};
        meters_.mixer.pushLevels(trackId, data);
        meters_.recording.pushLevels(trackId, data);
        meters_.remote.pushLevels(trackId, data);
    });
}

void MagdaAudioEngine::reportUnwired(const char* method, const char* issue) const {
    static std::set<std::string> said;
    if (!said.insert(method).second)
        return;

    juce::Logger::writeToLog(juce::String("[engine] ") + method +
                             " is not wired on magda::engine yet (" + issue + ")");
}

MagdaAudioEngine::~MagdaAudioEngine() {
    // The app destroys the engine with a plain reset() and no shutdown() call
    // (magda_daw_main.cpp), which would leave the MidiBridge pushing live
    // notes through a destroyed sink and the host rendering from a device it
    // never came off. Safe twice: every step below is.
    shutdown();
}

// --- what magda::engine answers ----------------------------------------------
//
// Transport and what is published with it. Below this section are the fork's
// remaining services and what nothing answers yet, each named with its issue.

bool MagdaAudioEngine::initialize() {
    // Services alone: an Edit would come with a playback context, an
    // AudioBridge mirroring every device into it and a second copy of every
    // external plugin (#2579).
    if (!fork_->initialiseServices())
        return false;

    // Before the device, so the first publish can already load the plugins a
    // project names rather than going without them until the second (#2566).
    host_->setPluginServices(fork_->getPluginFormatManager(), fork_->getKnownPluginList());

    meterInto();

    // Named before the first publish, so a track routed to "all" hears the
    // keyboard from its first note rather than from the next republish. The
    // system's MIDI list never holds it, so it is registered as virtual or a
    // device scan would take it back out of every "all" route.
    host_->registerVirtualMidiSource(qwertyMidiDeviceId());

    // After the fork, because the device is its to open: the settings UI, the
    // channel lists and the driver choice are all still on that side, and two
    // device managers over one interface is the failure this class exists not
    // to have. What changes is who fills the buffer.
    if (auto* devices = tracktion_->getDeviceManager())
        host_->start(*devices);

    // The MidiBridge is a service both engines share, so the activity light and
    // every live note are pointed at this engine's meters and this engine's
    // queue rather than at the fork's.
    auto* midi = fork_->getMidiBridge();
    api_->setMidiBridge(midi);

    if (midi != nullptr) {
        midi->setMeters(&meters_);
        midi->setLiveSink(this);
    }

    initialised_ = true;
    return true;
}
void MagdaAudioEngine::shutdown() {
    // The host is destroyed before the fork (member order), so the sink has to
    // be gone before the queue behind it is. setLiveSink(nullptr) returns only
    // once any in-flight MIDI callback has left.
    if (auto* midi = fork_->getMidiBridge()) {
        midi->setLiveSink(nullptr);
        midi->setMeters(nullptr);
    }

    // The bridge goes with the fork below, and the API outlives this call.
    api_->setMidiBridge(nullptr);

    // Before the fork's, which closes the device this is rendering into.
    host_->stop();
    tracktion_->shutdown();
    initialised_ = false;
}
bool MagdaAudioEngine::hasActiveEdit() const {
    return initialised_;
}
BeatDuration MagdaAudioEngine::getEditLengthBeats() const {
    return BeatDuration{daw::engine_host::projectEndBeat()};
}
juce::File MagdaAudioEngine::getEditFile() const {
    // What the fork answers with no Edit: the file is the project layer's.
    return juce::File{};
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
// Tempo, time signature and loop are the host's: there is no Edit to hold a
// second copy of them, and what the ruler converts through is the same map the
// engine renders with. Tempo automation reaching it is #2554.
void MagdaAudioEngine::setTempo(double bpm) {
    host_->setTempo(bpm);
}
double MagdaAudioEngine::getTempo() const {
    return host_->tempo();
}
void MagdaAudioEngine::setTimeSignature(int numerator, int denominator) {
    host_->setTimeSignature(numerator, denominator);
}
void MagdaAudioEngine::getTimeSignature(int& numerator, int& denominator) const {
    host_->getTimeSignature(numerator, denominator);
}
const TempoMap* MagdaAudioEngine::tempoMap() const {
    return host_->tempoMap();
}
void MagdaAudioEngine::setLooping(bool enabled) {
    const auto loop = host_->loop();
    host_->setLoop(enabled, loop.startBeat, loop.endBeat);
}
void MagdaAudioEngine::setLoopRegionBeats(BeatRange range) {
    host_->setLoop(host_->loop().enabled, range.start.value, range.end.value);
}
bool MagdaAudioEngine::isLooping() const {
    return host_->loop().enabled;
}
BeatRange MagdaAudioEngine::getLoopRegionBeats() const {
    const auto loop = host_->loop();
    return {{loop.startBeat}, {loop.endBeat}};
}
void MagdaAudioEngine::setMetronomeEnabled(bool enabled) {
    host_->setMetronomeEnabled(enabled);
}
bool MagdaAudioEngine::isMetronomeEnabled() const {
    return host_->isMetronomeEnabled();
}
void MagdaAudioEngine::setCountInMode(int mode) {
    countInMode_ = mode;
}
int MagdaAudioEngine::getCountInMode() const {
    return countInMode_;
}

/// The play-start and loop edges the modulators retrigger on, once a frame.
void MagdaAudioEngine::updateTriggerState() {
    const bool playing = host_->isPlaying();
    const double position = host_->positionSeconds();

    const bool justStarted = playing && !wasPlaying_;

    // 100 ms backwards, the fork's own threshold: anything smaller is jitter
    // rather than a loop.
    const bool justLooped = playing && isLooping() && lastPosition_ - position > 0.1;

    wasPlaying_ = playing;
    lastPosition_ = position;

    TrackManager::getInstance().updateTransportState(playing, getTempo(), justStarted, justLooped);
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
// Null because there is no Edit for one to mirror devices into: the file
// comment says what the fork is under this engine.
AudioBridge* MagdaAudioEngine::getAudioBridge() {
    return nullptr;
}
const AudioBridge* MagdaAudioEngine::getAudioBridge() const {
    return nullptr;
}

/**
 * @brief Both engines, in that order (#2581).
 *
 * The fork first, because a MAGDA device's state is captured nowhere else and
 * its synced plugin is still where that is read from. The instances this
 * renders through go over the top: for an external plugin the fork holds a
 * parallel copy that never heard a note, and the chunk that copy writes is the
 * one the project was loaded with.
 */
void MagdaAudioEngine::captureAllPluginStates() {
    tracktion_->captureAllPluginStates();

    if (host_ != nullptr)
        host_->captureExternalPluginStates();
}

void MagdaAudioEngine::capturePluginStateAt(const ChainNodePath& devicePath) {
    tracktion_->capturePluginStateAt(devicePath);

    if (host_ != nullptr)
        host_->captureExternalPluginStateAt(devicePath);
}

void MagdaAudioEngine::applyPluginStateAt(const ChainNodePath& devicePath) {
    tracktion_->applyPluginStateAt(devicePath);

    if (host_ != nullptr)
        host_->applyExternalPluginStateAt(devicePath);
}
MidiBridge* MagdaAudioEngine::getMidiBridge() {
    return tracktion_->getMidiBridge();
}
const MidiBridge* MagdaAudioEngine::getMidiBridge() const {
    return tracktion_->getMidiBridge();
}
MagdaApi& MagdaAudioEngine::getMagdaApi() {
    return *api_;
}
PluginWindowManager* MagdaAudioEngine::getPluginWindowManager() {
    // It drives te::ExternalPlugin::windowState, and the instance a window
    // would open onto is not the one rendering.
    reportUnwired("getPluginWindowManager", "#2580");
    return nullptr;
}
const PluginWindowManager* MagdaAudioEngine::getPluginWindowManager() const {
    return nullptr;
}
InsertRenderCaptureService* MagdaAudioEngine::getInsertRenderCaptureService() {
    return nullptr;
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
    juce::ignoreUnused(resumePlaybackWhenFinished);
    reportUnwired("createOfflineRenderSession", "#2555");
    return nullptr;
}
std::vector<SamplerMediaReference> MagdaAudioEngine::getSamplerMediaReferences() {
    reportUnwired("getSamplerMediaReferences", "#2554");
    return {};
}
std::unique_ptr<UndoableCommand> MagdaAudioEngine::createTempoSequenceRippleCommand(
    TempoSequenceRippleMode mode, BeatPosition start, BeatPosition end) {
    juce::ignoreUnused(mode, start, end);
    reportUnwired("createTempoSequenceRippleCommand", "#2554");
    return nullptr;
}
void MagdaAudioEngine::previewNoteOnTrack(const std::string& track_id, int noteNumber, int velocity,
                                          bool isNoteOn) {
    TrackId trackId = INVALID_TRACK_ID;
    try {
        trackId = std::stoi(track_id);
    } catch (const std::exception&) {
        return;
    }

    // Channel 1, like the fork's preview, and auditioned rather than routed:
    // the track it is played on need not be monitoring anything (#762).
    host_->audition(
        trackId,
        isNoteOn ? juce::MidiMessage::noteOn(1, noteNumber, static_cast<juce::uint8>(velocity))
                 : juce::MidiMessage::noteOff(1, noteNumber, static_cast<juce::uint8>(velocity)));
}
void MagdaAudioEngine::pushMidi(const juce::String& deviceId, const juce::MidiMessage& message) {
    host_->pushMidi(deviceId, message);
}
void MagdaAudioEngine::audition(TrackId trackId, const juce::MidiMessage& message) {
    host_->audition(trackId, message);
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
    host_->setTempo(bpm);
}
void MagdaAudioEngine::onTimeSignatureChanged(int numerator, int denominator) {
    host_->setTimeSignature(numerator, denominator);
}
void MagdaAudioEngine::onLoopRegionChanged(double startSeconds, double endSeconds, bool enabled) {
    // The UI sends a loop in seconds; the transport is published in beats.
    const auto* map = host_->tempoMap();
    host_->setLoop(enabled, map->timeToBeat(startSeconds), map->timeToBeat(endSeconds));
}
void MagdaAudioEngine::onLoopEnabledChanged(bool enabled) {
    setLooping(enabled);
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
