#pragma once

#include <functional>
#include <optional>

// clang-format off
#include <tracktion_engine/tracktion_engine.h>
// clang-format on

#include "AudioEngine.hpp"

namespace magda {

/** Engine-internal preparation hook retained for focused render-state tests. */
void prepareEditForOfflineRender(tracktion::Edit& edit);

/**
 * Resolve model track IDs to Tracktion's renderer bitset.
 *
 * nullopt means an explicit filter could not be represented safely. In
 * particular, Tracktion interprets a zero bitset as "all tracks", so an
 * explicit filter that resolves to zero must fail rather than broaden.
 */
std::optional<juce::BigInteger> resolveOfflineRenderTrackFilter(
    const OfflineRenderRequest& request, int numTracks,
    const std::function<int(TrackId)>& indexForTrack);

}  // namespace magda
