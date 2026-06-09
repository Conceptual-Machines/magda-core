#include "MixAnalysisService.hpp"

#include "../audio/analysis/OfflineMixAnalysis.hpp"
#include "../engine/TracktionEngineWrapper.hpp"
#include "../project/ProjectManager.hpp"
#include "SelectionManager.hpp"
#include "TrackManager.hpp"
#include "TrackMeasurementManager.hpp"

namespace magda {

namespace {
// The channels the analysis is scoped to: the current mixer selection, minus the
// master. The mixer always has a selection, and master vs tracks are mutually
// exclusive: selecting the master is the "analyse the whole mix" gesture (the
// master is the summed output, not a renderable stem), so it filters to an empty
// set here -- which OfflineMixAnalysis treats as "all audio tracks". Selecting
// real tracks analyses just those against each other. (The master is always
// measured for context regardless.)
std::vector<TrackId> selectedTrackSet() {
    std::vector<TrackId> out;
    for (auto id : SelectionManager::getInstance().getSelectedTracks())
        if (id != MASTER_TRACK_ID && id != INVALID_TRACK_ID)
            out.push_back(id);
    return out;
}
}  // namespace

MixAnalysisService& MixAnalysisService::getInstance() {
    static MixAnalysisService instance;
    return instance;
}

std::optional<MixAnalysisAgent::Input> MixAnalysisService::cached(Mode mode) const {
    switch (mode) {
        case Mode::Live:
            return cacheLive_;
        case Mode::Offline:
            return cacheOffline_;
    }
    return std::nullopt;
}

std::optional<MixAnalysisAgent::Input> MixAnalysisService::latest() const {
    if (!haveLatest_)
        return std::nullopt;
    return cached(latestMode_);
}

void MixAnalysisService::store(Mode mode, MixAnalysisAgent::Input input) {
    switch (mode) {
        case Mode::Live:
            cacheLive_ = std::move(input);
            break;
        case Mode::Offline:
            cacheOffline_ = std::move(input);
            break;
    }
    latestMode_ = mode;
    haveLatest_ = true;
}

void MixAnalysisService::setBusy(bool busy, Mode mode) {
    busy_ = busy;
    busyMode_ = mode;
    listeners_.call(&Listener::mixAnalysisChanged);
}

// ---------------------------------------------------------------------------
// Offline (Quick / Deep)
// ---------------------------------------------------------------------------

void MixAnalysisService::runOffline() {
    if (busy_ || capturing_)
        return;

    auto* engine =
        dynamic_cast<TracktionEngineWrapper*>(TrackManager::getInstance().getAudioEngine());
    if (engine == nullptr || engine->getEdit() == nullptr) {
        lastError_ = "No active edit to analyse.";
        listeners_.call(&Listener::mixAnalysisChanged);
        return;
    }

    lastError_.clear();
    progressText_.clear();
    const int runId = ++runId_;
    setBusy(true, Mode::Offline);

    namespace mix = magda::daw::audio;
    mix::OfflineMixAnalysis::Request req;
    // Always a per-track (Deep) render; the master is always measured too, for
    // context. The selection only scopes WHICH tracks: specific channels -> just
    // those (compared against each other); the master selected -> every track (the
    // full mix). The master is the summed output, not a stem, so it filters out of
    // the set, and an empty set means "all audio tracks" to OfflineMixAnalysis.
    req.depth = mix::OfflineMixAnalysis::Depth::Deep;
    // Honour a loop region: when the transport is looping, render only that part
    // (also makes the render far faster than the whole song).
    req.range = engine->isLooping() ? mix::OfflineMixAnalysis::RangeMode::LoopRange
                                    : mix::OfflineMixAnalysis::RangeMode::WholeEdit;
    req.trackSet = selectedTrackSet();  // empty (master selected) => all audio tracks
    const double tempo = ProjectManager::getInstance().getCurrentProjectInfo().tempo;
    if (tempo > 0.0)
        req.bpm = static_cast<float>(tempo);
    req.skipAgent = true;  // measure only -- the LLM lives in the console

    offlineCancel_ = mix::OfflineMixAnalysis::start(
        *engine, std::move(req),
        [this, runId](const juce::String& msg) {
            if (runId != runId_)
                return;
            progressText_ = msg;
            listeners_.call(&Listener::mixAnalysisChanged);
        },
        [this, runId](MixAnalysisAgent::Result result) {
            if (runId != runId_)
                return;  // cancelled / superseded
            if (result.hasError)
                lastError_ = result.error.empty() ? juce::String("Mix analysis failed.")
                                                  : juce::String(result.error);
            offlineCancel_.reset();
            setBusy(false, busyMode_);
        },
        [this, runId](MixAnalysisAgent::Input input) {
            if (runId != runId_)
                return;
            store(Mode::Offline, std::move(input));  // ready; setBusy(false) in onComplete
            listeners_.call(&Listener::mixAnalysisChanged);
        });
}

// ---------------------------------------------------------------------------
// Live capture
// ---------------------------------------------------------------------------

void MixAnalysisService::startLiveCapture() {
    if (busy_ || capturing_)
        return;

    auto& tmm = TrackMeasurementManager::getInstance();
    captureAddedTracks_.clear();
    captureAddedGlobal_ = !tmm.isGlobalEnabled();
    captureAddedMasking_ = !tmm.isMaskingAnalysisEnabled();

    if (captureAddedGlobal_)
        tmm.setGlobalEnabled(true);
    // Arm the selected channels; selecting the master (which filters to an empty
    // real-track set) arms every track for a whole-mix capture.
    const auto sel = selectedTrackSet();
    const std::set<TrackId> scope(sel.begin(), sel.end());
    for (const auto& track : TrackManager::getInstance().getTracks()) {
        const bool inScope = scope.empty() || scope.count(track.id) > 0;
        if (track.id != INVALID_TRACK_ID && inScope && !tmm.isTrackEnabled(track.id)) {
            tmm.setTrackEnabled(track.id, true);
            captureAddedTracks_.insert(track.id);
        }
    }
    if (captureAddedMasking_)
        tmm.setMaskingAnalysisEnabled(true);

    capturing_ = true;
    lastError_.clear();
    listeners_.call(&Listener::mixAnalysisChanged);
}

void MixAnalysisService::restoreCaptureState() {
    auto& tmm = TrackMeasurementManager::getInstance();
    for (auto id : captureAddedTracks_)
        tmm.setTrackEnabled(id, false);
    captureAddedTracks_.clear();
    if (captureAddedMasking_)
        tmm.setMaskingAnalysisEnabled(false);
    if (captureAddedGlobal_)
        tmm.setGlobalEnabled(false);
    captureAddedMasking_ = false;
    captureAddedGlobal_ = false;
}

MixAnalysisAgent::Input MixAnalysisService::buildLiveInput() const {
    auto& tmm = TrackMeasurementManager::getInstance();
    auto& tmgr = TrackManager::getInstance();
    auto trackName = [&tmgr](TrackId id) -> juce::String {
        const auto* t = tmgr.getTrack(id);
        return (t != nullptr && t->name.isNotEmpty()) ? t->name : juce::String(id);
    };

    // Live capture has no tonal/spectral detail (only the offline pipeline
    // computes it); unset fields read as "not measured" downstream.
    MixAnalysisAgent::Input input;
    for (const auto& [id, snap] : tmm.getAllSnapshots()) {
        MixAnalysisAgent::TrackMix tm;
        tm.name = trackName(id).toStdString();
        tm.role = tmgr.getPrimaryInstrument(id) ? "MIDI" : "audio";
        tm.chain = tmgr.getChainSummary(id);
        tm.integratedLufs = snap.integratedLufs;
        tm.shortTermLufs = snap.shortTermLufs;
        tm.samplePeakDb = snap.samplePeakDb;
        tm.truePeakDb = snap.truePeakDb;
        tm.truePeakValid = snap.truePeakValid;
        tm.plr = snap.plr;
        tm.psr = snap.psr;
        tm.correlation = snap.correlation;
        tm.width = snap.width;
        input.tracks.push_back(std::move(tm));
    }
    for (const auto& f : tmm.getMaskingFindings()) {
        MixAnalysisAgent::MaskingPair mp;
        mp.a = f.nameA.toStdString();
        mp.b = f.nameB.toStdString();
        mp.loHz = f.loHz;
        mp.hiHz = f.hiHz;
        mp.severity = f.severity;
        input.masking.push_back(std::move(mp));
    }
    const double tempo = ProjectManager::getInstance().getCurrentProjectInfo().tempo;
    if (tempo > 0.0)
        input.bpm = static_cast<float>(tempo);
    return input;
}

void MixAnalysisService::stopLiveCapture() {
    if (!capturing_)
        return;

    // Gather while the taps are still armed, then restore.
    auto input = buildLiveInput();
    restoreCaptureState();
    capturing_ = false;

    if (input.tracks.empty()) {
        lastError_ = "No levels captured (was the mix playing?).";
        listeners_.call(&Listener::mixAnalysisChanged);
        return;
    }
    lastError_.clear();
    store(Mode::Live, std::move(input));
    listeners_.call(&Listener::mixAnalysisChanged);
}

juce::String MixAnalysisService::scopeDescription() const {
    const auto n = selectedTrackSet().size();
    if (n == 0)
        return "the full mix";
    return juce::String(static_cast<int>(n)) +
           (n == 1 ? " selected channel" : " selected channels");
}

juce::String MixAnalysisService::rangeDescription() const {
    auto* engine =
        dynamic_cast<TracktionEngineWrapper*>(TrackManager::getInstance().getAudioEngine());
    return (engine != nullptr && engine->isLooping()) ? "loop region" : "whole song";
}

void MixAnalysisService::cancel() {
    if (capturing_) {
        restoreCaptureState();
        capturing_ = false;
    }
    if (busy_) {
        ++runId_;  // drop the in-flight offline run's late result
        if (offlineCancel_)
            offlineCancel_->store(true);  // actually abort the render at the next chunk
        offlineCancel_.reset();
        busy_ = false;
    }
    progressText_.clear();
    listeners_.call(&Listener::mixAnalysisChanged);
}

}  // namespace magda
