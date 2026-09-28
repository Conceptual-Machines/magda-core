#pragma once

#include <juce_core/juce_core.h>

#include "core/ChainNodePath.hpp"

namespace magda::sampler_edits {

/**
 * @brief Authoring a sampler's state on the MODEL, which owns it; the running
 *        device is a projection (#2317).
 *
 * A faceplate that writes the device instead loses the edit at the next rebuild
 * and never reaches the native leg (#2379). Each returns false when @p
 * devicePath does not resolve to an internal device this build can rewrite.
 */

/// Loop on/off — authored state, never a parameter.
bool setLoopEnabled(const ChainNodePath& devicePath, bool enabled);

/// The note the sample plays back untransposed at. Authored state too.
bool setRootNote(const ChainNodePath& devicePath, int note);

/// Point the sampler at @p file and reset its markers to span it. The markers
/// are authored here rather than left to the device, whose restore keeps
/// whatever the model holds. False for a file no sample format can read.
bool loadSample(const ChainNodePath& devicePath, const juce::File& file);

}  // namespace magda::sampler_edits
