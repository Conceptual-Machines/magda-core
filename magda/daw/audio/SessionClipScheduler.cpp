#include "SessionClipScheduler.hpp"

#include <cmath>

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
    // When clips are removed, clean up any active/queued session clips that no longer exist
    auto& cm = ClipManager::getInstance();

    // Clean up queued clips that were deleted
    queuedClips_.erase(
        std::remove_if(queuedClips_.begin(), queuedClips_.end(),
                       [&cm](const QueuedClip& qc) { return cm.getClip(qc.clipId) == nullptr; }),
        queuedClips_.end());

    // Clean up active session clips that were deleted
    std::vector<ClipId> toRemove;
    for (const auto& [clipId, engineId] : activeSessionClips_) {
        if (cm.getClip(clipId) == nullptr) {
            toRemove.push_back(clipId);
        }
    }
    for (auto clipId : toRemove) {
        deactivateSessionClip(clipId);
    }
}

void SessionClipScheduler::clipPlaybackStateChanged(ClipId clipId) {
    auto& cm = ClipManager::getInstance();
    const auto* clip = cm.getClip(clipId);
    if (!clip || clip->view != ClipView::Session) {
        return;
    }

    if (clip->isQueued && !clip->isPlaying) {
        // Clip was just queued for playback
        if (clip->launchQuantize == LaunchQuantize::None) {
            // Immediate: activate right away
            activateSessionClip(clipId);
            cm.setClipPlayingState(clipId, true);
        } else {
            // Quantized: calculate target beat and queue
            double currentBeat = getCurrentBeatPosition();
            double targetBeat = getNextQuantizeBoundary(clip->launchQuantize, currentBeat);

            // Add to queue (replace if already queued)
            queuedClips_.erase(
                std::remove_if(queuedClips_.begin(), queuedClips_.end(),
                               [clipId](const QueuedClip& qc) { return qc.clipId == clipId; }),
                queuedClips_.end());

            queuedClips_.push_back({clipId, targetBeat});

            // Start timer if not already running
            if (!isTimerRunning()) {
                startTimerHz(60);
            }

            DBG("SessionClipScheduler: Queued clip " << clipId << " for beat " << targetBeat
                                                     << " (current: " << currentBeat << ")");
        }
    } else if (!clip->isQueued && !clip->isPlaying) {
        // Clip was stopped - deactivate if active
        // Remove from queue if queued
        queuedClips_.erase(
            std::remove_if(queuedClips_.begin(), queuedClips_.end(),
                           [clipId](const QueuedClip& qc) { return qc.clipId == clipId; }),
            queuedClips_.end());

        if (activeSessionClips_.count(clipId) > 0) {
            deactivateSessionClip(clipId);
        }

        // Stop timer if nothing left to monitor
        if (queuedClips_.empty() && activeSessionClips_.empty()) {
            stopTimer();
        }
    }
}

// =============================================================================
// Timer
// =============================================================================

void SessionClipScheduler::timerCallback() {
    double currentBeat = getCurrentBeatPosition();
    auto& cm = ClipManager::getInstance();

    // Check queued clips for activation
    std::vector<ClipId> toActivate;
    for (const auto& qc : queuedClips_) {
        if (currentBeat >= qc.targetStartBeat) {
            toActivate.push_back(qc.clipId);
        }
    }

    for (auto clipId : toActivate) {
        // Remove from queue
        queuedClips_.erase(
            std::remove_if(queuedClips_.begin(), queuedClips_.end(),
                           [clipId](const QueuedClip& qc) { return qc.clipId == clipId; }),
            queuedClips_.end());

        // Verify clip still exists and is still queued
        const auto* clip = cm.getClip(clipId);
        if (clip && clip->isQueued) {
            activateSessionClip(clipId);
            cm.setClipPlayingState(clipId, true);
            DBG("SessionClipScheduler: Activated queued clip " << clipId << " at beat "
                                                               << currentBeat);
        }
    }

    // Check active non-looping clips for natural end
    std::vector<ClipId> toDeactivate;
    for (const auto& [clipId, engineId] : activeSessionClips_) {
        const auto* clip = cm.getClip(clipId);
        if (!clip) {
            toDeactivate.push_back(clipId);
            continue;
        }

        // Non-looping clips: check if we've passed the clip's end
        if (!clip->internalLoopEnabled) {
            auto transportPos = edit_.getTransport().position.get();

            // Find the TE clip's start time to calculate expected end
            // We stored the engine clip at the transport position when activated,
            // so we need to find the actual TE clip to get its end position
            for (auto* track : te::getAudioTracks(edit_)) {
                for (auto* teClip : track->getClips()) {
                    if (teClip->itemID.toString().toStdString() == engineId) {
                        auto clipEnd = teClip->getPosition().getEnd();
                        if (transportPos >= clipEnd) {
                            toDeactivate.push_back(clipId);
                        }
                        goto nextClip;  // Found the TE clip, check next active clip
                    }
                }
            }
        nextClip:;
        }
    }

    for (auto clipId : toDeactivate) {
        deactivateSessionClip(clipId);
        // Update ClipManager state
        auto* clip = cm.getClip(clipId);
        if (clip && (clip->isPlaying || clip->isQueued)) {
            cm.setClipPlayingState(clipId, false);
        }
    }

    // Stop timer if nothing left to monitor
    if (queuedClips_.empty() && activeSessionClips_.empty()) {
        stopTimer();
    }
}

