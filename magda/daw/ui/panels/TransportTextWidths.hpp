#pragma once

#include "TransportLayout.hpp"

namespace magda::daw::ui::transport {

// The AUTO / SNAP captions live here rather than at the point the buttons are
// built, so the width measured for them and the text drawn in them are the
// same string.
inline constexpr const char* kAutoGridCaption = "AUTO";
inline constexpr const char* kSnapCaption = "SNAP";

// Font sizes the transport itself chooses, for the children that cache a font
// rather than resolving one at paint time. Declared here so the size measured
// and the size applied to the widget are one number. The children that do
// resolve their own font publish theirs instead (BarsBeatsTicksLabel,
// GridDivisionButton, SmallButtonLookAndFeel), and the measurement reads it
// from them.
inline constexpr float kReadoutFontSize = 14.0f;  // BPM and time signature
inline constexpr float kCpuTitleFontSize = 8.0f;
inline constexpr float kCpuValueFontSize = 11.0f;
inline constexpr float kBannerFontSize = 10.0f;  // the AUTOMATION WRITE banner

/** Measures the widest string each text-sized section of the transport has to
 *  hold, in the font that section actually draws with.
 *
 *  This is the half of the layout that needs a graphics context, kept apart
 *  from TransportLayout so the arithmetic there stays a pure function and can
 *  be asserted without one.
 */
TextWidths measureTextWidths();

}  // namespace magda::daw::ui::transport
