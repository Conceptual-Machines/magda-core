#include "SessionClipScheduler.hpp"

#include <iostream>

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
    // Clean up launchedClips_ and queuedClips_ for deleted clips
    auto& cm = ClipManager::getInstance();

    std::vector<ClipId> toRemove;
    for (auto clipId : launchedClips_) {
        if (cm.getClip(clipId) == nullptr) {
            toRemove.push_back(clipId);
        }
    }
    for (auto clipId : queuedClips_) {
        if (cm.getClip(clipId) == nullptr) {
            toRemove.push_back(clipId);
        }
    }
    for (auto clipId : toRemove) {
        audioBridge_.stopSessionClip(clipId);
        launchedClips_.erase(clipId);
        queuedClips_.erase(clipId);
    }

    if (launchedClips_.empty() && queuedClips_.empty()) {
        stopTimer();
    }
}

void SessionClipScheduler::clipPropertyChanged(ClipId clipId) {
    if (launchedClips_.count(clipId) == 0)
        return;

    const auto* clip = ClipManager::getInstance().getClip(clipId);
    if (!clip)
        return;

    // Update cached state — AudioBridge::clipPropertyChanged handles
    // propagating loop changes to the LaunchHandle (setLooping nullopt),
    // which makes TE stop the clip at the end of the current pass.
    // The timer will then detect PlayState::stopped and clean up.
    updateLaunchTimings(clip);
}

void SessionClipScheduler::clipPlaybackRequested(ClipId clipId, ClipPlaybackRequest request) {
    auto& cm = ClipManager::getInstance();
    const auto* clip = cm.getClip(clipId);
    if (!clip || clip->view != ClipView::Session) {
        return;
    }

    std::cout << "[SessionClipScheduler] clipPlaybackRequested clipId=" << clipId
              << " request=" << (request == ClipPlaybackRequest::Play ? "Play" : "Stop")
              << " launchedClips=" << launchedClips_.size() << std::endl;

    if (request == ClipPlaybackRequest::Play) {
        // Toggle mode: if clip is already playing or queued, stop it instead
        if (clip->launchMode == LaunchMode::Toggle &&
            (launchedClips_.count(clipId) || queuedClips_.count(clipId))) {
            // Recurse with Stop request
            clipPlaybackRequested(clipId, ClipPlaybackRequest::Stop);
            return;
        }

        // Same-track exclusive stop: stop other clips on the same track
        for (const auto& otherClip : cm.getSessionClips()) {
            if (otherClip.trackId == clip->trackId && otherClip.id != clipId) {
                if (launchedClips_.count(otherClip.id) || queuedClips_.count(otherClip.id)) {
                    audioBridge_.stopSessionClip(otherClip.id);
                    launchedClips_.erase(otherClip.id);
                    queuedClips_.erase(otherClip.id);
                    cm.notifyClipPlaybackStateChanged(otherClip.id);
                }
            }
        }

        // Queue the clip
        queuedClips_.insert(clipId);
        cm.notifyClipPlaybackStateChanged(clipId);

        // Ensure transport is playing
        auto& transport = edit_.getTransport();
        if (!transport.isPlaying()) {
            transport.play(false);
        }

        // Record launch position and clip properties for playhead
        if (launchedClips_.empty() && queuedClips_.size() == 1) {
            launchTransportPos_ = transport.getPosition().inSeconds();
            updateLaunchTimings(clip);
        }

        // Launch via LaunchHandle
        audioBridge_.launchSessionClip(clipId);
        queuedClips_.erase(clipId);
        launchedClips_.insert(clipId);
        cm.notifyClipPlaybackStateChanged(clipId);

        std::cout << "[SessionClipScheduler]   launched clipId=" << clipId
                  << " launchedClips=" << launchedClips_.size() << std::endl;

        // Start timer to monitor for natural clip end (one-shot clips)
        if (!isTimerRunning()) {
            startTimerHz(30);
        }

    } else {
        // Stop request
        bool wasTracked = launchedClips_.count(clipId) > 0 || queuedClips_.count(clipId) > 0;
        if (wasTracked) {
            audioBridge_.stopSessionClip(clipId);
            launchedClips_.erase(clipId);
            queuedClips_.erase(clipId);
        }
        cm.notifyClipPlaybackStateChanged(clipId);

        std::cout << "[SessionClipScheduler]   stopped: wasTracked=" << wasTracked
                  << " launchedClips=" << launchedClips_.size() << std::endl;

        if (launchedClips_.empty() && queuedClips_.empty()) {
            stopTimer();
            auto& transport = edit_.getTransport();
            if (transport.isPlaying()) {
                std::cout << "[SessionClipScheduler]   no more clips, stopping transport"
                          << std::endl;
                transport.stop(false, false);
            }
        }
    }
}

