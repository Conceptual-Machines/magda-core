#include "MixerStripOrder.hpp"

namespace magda {

namespace {

const TrackInfo* findTrack(const std::vector<TrackInfo>& tracks, TrackId id) {
    for (const auto& track : tracks)
        if (track.id == id)
            return &track;
    return nullptr;
}

}  // namespace

bool isMixerStrip(const TrackInfo& track, const std::vector<TrackInfo>& tracks, ViewMode mode) {
    if (!track.isVisibleIn(mode))
        return false;

    // Aux returns are drawn in their own section beside the channel strips, so
    // they take no position among them.
    if (track.type == TrackType::Aux)
        return false;

    if (track.hasParent()) {
        if (const auto* parent = findTrack(tracks, track.parentId)) {
            if ((parent->isGroup() || parent->hasChildren()) && parent->isCollapsedIn(mode))
                return false;
        }
    }
    return true;
}

std::vector<TrackId> mixerStripOrder(const std::vector<TrackInfo>& tracks, ViewMode mode) {
    std::vector<TrackId> order;
    order.reserve(tracks.size());
    for (const auto& track : tracks)
        if (isMixerStrip(track, tracks, mode))
            order.push_back(track.id);
    return order;
}

TrackId mixerStripAtPosition(const std::vector<TrackInfo>& tracks, ViewMode mode, int position) {
    if (position < 1)
        return INVALID_TRACK_ID;

    int seen = 0;
    for (const auto& track : tracks) {
        if (!isMixerStrip(track, tracks, mode))
            continue;
        if (++seen == position)
            return track.id;
    }
    return INVALID_TRACK_ID;
}

}  // namespace magda
