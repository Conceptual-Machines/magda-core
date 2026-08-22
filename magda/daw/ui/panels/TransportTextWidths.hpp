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

// The range the timecode readouts accept, in beats. Declared here so the
// labels and the width measured for them agree on how large a bar number the
// box has to hold.
inline constexpr double kTimecodeMaxBeats = 100000.0;

// How far the retained peak has to run ahead of the average before the CPU
// readout shows it as well.
inline constexpr int kCpuPeakMargin = 2;

/** The CPU readout: the smoothed average, with the retained peak beside it once
 *  the peak has run far enough ahead to be worth showing. The label and the
 *  width measured for it both format through here, so the slot always fits what
 *  it will be asked to draw. */
juce::String cpuReadoutText(int averagePercent, int peakPercent);

/** Measures the widest string each text-sized section of the transport has to
 *  hold, in the font that section actually draws with.
 *
 *  This is the half of the layout that needs a graphics context, kept apart
 *  from TransportLayout so the arithmetic there stays a pure function and can
 *  be asserted without one.
 */
TextWidths measureTextWidths();

}  // namespace magda::daw::ui::transport