// =============================================================================
// Quantization
// =============================================================================

double SessionClipScheduler::getCurrentBeatPosition() const {
    auto transportPos = edit_.getTransport().position.get();
    auto beatPos = edit_.tempoSequence.timeToBeats(transportPos);
    return beatPos.inBeats();
}

double SessionClipScheduler::getNextQuantizeBoundary(LaunchQuantize q, double currentBeat) const {
    // Assumes 4/4 time signature
    switch (q) {
        case LaunchQuantize::None:
            return currentBeat;
        case LaunchQuantize::OneBar:
            return std::ceil(currentBeat / 4.0) * 4.0;
        case LaunchQuantize::HalfBar:
            return std::ceil(currentBeat / 2.0) * 2.0;
        case LaunchQuantize::QuarterBar:
            return std::ceil(currentBeat / 1.0) * 1.0;
        case LaunchQuantize::EighthBar:
            return std::ceil(currentBeat / 0.5) * 0.5;
    }
    return currentBeat;
}

// =============================================================================
// Clip Lifecycle
// =============================================================================

void SessionClipScheduler::activateSessionClip(ClipId clipId) {
    // If already active, deactivate first (re-trigger)
    if (activeSessionClips_.count(clipId) > 0) {
        deactivateSessionClip(clipId);
    }

    // Get current transport position as the start time for the session clip
    double startTimeSeconds = edit_.getTransport().position.get().inSeconds();

    auto& cm = ClipManager::getInstance();
    const auto* clip = cm.getClip(clipId);
    if (!clip) {
        return;
    }

    std::string engineClipId;
    if (clip->type == ClipType::Audio) {
        engineClipId = audioBridge_.createSessionAudioClip(clipId, *clip, startTimeSeconds);
    } else if (clip->type == ClipType::MIDI) {
        engineClipId = audioBridge_.createSessionMidiClip(clipId, *clip, startTimeSeconds);
    }

    if (!engineClipId.empty()) {
        activeSessionClips_[clipId] = engineClipId;
        DBG("SessionClipScheduler: Activated clip " << clipId << " (engine: " << engineClipId
                                                    << ") at " << startTimeSeconds << "s");

        // Start timer to monitor for natural clip end
        if (!isTimerRunning()) {
            startTimerHz(60);
        }
    } else {
        DBG("SessionClipScheduler: Failed to activate clip " << clipId);
    }
}

void SessionClipScheduler::deactivateSessionClip(ClipId clipId) {
    auto it = activeSessionClips_.find(clipId);
    if (it == activeSessionClips_.end()) {
        return;
    }

    std::string engineClipId = it->second;
    activeSessionClips_.erase(it);

    audioBridge_.removeSessionClip(engineClipId);

    DBG("SessionClipScheduler: Deactivated clip " << clipId);
}

void SessionClipScheduler::deactivateAllSessionClips() {
    auto& cm = ClipManager::getInstance();

    // Copy keys since we modify during iteration
    std::vector<ClipId> clipIds;
    clipIds.reserve(activeSessionClips_.size());
    for (const auto& [clipId, engineId] : activeSessionClips_) {
        clipIds.push_back(clipId);
    }

    for (auto clipId : clipIds) {
        deactivateSessionClip(clipId);
        auto* clip = cm.getClip(clipId);
        if (clip && (clip->isPlaying || clip->isQueued)) {
            cm.setClipPlayingState(clipId, false);
        }
    }

    queuedClips_.clear();
    stopTimer();
}

}  // namespace magda
