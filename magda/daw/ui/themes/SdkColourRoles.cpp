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
        case Role::TextBright:
            return ActiveTheme::getColour(ActiveTheme::TEXT_BRIGHT);
        case Role::Curve:
            return ActiveTheme::getColour(ActiveTheme::AUTOMATION_BEZIER);
        case Role::CurvePoint:
            return ActiveTheme::getColour(ActiveTheme::CURVE_POINT);
        case Role::Handle:
            return ActiveTheme::getColour(ActiveTheme::CURVE_HANDLE_BACKGROUND);
        case Role::HandleStroke:
            return ActiveTheme::getColour(ActiveTheme::CURVE_HANDLE_NORMAL);
        case Role::Tooltip:
            return ActiveTheme::getColour(ActiveTheme::CURVE_TOOLTIP_BACKGROUND);
        case Role::TooltipText:
            return ActiveTheme::getColour(ActiveTheme::CURVE_TOOLTIP_TEXT);
        case Role::Guide:
            return ActiveTheme::getColour(ActiveTheme::AUTOMATION_GUIDE);
        case Role::Shade:
            return ActiveTheme::getColour(ActiveTheme::TEXT_DARK);
        case Role::Waveform:
            return ActiveTheme::getColour(ActiveTheme::ACCENT_PRIMARY);
        case Role::LoopRegion:
            return ActiveTheme::getColour(ActiveTheme::ACCENT_POSITIVE);
        case Role::MarkerStart:
            return ActiveTheme::getColour(ActiveTheme::SAMPLER_START_MARKER);
        case Role::MarkerEnd:
            return ActiveTheme::getColour(ActiveTheme::SAMPLER_END_MARKER);
        case Role::Playhead:
            return ActiveTheme::getColour(ActiveTheme::TEXT_BRIGHT);
        case Role::MeterLow:
        case Role::MeterMid:
        case Role::MeterHigh:
        case Role::MeterClip:
            break;
    }
    return juce::Colour(sdk::display::defaultColour(role));
}

}  // namespace magda
