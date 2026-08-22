#include "TransportLayout.hpp"

namespace magda::daw::ui::transport {

bool Layout::isVisible(Section section) const {
    switch (section) {
        case Section::RightCluster:
            return rightClusterVisible;
        case Section::Grid:
            return gridVisible;
        case Section::Punch:
            return punchVisible;
        case Section::LoopBack:
            return loopBackVisible;
        case Section::Nav:
            return navVisible;
        case Section::SelLoopTimes:
            return selLoopTimesVisible;
    }
    return true;
}

namespace {

// Spacing tokens at normal density. These scale with the user's spacing
// density; the widget sizes they sit between (icon buttons, readout cells) do
// not, so the controls keep their hit targets at every density.
constexpr int kEdgePad = 6;        // left inset, before the first button
constexpr int kButtonGap = 1;      // between icon buttons within a group
constexpr int kGroupPad = 3;       // between the button groups and the punch box
constexpr int kCellPad = 6;        // inside a text readout, each side
constexpr int kTimeBoxSlack = 24;  // how far a timecode box may grow into spare width
constexpr int kTimeLeadPad = 10;   // before the first time group
constexpr int kTimeGroupGap = 6;   // between the SEL / LOOP / CUR groups
constexpr int kTimeTrailPad = 10;  // after the cursor group, matching the lead
constexpr int kMetroSidePad = 10;  // around the BPM cluster
constexpr int kGutterGap = 3;      // between the BPM readouts and their icon gutter
constexpr int kGridLeadPad = 6;    // before the grid cluster
constexpr int kGridGap = 4;        // between the division button and AUTO/SNAP
constexpr int kCpuPad = 10;        // inside the CPU frame, each side
constexpr int kRightItemGap = 4;   // between items in the right cluster
constexpr int kRightEdgePad = 4;   // right inset
constexpr int kAutoWriteGap = 8;   // minimum air left of the automation-write banner

// Geometry that follows the panel height or a glyph relationship rather than
// the spacing density.
constexpr int kButtonMargin = 3;  // vertical inset of the icon-button row
constexpr int kMinButtonSize = 24;
constexpr int kRowGap = 2;             // between the two stacked readout rows
constexpr int kTimeSigOverlap = 4;     // the denominator tucks under the numerator's slash
constexpr int kPunchIconInset = 4;     // punch icons ride the right end of their box
constexpr int kCpuFrameInsetX = 4;     // CPU frame inset, matching the painted rounded rect
constexpr int kCpuFrameInsetY = 3;     //
constexpr int kCpuHeaderPercent = 20;  // share of the CPU frame taken by the title row

// Everything the layout derives from the panel's own size, the measured text
// and the spacing density. Each width appears here once, and both the fit
// decision and the placement read it from here.
struct Metrics {
    int width = 0;
    int height = 0;

    // Vertical
    int buttonSize = 0;
    int buttonY = 0;
    int rowHeight = 0;
    int rowY1 = 0;
    int rowY2 = 0;
    int gutterIcon = 0;  // count-in / metronome icons beside the BPM readouts
    int punchIcon = 0;

    // Density-scaled spacing
    int edgePad = 0;
    int buttonGap = 0;
    int groupPad = 0;
    int timeLeadPad = 0;
    int timeGroupGap = 0;
    int timeTrailPad = 0;
    int metroSidePad = 0;
    int gutterGap = 0;
    int gridLeadPad = 0;
    int gridGap = 0;
    int rightItemGap = 0;
    int rightEdgePad = 0;
    int autoWriteGap = 0;
    int timeBoxSlack = 0;

