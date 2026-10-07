#pragma once

#include <BinaryData.h>

#include <memory>

#include "ActiveTheme.hpp"
#include "SvgButton.hpp"

namespace magda {

// Mute and solo, shared by every view that shows them (track headers, inspector, mixer,
// session strips, master headers). One recipe so they read the same everywhere: a neutral
// chip whose glyph carries the state, and M / S letters when the preference asks for them.

/** @brief Mute: grey speaker (master_on) when audible, crossed speaker (master_off) in
 *  warning yellow when muted. Needs the dual-icon master_on / master_off button. */
inline void configureMasterSpeakerButton(SvgButton& button) {
    button.setClickingTogglesState(true);
    button.setBorderColor(ActiveTheme::BORDER);
    button.setNormalBackgroundColor(ActiveTheme::SURFACE);
    button.setActiveBackgroundColor(ActiveTheme::SURFACE);
    button.setStateColourReplacement(juce::Colour(0xFFB3B3B3), ActiveTheme::ICON_NEUTRAL,
                                     ActiveTheme::STATUS_WARNING);
    button.setStateColourReplacement(juce::Colour(0xFF1E1E1E), ActiveTheme::ICON_NEUTRAL,
                                     ActiveTheme::STATUS_WARNING);
    button.setIconPadding(3.5f);
    button.setLetterGlyph("M");
}

/** @brief Solo: a grey ring, amber when soloed. Needs the solo_svg button. */
inline void configureSoloButton(SvgButton& button) {
    button.setClickingTogglesState(true);
    button.setBorderColor(ActiveTheme::BORDER);
    button.setNormalBackgroundColor(ActiveTheme::SURFACE);
    button.setActiveBackgroundColor(ActiveTheme::SURFACE);
    button.setStateColourReplacement(juce::Colour(0xFFB3B3B3), ActiveTheme::ICON_NEUTRAL,
                                     ActiveTheme::DEVICE_AMBER);
    button.setIconPadding(4.5f);
    button.setLetterGlyph("S");
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

/** @brief For a single-icon mute (the chipless device-style ones): swaps in the crossed
 *  speaker while muted. */
inline void syncMuteGlyph(SvgButton& button, bool muted) {
    button.setToggleState(muted, juce::dontSendNotification);
    button.updateSvgData(muted ? BinaryData::master_off_svg : BinaryData::master_on_svg,
                         muted ? BinaryData::master_off_svgSize : BinaryData::master_on_svgSize);
}

}  // namespace magda
