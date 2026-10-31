// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <cstdint>

namespace magda::daw::ui::transport {

/** Widths of the strings the transport bar has to hold, measured by the caller
 *  from the exact fonts that will draw them.
 *
 *  The layout takes these as input instead of measuring them itself so the
 *  arithmetic stays a pure function of (width, height, text, density)
 *  and can be asserted without a graphics context.
 */
struct TextWidths {
    int timecodeBox = 0;          // a whole bars.beats.ticks readout, as it sizes itself
    int headlineTimecodeBox = 0;  // the same at the playhead headline size
    int clock = 0;                // the playhead in minutes and seconds beside the headline
    int tempo = 0;                // "999.99" in the headline font
    int timeSigNumerator = 0;     // "16/" -- the numerator carries the slash
    int timeSigDenominator = 0;   // "16"
    int rangeCaption = 0;         // the wider of the SEL / LOOP captions
    int memoryCaption = 0;        // "MEM"
    int memoryTime = 0;           // "88:88 / 88:88"
    int cpuTitle = 0;             // the localized "CPU" caption
    int cpuValue = 0;             // "100%"
    int gridDivision = 0;         // the widest division label on one line, e.g. "1/32."
    int gridToggle = 0;           // the wider of the AUTO / SNAP captions
    int banner = 0;               // "AUTOMATION WRITE"
};

/** The collapsible sections, in the order they collapse or drop when the panel
 *  is too narrow to hold them all. Play/stop/record/automation-write, the tempo
 *  group and the playhead are never dropped.
 */
enum class Section : std::uint8_t {
    MemoryMeter,    // the master buffer meter shrinks to its dot and capture button
    Clock,          // the seconds clock beside the playhead
    LinkLettering,  // the Link chip shrinks to its LED
    RangeEnd,       // selection and loop show their starts, the ends in the tooltip
    RightCluster,   // memory, QWERTY toggle and CPU; the overflow button takes their place
    Grid,           // grid division + AUTO/SNAP
    Punch,          // punch in/out
    LoopBack,       // loop + back-to-arrangement
    Nav,            // home / prev / next
    SelLoopTimes,   // selection + loop readouts
};

inline constexpr std::array<Section, 10> kDropOrder{
    Section::MemoryMeter,  Section::Clock,        Section::LinkLettering, Section::RangeEnd,
    Section::RightCluster, Section::Grid,         Section::Punch,         Section::LoopBack,
    Section::Nav,          Section::SelLoopTimes,
};

/** Where every child goes and which of them are on. A rectangle belonging to a
 *  dropped section is left empty, so nothing downstream can read a stale one.
 */
struct Layout {
    bool navVisible = true;
    bool loopBackVisible = true;
    bool punchVisible = true;
    bool linkLetteringVisible = true;
    bool clockVisible = true;
    bool selLoopTimesVisible = true;
    bool rangeEndVisible = true;
    bool gridVisible = true;
    bool memoryMeterVisible = true;   // false shows only the dot and capture button
    bool rightClusterVisible = true;  // memory + QWERTY toggle + CPU
    bool overflowVisible = false;
    bool automationWriteLabelFits = false;

    bool isVisible(Section section) const;

    // Group frames, painted behind their children.
    juce::Rectangle<int> punchFrame, tempoFrame, cursorFrame, selectionFrame, loopFrame, gridFrame,
        memoryFrame;

    juce::Rectangle<int> home, prev, next;
    juce::Rectangle<int> play, stop, record, automationWrite, loop, backToArrangement;
    juce::Rectangle<int> punchIn, punchOut;  // the range itself is in a popup
    juce::Rectangle<int> link;               // the Ableton Link chip
    juce::Rectangle<int> tempo, countIn, timeSigNumerator, timeSigDenominator, metronome;
    juce::Rectangle<int> playhead, clock;
    juce::Rectangle<int> selectionCaption, selectionStart, selectionEnd;
    juce::Rectangle<int> loopCaption, loopStart, loopEnd;
    juce::Rectangle<int> gridDivision, autoGrid, snap;
    juce::Rectangle<int> memoryDot, memoryCaption, memoryMeter, memoryTime, capture;
    juce::Rectangle<int> qwerty, overflow;
    juce::Rectangle<int> automationWriteLabel;
    juce::Rectangle<int> cpu, cpuTitle, cpuValue;
};

/** Proportions of the Ableton Link chip, shared by the layout and the chip's paint. */
namespace link_chip {
inline constexpr float kLetteringShare = 0.6f;  // of the chip's height
inline constexpr float kLedShare = 0.24f;
inline constexpr float kPadShare = 0.2f;
inline constexpr float kGapShare = 0.25f;                   // between LED and lettering
inline constexpr float kLetteringAspect = 432.0f / 230.0f;  // Ableton's artwork

inline int widthForHeight(int height) {
    const auto h = static_cast<float>(height);
    return juce::roundToInt((h * kPadShare * 2) + (h * kLedShare) + (h * kGapShare) +
                            (h * kLetteringShare * kLetteringAspect));
}

/// The chip shrunk to its LED.
inline int ledOnlyWidthForHeight(int height) {
    const auto h = static_cast<float>(height);
    return juce::roundToInt((h * kPadShare * 2) + (h * kLedShare));
}
}  // namespace link_chip

/** Resolves the whole bar: measures every group from the same code that
 *  places it, collapses and drops sections in kDropOrder until the survivors
 *  fit, then centres the time and tempo groups in the slack. densityScale is
 *  the user's spacing density (LayoutConfig).
 */
Layout compute(int width, int height, const TextWidths& text, float densityScale);

}  // namespace magda::daw::ui::transport
