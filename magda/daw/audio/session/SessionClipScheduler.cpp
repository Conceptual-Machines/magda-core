#include "session/SessionClipScheduler.hpp"

#include "../../core/TrackManager.hpp"
#include "AudioBridge.hpp"
#include "session/SessionClipAudioMonitor.hpp"
#include "session/SessionClipCommandQueue.hpp"
#include "session/SessionClipStateQueue.hpp"

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
    auto& tm = TrackManager::getInstance();

    // If a clip was deleted that was the active session clip on a track, clean up.
    // Only clear if we previously knew about this clip (had a LaunchHandle or state).
    // During project load, clips may not exist yet — don't wipe the restored ID.
    for (const auto& track : tm.getTracks()) {
        ClipId clipId = track.activeSessionClipId;
        if (clipId != INVALID_CLIP_ID && cm.getClip(clipId) == nullptr) {
            bool wasKnown =
                activeLaunchHandles_.count(clipId) > 0 || lastNotifiedState_.count(clipId) > 0;
            if (wasKnown) {
                if (auto* t = tm.getTrack(track.id))
                    t->activeSessionClipId = INVALID_CLIP_ID;
                sendUnmonitorCommand(clipId);
                releaseLaunchHandle(clipId);
                clipLaunchData_.erase(clipId);
                lastNotifiedState_.erase(clipId);
            }
        }
    }
    syncTrackPlaybackModes();
}

void SessionClipScheduler::clipPropertyChanged(ClipId clipId) {
    const auto* clip = ClipManager::getInstance().getClip(clipId);
    if (!clip || clip->view != ClipView::Session)
        return;

    // Only update timings if this clip is the active one on its track
    const auto* track = TrackManager::getInstance().getTrack(clip->trackId);
    if (track && track->activeSessionClipId == clipId)
        updateLaunchTimings(clipId, clip);
}

void SessionClipScheduler::clipPlaybackRequested(ClipId clipId, ClipPlaybackRequest request) {
    auto& cm = ClipManager::getInstance();
    auto& tm = TrackManager::getInstance();
    const auto* clip = cm.getClip(clipId);
    if (!clip || clip->view != ClipView::Session)
        return;

    if (request == ClipPlaybackRequest::Play) {
        auto* track = tm.getTrack(clip->trackId);
        if (!track)
            return;

        // Toggle mode: if this clip is already the active one, stop it
        if (clip->launchMode == LaunchMode::Toggle && track->activeSessionClipId == clipId) {
            clipPlaybackRequested(clipId, ClipPlaybackRequest::Stop);
            return;
        }

        // Session clips always loop. Route through ClipManager so Tracktion
        // state is resynced before launch if an old project has loop disabled.
        if (!clip->loopEnabled) {
            cm.setClipLoopEnabled(clipId, true);
            clip = cm.getClip(clipId);
            if (!clip || clip->view != ClipView::Session)
                return;
        }

        // Ensure the audio-thread monitor plugin is installed
        audioBridge_.ensureSessionMonitorPlugin();

        // Snapshot state BEFORE any mutations in this batch.
        if (!launchSnapshotValid_) {
            launchSnapshot_.transportPlaying = edit_.getTransport().isPlaying();
            launchSnapshot_.hasActiveClips = hasActiveClips();
            launchSnapshotValid_ = true;
            juce::MessageManager::callAsync([this] { launchSnapshotValid_ = false; });
        }

        // Record clip timing data for playhead conversion
        updateLaunchTimings(clipId, clip);

        // Force immediate when nothing is playing
        bool forceImmediate = !launchSnapshot_.transportPlaying || !launchSnapshot_.hasActiveClips;

        // Previous clip on this track (if any) keeps playing until TE transitions.
        // The orphan sweep in processStateEvents will clean it up when its
        // LaunchHandle reports stopped.

        // Set active clip on track and sync modes BEFORE starting transport.
        // This ensures playSlotClips is already true when the audio thread
        // starts processing.
        track->activeSessionClipId = clipId;
        syncTrackPlaybackModes();

        // A new launch on this track supersedes any pending row-stop, so
        // empty slots should stop blinking the stop icon.
        stopPendingTracks_.erase(clip->trackId);

        // Ensure transport is playing
        if (!edit_.getTransport().isPlaying()) {
            edit_.getTransport().play(false);
            // Update immediately so processStateEvents doesn't see a false
            // stopped→playing transition and re-launch clips we just launched.
            wasTransportPlaying_ = true;
        }

        // Tell TE to play the clip
        audioBridge_.launchSessionClip(clipId, forceImmediate);

        // Retain handle and set up audio-thread monitoring for playhead
        retainLaunchHandle(clipId);
        sendMonitorCommand(clipId);

        // Track UI state
        lastNotifiedState_[clipId] = SessionClipPlayState::Queued;
        playheadClipId_ = clipId;
        cm.notifyClipPlaybackStateChanged(clipId);

    } else {
        // Stop request
        DBG("SessionClipScheduler: Stop requested for clip " << clipId);
        if (const auto* c = cm.getClip(clipId)) {
            if (auto* track = tm.getTrack(c->trackId)) {
                if (track->activeSessionClipId == clipId)
                    track->activeSessionClipId = INVALID_CLIP_ID;
            }
        }

        audioBridge_.stopSessionClip(clipId);
        sendUnmonitorCommand(clipId);
        releaseLaunchHandle(clipId);
        clipLaunchData_.erase(clipId);
        lastNotifiedState_.erase(clipId);

        if (auto* c = cm.getClip(clipId))
            c->sessionPlayheadPos = -1.0;

        syncTrackPlaybackModes();
        DBG("SessionClipScheduler: Notifying playback state changed (stopped) for clip " << clipId);
        cm.notifyClipPlaybackStateChanged(clipId);

        // Update playhead tracking
        if (playheadClipId_ == clipId)
            playheadClipId_ = INVALID_CLIP_ID;
    }
}

