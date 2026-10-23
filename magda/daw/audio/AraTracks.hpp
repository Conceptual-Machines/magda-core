#pragma once

#include <juce_core/juce_core.h>

#include <optional>

#include "core/ClipTypes.hpp"
#include "core/TypeIds.hpp"

/**
 * @file AraTracks.hpp
 * @brief Which tracks an ARA plugin plays, for the UI (#1579).
 *
 * On such a track the plugin renders the clips from their raw files, so MAGDA's own stretch
 * and pitch do not apply there, and the controls for them stand down.
 */

namespace magda {

/** @brief The name of an ARA plugin on @p track, at any depth of its chain, or nothing. */
std::optional<juce::String> araPluginOnTrack(TrackId track);

/** @brief The ARA plugin on the track that holds @p clip, or nothing. */
std::optional<juce::String> araPluginForClip(ClipId clip);

}  // namespace magda
