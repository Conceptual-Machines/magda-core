#pragma once

#include <juce_core/juce_core.h>

#include "core/ChainNodePath.hpp"

namespace magda::faust_edits {

/**
 * @brief Make @p source the patch of the runtime Faust device at @p devicePath, on the model.
 *
 * The source is compiled on a detached device first, so a failure leaves the model untouched and
 * @p error says why. On success the model takes the source together with the parameters, meters
 * and sidechain port the patch declares, and both engines follow it (#2317, #2659).
 */
bool loadSource(const ChainNodePath& devicePath, const juce::String& name,
                const juce::String& source, juce::String& error);

}  // namespace magda::faust_edits