// =============================================================================
// Play State Query — reads TE LaunchHandle directly
// =============================================================================

SessionClipPlayState SessionClipScheduler::getClipPlayState(ClipId clipId) const {
    auto it = activeLaunchHandles_.find(clipId);
    if (it == activeLaunchHandles_.end())
        return SessionClipPlayState::Stopped;

    auto* handle = it->second.get();
    if (!handle)
        return SessionClipPlayState::Stopped;

    auto playState = handle->getPlayingStatus();
    auto queuedState = handle->getQueuedStatus();

    if (playState == te::LaunchHandle::PlayState::playing)
        return SessionClipPlayState::Playing;

    if (queuedState && *queuedState == te::LaunchHandle::QueueState::playQueued)
        return SessionClipPlayState::Queued;

    return SessionClipPlayState::Stopped;
}

bool SessionClipScheduler::hasActiveClips() const {
    auto& tm = TrackManager::getInstance();
    for (const auto& track : tm.getTracks()) {
        if (track.activeSessionClipId != INVALID_CLIP_ID)
            return true;
    }
    return false;
}

// =============================================================================
// LaunchHandle Lifecycle
// =============================================================================

void SessionClipScheduler::retainLaunchHandle(ClipId clipId) {
    auto* teClip = audioBridge_.getSessionTeClip(clipId);
    if (!teClip)
        return;
    auto handle = teClip->getLaunchHandle();
    if (handle)
        activeLaunchHandles_[clipId] = handle;
}

void SessionClipScheduler::releaseLaunchHandle(ClipId clipId) {
    auto it = activeLaunchHandles_.find(clipId);
    if (it == activeLaunchHandles_.end())
        return;
    // Park the shared_ptr until the next processStateEvents() pass — see
    // header comment on deferredHandlesCurrent_ for why we can't drop it now.
    deferredHandlesCurrent_.push_back(std::move(it->second));
    activeLaunchHandles_.erase(it);
}

void SessionClipScheduler::releaseAllLaunchHandles() {
    deferredHandlesCurrent_.reserve(deferredHandlesCurrent_.size() + activeLaunchHandles_.size());
    for (auto& [_, h] : activeLaunchHandles_)
        deferredHandlesCurrent_.push_back(std::move(h));
    activeLaunchHandles_.clear();
}

void SessionClipScheduler::drainDeferredHandles() {
    // Destructors of last cycle's parked handles run here on the message
    // thread — by now the audio thread has had at least one full block to
    // drain any Unmonitor commands referencing them.
    deferredHandlesPrevious_.clear();
    deferredHandlesPrevious_.swap(deferredHandlesCurrent_);
}

void SessionClipScheduler::sendMonitorCommand(ClipId clipId) {
    auto it = activeLaunchHandles_.find(clipId);
    if (it == activeLaunchHandles_.end())
        return;

    SessionClipCommand cmd;
    cmd.clipId = clipId;
    cmd.action = SessionClipCommand::Action::Monitor;
    cmd.launchHandle = it->second.get();
    audioMonitor_.commandQueue().push(cmd);
}

