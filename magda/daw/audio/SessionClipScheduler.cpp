#include "SessionClipScheduler.hpp"

#include "../core/TrackManager.hpp"
#include "AudioBridge.hpp"

namespace magda {

SessionClipScheduler::SessionClipScheduler(AudioBridge& audioBridge, te::Edit& edit)
    : audioBridge_(audioBridge), edit_(edit) {
    ClipManager::getInstance().addListener(this);
}

SessionClipScheduler::~SessionClipScheduler() {
    stopTimer();
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
        audioBridge_.stopSessionClip(clipId);
        activeClips_.erase(clipId);
        stoppedCounters_.erase(clipId);
        lastNotifiedState_.erase(clipId);
    }

    if (activeClips_.empty()) {
        stoppedCounters_.clear();
        lastNotifiedState_.clear();
        stopTimer();
    }
}

void SessionClipScheduler::clipPropertyChanged(ClipId clipId) {
    if (activeClips_.count(clipId) == 0)
        return;

    const auto* clip = ClipManager::getInstance().getClip(clipId);
    if (!clip)
        return;

    updateLaunchTimings(clip);
}

void SessionClipScheduler::clipPlaybackRequested(ClipId clipId, ClipPlaybackRequest request) {
    auto& cm = ClipManager::getInstance();
    const auto* clip = cm.getClip(clipId);
    if (!clip || clip->view != ClipView::Session) {
        return;
    }

    if (request == ClipPlaybackRequest::Play) {
        // Toggle mode: if clip is already active, stop it instead
        if (clip->launchMode == LaunchMode::Toggle && activeClips_.count(clipId)) {
            clipPlaybackRequested(clipId, ClipPlaybackRequest::Stop);
            return;
        }

        // Same-track exclusive: the actual LaunchHandle stop is handled inside
        // ClipSynchronizer::launchSessionClip at the same quantized beat as
        // the new clip's start. Keep the old clip in activeClips_ so the
        // playhead keeps rendering — the timer's stopped-detection will
        // clean it up naturally when it actually stops.

        // Switch the track to session mode BEFORE starting the transport.
        // This ensures playSlotClips is already true when the audio thread
        // starts processing, preventing a click from arrangement audio being
        // output for a few blocks then abruptly stopping.
        TrackManager::getInstance().setTrackPlaybackMode(clip->trackId, TrackPlaybackMode::Session);

        // Ensure transport is playing — track whether it was already running
        // so we know if quantized launch is safe (sync point is only valid
        // once the audio thread has been processing for at least one buffer).
        auto& transport = edit_.getTransport();
        bool transportWasPlaying = transport.isPlaying();
        if (!transportWasPlaying) {
            transport.play(false);
        }

        // Record launch position and clip properties for playhead.
        // Only update immediately if no clips are currently playing —
        // otherwise the pendingPlayheadClip_ timer mechanism will update
        // launchTransportPos_ when the new clip actually starts playing,
        // avoiding a premature playhead during the queued period.
        if (activeClips_.empty()) {
            launchTransportPos_ = transport.getPosition().inSeconds();
            updateLaunchTimings(clip);
            playheadClipId_ = clipId;
        }
        pendingPlayheadClip_ = clipId;

        // Activate the clip and launch via LaunchHandle.
        // Force immediate launch when the transport was just cold-started —
        // the SyncPoint / MonotonicBeat aren't valid yet so quantized launch
        // would target beat 0 (already in the past).
        activeClips_.insert(clipId);
        stoppedCounters_.erase(clipId);  // Reset debounce for freshly launched clip
        audioBridge_.launchSessionClip(clipId, /*forceImmediate=*/!transportWasPlaying);

        // Snapshot the current state so the timer's dedup logic doesn't
        // re-fire a duplicate notification on its first tick.
        lastNotifiedState_[clipId] = queryLaunchHandleState(clipId);
        cm.notifyClipPlaybackStateChanged(clipId);

        // Start timer to monitor clip state from TE
        if (!isTimerRunning()) {
            startTimerHz(30);
        }

    } else {
        // Stop request
        bool wasActive = activeClips_.count(clipId) > 0;
        if (wasActive) {
            audioBridge_.stopSessionClip(clipId);
            activeClips_.erase(clipId);
            lastNotifiedState_.erase(clipId);
        }
        cm.notifyClipPlaybackStateChanged(clipId);

        if (activeClips_.empty()) {
            stopTimer();
            // Keep tracks in Session mode — user must press "Back to Arrangement"
            // to explicitly revert, matching Ableton/Bitwig behaviour.
            auto& transport = edit_.getTransport();
            if (transport.isPlaying()) {
                transport.stop(false, false);
            }
        }
    }
}