// =============================================================================
// Play State Query
// =============================================================================

SessionClipPlayState SessionClipScheduler::getClipPlayState(ClipId clipId) const {
    if (queuedClips_.count(clipId))
        return SessionClipPlayState::Queued;
    if (launchedClips_.count(clipId))
        return SessionClipPlayState::Playing;
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
// Timer — monitor for natural one-shot clip end
// =============================================================================

void SessionClipScheduler::timerCallback() {
    auto& cm = ClipManager::getInstance();

    std::vector<ClipId> toStop;
    for (auto clipId : launchedClips_) {
        auto* teClip = audioBridge_.getSessionTeClip(clipId);
        if (!teClip) {
            toStop.push_back(clipId);
            continue;
        }

        auto launchHandle = teClip->getLaunchHandle();
        if (!launchHandle) {
            toStop.push_back(clipId);
            continue;
        }

        // Check if a one-shot clip ended naturally
        if (launchHandle->getPlayingStatus() == te::LaunchHandle::PlayState::stopped) {
            toStop.push_back(clipId);
        }
    }

    for (auto clipId : toStop) {
        launchedClips_.erase(clipId);
        cm.notifyClipPlaybackStateChanged(clipId);
    }

    if (launchedClips_.empty() && queuedClips_.empty()) {
        stopTimer();
        // Stop transport when all session clips have ended
        auto& transport = edit_.getTransport();
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

    std::cout << "[SessionClipScheduler] deactivateAllSessionClips: " << launchedClips_.size()
              << " clips" << std::endl;

    // Copy the sets — notifications can trigger re-entrant calls
    auto launchedCopy = launchedClips_;
    auto queuedCopy = queuedClips_;
    launchedClips_.clear();
    queuedClips_.clear();
    stopTimer();

    for (auto clipId : launchedCopy) {
        std::cout << "[SessionClipScheduler]   stopping clipId=" << clipId << std::endl;
        audioBridge_.stopSessionClip(clipId);
        cm.notifyClipPlaybackStateChanged(clipId);
    }
    for (auto clipId : queuedCopy) {
        cm.notifyClipPlaybackStateChanged(clipId);
    }

    // Return all tracks to arrangement mode now that no session clips are playing
    TrackManager::getInstance().setAllTracksPlaybackMode(TrackPlaybackMode::Arrangement);
}

double SessionClipScheduler::getSessionPlayheadPosition() const {
    if (launchedClips_.empty() || launchClipLength_ <= 0.0)
        return -1.0;

    auto& transport = edit_.getTransport();
    double currentPos = transport.getPosition().inSeconds();
    double elapsed = currentPos - launchTransportPos_;
    if (elapsed < 0.0)
        elapsed = 0.0;

    if (launchClipLooping_ && launchLoopLength_ > 0.0) {
        // Looping: wrap playhead at loop boundary
        return std::fmod(elapsed, launchLoopLength_);
    }

    // Non-looping: let playhead run to full clip duration, then clamp
    return std::min(elapsed, launchClipLength_);
}

}  // namespace magda
