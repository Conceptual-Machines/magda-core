#include "WarpMarkerCommands.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

#include "../audio/AudioBridge.hpp"
#include "ClipManager.hpp"

namespace magda {

std::vector<WarpMarker> getClipWarpMarkers(ClipId clipId) {
    const auto* event = primaryEventOf(ClipManager::getInstance().getClip(clipId));
    if (event == nullptr)
        return {};
    if (!event->warpMarkers.empty()) {
        auto markers = event->warpMarkers;
        std::stable_sort(markers.begin(), markers.end(),
                         [](const auto& a, const auto& b) { return a.sourceTime < b.sourceTime; });
        return markers;
    }
    const double length = event->sourceDurationSeconds();
    if (!std::isfinite(length) || length <= 0.0)
        return {};
    return {{0.0, 0.0}, {length, length}};
}

namespace {
void storeMarkers(ClipId clipId, const std::vector<WarpMarker>& markers) {
    auto& manager = ClipManager::getInstance();
    if (auto* event = primaryEventOf(manager.getClip(clipId))) {
        if (event->warpMarkers == markers)
            return;
        event->warpMarkers = markers;
        manager.forceNotifyClipPropertyChanged(clipId);
    }
}

// Match the stretch limits used by Tracktion's WarpTimeManager. Intersect both
// neighbours' ranges so dragging cannot cross a marker or collapse a segment.
double constrainWarpTime(const std::vector<WarpMarker>& markers, int index, double time) {
    constexpr double minRatio = 0.10001;
    constexpr double maxRatio = 19.9999;
    double low = -std::numeric_limits<double>::infinity();
    double high = std::numeric_limits<double>::infinity();
    if (index > 0) {
        const auto& previous = markers[static_cast<size_t>(index - 1)];
        const double span = markers[static_cast<size_t>(index)].sourceTime - previous.sourceTime;
        low = previous.warpTime + span * minRatio;
        high = previous.warpTime + span * maxRatio;
    }
    if (index + 1 < static_cast<int>(markers.size())) {
        const auto& next = markers[static_cast<size_t>(index + 1)];
        const double span = next.sourceTime - markers[static_cast<size_t>(index)].sourceTime;
        low = std::max(low, next.warpTime - span * maxRatio);
        high = std::min(high, next.warpTime - span * minRatio);
    }
    return low <= high ? std::clamp(time, low, high) : markers[static_cast<size_t>(index)].warpTime;
}
}  // namespace

// =============================================================================
// AddWarpMarkerCommand
// =============================================================================

AddWarpMarkerCommand::AddWarpMarkerCommand(AudioBridge* bridge, ClipId clipId, double sourceTime,
                                           double warpTime)
    : bridge_(bridge), clipId_(clipId), sourceTime_(sourceTime), warpTime_(warpTime) {}

void AddWarpMarkerCommand::execute() {
    if (!bridge_) {
        auto* event = primaryEventOf(ClipManager::getInstance().getClip(clipId_));
        if (!event || !std::isfinite(sourceTime_) || !std::isfinite(warpTime_))
            return;
        auto markers = getClipWarpMarkers(clipId_);
        auto at = std::lower_bound(
            markers.begin(), markers.end(), sourceTime_,
            [](const auto& marker, double time) { return marker.sourceTime < time; });
        // Boundary markers already exist; duplicate source positions have no valid slope.
        if (at == markers.begin() || at == markers.end() || at->sourceTime == sourceTime_)
            return;
        if (!oldMarkers_)
            oldMarkers_ = event->warpMarkers;
        addedIndex_ = static_cast<int>(at - markers.begin());
        markers.insert(at, {sourceTime_, warpTime_});
        markers[static_cast<size_t>(addedIndex_)].warpTime =
            constrainWarpTime(markers, addedIndex_, warpTime_);
        storeMarkers(clipId_, markers);
        return;
    }

    addedIndex_ = bridge_->addWarpMarker(clipId_, sourceTime_, warpTime_);
    ClipManager::getInstance().forceNotifyClipPropertyChanged(clipId_);
}

void AddWarpMarkerCommand::undo() {
    if (!bridge_) {
        if (oldMarkers_)
            storeMarkers(clipId_, *oldMarkers_);
        return;
    }
    if (addedIndex_ < 0)
        return;

    bridge_->removeWarpMarker(clipId_, addedIndex_);
    ClipManager::getInstance().forceNotifyClipPropertyChanged(clipId_);
    addedIndex_ = -1;
}

// =============================================================================
// MoveWarpMarkerCommand
// =============================================================================

MoveWarpMarkerCommand::MoveWarpMarkerCommand(AudioBridge* bridge, ClipId clipId, int index,
                                             double newWarpTime)
    : bridge_(bridge), clipId_(clipId), index_(index), newWarpTime_(newWarpTime) {}

void MoveWarpMarkerCommand::execute() {
    if (!bridge_) {
        auto* event = primaryEventOf(ClipManager::getInstance().getClip(clipId_));
        auto markers = getClipWarpMarkers(clipId_);
        if (!event || !std::isfinite(newWarpTime_) || index_ < 0 ||
            index_ >= static_cast<int>(markers.size()))
            return;
        if (!oldMarkers_)
            oldMarkers_ = event->warpMarkers;
        markers[static_cast<size_t>(index_)].warpTime =
            constrainWarpTime(markers, index_, newWarpTime_);
        storeMarkers(clipId_, markers);
        return;
    }

    // Capture old position if we haven't already
    if (!hasOldTime_) {
        auto markers = bridge_->getWarpMarkers(clipId_);
        if (index_ >= 0 && index_ < static_cast<int>(markers.size())) {
            oldWarpTime_ = markers[static_cast<size_t>(index_)].warpTime;
            hasOldTime_ = true;
        }
    }

    bridge_->moveWarpMarker(clipId_, index_, newWarpTime_);
    ClipManager::getInstance().forceNotifyClipPropertyChanged(clipId_);
}

void MoveWarpMarkerCommand::undo() {
    if (!bridge_) {
        if (oldMarkers_)
            storeMarkers(clipId_, *oldMarkers_);
        return;
    }
    if (!hasOldTime_)
        return;

    bridge_->moveWarpMarker(clipId_, index_, oldWarpTime_);
    ClipManager::getInstance().forceNotifyClipPropertyChanged(clipId_);
}

bool MoveWarpMarkerCommand::canMergeWith(const UndoableCommand* other) const {
    const auto* otherMove = dynamic_cast<const MoveWarpMarkerCommand*>(other);
    if (!otherMove)
        return false;

    // Merge consecutive moves of the same marker
    return clipId_ == otherMove->clipId_ && index_ == otherMove->index_;
}

void MoveWarpMarkerCommand::mergeWith(const UndoableCommand* other) {
    const auto* otherMove = dynamic_cast<const MoveWarpMarkerCommand*>(other);
    if (otherMove) {
        // Keep our oldWarpTime_, update newWarpTime_ to the latest
        newWarpTime_ = otherMove->newWarpTime_;
    }
}

// =============================================================================
// RemoveWarpMarkerCommand
// =============================================================================

RemoveWarpMarkerCommand::RemoveWarpMarkerCommand(AudioBridge* bridge, ClipId clipId, int index)
    : bridge_(bridge), clipId_(clipId), index_(index) {}

void RemoveWarpMarkerCommand::execute() {
    if (!bridge_) {
        auto* event = primaryEventOf(ClipManager::getInstance().getClip(clipId_));
        auto markers = getClipWarpMarkers(clipId_);
        if (!event || index_ < 0 || index_ >= static_cast<int>(markers.size()))
            return;
        if (!oldMarkers_)
            oldMarkers_ = event->warpMarkers;
        if (index_ == 0 || index_ + 1 == static_cast<int>(markers.size()))
            markers[static_cast<size_t>(index_)].warpTime =
                constrainWarpTime(markers, index_, markers[static_cast<size_t>(index_)].sourceTime);
        else
            markers.erase(markers.begin() + index_);
        storeMarkers(clipId_, markers);
        return;
    }

    // Capture state before removal
    if (!hasCapturedState_) {
        auto markers = bridge_->getWarpMarkers(clipId_);
        if (index_ >= 0 && index_ < static_cast<int>(markers.size())) {
            removedSourceTime_ = markers[static_cast<size_t>(index_)].sourceTime;
            removedWarpTime_ = markers[static_cast<size_t>(index_)].warpTime;
            hasCapturedState_ = true;
        }
    }

    bridge_->removeWarpMarker(clipId_, index_);
    ClipManager::getInstance().forceNotifyClipPropertyChanged(clipId_);
}

void RemoveWarpMarkerCommand::undo() {
    if (!bridge_) {
        if (oldMarkers_)
            storeMarkers(clipId_, *oldMarkers_);
        return;
    }
    if (!hasCapturedState_)
        return;

    // Re-add the marker at its original position
    bridge_->addWarpMarker(clipId_, removedSourceTime_, removedWarpTime_);
    ClipManager::getInstance().forceNotifyClipPropertyChanged(clipId_);
}

}  // namespace magda