// =============================================================================
// Play State Query — derived from TE LaunchHandle (single source of truth)
// =============================================================================

SessionClipPlayState SessionClipScheduler::getClipPlayState(ClipId clipId) const {
    if (activeClips_.count(clipId) == 0)
        return SessionClipPlayState::Stopped;

    return queryLaunchHandleState(clipId);
}

SessionClipPlayState SessionClipScheduler::queryLaunchHandleState(ClipId clipId) const {
    auto* teClip = audioBridge_.getSessionTeClip(clipId);
    if (!teClip)
        return SessionClipPlayState::Stopped;

    auto launchHandle = teClip->getLaunchHandle();
    if (!launchHandle)
        return SessionClipPlayState::Stopped;

    if (launchHandle->getPlayingStatus() == te::LaunchHandle::PlayState::playing)
        return SessionClipPlayState::Playing;

    auto queued = launchHandle->getQueuedStatus();
    if (queued && *queued == te::LaunchHandle::QueueState::playQueued)
        return SessionClipPlayState::Queued;

    return SessionClipPlayState::Stopped;
}

// =============================================================================
// Launch Timing Helper
// =============================================================================

void SessionClipScheduler::updateLaunchTimings(const ClipInfo* clip) {
    launchClipLooping_ = clip->loopEnabled;

    if (clip->type == ClipType::Audio && clip->autoTempo) {
        // AutoTempo: cache wall-clock durations for playhead tracking,
        // derived from beat values and current project BPM
        double bpm = edit_.tempoSequence.getBpmAt(te::TimePosition());
        if (bpm <= 0.0)
            bpm = 120.0;
        launchClipLength_ = clip->lengthBeats * 60.0 / bpm;
        launchLoopLength_ =
            (clip->loopLengthBeats > 0.0) ? clip->loopLengthBeats * 60.0 / bpm : launchClipLength_;
    } else {
        launchClipLength_ = clip->length;
        // Source length is in seconds, convert to stretched time
        double srcLength =
            clip->loopLength > 0.0 ? clip->loopLength : clip->length * clip->speedRatio;
        launchLoopLength_ = srcLength / clip->speedRatio;
    }
}

// =============================================================================
// Timer — monitor TE LaunchHandle state
// =============================================================================

