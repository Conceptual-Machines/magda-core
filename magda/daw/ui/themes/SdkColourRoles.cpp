#include "SdkColourRoles.hpp"

#include "ActiveTheme.hpp"

namespace magda {

juce::Colour sdkRoleColour(sdk::display::ColourRole role) {
    using Role = sdk::display::ColourRole;
    switch (role) {
        case Role::Background:
            return ActiveTheme::getColour(ActiveTheme::BACKGROUND);
        case Role::Surface:
            return ActiveTheme::getColour(ActiveTheme::SURFACE);
        case Role::Border:
            return ActiveTheme::getColour(ActiveTheme::BORDER);
        case Role::Text:
            return ActiveTheme::getColour(ActiveTheme::TEXT_PRIMARY);
        case Role::TextDim:
            return ActiveTheme::getColour(ActiveTheme::TEXT_SECONDARY);
        case Role::Accent:
            return ActiveTheme::getColour(ActiveTheme::ACCENT_PRIMARY);
        case Role::MeterLow:
        case Role::MeterMid:
        case Role::MeterHigh:
        case Role::MeterClip:
            break;
    }
    return juce::Colour(sdk::display::defaultColour(role));
}

}  // namespace magda
