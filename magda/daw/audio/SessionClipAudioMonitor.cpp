#include "SessionClipAudioMonitor.hpp"

namespace magda {

void SessionClipAudioMonitor::process(double transportPositionSeconds) {
    // 1. Drain command queue — add/remove monitored clips
    SessionClipCommand cmd;
    while (commandQueue_.pop(cmd)) {
        if (cmd.action == SessionClipCommand::Action::Monitor) {
            int idx = findClipIndex(cmd.clipId);
            if (idx < 0 && numMonitored_ < kMaxMonitoredClips) {
                idx = numMonitored_++;
            }
            if (idx >= 0) {
                auto& mc = monitoredClips_[idx];
                mc.clipId = cmd.clipId;
                mc.launchHandle = cmd.launchHandle;
                mc.lastState = te::LaunchHandle::PlayState::stopped;
                findOrAllocPlayheadSlot(cmd.clipId);
            }
        } else {
            // Unmonitor
            int idx = findClipIndex(cmd.clipId);
            if (idx >= 0) {
                freePlayheadSlot(cmd.clipId);
                // Swap with last element to keep array compact
                --numMonitored_;
                if (idx < numMonitored_) {
                    monitoredClips_[idx] = monitoredClips_[numMonitored_];
                }
                monitoredClips_[numMonitored_] = {};
            }
        }
    }

    // 2. Monitor each clip's LaunchHandle state
    for (int i = 0; i < numMonitored_; ++i) {
        auto& mc = monitoredClips_[i];
        if (!mc.launchHandle)
            continue;

        auto currentState = mc.launchHandle->getPlayingStatus();

        // Detect transitions
        if (currentState != mc.lastState) {
            SessionClipStateEvent event;
            event.clipId = mc.clipId;
            event.transportPositionSeconds = transportPositionSeconds;

            if (currentState == te::LaunchHandle::PlayState::playing) {
                event.type = SessionClipStateEvent::Type::StartedPlaying;
            } else {
                event.type = SessionClipStateEvent::Type::StoppedPlaying;
            }

            stateQueue_.push(event);
            mc.lastState = currentState;
        }

        // Update playhead for playing clips
        if (currentState == te::LaunchHandle::PlayState::playing) {
            auto playedRange = mc.launchHandle->getPlayedRange();
            if (playedRange) {
                double elapsed = playedRange->getLength().inBeats();
                // Write to atomic slot
                for (int s = 0; s < kMaxMonitoredClips; ++s) {
                    if (playheadSlots_[s].clipId.load(std::memory_order_relaxed) == mc.clipId) {
                        playheadSlots_[s].elapsedBeats.store(elapsed, std::memory_order_relaxed);
                        break;
                    }
                }
            }
        }
    }
}

double SessionClipAudioMonitor::getClipElapsedBeats(ClipId clipId) const {
    for (int i = 0; i < kMaxMonitoredClips; ++i) {
        if (playheadSlots_[i].clipId.load(std::memory_order_relaxed) == clipId) {
            return playheadSlots_[i].elapsedBeats.load(std::memory_order_relaxed);
        }
    }
    return -1.0;
}

void SessionClipAudioMonitor::getActivePlayheadBeats(
    std::unordered_map<ClipId, double>& out) const {
    out.clear();
    for (int i = 0; i < kMaxMonitoredClips; ++i) {
        auto id = playheadSlots_[i].clipId.load(std::memory_order_relaxed);
        if (id != INVALID_CLIP_ID) {
            double beats = playheadSlots_[i].elapsedBeats.load(std::memory_order_relaxed);
            if (beats >= 0.0) {
                out[id] = beats;
            }
        }
    }
}

void SessionClipAudioMonitor::clear() {
    numMonitored_ = 0;
    for (auto& mc : monitoredClips_)
        mc = {};
    for (int i = 0; i < kMaxMonitoredClips; ++i) {
        playheadSlots_[i].clipId.store(INVALID_CLIP_ID, std::memory_order_relaxed);
        playheadSlots_[i].elapsedBeats.store(-1.0, std::memory_order_relaxed);
    }
    commandQueue_.clear();
    stateQueue_.clear();
}

int SessionClipAudioMonitor::findClipIndex(ClipId clipId) const {
    for (int i = 0; i < numMonitored_; ++i) {
        if (monitoredClips_[i].clipId == clipId)
            return i;
    }
    return -1;
}

int SessionClipAudioMonitor::findOrAllocPlayheadSlot(ClipId clipId) {
    // Check if already allocated
    for (int i = 0; i < kMaxMonitoredClips; ++i) {
        if (playheadSlots_[i].clipId.load(std::memory_order_relaxed) == clipId)
            return i;
    }
    // Find free slot
    for (int i = 0; i < kMaxMonitoredClips; ++i) {
        if (playheadSlots_[i].clipId.load(std::memory_order_relaxed) == INVALID_CLIP_ID) {
            playheadSlots_[i].clipId.store(clipId, std::memory_order_relaxed);
            playheadSlots_[i].elapsedBeats.store(-1.0, std::memory_order_relaxed);
            return i;
        }
    }
    return -1;
}

void SessionClipAudioMonitor::freePlayheadSlot(ClipId clipId) {
    for (int i = 0; i < kMaxMonitoredClips; ++i) {
        if (playheadSlots_[i].clipId.load(std::memory_order_relaxed) == clipId) {
            playheadSlots_[i].clipId.store(INVALID_CLIP_ID, std::memory_order_relaxed);
            playheadSlots_[i].elapsedBeats.store(-1.0, std::memory_order_relaxed);
            return;
        }
    }
}

}  // namespace magda