void SessionClipScheduler::timerCallback() {
    auto& cm = ClipManager::getInstance();
    auto& transport = edit_.getTransport();

    // If the transport was stopped externally (e.g. global stop button),
    // clean up all tracked clips — LaunchHandle states won't update once
    // the audio thread has stopped processing.
    if (!transport.isPlaying()) {
        if (!activeClips_.empty()) {
            auto copy = activeClips_;
            activeClips_.clear();
            stoppedCounters_.clear();
            lastNotifiedState_.clear();
            pendingPlayheadClip_ = INVALID_CLIP_ID;
            playheadClipId_ = INVALID_CLIP_ID;
            for (auto clipId : copy)
                cm.notifyClipPlaybackStateChanged(clipId);
            // Keep tracks in Session mode — user must press "Back to Arrangement"
        }
        stopTimer();
        return;
    }

    // If a clip was just launched (possibly quantized), detect when it
    // actually starts playing and update playhead timings at that moment.
    if (pendingPlayheadClip_ != INVALID_CLIP_ID) {
        if (queryLaunchHandleState(pendingPlayheadClip_) == SessionClipPlayState::Playing) {
            launchTransportPos_ = transport.getPosition().inSeconds();
            const auto* pendingClip = cm.getClip(pendingPlayheadClip_);
            if (pendingClip)
                updateLaunchTimings(pendingClip);
            playheadClipId_ = pendingPlayheadClip_;
            pendingPlayheadClip_ = INVALID_CLIP_ID;
        }
    }

    // Check for clips whose LaunchHandle has transitioned to stopped
    // (one-shot clip ended naturally, or TE finished a queued stop).
    // Debounce: require kStoppedThreshold consecutive Stopped readings
    // to avoid false positives from LaunchHandle's try_to_lock failing
    // when the audio thread holds the spin mutex.
    std::vector<ClipId> toRemove;
    for (auto clipId : activeClips_) {
        auto state = queryLaunchHandleState(clipId);
        if (state == SessionClipPlayState::Stopped) {
            // Don't remove looping clips — TE may briefly report Stopped
            // between loop cycles
            const auto* clip = cm.getClip(clipId);
            if (clip && clip->loopEnabled) {
                stoppedCounters_.erase(clipId);
                continue;
            }
            int& count = stoppedCounters_[clipId];
            ++count;
            if (count >= kStoppedThreshold) {
                toRemove.push_back(clipId);
            }
        } else {
            // Not stopped — reset the counter
            stoppedCounters_.erase(clipId);
        }
    }

    for (auto clipId : toRemove) {
        activeClips_.erase(clipId);
        stoppedCounters_.erase(clipId);
        lastNotifiedState_.erase(clipId);
        cm.notifyClipPlaybackStateChanged(clipId);
    }

    // Only notify when a clip's state actually changes (e.g. queued → playing)
    for (auto clipId : activeClips_) {
        auto state = queryLaunchHandleState(clipId);
        auto& prev = lastNotifiedState_[clipId];
        if (state != prev) {
            prev = state;
            cm.notifyClipPlaybackStateChanged(clipId);
        }
    }

    if (activeClips_.empty()) {
        stopTimer();
        stoppedCounters_.clear();
        if (transport.isPlaying()) {
            transport.stop(false, false);
        }
    }
}

// =============================================================================
// Deactivate All
// =============================================================================

void SessionClipScheduler::deactivateAllSessionClips() {
    auto& cm = ClipManager::getInstance();

    auto copy = activeClips_;
    activeClips_.clear();
    stoppedCounters_.clear();
    lastNotifiedState_.clear();
    pendingPlayheadClip_ = INVALID_CLIP_ID;
    playheadClipId_ = INVALID_CLIP_ID;
    stopTimer();

    for (auto clipId : copy) {
        audioBridge_.stopSessionClip(clipId);
        cm.notifyClipPlaybackStateChanged(clipId);
    }

    TrackManager::getInstance().setAllTracksPlaybackMode(TrackPlaybackMode::Arrangement);
}

double SessionClipScheduler::getSessionPlayheadPosition() const {
    if (activeClips_.empty() || launchClipLength_ <= 0.0)
        return -1.0;

    // Only return a position if at least one clip is actually playing.
    // Looping clips that are still in activeClips_ count as playing even if
    // the LaunchHandle briefly reports Stopped between loop cycles.
    bool anyPlaying = false;
    auto& cm = ClipManager::getInstance();
    for (auto clipId : activeClips_) {
        if (queryLaunchHandleState(clipId) == SessionClipPlayState::Playing) {
            anyPlaying = true;
            break;
        }
        const auto* clip = cm.getClip(clipId);
        if (clip && clip->loopEnabled) {
            anyPlaying = true;
            break;
        }
    }
    if (!anyPlaying)
        return -1.0;

    auto& transport = edit_.getTransport();
    double currentPos = transport.getPosition().inSeconds();
    double elapsed = currentPos - launchTransportPos_;
    if (elapsed < 0.0)
        elapsed = 0.0;

    if (launchClipLooping_ && launchLoopLength_ > 0.0) {
        return std::fmod(elapsed, launchLoopLength_);
    }

    return std::min(elapsed, launchClipLength_);
}

}  // namespace magda
