#include "MixerStripOrder.hpp"

#include <algorithm>
#include <iterator>
#include <ranges>

#include "RangesHelpers.hpp"

namespace magda {

namespace {

const TrackInfo* findTrack(const std::vector<TrackInfo>& tracks, TrackId id) {
    const auto hasId = [id](const TrackInfo& track) { return track.id == id; };
    const auto found = std::ranges::find_if(tracks, hasId);
    return found == tracks.end() ? nullptr : &*found;
}

auto isStripIn(const std::vector<TrackInfo>& tracks, ViewMode mode) {
    return [&tracks, mode](const TrackInfo& track) { return isMixerStrip(track, tracks, mode); };
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
    return tracks | std::views::filter(isStripIn(tracks, mode)) |
           std::views::transform(&TrackInfo::id) | toStd<std::vector<TrackId>>();
}

TrackId mixerStripAtPosition(const std::vector<TrackInfo>& tracks, ViewMode mode, int position) {
    if (position < 1)
        return INVALID_TRACK_ID;

    // Positions count from 1: the strip at N is the (N - 1)th one along.
    auto strips = tracks | std::views::filter(isStripIn(tracks, mode));
    const auto found = std::ranges::next(strips.begin(), position - 1, strips.end());
    return found == strips.end() ? INVALID_TRACK_ID : found->id;
}

}  // namespace magda
