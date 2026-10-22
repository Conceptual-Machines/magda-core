#pragma once

#include "clip/ClipSnapshot.hpp"

/**
 * @file AraRegionTiming.hpp
 * @brief Where an audio event's ARA playback region sits, in both of ARA's time domains (#1579).
 *
 * Offline commit renders the edited source back through MAGDA's own reading chain, so stretch,
 * warp and reverse apply after the plugin. A region is therefore never transformed: it is the
 * stretch of source the event plays, laid at the event's start.
 */

namespace magda::daw::audio::ara {

/** @brief An ARA playback region's two ranges, in seconds. */
struct AraRegionTiming {
    double startInModification = 0.0;
    double durationInModification = 0.0;
    double startInPlayback = 0.0;
    double durationInPlayback = 0.0;
};

/** @brief @p event's source range (its loop region when looping), clamped to the file. */
AraRegionTiming regionTimingFor(const engine::AudioClipPlayback& clip,
                                const engine::AudioEventPlayback& event);

}  // namespace magda::daw::audio::ara
