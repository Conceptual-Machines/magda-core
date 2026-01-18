#pragma once

#include <juce_core/juce_core.h>
#include <juce_graphics/juce_graphics.h>

namespace magica {

/**
 * @brief Track data structure containing all track properties
 */
struct TrackInfo {
    int id = -1;          // Unique identifier
    juce::String name;    // Track name
    juce::Colour colour;  // Track color

    // Mixer state
    float volume = 0.75f;  // Volume level (0-1)
    float pan = 0.0f;      // Pan position (-1 to 1)
    bool muted = false;
    bool soloed = false;
    bool recordArmed = false;

    // Default track colors
    static inline const std::array<juce::uint32, 8> defaultColors = {
        0xFF5588AA,  // Blue
        0xFF55AA88,  // Teal
        0xFF88AA55,  // Green
        0xFFAAAA55,  // Yellow
        0xFFAA8855,  // Orange
        0xFFAA5555,  // Red
        0xFFAA55AA,  // Purple
        0xFF5555AA,  // Indigo
    };

    static juce::Colour getDefaultColor(int index) {
        return juce::Colour(defaultColors[index % defaultColors.size()]);
    }
};

}  // namespace magica
