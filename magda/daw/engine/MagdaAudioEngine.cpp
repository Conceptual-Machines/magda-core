#include "MagdaAudioEngine.hpp"

#include <cstdlib>

#include "../core/UndoManager.hpp"  // complete type for the unique_ptr this forwards
#include "TracktionEngineWrapper.hpp"

namespace magda {

MagdaAudioEngine::MagdaAudioEngine() : tracktion_(std::make_unique<TracktionEngineWrapper>()) {}

MagdaAudioEngine::~MagdaAudioEngine() = default;

bool MagdaAudioEngine::requested(const AudioEngineOptions& options) {
    juce::ignoreUnused(options);

    // Named symmetrically, and by what each engine is rather than by which one
    // is the newcomer: "native" only means anything while there is something
    // for it to be native against, and after #2557 there will not be.
    //
    // The environment wins over the setting (#2559), so a bug report asking
    // "does it still happen on the other engine" is answered by one run rather
    // than by changing somebody's preferences.
    if (const auto* value = std::getenv("MAGDA_AUDIO_ENGINE")) {
        const auto choice = juce::String(value).trim().toLowerCase();
        if (choice == "magda")
            return true;
        if (choice == "tracktion")
            return false;
    }

    return false;
}

// --- the delegated half ------------------------------------------------------
//
// Everything below forwards. Each method moves up into a native implementation
// as its subsystem is wired, and what is left when #2554 lands is what actually
// had to be on this interface.

bool MagdaAudioEngine::initialize() {
    // Said once, out loud. Which engine a session ran on is the first question
    // any report about it raises, and the answer should not need a debugger.
    juce::Logger::writeToLog("[engine] rendering through magda::engine (#2551)");
    return tracktion_->initialize();
}
void MagdaAudioEngine::shutdown() {
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
    tracktion_->play();
}
void MagdaAudioEngine::stop() {
    tracktion_->stop();
}
void MagdaAudioEngine::pause() {
    tracktion_->pause();
}
void MagdaAudioEngine::record() {
    tracktion_->record();
}
void MagdaAudioEngine::locate(double positionSeconds) {
    tracktion_->locate(positionSeconds);
}
double MagdaAudioEngine::getCurrentPosition() const {
    return tracktion_->getCurrentPosition();
}
bool MagdaAudioEngine::isPlaying() const {
    return tracktion_->isPlaying();
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
    return tracktion_->getAudioThreadTransportSeconds();
}
void MagdaAudioEngine::deactivateAllSessionClips() {
    tracktion_->deactivateAllSessionClips();
}
void MagdaAudioEngine::setTempo(double bpm) {
    tracktion_->setTempo(bpm);
}
double MagdaAudioEngine::getTempo() const {
    return tracktion_->getTempo();
}
void MagdaAudioEngine::setTimeSignature(int numerator, int denominator) {
    tracktion_->setTimeSignature(numerator, denominator);
}
void MagdaAudioEngine::getTimeSignature(int& numerator, int& denominator) const {
    tracktion_->getTimeSignature(numerator, denominator);
}
const TempoMap* MagdaAudioEngine::tempoMap() const {
    return tracktion_->tempoMap();
}
void MagdaAudioEngine::setLooping(bool enabled) {
    tracktion_->setLooping(enabled);
}
void MagdaAudioEngine::setLoopRegionBeats(BeatRange range) {
    tracktion_->setLoopRegionBeats(range);
}
bool MagdaAudioEngine::isLooping() const {
    return tracktion_->isLooping();
}
BeatRange MagdaAudioEngine::getLoopRegionBeats() const {
    return tracktion_->getLoopRegionBeats();
}
void MagdaAudioEngine::setMetronomeEnabled(bool enabled) {
    tracktion_->setMetronomeEnabled(enabled);
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
void MagdaAudioEngine::onTransportPlay(double positionSeconds) {
    tracktion_->onTransportPlay(positionSeconds);
}
void MagdaAudioEngine::onTransportStop(double returnPositionSeconds) {
    tracktion_->onTransportStop(returnPositionSeconds);
}
void MagdaAudioEngine::onTransportPause() {
    tracktion_->onTransportPause();
}
void MagdaAudioEngine::onTransportRecord(double positionSeconds) {
    tracktion_->onTransportRecord(positionSeconds);
}
void MagdaAudioEngine::onTransportStopRecording() {
    tracktion_->onTransportStopRecording();
}
void MagdaAudioEngine::onEditPositionChanged(double positionSeconds) {
    tracktion_->onEditPositionChanged(positionSeconds);
}
void MagdaAudioEngine::onTempoChanged(double bpm) {
    tracktion_->onTempoChanged(bpm);
}
void MagdaAudioEngine::onTimeSignatureChanged(int numerator, int denominator) {
    tracktion_->onTimeSignatureChanged(numerator, denominator);
}
void MagdaAudioEngine::onLoopRegionChanged(double startSeconds, double endSeconds, bool enabled) {
    tracktion_->onLoopRegionChanged(startSeconds, endSeconds, enabled);
}
void MagdaAudioEngine::onLoopEnabledChanged(bool enabled) {
    tracktion_->onLoopEnabledChanged(enabled);
}

}  // namespace magda
