#pragma once

#include <juce_core/juce_core.h>

#include "core/TypeIds.hpp"

/**
 * @file TrackRouting.hpp
 * @brief How a track's routing fields name another track.
 *
 * Shared by everything that has to agree on what a routing field means: the
 * compiler, which turns routes into edges, and the value layer, which follows
 * an output route to see whether a track inherits its destination's mute. One
 * parser, so a route cannot mean two things in one engine.
 */

namespace magda::engine {

/// What a routing field resolves to. Malformed is deliberately distinct from
/// External: a broken "track:..." value is a broken internal route, and
/// collapsing the two would quietly reinterpret it as a hardware device or as
/// the default master routing instead of reporting it. None means the track
/// does not read that input at all, so there is nothing to route or report.
enum class RouteKind { None, External, Track, Malformed };

struct TrackRoute {
    RouteKind kind = RouteKind::External;
    TrackId trackId = INVALID_TRACK_ID;

    bool namesTrack() const {
        return kind == RouteKind::Track;
    }
};

/// Parses a routing field: "track:<id>" for an internal route, anything else
/// (a hardware device name, the default "master", an empty field) is external.
inline TrackRoute parseTrackRoute(const juce::String& routing) {
    if (!routing.startsWith("track:"))
        return {RouteKind::External, INVALID_TRACK_ID};

    const auto id = routing.substring(6).trim();
    // Rejects "1-2" and a bare "-": getIntValue would take the leading digits
    // and point the signal at a track that was never selected.
    if (id.isEmpty() || !id.containsOnly("-0123456789") || juce::String(id.getIntValue()) != id)
        return {RouteKind::Malformed, INVALID_TRACK_ID};

    return {RouteKind::Track, id.getIntValue()};
}

}  // namespace magda::engine
