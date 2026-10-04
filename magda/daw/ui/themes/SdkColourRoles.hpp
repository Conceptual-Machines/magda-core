#pragma once

#include <juce_graphics/juce_graphics.h>

#include "magda/sdk/display/ColourRole.hpp"

namespace magda {

/**
 * @brief The active theme's colour for an SDK display-list role, read at paint time.
 *
 * The meter roles keep the SDK reference palette, which is what LevelMeter has always drawn;
 * the theme's LEVEL_METER_* roles are different colours.
 */
juce::Colour sdkRoleColour(sdk::display::ColourRole role);

}  // namespace magda
