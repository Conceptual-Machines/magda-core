#include "SessionClipScheduler.hpp"

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
    // Clean up launchedClips_ for deleted clips
    auto& cm = ClipManager::getInstance();

    std::vector<ClipId> toRemove;
    for (auto clipId : launchedClips_) {
        if (cm.getClip(clipId) == nullptr) {
            toRemove.push_back(clipId);
        }
    }
    for (auto clipId : toRemove) {
        audioBridge_.stopSessionClip(clipId);
        launchedClips_.erase(clipId);
    }

    if (launchedClips_.empty()) {
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
    launchClipLooping_ = clip->internalLoopEnabled;
    launchClipLength_ = clip->length;

    // Convert loop length from beats to seconds for playhead wrapping
    auto& tempoSeq = edit_.tempoSequence;
    auto loopTime = tempoSeq.beatsToTime(te::BeatPosition::fromBeats(clip->internalLoopLength));
    launchLoopLength_ = loopTime.inSeconds();
}

void SessionClipScheduler::clipPlaybackStateChanged(ClipId clipId) {
    auto& cm = ClipManager::getInstance();
    const auto* clip = cm.getClip(clipId);
    if (!clip || clip->view != ClipView::Session) {
        return;
    }

    DBG("SessionClipScheduler::clipPlaybackStateChanged: clip "
        << clipId << " isQueued=" << (int)clip->isQueued << " isPlaying=" << (int)clip->isPlaying
        << " loopEnabled=" << (int)clip->internalLoopEnabled
        << " loopLength=" << clip->internalLoopLength << " length=" << clip->length);

    if (clip->isQueued && !clip->isPlaying) {
        // Clip was just queued for playback — launch it via LaunchHandle
        auto& transport = edit_.getTransport();
        if (!transport.isPlaying()) {
            transport.play(false);
        }

        // Record launch position and clip properties for playhead
        if (launchedClips_.empty()) {
            launchTransportPos_ = transport.getPosition().inSeconds();
            launchClipLooping_ = clip->internalLoopEnabled;
            launchClipLength_ = clip->length;

            // Convert loop length from beats to seconds for playhead wrapping
            auto& tempoSeq = edit_.tempoSequence;
            auto loopTime =
                tempoSeq.beatsToTime(te::BeatPosition::fromBeats(clip->internalLoopLength));
            launchLoopLength_ = loopTime.inSeconds();
        }

        audioBridge_.launchSessionClip(clipId);
        cm.setClipPlayingState(clipId, true);
        launchedClips_.insert(clipId);

        // Start timer to monitor for natural clip end (one-shot clips)
        if (!isTimerRunning()) {
            startTimerHz(30);
        }

    } else if (!clip->isQueued && !clip->isPlaying) {
        // Clip was stopped — stop it via LaunchHandle
        if (launchedClips_.count(clipId) > 0) {
            audioBridge_.stopSessionClip(clipId);
            launchedClips_.erase(clipId);
        }

        if (launchedClips_.empty()) {
            stopTimer();
            auto& transport = edit_.getTransport();
            if (transport.isPlaying()) {
                transport.stop(false, false);
            }
        }
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

        // Update ClipManager state so UI reflects the stop
        auto* clip = cm.getClip(clipId);
        if (clip && (clip->isPlaying || clip->isQueued)) {
            cm.setClipPlayingState(clipId, false);
        }
    }

    if (launchedClips_.empty()) {
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

    for (auto clipId : launchedClips_) {
        audioBridge_.stopSessionClip(clipId);

        auto* clip = cm.getClip(clipId);
        if (clip && (clip->isPlaying || clip->isQueued)) {
            cm.setClipPlayingState(clipId, false);
        }
    }

    launchedClips_.clear();
    stopTimer();
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
