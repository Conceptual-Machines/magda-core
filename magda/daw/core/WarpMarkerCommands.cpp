#include "WarpMarkerCommands.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

#include "../audio/AudioThumbnailManager.hpp"
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

void seedWarpMarkersFromTransients(ClipId clipId, double bpm) {
    auto& manager = ClipManager::getInstance();
    const auto* clip = manager.getClip(clipId);
    auto* event = primaryEventOf(manager.getClip(clipId));
    if (clip == nullptr || event == nullptr)
        return;

    const double length = event->sourceDurationSeconds();
    const auto* transients =
        AudioThumbnailManager::getInstance().getCachedTransients(event->sourceFilePath());
    std::vector<WarpMarker> markers;
    if (transients != nullptr && std::isfinite(length) && length > 0.0) {
        // Identity markers at the transients the clip shows, between the two boundaries.
        const double visibleStart = event->anchorSeconds();
        const double visibleEnd =
            visibleStart + event->timelineToSource(clip->getTimelineLength(bpm));
        markers.push_back({0.0, 0.0});
        for (const double time : *transients)
            if (time >= visibleStart && time <= visibleEnd && time > 0.0 && time < length)
                markers.push_back({time, time});
        markers.push_back({length, length});
    }
    storeMarkers(clipId, markers);
}

void clearWarpMarkers(ClipId clipId) {
    storeMarkers(clipId, {});
}

// =============================================================================
// AddWarpMarkerCommand
// =============================================================================

AddWarpMarkerCommand::AddWarpMarkerCommand(ClipId clipId, double sourceTime, double warpTime)
    : clipId_(clipId), sourceTime_(sourceTime), warpTime_(warpTime) {}

void AddWarpMarkerCommand::execute() {
    auto* event = primaryEventOf(ClipManager::getInstance().getClip(clipId_));
    if (!event || !std::isfinite(sourceTime_) || !std::isfinite(warpTime_))
        return;
    auto markers = getClipWarpMarkers(clipId_);
    auto at =
        std::lower_bound(markers.begin(), markers.end(), sourceTime_,
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
}

void AddWarpMarkerCommand::undo() {
    if (oldMarkers_)
        storeMarkers(clipId_, *oldMarkers_);
}

// =============================================================================
// MoveWarpMarkerCommand
// =============================================================================

MoveWarpMarkerCommand::MoveWarpMarkerCommand(ClipId clipId, int index, double newWarpTime)
    : clipId_(clipId), index_(index), newWarpTime_(newWarpTime) {}

void MoveWarpMarkerCommand::execute() {
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
}

void MoveWarpMarkerCommand::undo() {
    if (oldMarkers_)
        storeMarkers(clipId_, *oldMarkers_);
}

bool MoveWarpMarkerCommand::canMergeWith(const UndoableCommand* other) const {
    const auto* otherMove = dynamic_cast<const MoveWarpMarkerCommand*>(other);
    if (!otherMove)
        return false;

    // Merge consecutive moves of the same marker
    return clipId_ == otherMove->clipId_ && index_ == otherMove->index_;
}

void MoveWarpMarkerCommand::mergeWith(const UndoableCommand* other) {
    if (const auto* otherMove = dynamic_cast<const MoveWarpMarkerCommand*>(other))
        newWarpTime_ = otherMove->newWarpTime_;
}

// =============================================================================
// RemoveWarpMarkerCommand
// =============================================================================

RemoveWarpMarkerCommand::RemoveWarpMarkerCommand(ClipId clipId, int index)
    : clipId_(clipId), index_(index) {}

void RemoveWarpMarkerCommand::execute() {
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
}

void RemoveWarpMarkerCommand::undo() {
    if (oldMarkers_)
        storeMarkers(clipId_, *oldMarkers_);
}

}  // namespace magda