void SessionClipScheduler::sendUnmonitorCommand(ClipId clipId) {
    SessionClipCommand cmd;
    cmd.clipId = clipId;
    cmd.action = SessionClipCommand::Action::Unmonitor;
    cmd.launchHandle = nullptr;
    audioMonitor_.commandQueue().push(cmd);
}

// =============================================================================
// Poll State & Update UI
// =============================================================================

void SessionClipScheduler::processStateEvents() {
    auto& cm = ClipManager::getInstance();
    auto& tm = TrackManager::getInstance();

    // Surface queue overflow. Both queues silently drop on full, so without
    // this telemetry a flurry of scene launches or rapid controller mashing
    // can lose Monitor/Unmonitor commands or state transitions and the only
    // symptom is "the UI looks wrong but no logs say why."
    if (uint32_t cmdDropped = audioMonitor_.commandQueue().getDroppedCount()) {
        DBG("SessionClip command queue dropped " << static_cast<int>(cmdDropped)
                                                 << " push(es) — bursty scene launches exceeding "
                                                    "queue size?");
        audioMonitor_.commandQueue().clearDroppedCount();
    }
    if (uint32_t stateDropped = audioMonitor_.stateQueue().getDroppedCount()) {
        DBG("SessionClip state queue dropped " << static_cast<int>(stateDropped)
                                               << " push(es) — message thread fell behind audio");
        audioMonitor_.stateQueue().clearDroppedCount();
    }

    // Drain stale events from the audio monitor state queue
    SessionClipStateEvent event;
    while (audioMonitor_.stateQueue().pop(event)) {
        // discard — we read LaunchHandle state directly below
    }

    // TE follow actions can start a different LaunchHandle from inside the
    // audio graph. Adopt those handles back into Magda's track state so the UI
    // and playhead monitor follow the clip that is actually sounding.
    bool adoptedFollowActionClip = false;
    for (const auto& clip : cm.getSessionClips()) {
        auto* teClip = audioBridge_.getSessionTeClip(clip.id);
        if (!teClip)
            continue;

        auto handle = teClip->getLaunchHandle();
        if (!handle)
            continue;

        auto playState = handle->getPlayingStatus();
        auto queuedState = handle->getQueuedStatus();
        const bool isPlaying = playState == te::LaunchHandle::PlayState::playing;
        const bool isQueued =
            queuedState && *queuedState == te::LaunchHandle::QueueState::playQueued;
        if (!isPlaying && !isQueued)
            continue;

        auto* track = tm.getTrack(clip.trackId);
        if (!track || track->activeSessionClipId == clip.id)
            continue;

        track->activeSessionClipId = clip.id;
        updateLaunchTimings(clip.id, &clip);
        retainLaunchHandle(clip.id);
        sendMonitorCommand(clip.id);
        lastNotifiedState_[clip.id] =
            isPlaying ? SessionClipPlayState::Playing : SessionClipPlayState::Queued;
        playheadClipId_ = clip.id;
        stopPendingTracks_.erase(clip.trackId);
        cm.notifyClipPlaybackStateChanged(clip.id);
        adoptedFollowActionClip = true;
    }
    if (adoptedFollowActionClip)
        syncTrackPlaybackModes();

    bool transportPlaying = edit_.getTransport().isPlaying();

    // Transport just stopped — stop all LaunchHandles so clips reset to start.
    // activeSessionClipId stays set (user intent preserved).
    if (wasTransportPlaying_ && !transportPlaying && hasActiveClips()) {
        DBG("SessionClipScheduler: Transport stopped — stopping all LaunchHandles");
        for (const auto& track : tm.getTracks()) {
            ClipId clipId = track.activeSessionClipId;
            if (clipId == INVALID_CLIP_ID)
                continue;
            DBG("SessionClipScheduler: Stopping clip " << clipId << " (transport stop)");
            audioBridge_.stopSessionClip(clipId);
            sendUnmonitorCommand(clipId);
            releaseLaunchHandle(clipId);
            if (auto* c = cm.getClip(clipId))
                c->sessionPlayheadPos = -1.0;
            cm.notifyClipPlaybackStateChanged(clipId);
        }
        // Clear ALL retained handles/state — not just active clips.
        // releaseAllLaunchHandles parks shared_ptrs in the deferred queue
        // so the audio thread keeps a valid handle until the next sweep.
        releaseAllLaunchHandles();
        clipLaunchData_.clear();
        lastNotifiedState_.clear();
        stopPendingTracks_.clear();
    }

    // Transport just resumed with active session clips — re-launch them.
    if (!wasTransportPlaying_ && transportPlaying && hasActiveClips())
        relaunchActiveClips();

    wasTransportPlaying_ = transportPlaying;

    // Poll LaunchHandle state for each active session clip and notify UI
    for (const auto& track : tm.getTracks()) {
        ClipId clipId = track.activeSessionClipId;
        if (clipId == INVALID_CLIP_ID)
            continue;

        auto state = getClipPlayState(clipId);
        auto& prev = lastNotifiedState_[clipId];

        if (state != prev) {
            DBG("SessionClipScheduler: Clip " << clipId << " state changed: " << (int)prev << " -> "
                                              << (int)state);
            prev = state;
            cm.notifyClipPlaybackStateChanged(clipId);
        }

        // If handle reports stopped on a non-looping clip, it finished naturally
        if (state == SessionClipPlayState::Stopped) {
            const auto* clip = cm.getClip(clipId);
            if (!clip || !clip->loopEnabled) {
                if (auto* t = tm.getTrack(track.id))
                    t->activeSessionClipId = INVALID_CLIP_ID;
                sendUnmonitorCommand(clipId);
                releaseLaunchHandle(clipId);
                clipLaunchData_.erase(clipId);
                lastNotifiedState_.erase(clipId);
                if (auto* c = cm.getClip(clipId))
                    c->sessionPlayheadPos = -1.0;
                syncTrackPlaybackModes();
                cm.notifyClipPlaybackStateChanged(clipId);
            }
        }
    }

    // Clean up orphaned LaunchHandles (clips replaced by another on the same track)
    std::vector<ClipId> orphaned;
    for (const auto& [cid, handle] : activeLaunchHandles_) {
        bool isActive = false;
        for (const auto& track : tm.getTracks()) {
            if (track.activeSessionClipId == cid) {
                isActive = true;
                break;
            }
        }
        if (!isActive) {
            auto state = getClipPlayState(cid);
            if (state == SessionClipPlayState::Stopped)
                orphaned.push_back(cid);
        }
    }
    for (auto cid : orphaned) {
        sendUnmonitorCommand(cid);
        releaseLaunchHandle(cid);
        clipLaunchData_.erase(cid);
        lastNotifiedState_.erase(cid);
        if (auto* c = cm.getClip(cid)) {
            c->sessionPlayheadPos = -1.0;
            stopPendingTracks_.erase(c->trackId);
        }
        cm.notifyClipPlaybackStateChanged(cid);
    }
    if (!orphaned.empty())
        syncTrackPlaybackModes();

    // Update playhead tracking
    if (playheadClipId_ != INVALID_CLIP_ID && !hasActiveClips())
        playheadClipId_ = INVALID_CLIP_ID;

    // Last step: drop the LaunchHandles parked in the previous cycle. By now
    // the audio thread has run many blocks and has either processed every
    // Unmonitor command we queued or — at worst — its stale entry pointed
    // at a still-alive handle. Doing this AFTER the polling loop above
    // ensures any handle we re-park in this cycle survives one more pass.
    drainDeferredHandles();
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

    if (clip->isAudio() && clip->autoTempo) {
        data.clipLengthBeats = clip->getLengthInBeats(bpm);
        data.loopLengthBeats =
            (clip->loopLengthBeats > 0.0) ? clip->loopLengthBeats : data.clipLengthBeats;
    } else if (clip->isAudio()) {
        double srcLength = clip->getSourceLoopLength() > 0.0
                               ? clip->getSourceLoopLength()
                               : clip->timelineToSource(clip->getTimelineLength(bpm));
        double durationSeconds = srcLength / clip->speedRatio;
        data.clipLengthBeats = durationSeconds * bpm / 60.0;
        data.loopLengthBeats = data.clipLengthBeats;
    } else {
        data.clipLengthBeats = clip->getLengthInBeats(bpm);
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

    std::unordered_map<ClipId, double> beatPositions;
    audioMonitor_.getActivePlayheadBeats(beatPositions);

    for (const auto& [clipId, beats] : beatPositions) {
        // Include any clip that has a retained LaunchHandle (still physically playing)
        if (beats >= 0.0 && activeLaunchHandles_.count(clipId))
            positions[clipId] = elapsedBeatsToSeconds(clipId, beats);
    }

    return positions;
}

// =============================================================================
// Track Playback Mode Sync
// =============================================================================

void SessionClipScheduler::syncTrackPlaybackModes() {
    auto& tm = TrackManager::getInstance();

    for (const auto& track : tm.getTracks()) {
        auto mode = (track.activeSessionClipId != INVALID_CLIP_ID) ? TrackPlaybackMode::Session
                                                                   : TrackPlaybackMode::Arrangement;
        tm.setTrackPlaybackMode(track.id, mode);
    }
}

// =============================================================================
// Re-launch
// =============================================================================

void SessionClipScheduler::relaunchActiveClips() {
    auto& cm = ClipManager::getInstance();
    auto& tm = TrackManager::getInstance();

    bool hasAny = false;
    for (const auto& track : tm.getTracks()) {
        ClipId clipId = track.activeSessionClipId;
        if (clipId == INVALID_CLIP_ID)
            continue;

        // Skip clips whose LaunchHandle is already playing or queued
        auto state = getClipPlayState(clipId);
        if (state == SessionClipPlayState::Playing || state == SessionClipPlayState::Queued)
            continue;

        const auto* clip = cm.getClip(clipId);
        if (!clip)
            continue;

        if (!hasAny) {
            // First clip: ensure monitor plugin and track modes are set
            audioBridge_.ensureSessionMonitorPlugin();
            syncTrackPlaybackModes();
            hasAny = true;
        }

        DBG("SessionClipScheduler::relaunchActiveClips: launching clip " << clipId << " on track "
                                                                         << track.id);
        updateLaunchTimings(clipId, clip);
        audioBridge_.launchSessionClip(clipId, true);
        retainLaunchHandle(clipId);
        sendMonitorCommand(clipId);
        lastNotifiedState_[clipId] = SessionClipPlayState::Queued;
        if (playheadClipId_ == INVALID_CLIP_ID)
            playheadClipId_ = clipId;
    }

    wasTransportPlaying_ = true;
}

// =============================================================================
// Deactivate All
// =============================================================================

void SessionClipScheduler::deactivateAllSessionClips() {
    auto& cm = ClipManager::getInstance();
    auto& tm = TrackManager::getInstance();

    for (const auto& track : tm.getTracks()) {
        ClipId clipId = track.activeSessionClipId;
        if (clipId == INVALID_CLIP_ID)
            continue;

        if (auto* t = tm.getTrack(track.id))
            t->activeSessionClipId = INVALID_CLIP_ID;

        audioBridge_.stopSessionClip(clipId);
        sendUnmonitorCommand(clipId);

        if (auto* c = cm.getClip(clipId))
            c->sessionPlayheadPos = -1.0;
        cm.notifyClipPlaybackStateChanged(clipId);
    }

    releaseAllLaunchHandles();
    clipLaunchData_.clear();
    lastNotifiedState_.clear();
    stopPendingTracks_.clear();
    playheadClipId_ = INVALID_CLIP_ID;
    syncTrackPlaybackModes();
}

// =============================================================================
// Stop Track (empty slot in scene launch)
// =============================================================================

void SessionClipScheduler::stopSessionTrack(TrackId trackId) {
    auto& tm = TrackManager::getInstance();

    auto* track = tm.getTrack(trackId);
    if (!track || track->activeSessionClipId == INVALID_CLIP_ID)
        return;

    ClipId clipId = track->activeSessionClipId;
    const auto* clip = ClipManager::getInstance().getClip(clipId);

    // Use the active clip's quantize setting for the stop timing
    LaunchQuantize quantize = clip ? clip->launchQuantize : LaunchQuantize::None;

    // Queue the stop — keep everything alive (handle, monitor, track mode)
    // so TE continues playing the clip until the quantized beat.
    // processStateEvents will clean up when the handle reports stopped.
    audioBridge_.stopSessionClipQueued(clipId, quantize);

    // Clear active clip so processStateEvents knows this is winding down
    track->activeSessionClipId = INVALID_CLIP_ID;
    // Don't sync track modes yet — TE needs playSlotClips=true until the handle stops

    // Mark the track for the stop-pending blink only if there's actually
    // a quantize wait — with LaunchQuantize::None TE stops immediately and
    // there's no window to blink during.
    if (quantize != LaunchQuantize::None) {
        stopPendingTracks_.insert(trackId);
        ClipManager::getInstance().notifyClipPlaybackStateChanged(clipId);
    }
}

bool SessionClipScheduler::isSessionTrackStopPending(TrackId trackId) const {
    return stopPendingTracks_.count(trackId) != 0;
}

}  // namespace magda
