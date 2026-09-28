#pragma once

#include <BinaryData.h>

#include <memory>

#include "ActiveTheme.hpp"
#include "SvgButton.hpp"

namespace magda {

// The master mute speaker button, shared by every surface that shows one
// (arrange master header, inspector, mixer master strip, chain header, track
// headers). One recipe so the glyph reads the same everywhere: gray speaker
// (master_on) on a surface chip when audible, crossed speaker (master_off) on
// a warning chip when muted.

inline void configureMasterSpeakerButton(SvgButton& button) {
    button.setClickingTogglesState(true);
    button.setBorderColor(ActiveTheme::getColour(ActiveTheme::BORDER));
    button.setNormalBackgroundColor(ActiveTheme::getColour(ActiveTheme::SURFACE));
    button.setActiveBackgroundColor(ActiveTheme::getColour(ActiveTheme::STATUS_WARNING));
    button.setStateColourReplacement(juce::Colour(0xFFB3B3B3), ActiveTheme::ICON_NEUTRAL,
                                     ActiveTheme::ICON_ON_ACCENT);
    button.setStateColourReplacement(juce::Colour(0xFF1E1E1E), ActiveTheme::ICON_NEUTRAL,
                                     ActiveTheme::ICON_ON_ACCENT);
    button.setIconPadding(3.5f);  // larger speaker glyph
}

inline std::unique_ptr<SvgButton> makeMasterSpeakerButton() {
    auto button = std::make_unique<SvgButton>(
        "Speaker", BinaryData::master_on_svg, BinaryData::master_on_svgSize,
        BinaryData::master_off_svg, BinaryData::master_off_svgSize);
    configureMasterSpeakerButton(*button);
    return button;
}

inline void syncMasterSpeakerButton(SvgButton& button, bool muted) {
    button.setToggleState(muted, juce::dontSendNotification);
    button.setTooltip(muted ? "Unmute master" : "Mute master");
}

}  // namespace magda
