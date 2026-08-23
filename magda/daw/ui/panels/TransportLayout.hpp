#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <cstdint>

namespace magda::daw::ui::transport {

/** Widths of the strings the transport bar has to hold, measured by the caller
 *  from the exact fonts that will draw them.
 *
 *  The layout takes these as input instead of measuring them itself so the
 *  arithmetic stays a pure function of (width, height, text, density) and can
 *  be asserted without a graphics context. Each field is the pixel width of the
 *  widest content that widget will ever show, asked of the widget itself where
 *  the widget is the one that knows.
 */
struct TextWidths {
    int timecodeBox = 0;         // a whole bars.beats.ticks readout, as it sizes itself
    int timecodeCaption = 0;     // the widest of the SEL / LOOP / CUR captions, in their font
    int timecodeGlyphInset = 0;  // how far inside a readout's edge its last glyph already stops
    int tempo = 0;               // "999.99" in the BPM readout font
    int timeSigNumerator = 0;    // "16/" -- the numerator carries the slash
    int timeSigDenominator = 0;  // "16"
    int cpuTitle = 0;            // the localized "CPU" caption
    int cpuValue = 0;            // "100%"
    int gridDivision = 0;        // widest single line of a division label, e.g. "32."
    int gridToggle = 0;          // the wider of the AUTO / SNAP captions
};

/** The collapsible sections, in the order they are dropped when the panel is
 *  too narrow to hold them all. Play/stop/record/automation-write, the BPM
 *  cluster and the playhead/edit-cursor group have no entry here: they are
 *  never dropped, because a transport without them is not a transport.
 */
enum class Section : std::uint8_t {
    RightCluster,  // CPU meter + QWERTY toggle; the overflow button takes their place
    Grid,          // grid division + AUTO/SNAP
    Punch,         // punch in/out box
    LoopBack,      // loop + back-to-arrangement
    Nav,           // home / prev / next
    SelLoopTimes,  // selection + loop readouts
};

inline constexpr std::array<Section, 6> kDropOrder{
    Section::RightCluster, Section::Grid, Section::Punch,
    Section::LoopBack,     Section::Nav,  Section::SelLoopTimes,
};

/** Where every child goes and which of them are on. A rectangle belonging to a
 *  dropped section is left empty, so nothing downstream can read a stale one.
 */
struct Layout {
    bool navVisible = true;
    bool loopBackVisible = true;
    bool punchVisible = true;
    bool selLoopTimesVisible = true;
    bool gridVisible = true;
    bool rightClusterVisible = true;  // CPU meter + QWERTY toggle
    bool overflowVisible = false;
    bool automationWriteLabelFits = true;

    bool isVisible(Section section) const;

    // What every timecode readout keeps clear at its right end, where the
    // group caption is drawn over it and the punch box carries its icons. The
    // readouts' widths include it; TransportPanel hands it to each label.
    int timeBoxTrailingInset = 0;

    // Right edge of each separator-delimited band, in panel coordinates.
    int transportRight = 0;
    int metroRight = 0;
    int timeRight = 0;

    juce::Rectangle<int> home, prev, next;
    juce::Rectangle<int> play, stop, record, automationWrite;
    juce::Rectangle<int> loop, backToArrangement;
    juce::Rectangle<int> punchStart, punchEnd, punchIn, punchOut;
    juce::Rectangle<int> tempo, timeSigNumerator, timeSigDenominator;
    juce::Rectangle<int> countIn, metronome;
    juce::Rectangle<int> selectionStart, selectionEnd, loopStart, loopEnd;
    juce::Rectangle<int> playhead, editCursor;
    juce::Rectangle<int> gridDivision, autoGrid, snap;
    juce::Rectangle<int> qwerty, overflow;
    juce::Rectangle<int> automationWriteLabel;
    juce::Rectangle<int> cpu, cpuTitle, cpuValue;
};

/** Resolves the whole bar: measures every section from the same code that
 *  places it, drops sections in kDropOrder until the survivors fit, then
 *  arranges them. densityScale is the user's spacing density (LayoutConfig).
 */
Layout compute(int width, int height, const TextWidths& text, float densityScale);

}  // namespace magda::daw::ui::transport
