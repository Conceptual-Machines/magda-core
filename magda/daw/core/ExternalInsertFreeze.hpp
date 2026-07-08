#pragma once

#include <juce_core/juce_core.h>

#include <vector>

#include "ClipInfo.hpp"
#include "TypeIds.hpp"

namespace magda {

/**
 * @brief Freeze state of an External FX / External Instrument device (#1623).
 *
 * Offline export cannot capture outboard gear, so freeze plays the track
 * through the live engine, records the insert's audio return into an audio
 * clip, and replaces the track's clips with it. This struct holds everything
 * unfreeze needs to restore the track exactly.
 *
 * Referenced from DeviceInfo through shared_ptr<const ...> so model copies stay
 * cheap — treat instances as immutable once published: replace the pointer,
 * never mutate the pointee.
 */
struct ExternalInsertFreeze {
    // Capture wav path, relative to the project media directory.
    juce::String captureFile;

    // The audio clip holding the captured return, placed on the track.
    ClipId frozenClipId = INVALID_CLIP_ID;

    // The track's original clips, removed by freeze and restored on unfreeze.
    // Beats fields are authoritative; derived seconds are recomputed against
    // the live tempo when the clips are restored.
    std::vector<ClipInfo> stashedClips;

    // Devices this freeze bypassed (the insert itself plus every device before
    // it in the chain, minus any that were already bypassed by the user).
    std::vector<DeviceId> bypassedDevices;
};

}  // namespace magda
