#include "NativeAudioEngine.hpp"

#include <cstdlib>

#include "../core/UndoManager.hpp"  // complete type for the unique_ptr this forwards
#include "TracktionEngineWrapper.hpp"

namespace magda {

NativeAudioEngine::NativeAudioEngine() : fork_(std::make_unique<TracktionEngineWrapper>()) {}

NativeAudioEngine::~NativeAudioEngine() = default;

bool NativeAudioEngine::requested(const AudioEngineOptions& options) {
    juce::ignoreUnused(options);

    // The environment wins, so a single run can be switched without touching
    // the settings file -- which is what a bug report asking "does it do this
    // on the other engine" needs.
    if (const auto* value = std::getenv("MAGDA_NATIVE_ENGINE")) {
        juce::String flag(value);
        flag = flag.trim().toLowerCase();
        return flag.isNotEmpty() && flag != "0" && flag != "false" && flag != "off" && flag != "no";
    }

    return false;
}

// --- the delegated half ------------------------------------------------------
//
// Everything below forwards. Each method moves up into a native implementation
// as its subsystem is wired, and what is left when #2554 lands is what actually
// had to be on this interface.

bool NativeAudioEngine::initialize() {
    // Said once, out loud. Which engine a session ran on is the first question
    // any report about it raises, and the answer should not need a debugger.
    juce::Logger::writeToLog("[engine] MAGDA native engine selected (#2551)");
    return fork_->initialize();
}
void NativeAudioEngine::shutdown() {
    fork_->shutdown();
}
bool NativeAudioEngine::hasActiveEdit() const {
    return fork_->hasActiveEdit();
}
BeatDuration NativeAudioEngine::getEditLengthBeats() const {
    return fork_->getEditLengthBeats();
}
juce::File NativeAudioEngine::getEditFile() const {
    return fork_->getEditFile();
}
void NativeAudioEngine::play() {
    fork_->play();
}
void NativeAudioEngine::stop() {
    fork_->stop();
}
void NativeAudioEngine::pause() {
    fork_->pause();
}
void NativeAudioEngine::record() {
    fork_->record();
}
void NativeAudioEngine::locate(double positionSeconds) {
    fork_->locate(positionSeconds);
}
double NativeAudioEngine::getCurrentPosition() const {
    return fork_->getCurrentPosition();
}
bool NativeAudioEngine::isPlaying() const {
    return fork_->isPlaying();
}
bool NativeAudioEngine::isRecording() const {
    return fork_->isRecording();
}
double NativeAudioEngine::getSessionPlayheadPosition() const {
    return fork_->getSessionPlayheadPosition();
}
ClipId NativeAudioEngine::getSessionPlayheadClipId() const {
    return fork_->getSessionPlayheadClipId();
}
std::unordered_map<ClipId, double> NativeAudioEngine::getActiveClipPlayheadPositions() const {
    return fork_->getActiveClipPlayheadPositions();
}
SessionClipPlayState NativeAudioEngine::getSessionClipPlayState(ClipId clipId) const {
    return fork_->getSessionClipPlayState(clipId);
}
void NativeAudioEngine::stopSessionTrack(TrackId trackId) {
    fork_->stopSessionTrack(trackId);
}
bool NativeAudioEngine::isSessionTrackStopPending(TrackId trackId) const {
    return fork_->isSessionTrackStopPending(trackId);
}
double NativeAudioEngine::getAudioThreadTransportSeconds() const {
    return fork_->getAudioThreadTransportSeconds();
}
void NativeAudioEngine::deactivateAllSessionClips() {
    fork_->deactivateAllSessionClips();
}
void NativeAudioEngine::setTempo(double bpm) {
    fork_->setTempo(bpm);
}
double NativeAudioEngine::getTempo() const {
    return fork_->getTempo();
}
void NativeAudioEngine::setTimeSignature(int numerator, int denominator) {
    fork_->setTimeSignature(numerator, denominator);
}
void NativeAudioEngine::getTimeSignature(int& numerator, int& denominator) const {
    fork_->getTimeSignature(numerator, denominator);
}
const TempoMap* NativeAudioEngine::tempoMap() const {
    return fork_->tempoMap();
}
void NativeAudioEngine::setLooping(bool enabled) {
    fork_->setLooping(enabled);
}
void NativeAudioEngine::setLoopRegionBeats(BeatRange range) {
    fork_->setLoopRegionBeats(range);
}
bool NativeAudioEngine::isLooping() const {
    return fork_->isLooping();
}
BeatRange NativeAudioEngine::getLoopRegionBeats() const {
    return fork_->getLoopRegionBeats();
}
void NativeAudioEngine::setMetronomeEnabled(bool enabled) {
    fork_->setMetronomeEnabled(enabled);
}
bool NativeAudioEngine::isMetronomeEnabled() const {
    return fork_->isMetronomeEnabled();
}
void NativeAudioEngine::setCountInMode(int mode) {
    fork_->setCountInMode(mode);
}
int NativeAudioEngine::getCountInMode() const {
    return fork_->getCountInMode();
}
void NativeAudioEngine::updateTriggerState() {
    fork_->updateTriggerState();
}
void NativeAudioEngine::processSessionStateEvents() {
    fork_->processSessionStateEvents();
}
juce::AudioDeviceManager* NativeAudioEngine::getDeviceManager() {
    return fork_->getDeviceManager();
}
juce::BigInteger NativeAudioEngine::getEnabledWaveChannels(bool input) const {
    return fork_->getEnabledWaveChannels(input);
}
void NativeAudioEngine::setEnabledWaveChannels(bool input, const juce::BigInteger& channels) {
    fork_->setEnabledWaveChannels(input, channels);
}
void NativeAudioEngine::rescanWaveDevices(bool enableInputs, bool enableOutputs) {
    fork_->rescanWaveDevices(enableInputs, enableOutputs);
}
bool NativeAudioEngine::isDevicesLoading() const {
    return fork_->isDevicesLoading();
}
void NativeAudioEngine::setDevicesLoadingCallback(
    std::function<void(bool, const juce::String&)> callback) {
    fork_->setDevicesLoadingCallback(callback);
}
AudioBridge* NativeAudioEngine::getAudioBridge() {
    return fork_->getAudioBridge();
}
const AudioBridge* NativeAudioEngine::getAudioBridge() const {
    return fork_->getAudioBridge();
}
MidiBridge* NativeAudioEngine::getMidiBridge() {
    return fork_->getMidiBridge();
}
const MidiBridge* NativeAudioEngine::getMidiBridge() const {
    return fork_->getMidiBridge();
}
MagdaApi& NativeAudioEngine::getMagdaApi() {
    return fork_->getMagdaApi();
}
PluginWindowManager* NativeAudioEngine::getPluginWindowManager() {
    return fork_->getPluginWindowManager();
}
const PluginWindowManager* NativeAudioEngine::getPluginWindowManager() const {
    return fork_->getPluginWindowManager();
}
InsertRenderCaptureService* NativeAudioEngine::getInsertRenderCaptureService() {
    return fork_->getInsertRenderCaptureService();
}
juce::Array<juce::PluginDescription> NativeAudioEngine::getKnownPluginTypes() const {
    return fork_->getKnownPluginTypes();
}
juce::Array<juce::PluginDescription> NativeAudioEngine::getPreferredPluginTypes() const {
    return fork_->getPreferredPluginTypes();
}
void NativeAudioEngine::addPluginListChangeListener(juce::ChangeListener* listener) {
    fork_->addPluginListChangeListener(listener);
}
void NativeAudioEngine::removePluginListChangeListener(juce::ChangeListener* listener) {
    fork_->removePluginListChangeListener(listener);
}
void NativeAudioEngine::startPluginScan(
    std::function<void(float, const juce::String&)> progressCallback) {
    fork_->startPluginScan(progressCallback);
}
void NativeAudioEngine::abortPluginScan() {
    fork_->abortPluginScan();
}
void NativeAudioEngine::detectNewPlugins(
    std::function<void(PluginScanPhase, const juce::String&)> statusCallback,
    std::function<void(bool, int, int, const juce::StringArray&)> completionCallback) {
    fork_->detectNewPlugins(statusCallback, completionCallback);
}
void NativeAudioEngine::setPluginScanCompletionCallback(
    std::function<void(bool, int, const juce::StringArray&)> callback) {
    fork_->setPluginScanCompletionCallback(callback);
}
bool NativeAudioEngine::isPluginScanRunning() const {
    return fork_->isPluginScanRunning();
}
std::vector<ExcludedPlugin> NativeAudioEngine::getExcludedPlugins() const {
    return fork_->getExcludedPlugins();
}
void NativeAudioEngine::setExcludedPlugins(const std::vector<ExcludedPlugin>& excludedPlugins) {
    fork_->setExcludedPlugins(excludedPlugins);
}
juce::File NativeAudioEngine::getPluginScanReportFile() const {
    return fork_->getPluginScanReportFile();
}
std::vector<std::string> NativeAudioEngine::getSystemPluginSearchPaths() const {
    return fork_->getSystemPluginSearchPaths();
}
std::vector<ScannedPluginParameter> NativeAudioEngine::scanPluginParameters(
    const juce::String& pluginId, bool internalPlugin) {
    return fork_->scanPluginParameters(pluginId, internalPlugin);
}
bool NativeAudioEngine::upsertGrooveTemplate(const GrooveTemplateData& groove) {
    return fork_->upsertGrooveTemplate(groove);
}
juce::StringArray NativeAudioEngine::getGrooveTemplateNames() const {
    return fork_->getGrooveTemplateNames();
}
std::unique_ptr<OfflineRenderSession> NativeAudioEngine::createOfflineRenderSession(
    bool resumePlaybackWhenFinished) {
    return fork_->createOfflineRenderSession(resumePlaybackWhenFinished);
}
std::vector<SamplerMediaReference> NativeAudioEngine::getSamplerMediaReferences() {
    return fork_->getSamplerMediaReferences();
}
std::unique_ptr<UndoableCommand> NativeAudioEngine::createTempoSequenceRippleCommand(
    TempoSequenceRippleMode mode, BeatPosition start, BeatPosition end) {
    return fork_->createTempoSequenceRippleCommand(mode, start, end);
}
void NativeAudioEngine::previewNoteOnTrack(const std::string& track_id, int noteNumber,
                                           int velocity, bool isNoteOn) {
    fork_->previewNoteOnTrack(track_id, noteNumber, velocity, isNoteOn);
}
void NativeAudioEngine::onTransportPlay(double positionSeconds) {
    fork_->onTransportPlay(positionSeconds);
}
void NativeAudioEngine::onTransportStop(double returnPositionSeconds) {
    fork_->onTransportStop(returnPositionSeconds);
}
void NativeAudioEngine::onTransportPause() {
    fork_->onTransportPause();
}
void NativeAudioEngine::onTransportRecord(double positionSeconds) {
    fork_->onTransportRecord(positionSeconds);
}
void NativeAudioEngine::onTransportStopRecording() {
    fork_->onTransportStopRecording();
}
void NativeAudioEngine::onEditPositionChanged(double positionSeconds) {
    fork_->onEditPositionChanged(positionSeconds);
}
void NativeAudioEngine::onTempoChanged(double bpm) {
    fork_->onTempoChanged(bpm);
}
void NativeAudioEngine::onTimeSignatureChanged(int numerator, int denominator) {
    fork_->onTimeSignatureChanged(numerator, denominator);
}
void NativeAudioEngine::onLoopRegionChanged(double startSeconds, double endSeconds, bool enabled) {
    fork_->onLoopRegionChanged(startSeconds, endSeconds, enabled);
}
void NativeAudioEngine::onLoopEnabledChanged(bool enabled) {
    fork_->onLoopEnabledChanged(enabled);
}

}  // namespace magda
