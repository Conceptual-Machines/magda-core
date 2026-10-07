#include "ParamControlStyle.hpp"

#include "core/Config.hpp"

namespace magda::daw::ui {

const char* controlStyleKey(ParamControlStyle style) {
    switch (style) {
        case ParamControlStyle::Text:
            return "text";
        case ParamControlStyle::Knobs:
            return "knobs";
        case ParamControlStyle::Sliders:
            return "sliders";
    }
    return "text";
}

ParamControlStyle controlStyleFromKey(const juce::String& key) {
    if (key == "knobs")
        return ParamControlStyle::Knobs;
    if (key == "sliders")
        return ParamControlStyle::Sliders;
    return ParamControlStyle::Text;
}

ParamControlStyle resolveControlStyle(const juce::String& pluginOverride) {
    if (pluginOverride.isNotEmpty())
        return controlStyleFromKey(pluginOverride);
    return controlStyleFromKey(juce::String(Config::getInstance().getDeviceControlStyle()));
}

}  // namespace magda::daw::ui
