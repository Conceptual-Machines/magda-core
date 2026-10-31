// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <initializer_list>

#include "TransportLayout.hpp"
#include "core/TempoUtils.hpp"

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
inline constexpr float kHeadlineFontSize = 16.0f;          // BPM
inline constexpr float kHeadlineTimecodeFontSize = 18.0f;  // the playhead on its own
inline constexpr float kRowTimecodeFontSize = 12.0f;       // punch and range readouts
inline constexpr float kReadoutFontSize = 14.0f;           // time signature
inline constexpr float kMemoryCaptionFontSize = 8.0f;
inline constexpr float kMemoryTimeFontSize = 11.0f;
inline constexpr float kCpuTitleFontSize = 8.0f;
inline constexpr float kCpuValueFontSize = 11.0f;
inline constexpr float kBannerFontSize = 10.0f;  // the AUTOMATION WRITE banner

// The SEL / LOOP captions: the string measured and the string drawn are one
// string.
inline constexpr const char* kSelectionCaption = "SEL";
inline constexpr const char* kLoopCaption = "LOOP";
inline constexpr float kRangeCaptionFontSize = 9.0f;

inline constexpr float kClockFontSize = 9.0f;
inline constexpr const char* kClockWidest = "8:88:88.888";

/** The playhead clock beside the headline: m:ss.mmm, with hours once past one. */
juce::String clockText(double seconds);

inline constexpr const char* kMemoryCaption = "MEM";
inline constexpr const char* kMemoryTimeWidest = "88:88 / 88:88";

// Key names by ProjectInfo::keyRoot.
inline constexpr std::array<const char*, 12> kKeyRootNames{"C",  "C#", "D",  "D#", "E",  "F",
                                                           "F#", "G",  "G#", "A",  "A#", "B"};

// Every text the automation-write banner can show.
inline constexpr std::array<const char*, 3> kBannerTexts{"AUTOMATION WRITE", "AUTOMATION TOUCH",
                                                         "AUTOMATION LATCH"};

// The largest bar number the timecode readouts show, in any meter: five
// digits. The range the labels accept follows from it, in beats at the fewest
// beats per bar, so the labels and the width measured for them agree on how
// large a bar number the box has to hold. (A round beat count at one beat per
// bar put a sixth digit on the one-indexed bar number, and every readout in
// every meter paid for it.)
inline constexpr int kTimecodeMaxBars = 99999;
inline constexpr double kTimecodeMaxBeats =
    static_cast<double>((kTimecodeMaxBars - 1) * MIN_TIME_SIGNATURE_VALUE);

// How far the retained peak has to run ahead of the average before the CPU
// readout shows it as well.
inline constexpr int kCpuPeakMargin = 2;

/** The CPU readout: the smoothed average, with the retained peak beside it once
 *  the peak has run far enough ahead to be worth showing. The label and the
 *  width measured for it both format through here, so the slot always fits what
 *  it will be asked to draw. */
juce::String cpuReadoutText(int averagePercent, int peakPercent);

// The fewest bar-number digits the readouts make room for, so the bar re-lays
// itself out only once a value passes bar 999.
inline constexpr int kMinTimecodeBarDigits = 3;

/** Digits in the largest one-indexed bar number among `beats`, never fewer than
 *  kMinTimecodeBarDigits. */
int timecodeBarDigits(std::initializer_list<double> beats, int numerator, int denominator);

/** Measures the widest string each text-sized section of the transport has to
 *  hold, in the font that section actually draws with. The timecode readouts
 *  hold barDigits of bar number and the beat numbers of a numerator-beat bar.
 *
 *  This is the half of the layout that needs a graphics context, kept apart
 *  from TransportLayout so the arithmetic there stays a pure function and can
 *  be asserted without one.
 */
TextWidths measureTextWidths(int barDigits = kMinTimecodeBarDigits,
                             int numerator = DEFAULT_TIME_SIGNATURE_NUMERATOR);

}  // namespace magda::daw::ui::transport
