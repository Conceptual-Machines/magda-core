#pragma once

#include <juce_core/juce_core.h>

#include <cstdint>
#include <string>

namespace magda::daw::ui {

/** How a device's parameter cells draw: today's text fields, knobs, or
 *  horizontal sliders. Chosen globally in Preferences, overridable per plugin. */
enum class ParamControlStyle : std::uint8_t { Text, Knobs, Sliders };

const char* controlStyleKey(ParamControlStyle style);
ParamControlStyle controlStyleFromKey(const juce::String& key);

/** The style a device draws with: its plugin's override, else the global one. */
ParamControlStyle resolveControlStyle(const juce::String& pluginOverride);

}  // namespace magda::daw::ui
