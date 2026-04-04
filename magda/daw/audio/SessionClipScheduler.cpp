#include "SessionClipScheduler.hpp"

#include "../core/TrackManager.hpp"
#include "AudioBridge.hpp"
#include "SessionClipAudioMonitor.hpp"
#include "SessionClipCommandQueue.hpp"
#include "SessionClipStateQueue.hpp"

namespace magda {

SessionClipScheduler::SessionClipScheduler(AudioBridge& audioBridge, te::Edit& edit,
                                           SessionClipAudioMonitor& audioMonitor)
    : audioBridge_(audioBridge), edit_(edit), audioMonitor_(audioMonitor) {
    ClipManager::getInstance().addListener(this);
}

SessionClipScheduler::~SessionClipScheduler() {
    ClipManager::getInstance().removeListener(this);
}

// =============================================================================
// ClipManagerListener
// =============================================================================

void SessionClipScheduler::clipsChanged() {
    auto& cm = ClipManager::getInstance();

    std::vector<ClipId> toRemove;
    for (auto clipId : activeClips_) {
        if (cm.getClip(clipId) == nullptr) {
            toRemove.push_back(clipId);
        }
    }
    for (auto clipId : toRemove) {
        sendUnmonitorCommand(clipId);
        audioBridge_.stopSessionClip(clipId);
        activeClips_.erase(clipId);
        clipLaunchData_.erase(clipId);
        lastNotifiedState_.erase(clipId);
        activeLaunchHandles_.erase(clipId);
    }
}

void SessionClipScheduler::clipPropertyChanged(ClipId clipId) {
    if (activeClips_.count(clipId) == 0)
        return;

    const auto* clip = ClipManager::getInstance().getClip(clipId);
    if (!clip)
        return;

    updateLaunchTimings(clipId, clip);
}

void SessionClipScheduler::clipPlaybackRequested(ClipId clipId, ClipPlaybackRequest request) {
    auto& cm = ClipManager::getInstance();
    const auto* clip = cm.getClip(clipId);
    if (!clip || clip->view != ClipView::Session) {
        return;
    }

    if (request == ClipPlaybackRequest::Play) {
        // Mark this clip as the active session clip on its track
        if (auto* track = TrackManager::getInstance().getTrack(clip->trackId))
            track->activeSessionClipId = clipId;

        // Session clips always loop
        if (!clip->loopEnabled)
            cm.getClip(clipId)->loopEnabled = true;

        // Ensure the audio-thread monitor plugin is installed
        audioBridge_.ensureSessionMonitorPlugin();

        // Snapshot state BEFORE any mutations in this batch.
        // All clips launched in the same event cycle (e.g. scene launch) see the
        // same pre-mutation state, preventing clip A's launch from poisoning clip B's
        // forceImmediate decision.
        if (!launchSnapshotValid_) {
            launchSnapshot_.transportPlaying = edit_.getTransport().isPlaying();
            launchSnapshot_.hasActiveClips = !activeClips_.empty();
            launchSnapshotValid_ = true;

            // Reset after current message dispatch completes
            juce::MessageManager::callAsync([this] { launchSnapshotValid_ = false; });
        }

        // Toggle mode: if clip is already active, stop it instead
        if (clip->launchMode == LaunchMode::Toggle && activeClips_.count(clipId)) {
            clipPlaybackRequested(clipId, ClipPlaybackRequest::Stop);
            return;
        }

        // Record clip timing data for playhead conversion
        updateLaunchTimings(clipId, clip);

        if (activeClips_.empty()) {
            playheadClipId_ = clipId;
        }

        // Force immediate when nothing is playing — no point quantizing against thin air.
        // Uses the snapshot so all clips in a batch get the same decision.
        bool forceImmediate = !launchSnapshot_.transportPlaying || !launchSnapshot_.hasActiveClips;

        // Add to active set and derive track playback modes BEFORE starting transport.
        // This ensures playSlotClips is already true when the audio thread
        // starts processing, preventing a click from arrangement audio being
        // output for a few blocks then abruptly stopping.
        activeClips_.insert(clipId);
        syncTrackPlaybackModes();

        // Ensure transport is playing
        auto& transport = edit_.getTransport();
        if (!transport.isPlaying()) {
            transport.play(false);
        }

        audioBridge_.launchSessionClip(clipId, forceImmediate);

        // Keep LaunchHandle alive and start audio-thread monitoring
        sendMonitorCommand(clipId);

        // Set initial state — the audio monitor will detect the real transition
        lastNotifiedState_[clipId] = SessionClipPlayState::Queued;
        playheadClipId_ = clipId;
        cm.notifyClipPlaybackStateChanged(clipId);

    } else {
        // Stop request — clear active clip on the track
        if (const auto* c = cm.getClip(clipId)) {
            if (auto* track = TrackManager::getInstance().getTrack(c->trackId)) {
                if (track->activeSessionClipId == clipId)
                    track->activeSessionClipId = INVALID_CLIP_ID;
            }
        }
        if (auto* c = cm.getClip(clipId))
            c->sessionPlayheadPos = -1.0;
        bool wasActive = activeClips_.count(clipId) > 0;
        if (wasActive) {
            sendUnmonitorCommand(clipId);
            audioBridge_.stopSessionClip(clipId);
            activeClips_.erase(clipId);
            clipLaunchData_.erase(clipId);
            lastNotifiedState_.erase(clipId);
            activeLaunchHandles_.erase(clipId);
            syncTrackPlaybackModes();
        }
        cm.notifyClipPlaybackStateChanged(clipId);

        if (activeClips_.empty()) {
            playheadClipId_ = INVALID_CLIP_ID;
        }
    }
}

// =============================================================================
// Play State Query
// =============================================================================

SessionClipPlayState SessionClipScheduler::getClipPlayState(ClipId clipId) const {
    if (activeClips_.count(clipId) == 0)
        return SessionClipPlayState::Stopped;

    auto it = lastNotifiedState_.find(clipId);
    if (it != lastNotifiedState_.end())
        return it->second;

    return SessionClipPlayState::Queued;
}

// =============================================================================
// Audio Thread Command Helpers
// =============================================================================

void SessionClipScheduler::sendMonitorCommand(ClipId clipId) {
    auto* teClip = audioBridge_.getSessionTeClip(clipId);
    if (!teClip)
        return;

    auto launchHandle = teClip->getLaunchHandle();
    if (!launchHandle)
        return;

    // Keep the shared_ptr alive while the audio thread uses the raw pointer
    activeLaunchHandles_[clipId] = launchHandle;

    SessionClipCommand cmd;
    cmd.clipId = clipId;
    cmd.action = SessionClipCommand::Action::Monitor;
    cmd.launchHandle = launchHandle.get();
    audioMonitor_.commandQueue().push(cmd);
}

void SessionClipScheduler::sendUnmonitorCommand(ClipId clipId) {
    SessionClipCommand cmd;
    cmd.clipId = clipId;
    cmd.action = SessionClipCommand::Action::Unmonitor;
    cmd.launchHandle = nullptr;
    audioMonitor_.commandQueue().push(cmd);

    // Note: we keep activeLaunchHandles_[clipId] alive until the caller
    // erases it, giving the audio thread one buffer cycle to process
    // the Unmonitor command before the shared_ptr is released.
}

// =============================================================================
// Process Audio Thread State Events
// =============================================================================

void SessionClipScheduler::processStateEvents() {
    auto& cm = ClipManager::getInstance();

    // Drain the state queue from the audio thread
    SessionClipStateEvent event;
    while (audioMonitor_.stateQueue().pop(event)) {
        if (activeClips_.count(event.clipId) == 0)
            continue;

        if (event.type == SessionClipStateEvent::Type::StartedPlaying) {
            auto& prev = lastNotifiedState_[event.clipId];
            if (prev != SessionClipPlayState::Playing) {
                prev = SessionClipPlayState::Playing;
                cm.notifyClipPlaybackStateChanged(event.clipId);
            }

        } else if (event.type == SessionClipStateEvent::Type::StoppedPlaying) {
            // Check if clip is looping — TE may briefly report stopped between loops
            const auto* clip = cm.getClip(event.clipId);
            if (clip && clip->loopEnabled)
                continue;

            activeClips_.erase(event.clipId);
            clipLaunchData_.erase(event.clipId);
            lastNotifiedState_.erase(event.clipId);
            activeLaunchHandles_.erase(event.clipId);
            sendUnmonitorCommand(event.clipId);
            syncTrackPlaybackModes();
            if (auto* c = cm.getClip(event.clipId)) {
                // Non-looping clip finished naturally — clear from track
                if (auto* track = TrackManager::getInstance().getTrack(c->trackId)) {
                    if (track->activeSessionClipId == event.clipId)
                        track->activeSessionClipId = INVALID_CLIP_ID;
                }
                c->sessionPlayheadPos = -1.0;
            }
            cm.notifyClipPlaybackStateChanged(event.clipId);
        }
    }

    // Transport stopped externally — tear down audio state but leave
    // track.activeSessionClipId intact so clips re-launch on resume.
    if (!activeClips_.empty() && !edit_.getTransport().isPlaying()) {
        auto copy = activeClips_;
        activeClips_.clear();
        clipLaunchData_.clear();
        lastNotifiedState_.clear();
        playheadClipId_ = INVALID_CLIP_ID;

        for (auto clipId : copy) {
            sendUnmonitorCommand(clipId);
            if (auto* c = cm.getClip(clipId))
                c->sessionPlayheadPos = -1.0;
            cm.notifyClipPlaybackStateChanged(clipId);
        }
        activeLaunchHandles_.clear();
        syncTrackPlaybackModes();
    }

    // Transport resumed — re-launch each track's activeSessionClipId.
    if (activeClips_.empty() && edit_.getTransport().isPlaying()) {
        auto& tm = TrackManager::getInstance();
        std::vector<ClipId> toRelaunch;
        for (const auto& track : tm.getTracks()) {
            if (track.activeSessionClipId != INVALID_CLIP_ID)
                toRelaunch.push_back(track.activeSessionClipId);
        }
        for (auto clipId : toRelaunch)
            clipPlaybackRequested(clipId, ClipPlaybackRequest::Play);
    }

    // Clean up playhead if all clips have ended
    if (activeClips_.empty() && playheadClipId_ != INVALID_CLIP_ID) {
        playheadClipId_ = INVALID_CLIP_ID;
    }
}

// =============================================================================
// Launch Timing Helper
// =============================================================================

void SessionClipScheduler::updateLaunchTimings(ClipId clipId, const ClipInfo* clip) {
    auto& data = clipLaunchData_[clipId];
    data.looping = clip->loopEnabled;

    double bpm = edit_.tempoSequence.getBpmAt(te::TimePosition());
    if (bpm <= 0.0)
        bpm = 120.0;

    if (clip->type == ClipType::Audio && clip->autoTempo) {
        data.clipLengthBeats = clip->lengthBeats;
        data.loopLengthBeats =
            (clip->loopLengthBeats > 0.0) ? clip->loopLengthBeats : clip->lengthBeats;
    } else if (clip->type == ClipType::Audio) {
        double srcLength =
            clip->loopLength > 0.0 ? clip->loopLength : clip->length * clip->speedRatio;
        double durationSeconds = srcLength / clip->speedRatio;
        data.clipLengthBeats = durationSeconds * bpm / 60.0;
        data.loopLengthBeats = data.clipLengthBeats;
    } else {
        // MIDI: source length is in seconds
        double srcLength = clip->getSourceLength();
        data.clipLengthBeats = srcLength * bpm / 60.0;
        data.loopLengthBeats = data.clipLengthBeats;
    }
}

// =============================================================================
// Playhead Conversion
// =============================================================================

double SessionClipScheduler::elapsedBeatsToSeconds(ClipId clipId, double elapsedBeats) const {
    double bpm = edit_.tempoSequence.getBpmAt(te::TimePosition());
    if (bpm <= 0.0)
        bpm = 120.0;

    auto it = clipLaunchData_.find(clipId);
    if (it == clipLaunchData_.end())
        return elapsedBeats * 60.0 / bpm;

    const auto& data = it->second;
    double posBeats = elapsedBeats;

    if (data.looping && data.loopLengthBeats > 0.0) {
        posBeats = std::fmod(elapsedBeats, data.loopLengthBeats);
    } else if (data.clipLengthBeats > 0.0) {
        posBeats = std::min(elapsedBeats, data.clipLengthBeats);
    }

    return posBeats * 60.0 / bpm;
}

double SessionClipScheduler::getSessionPlayheadPosition() const {
    if (playheadClipId_ == INVALID_CLIP_ID)
        return -1.0;

    double elapsedBeats = audioMonitor_.getClipElapsedBeats(playheadClipId_);
    if (elapsedBeats < 0.0)
        return -1.0;

    return elapsedBeatsToSeconds(playheadClipId_, elapsedBeats);
}

std::unordered_map<ClipId, double> SessionClipScheduler::getActiveClipPlayheadPositions() const {
    std::unordered_map<ClipId, double> positions;

    if (activeClips_.empty())
        return positions;

    std::unordered_map<ClipId, double> beatPositions;
    audioMonitor_.getActivePlayheadBeats(beatPositions);

    for (const auto& [clipId, beats] : beatPositions) {
        if (activeClips_.count(clipId) && beats >= 0.0) {
            positions[clipId] = elapsedBeatsToSeconds(clipId, beats);
        }
    }

    return positions;
}

// =============================================================================
// Track Playback Mode Sync
// =============================================================================

void SessionClipScheduler::syncTrackPlaybackModes() {
    auto& tm = TrackManager::getInstance();
    auto& cm = ClipManager::getInstance();

    // Build set of tracks with active session clips (running or suspended)
    std::unordered_set<TrackId> sessionTracks;
    for (auto clipId : activeClips_) {
        if (const auto* clip = cm.getClip(clipId))
            sessionTracks.insert(clip->trackId);
    }

    // Tracks with activeSessionClipId set stay in Session mode even when
    // transport is stopped — the user hasn't pressed "back to arrangement".
    for (const auto& track : tm.getTracks()) {
        if (track.activeSessionClipId != INVALID_CLIP_ID)
            sessionTracks.insert(track.id);
    }

    for (const auto& track : tm.getTracks()) {
        auto mode = sessionTracks.count(track.id) ? TrackPlaybackMode::Session
                                                  : TrackPlaybackMode::Arrangement;
        tm.setTrackPlaybackMode(track.id, mode);
    }
}

// =============================================================================
// Deactivate All
// =============================================================================

void SessionClipScheduler::deactivateAllSessionClips() {
    auto& cm = ClipManager::getInstance();
    auto& tm = TrackManager::getInstance();

    auto copy = activeClips_;
    activeClips_.clear();
    clipLaunchData_.clear();
    lastNotifiedState_.clear();
    playheadClipId_ = INVALID_CLIP_ID;

    for (auto clipId : copy) {
        sendUnmonitorCommand(clipId);
        audioBridge_.stopSessionClip(clipId);
        if (auto* c = cm.getClip(clipId)) {
            if (auto* track = tm.getTrack(c->trackId))
                track->activeSessionClipId = INVALID_CLIP_ID;
            c->sessionPlayheadPos = -1.0;
        }
        cm.notifyClipPlaybackStateChanged(clipId);
    }
    activeLaunchHandles_.clear();
    syncTrackPlaybackModes();
}

}  // namespace magda
