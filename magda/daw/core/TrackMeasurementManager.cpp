#include "TrackMeasurementManager.hpp"

#include "../audio/AudioBridge.hpp"
#include "../audio/plugin_manager/PluginManager.hpp"
#include "../audio/plugins/TrackMeasurementPlugin.hpp"
#include "../engine/AudioEngine.hpp"
#include "TrackManager.hpp"

namespace magda {

namespace {

// The tap lifecycle lives in PluginManager, reached through the audio bridge.
// Returns nullptr before the engine/bridge exist (early startup, headless).
PluginManager* pluginManager() {
    auto* engine = TrackManager::getInstance().getAudioEngine();
    if (engine == nullptr)
        return nullptr;
    auto* bridge = engine->getAudioBridge();
    if (bridge == nullptr)
        return nullptr;
    return &bridge->getPluginManager();
}

}  // namespace

TrackMeasurementManager& TrackMeasurementManager::getInstance() {
    static TrackMeasurementManager instance;
    return instance;
}

void TrackMeasurementManager::setGlobalEnabled(bool shouldEnable) {
    if (globalEnabled_ == shouldEnable)
        return;
    globalEnabled_ = shouldEnable;
    // Reconcile every desired track: applyTrack removes taps when the global
    // switch is off (true kill switch) and re-inserts them when it comes back.
    for (TrackId trackId : enabledTracks_)
        applyTrack(trackId);
    if (!globalEnabled_)
        latest_.clear();
    updateTimer();
}

void TrackMeasurementManager::setTrackEnabled(TrackId trackId, bool shouldEnable) {
    const bool already = enabledTracks_.count(trackId) > 0;
    if (shouldEnable == already)
        return;
    if (shouldEnable)
        enabledTracks_.insert(trackId);
    else
        enabledTracks_.erase(trackId);
    applyTrack(trackId);
    updateTimer();
}

void TrackMeasurementManager::applyTrack(TrackId trackId) {
    auto* pm = pluginManager();
    if (pm == nullptr)
        return;
    const bool active = globalEnabled_ && enabledTracks_.count(trackId) > 0;
    if (active) {
        if (auto* tap = pm->ensureTrackMeasurementTap(trackId))
            tap->setMeasurementEnabled(true);
    } else {
        pm->removeTrackMeasurementTap(trackId);
        latest_.erase(trackId);
    }
}

void TrackMeasurementManager::updateTimer() {
    const bool shouldRun = globalEnabled_ && !enabledTracks_.empty();
    if (shouldRun && !isTimerRunning())
        startTimer(kPollIntervalMs);
    else if (!shouldRun && isTimerRunning())
        stopTimer();
}

void TrackMeasurementManager::timerCallback() {
    auto* pm = pluginManager();
    if (pm == nullptr)
        return;
    for (TrackId trackId : enabledTracks_) {
        if (auto* tap = pm->getTrackMeasurementTap(trackId))
            latest_[trackId] = tap->getSnapshot();
    }
    listeners_.call([](TrackMeasurementListener& l) { l.trackMeasurementsUpdated(); });
}

daw::audio::TrackMeasurementSnapshot TrackMeasurementManager::getSnapshot(TrackId trackId) const {
    auto it = latest_.find(trackId);
    if (it == latest_.end())
        return {};
    return it->second;
}

std::vector<std::pair<TrackId, daw::audio::TrackMeasurementSnapshot>>
TrackMeasurementManager::getAllSnapshots() const {
    std::vector<std::pair<TrackId, daw::audio::TrackMeasurementSnapshot>> out;
    out.reserve(latest_.size());
    for (const auto& [trackId, snapshot] : latest_)
        out.emplace_back(trackId, snapshot);
    return out;
}

}  // namespace magda