    // Measured widths
    int timeBox = 0;  // a bars.beats.ticks readout; grown by the flex pass
    int tempoCell = 0;
    int timeSigNum = 0;
    int timeSigDen = 0;
    int metroReadout = 0;
    int metroBox = 0;
    int gridDivision = 0;
    int gridToggle = 0;
    int cpu = 0;
};

int scaled(int basePx, float densityScale) {
    return juce::roundToInt(static_cast<float>(basePx) * densityScale);
}

Metrics metricsFor(int width, int height, const TextWidths& text, float densityScale) {
    Metrics m;
    m.width = width;
    m.height = height;

    m.buttonSize = juce::jmax(kMinButtonSize, height - (kButtonMargin * 2));
    m.buttonY = kButtonMargin;
    m.rowHeight = (m.buttonSize - 4) / 2;
    m.rowY1 = m.buttonY + 1;
    m.rowY2 = m.rowY1 + m.rowHeight + kRowGap;
    m.gutterIcon = juce::jmax(12, m.rowHeight - 3);
    m.punchIcon = (m.rowHeight / 2) + 2;

    m.edgePad = scaled(kEdgePad, densityScale);
    m.buttonGap = scaled(kButtonGap, densityScale);
    m.groupPad = scaled(kGroupPad, densityScale);
    m.timeLeadPad = scaled(kTimeLeadPad, densityScale);
    m.timeGroupGap = scaled(kTimeGroupGap, densityScale);
    m.timeTrailPad = scaled(kTimeTrailPad, densityScale);
    m.metroSidePad = scaled(kMetroSidePad, densityScale);
    m.gutterGap = scaled(kGutterGap, densityScale);
    m.gridLeadPad = scaled(kGridLeadPad, densityScale);
    m.gridGap = scaled(kGridGap, densityScale);
    m.rightItemGap = scaled(kRightItemGap, densityScale);
    m.rightEdgePad = scaled(kRightEdgePad, densityScale);
    m.autoWriteGap = scaled(kAutoWriteGap, densityScale);
    m.timeBoxSlack = scaled(kTimeBoxSlack, densityScale);

    const int cellPad = scaled(kCellPad, densityScale);
    m.timeBox = text.timecodeBox;
    m.tempoCell = text.tempo + (2 * cellPad);
    m.timeSigNum = text.timeSigNumerator + (2 * cellPad);
    m.timeSigDen = text.timeSigDenominator + (2 * cellPad);
    // The readout column holds the BPM on the top row and the time-signature
    // pair on the bottom, so it has to be as wide as the wider of the two.
    m.metroReadout = juce::jmax(m.tempoCell, m.timeSigNum + m.timeSigDen - kTimeSigOverlap);
    m.metroBox = m.metroReadout + m.gutterGap + m.gutterIcon;
    m.gridDivision = text.gridDivision + (2 * cellPad);
    m.gridToggle = text.gridToggle + (2 * cellPad);
    m.cpu = juce::jmax(text.cpuTitle, text.cpuValue) + (2 * scaled(kCpuPad, densityScale));
    return m;
}

// A section places its children with its left edge at x and returns the width
// it consumed. Measuring one is running the same function into a throwaway
// Layout, so the width the fit decision uses and the width the placement
// occupies are the same number by construction.
using Builder = int (*)(Layout&, const Metrics&, int);

int buildNav(Layout& l, const Metrics& m, int x) {
    const int x0 = x;
    l.home = {x, m.buttonY, m.buttonSize, m.buttonSize};
    x += m.buttonSize + m.buttonGap;
    l.prev = {x, m.buttonY, m.buttonSize, m.buttonSize};
    x += m.buttonSize + m.buttonGap;
    l.next = {x, m.buttonY, m.buttonSize, m.buttonSize};
    x += m.buttonSize + m.buttonGap;
    return x - x0;
}

int buildCore(Layout& l, const Metrics& m, int x) {
    const int x0 = x;
    l.play = {x, m.buttonY, m.buttonSize, m.buttonSize};
    x += m.buttonSize + m.buttonGap;
    l.stop = {x, m.buttonY, m.buttonSize, m.buttonSize};
    x += m.buttonSize + m.buttonGap;
    l.record = {x, m.buttonY, m.buttonSize, m.buttonSize};
    x += m.buttonSize + m.buttonGap;
    l.automationWrite = {x, m.buttonY, m.buttonSize, m.buttonSize};
    x += m.buttonSize + m.buttonGap;
    return x - x0;
}

int buildLoopBack(Layout& l, const Metrics& m, int x) {
    const int x0 = x;
    l.loop = {x, m.buttonY, m.buttonSize, m.buttonSize};
    x += m.buttonSize + m.buttonGap;
    l.backToArrangement = {x, m.buttonY, m.buttonSize, m.buttonSize};
    x += m.buttonSize + m.buttonGap;
    return x - x0;
}

int buildPunch(Layout& l, const Metrics& m, int x) {
    l.punchStart = {x, m.rowY1, m.timeBox, m.rowHeight};
    l.punchEnd = {x, m.rowY2, m.timeBox, m.rowHeight};
    const int iconX = x + m.timeBox - m.punchIcon - kPunchIconInset;
    const int iconOffset = (m.rowHeight - m.punchIcon) / 2;
    l.punchIn = {iconX, m.rowY1 + iconOffset, m.punchIcon, m.punchIcon};
    l.punchOut = {iconX, m.rowY2 + iconOffset, m.punchIcon, m.punchIcon};
    return m.timeBox + m.groupPad;
}

// BPM and time signature stacked in a readout column, with a narrow icon
// gutter on their right: count-in beside the BPM, metronome beside the meter.
int buildMetro(Layout& l, const Metrics& m, int x) {
    const int boxX = x + m.metroSidePad;
    const int gutterX = boxX + m.metroReadout + m.gutterGap;

    l.tempo = {boxX, m.rowY1, m.metroReadout, m.rowHeight};

    const int sigWidth = m.timeSigNum + m.timeSigDen - kTimeSigOverlap;
    const int sigX = boxX + ((m.metroReadout - sigWidth) / 2);
    l.timeSigNumerator = {sigX, m.rowY2, m.timeSigNum, m.rowHeight};
    l.timeSigDenominator = {sigX + m.timeSigNum - kTimeSigOverlap, m.rowY2, m.timeSigDen,
                            m.rowHeight};

    const int iconOffset = (m.rowHeight - m.gutterIcon) / 2;
    l.countIn = {gutterX, m.rowY1 + iconOffset, m.gutterIcon, m.gutterIcon};
    l.metronome = {gutterX, m.rowY2 + iconOffset, m.gutterIcon, m.gutterIcon};

    return m.metroBox + (2 * m.metroSidePad);
}

int buildSelLoopTimes(Layout& l, const Metrics& m, int x) {
    const int x0 = x;
    l.selectionStart = {x, m.rowY1, m.timeBox, m.rowHeight};
    l.selectionEnd = {x, m.rowY2, m.timeBox, m.rowHeight};
    x += m.timeBox + m.timeGroupGap;
    l.loopStart = {x, m.rowY1, m.timeBox, m.rowHeight};
    l.loopEnd = {x, m.rowY2, m.timeBox, m.rowHeight};
    x += m.timeBox + m.timeGroupGap;
    return x - x0;
}

int buildCursor(Layout& l, const Metrics& m, int x) {
    l.playhead = {x, m.rowY1, m.timeBox, m.rowHeight};
    l.editCursor = {x, m.rowY2, m.timeBox, m.rowHeight};
    return m.timeBox + m.timeTrailPad;
}

int buildGrid(Layout& l, const Metrics& m, int x) {
    const int divisionX = x + m.gridLeadPad;
    l.gridDivision = {divisionX, m.rowY1, m.gridDivision, (m.rowHeight * 2) + kRowGap};
    const int toggleX = divisionX + m.gridDivision + m.gridGap;
    l.autoGrid = {toggleX, m.rowY1, m.gridToggle, m.rowHeight};
    l.snap = {toggleX, m.rowY2, m.gridToggle, m.rowHeight};
    return m.gridLeadPad + m.gridDivision + m.gridGap + m.gridToggle;
}

int measure(Builder build, const Metrics& m) {
    Layout scratch;
    return build(scratch, m, 0);
}

// The intrinsic width of every section, measured through the placement code.
struct SectionWidths {
    int fixed = 0;  // the sections that are never dropped, plus the panel's own pads
    int nav = 0;
    int loopBack = 0;
    int punch = 0;
    int selLoopTimes = 0;
    int grid = 0;
    int rightCluster = 0;  // CPU meter + QWERTY toggle
    int overflowSlot = 0;  // what stands in for them once they are dropped
};

SectionWidths measureAll(const Metrics& m) {
    SectionWidths w;
    w.fixed = m.edgePad + measure(buildCore, m) + m.groupPad + measure(buildMetro, m) +
              m.timeLeadPad + measure(buildCursor, m);
    w.nav = measure(buildNav, m);
    w.loopBack = measure(buildLoopBack, m);
    w.punch = measure(buildPunch, m);
    w.selLoopTimes = measure(buildSelLoopTimes, m);
    w.grid = measure(buildGrid, m);
    w.rightCluster = m.rightEdgePad + m.cpu + m.rightItemGap + m.buttonSize;
    w.overflowSlot = m.rightEdgePad + m.buttonSize;
    return w;
}

// Drops sections in the declared order until the survivors fit, and returns
// the width they need. That can still exceed `width` on a panel too narrow for
// even the never-dropped sections, which is the caller's problem to clip.
int resolveVisibility(Layout& l, const SectionWidths& w, int width) {
    int required =
        w.fixed + w.nav + w.loopBack + w.punch + w.selLoopTimes + w.grid + w.rightCluster;

    for (auto section : kDropOrder) {
        if (required <= width)
            break;
        switch (section) {
            case Section::RightCluster:
                // The right cluster does not vanish: the overflow button takes
                // its place, so only the difference comes back.
                l.rightClusterVisible = false;
                required -= w.rightCluster - w.overflowSlot;
                break;
            case Section::Grid:
                l.gridVisible = false;
                required -= w.grid;
                break;
            case Section::Punch:
                l.punchVisible = false;
                required -= w.punch;
                break;
            case Section::LoopBack:
                l.loopBackVisible = false;
                required -= w.loopBack;
                break;
            case Section::Nav:
                l.navVisible = false;
                required -= w.nav;
                break;
            case Section::SelLoopTimes:
                l.selLoopTimesVisible = false;
                required -= w.selLoopTimes;
                break;
        }
    }

    l.overflowVisible = !l.rightClusterVisible;
    return required;
}

// Number of timecode readouts on screen, which is how spare width is shared
// out once the surviving set is known.
int visibleTimeBoxCount(const Layout& l) {
    int count = 1;  // the playhead / edit-cursor group is never dropped
    if (l.punchVisible)
        ++count;
    if (l.selLoopTimesVisible)
        count += 2;
    return count;
}

void arrange(Layout& l, const Metrics& m) {
    int x = m.edgePad;
    if (l.navVisible)
        x += buildNav(l, m, x);
    x += buildCore(l, m, x);
    if (l.loopBackVisible)
        x += buildLoopBack(l, m, x);
    x += m.groupPad;
    if (l.punchVisible)
        x += buildPunch(l, m, x);
    l.transportRight = x;

    x += buildMetro(l, m, x);
    l.metroRight = x;

    x += m.timeLeadPad;
    if (l.selLoopTimesVisible)
        x += buildSelLoopTimes(l, m, x);
    x += buildCursor(l, m, x);
    l.timeRight = x;

    int flowRight = x;
    if (l.gridVisible)
        flowRight += buildGrid(l, m, x);

    int right = m.width - m.rightEdgePad;
    if (l.overflowVisible) {
        l.overflow = {right - m.buttonSize, m.buttonY, m.buttonSize, m.buttonSize};
        right -= m.buttonSize + m.rightItemGap;
    } else {
        l.cpu = {right - m.cpu, 0, m.cpu, m.height};
        auto inner = l.cpu.reduced(kCpuFrameInsetX, kCpuFrameInsetY);
        l.cpuTitle = inner.removeFromTop((inner.getHeight() * kCpuHeaderPercent) / 100);
        l.cpuValue = inner;
        right -= m.cpu + m.rightItemGap;

        l.qwerty = {right - m.buttonSize, m.buttonY, m.buttonSize, m.buttonSize};
        right -= m.buttonSize + m.rightItemGap;
    }

    // The automation-write banner fills whatever is left between the flow and
    // the right cluster, and hides when that is nothing.
    const int bannerLeft = flowRight + m.autoWriteGap;
    l.automationWriteLabelFits = right > bannerLeft;
    if (l.automationWriteLabelFits)
        l.automationWriteLabel = {bannerLeft, 0, right - bannerLeft, m.height};
}

}  // namespace

Layout compute(int width, int height, const TextWidths& text, float densityScale) {
    Metrics m = metricsFor(width, height, text, densityScale);
    const SectionWidths widths = measureAll(m);

    Layout l;
    const int required = resolveVisibility(l, widths, width);

    // Spare width goes to the timecode readouts, which read better with air
    // around the digits, up to a cap; past that it belongs to the
    // automation-write banner.
    if (required < width) {
        const int share = (width - required) / visibleTimeBoxCount(l);
        m.timeBox += juce::jlimit(0, m.timeBoxSlack, share);
    }

    arrange(l, m);
    return l;
}

}  // namespace magda::daw::ui::transport
