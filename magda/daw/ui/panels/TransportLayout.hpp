#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <cstdint>
#include <string>

namespace magda::daw::ui::transport {

/** Widths of the strings the transport bar has to hold, measured by the caller
 *  from the exact fonts that will draw them.
 *
 *  The layout takes these as input instead of measuring them itself so the
 *  arithmetic stays a pure function of (width, height, text, density, style)
 *  and can be asserted without a graphics context.
 */
struct TextWidths {
    int timecodeBox = 0;          // a whole bars.beats.ticks readout, as it sizes itself
    int timecodeOverlay = 0;      // the S / E letter before the digits, with its gap
    int stackTimecodeBox = 0;     // a readout at the Justified stack's smaller size
    int headlineTimecodeBox = 0;  // the same at the playhead headline size
    int timecodeCaption = 0;      // the widest of the SEL / LOOP / CUR captions, in their font
    int timecodeGlyphInset = 0;   // how far inside a readout's edge its last glyph already stops
    int tempo = 0;                // "999.99" in the headline font
    int timeSigNumerator = 0;     // "16/" -- the numerator carries the slash
    int timeSigDenominator = 0;   // "16"
    int keyRoot = 0;              // the widest root name
    int keyQuality = 0;           // the wider of "maj" / "min"
    int rangeChip = 0;            // the wider of the SEL / LOOP chip captions
    int memoryCaption = 0;        // "MEM"
    int memoryTime = 0;           // "88:88 / 88:88"
    int keep = 0;                 // "KEEP"
    int cpuTitle = 0;             // the localized "CPU" caption
    int cpuValue = 0;             // "100%"
    int gridDivision = 0;         // widest single line of a division label, e.g. "32."
    int gridToggle = 0;           // the wider of the AUTO / SNAP captions
    int banner = 0;               // "AUTOMATION WRITE"
};

/** How the groups share the bar's width once they all fit. */
enum class Style : std::uint8_t {
    Anchored,    // transport left, time and tempo centred, tools right
    MemoryFill,  // everything left-packed; the master buffer meter takes the slack
    Justified,   // groups spaced evenly; SEL / LOOP / CUR in one three-line stack
};

/** The collapsible sections, in the order they are dropped when the panel is
 *  too narrow to hold them all. Play/stop/record/automation-write, the tempo
 *  group and the playhead are never dropped.
 */
enum class Section : std::uint8_t {
    MemoryMeter,   // the master buffer meter shrinks to its dot and KEEP
    RightCluster,  // KEEP, QWERTY toggle and CPU; the overflow button takes their place
    Grid,          // grid division + AUTO/SNAP
    Punch,         // punch in/out box
    LoopBack,      // loop + back-to-arrangement
    Nav,           // home / prev / next
    SelLoopTimes,  // selection + loop readouts
};

inline constexpr std::array<Section, 7> kDropOrder{
    Section::MemoryMeter, Section::RightCluster, Section::Grid,         Section::Punch,
    Section::LoopBack,    Section::Nav,          Section::SelLoopTimes,
};

/** Where every child goes and which of them are on. A rectangle belonging to a
 *  dropped section is left empty, so nothing downstream can read a stale one.
 */
struct Layout {
    Style style = Style::Anchored;

    bool navVisible = true;
    bool loopBackVisible = true;
    bool punchVisible = true;
    bool selLoopTimesVisible = true;
    bool gridVisible = true;
    bool memoryMeterVisible = true;   // false shows only the dot and KEEP
    bool rightClusterVisible = true;  // KEEP + QWERTY toggle + CPU
    bool overflowVisible = false;
    bool automationWriteLabelFits = false;

    bool isVisible(Section section) const;

    // Selection and loop share one pair of rows behind the SEL / LOOP chips,
    // except in the Justified stack, which shows both.
    bool selectionAndLoopShareRows() const {
        return style != Style::Justified;
    }

    // What the readouts keep clear at their right end: the punch icons, and
    // the CUR caption over the headline playhead.
    int punchTrailingInset = 0;
    int cursorTrailingInset = 0;

    // Group frames, painted behind their children.
    juce::Rectangle<int> punchFrame, tempoFrame, cursorFrame, rangeFrame, stackFrame, gridFrame,
        memoryFrame;

    juce::Rectangle<int> home, prev, next;
    juce::Rectangle<int> play, stop, record, automationWrite;
    juce::Rectangle<int> loop, backToArrangement;
    juce::Rectangle<int> punchStart, punchEnd, punchIn, punchOut;
    juce::Rectangle<int> tempo, countIn, timeSigNumerator, timeSigDenominator, metronome, key;
    int keyDividerX = 0;
    juce::Rectangle<int> selChip, loopChip;
    juce::Rectangle<int> selCaption, loopCaption, cursorCaption;  // Justified stack rows
    juce::Rectangle<int> selectionStart, selectionEnd, loopStart, loopEnd;
    juce::Rectangle<int> playhead, editCursor;
    juce::Rectangle<int> gridDivision, autoGrid, snap;
    juce::Rectangle<int> memoryDot, memoryCaption, memoryMeter, memoryTime, keep;
    juce::Rectangle<int> qwerty, overflow;
    juce::Rectangle<int> automationWriteLabel;
    juce::Rectangle<int> cpu, cpuTitle, cpuValue;
};

/** Resolves the whole bar: measures every group from the same code that
 *  places it, drops sections in kDropOrder until the survivors fit, then
 *  spreads the slack the way the style asks. densityScale is the user's
 *  spacing density (LayoutConfig).
 */
Layout compute(int width, int height, const TextWidths& text, float densityScale,
               Style style = Style::Anchored);

/** The config word for each style, and back; an unknown word is Anchored. */
const char* styleKey(Style style);
Style styleFromKey(const std::string& key);

}  // namespace magda::daw::ui::transport
