// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include "TransportLayout.hpp"

#include <vector>

namespace magda::daw::ui::transport {

bool Layout::isVisible(Section section) const {
    switch (section) {
        case Section::MemoryMeter:
            return memoryMeterVisible;
        case Section::Clock:
            return clockVisible;
        case Section::LinkLettering:
            return linkLetteringVisible;
        case Section::RangeEnd:
            return rangeEndVisible;
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

const char* styleKey(Style style) {
    switch (style) {
        case Style::Boxed:
            return "boxed";
        case Style::Strip:
            return "strip";
    }
    return "boxed";
}

Style styleFromKey(const std::string& key) {
    if (key == "strip")
        return Style::Strip;
    return Style::Boxed;
}

namespace {

// Spacing tokens at normal density. These scale with the user's spacing
// density; the widget sizes they sit between (icon buttons, readout cells) do
// not, so the controls keep their hit targets at every density.
constexpr int kEdgePad = 5;     // outer inset, each side
constexpr int kButtonGap = 2;   // between icon button tiles
constexpr int kGroupGap = 5;    // between groups when packed
constexpr int kFramePad = 4;    // inside a group frame, each side
constexpr int kItemGap = 3;     // between items inside a frame
constexpr int kCellPad = 3;     // inside a text readout, each side
constexpr int kJoinWidth = 14;  // the dash or arrow between a start and an end
constexpr int kCpuPad = 6;      // inside the CPU frame, each side
constexpr int kAutoWriteGap = 8;

// Geometry that follows the panel height or a glyph relationship rather than
// the spacing density.
constexpr int kFrameMargin = 4;      // vertical inset of every group frame
constexpr int kFrameInsetY = 2;      // the row inside a frame, top and bottom
constexpr int kChipInsetY = 4;       // chips and toggles, top and bottom of the frame
constexpr int kMinButtonSize = 20;   //
constexpr int kMaxButtonSize = 28;   // a taller bar gives the readouts room, not the icons
constexpr int kButtonPercent = 88;   // icon tiles against the frame height
constexpr int kTimeSigOverlap = 4;   // the denominator tucks under the numerator's slash
constexpr int kMemoryDot = 8;        //
constexpr int kMemoryMeterMin = 56;  //

struct Metrics {
    int width = 0;
    int height = 0;

    // Vertical
    int frameY = 0;
    int frameHeight = 0;
    int buttonSize = 0;
    int buttonY = 0;
    int rowY = 0;
    int rowHeight = 0;
    int chipY = 0;
    int chipHeight = 0;
    int icon = 0;         // metronome
    int countInIcon = 0;  // the count-in dot, smaller than the icons beside it
    int punchIcon = 0;

    // Density-scaled spacing
    int edgePad = 0;
    int buttonGap = 0;
    int groupGap = 0;
    int framePad = 0;
    int itemGap = 0;
    int joinWidth = 0;
    int cpuPad = 0;
    int autoWriteGap = 0;

    // Measured widths
    int timeBox = 0;
    int headlineBox = 0;
    int clock = 0;
    int tempoCell = 0;
    int timeSigNum = 0;
    int timeSigDen = 0;
    int rangeCaption = 0;
    int memoryCaption = 0;
    int memoryTime = 0;
    int gridDivision = 0;
    int gridToggle = 0;
    int cpuTitle = 0;
    int cpuValue = 0;
    int banner = 0;
};

int scaled(int basePx, float densityScale) {
    return juce::roundToInt(static_cast<float>(basePx) * densityScale);
}

Metrics metricsFor(int width, int height, const TextWidths& text, float densityScale) {
    Metrics m;
    m.width = width;
    m.height = height;

    m.frameY = kFrameMargin;
    m.frameHeight = juce::jmax(kMinButtonSize, height - (kFrameMargin * 2));
    m.buttonSize =
        juce::jlimit(kMinButtonSize, kMaxButtonSize, (m.frameHeight * kButtonPercent) / 100);
    m.buttonY = m.frameY + ((m.frameHeight - m.buttonSize) / 2);
    m.rowY = m.frameY + kFrameInsetY;
    m.rowHeight = m.frameHeight - (2 * kFrameInsetY);
    m.chipY = m.frameY + kChipInsetY;
    m.chipHeight = m.frameHeight - (2 * kChipInsetY);
    m.icon = juce::jmax(12, m.rowHeight - 8);
    m.countInIcon = juce::jmax(8, (m.icon * 2) / 3);
    m.punchIcon = juce::jmax(10, m.rowHeight - 10);

    m.edgePad = scaled(kEdgePad, densityScale);
    m.buttonGap = scaled(kButtonGap, densityScale);
    m.groupGap = scaled(kGroupGap, densityScale);
    m.framePad = scaled(kFramePad, densityScale);
    m.itemGap = scaled(kItemGap, densityScale);
    m.joinWidth = scaled(kJoinWidth, densityScale);
    m.cpuPad = scaled(kCpuPad, densityScale);
    m.autoWriteGap = scaled(kAutoWriteGap, densityScale);

    const int cellPad = scaled(kCellPad, densityScale);

    m.timeBox = text.timecodeBox;
    m.headlineBox = text.headlineTimecodeBox;
    m.clock = text.clock;
    m.tempoCell = text.tempo + (2 * cellPad);
    m.timeSigNum = text.timeSigNumerator + (2 * cellPad);
    m.timeSigDen = text.timeSigDenominator + (2 * cellPad);
    m.rangeCaption = text.rangeCaption;
    m.memoryCaption = text.memoryCaption;
    m.memoryTime = text.memoryTime;
    m.gridDivision = text.gridDivision + (2 * cellPad);
    m.gridToggle = text.gridToggle + (2 * cellPad);
    m.cpuTitle = text.cpuTitle;
    m.cpuValue = text.cpuValue;
    m.banner = text.banner;
    return m;
}

juce::Rectangle<int> frameAt(const Metrics& m, int x, int width) {
    return {x, m.frameY, width, m.frameHeight};
}

juce::Rectangle<int> centredSquare(const Metrics& m, int x, int size) {
    return {x, m.frameY + ((m.frameHeight - size) / 2), size, size};
}

// A group places its children with its left edge at x and returns the width
// it consumed. Measuring one is running the same function into a scratch
// Layout, so the width the fit decision uses and the width the placement
// occupies are the same number by construction.
enum class Group : std::uint8_t {
    Buttons,
    Punch,
    Tempo,
    Cursor,
    Selection,
    Loop,
    Grid,
    Memory,
    Qwerty,
    Cpu,
    Overflow,
};

// Groups that are a bare tile; the Strip style draws no hairline beside them.
bool isTile(Group group) {
    return group == Group::Buttons || group == Group::Qwerty || group == Group::Overflow;
}

int buildButtons(Layout& l, const Metrics& m, int x) {
    const int x0 = x;
    const auto place = [&](juce::Rectangle<int>& r) {
        r = {x, m.buttonY, m.buttonSize, m.buttonSize};
        x += m.buttonSize + m.buttonGap;
    };
    if (l.navVisible) {
        place(l.home);
        place(l.prev);
        place(l.next);
    }
    place(l.play);
    place(l.stop);
    place(l.record);
    place(l.automationWrite);
    if (l.loopBackVisible) {
        place(l.loop);
        place(l.backToArrangement);
    }
    return x - x0 - m.buttonGap;
}

// The punch-in and punch-out toggles.
int buildPunch(Layout& l, const Metrics& m, int x) {
    const int x0 = x;
    x += m.framePad;
    l.punchIn = centredSquare(m, x, m.punchIcon);
    x += m.punchIcon + m.itemGap;
    l.punchOut = centredSquare(m, x, m.punchIcon);
    x += m.punchIcon + m.framePad;
    l.punchFrame = frameAt(m, x0, x - x0);
    return x - x0;
}

// The Link chip, then BPM, count-in, meter and metronome.
int buildTempo(Layout& l, const Metrics& m, int x) {
    const int x0 = x;
    x += m.framePad;

    const int linkWidth = l.linkLetteringVisible ? link_chip::widthForHeight(m.chipHeight)
                                                 : link_chip::ledOnlyWidthForHeight(m.chipHeight);
    l.link = {x, m.chipY, linkWidth, m.chipHeight};
    x += linkWidth + m.itemGap;
    l.tempo = {x, m.frameY, m.tempoCell, m.frameHeight};
    x += m.tempoCell + m.itemGap;
    l.countIn = centredSquare(m, x, m.countInIcon);
    x += m.countInIcon + m.itemGap;
    l.timeSigNumerator = {x, m.frameY, m.timeSigNum, m.frameHeight};
    x += m.timeSigNum - kTimeSigOverlap;
    l.timeSigDenominator = {x, m.frameY, m.timeSigDen, m.frameHeight};
    x += m.timeSigDen + m.itemGap;
    l.metronome = centredSquare(m, x, m.icon);
    x += m.icon + m.framePad;

    l.tempoFrame = frameAt(m, x0, x - x0);
    return x - x0;
}

// The playhead at headline size, its seconds clock beside it.
int buildCursor(Layout& l, const Metrics& m, int x) {
    const int x0 = x;
    x += m.framePad;
    l.playhead = {x, m.rowY, m.headlineBox, m.rowHeight};
    x += m.headlineBox;
    if (l.clockVisible) {
        x += m.itemGap;
        l.clock = {x, m.rowY, m.clock, m.rowHeight};
        x += m.clock;
    }
    x += m.framePad;
    l.cursorFrame = frameAt(m, x0, x - x0);
    return x - x0;
}

// A caption, then the start and end of the range.
int buildRange(const Metrics& m, int x, bool endVisible, juce::Rectangle<int>& frame,
               juce::Rectangle<int>& caption, juce::Rectangle<int>& start,
               juce::Rectangle<int>& end) {
    const int x0 = x;
    x += m.framePad;
    caption = {x, m.rowY, m.rangeCaption, m.rowHeight};
    x += m.rangeCaption + m.itemGap;
    start = {x, m.rowY, m.timeBox, m.rowHeight};
    x += m.timeBox;
    if (endVisible) {
        x += m.joinWidth;
        end = {x, m.rowY, m.timeBox, m.rowHeight};
        x += m.timeBox;
    }
    x += m.framePad;
    frame = frameAt(m, x0, x - x0);
    return x - x0;
}

int buildSelection(Layout& l, const Metrics& m, int x) {
    return buildRange(m, x, l.rangeEndVisible, l.selectionFrame, l.selectionCaption,
                      l.selectionStart, l.selectionEnd);
}

int buildLoop(Layout& l, const Metrics& m, int x) {
    return buildRange(m, x, l.rangeEndVisible, l.loopFrame, l.loopCaption, l.loopStart, l.loopEnd);
}

int buildGrid(Layout& l, const Metrics& m, int x) {
    const int x0 = x;
    x += m.framePad;
    l.gridDivision = {x, m.chipY, m.gridDivision, m.chipHeight};
    x += m.gridDivision + m.itemGap;
    l.autoGrid = {x, m.chipY, m.gridToggle, m.chipHeight};
    x += m.gridToggle + m.itemGap;
    l.snap = {x, m.chipY, m.gridToggle, m.chipHeight};
    x += m.gridToggle + m.framePad;
    l.gridFrame = frameAt(m, x0, x - x0);
    return x - x0;
}

// The master buffer: a dot, then (while the meter is on) its caption, level
// history and length, then the capture button.
int buildMemory(Layout& l, const Metrics& m, int x) {
    const int x0 = x;
    x += m.framePad;
    l.memoryDot = centredSquare(m, x, kMemoryDot);
    x += kMemoryDot + m.itemGap;
    if (l.memoryMeterVisible) {
        l.memoryCaption = {x, m.frameY, m.memoryCaption, m.frameHeight};
        x += m.memoryCaption + m.itemGap;
        l.memoryMeter = {x, m.rowY, kMemoryMeterMin, m.rowHeight};
        x += kMemoryMeterMin + m.itemGap;
        l.memoryTime = {x, m.frameY, m.memoryTime, m.frameHeight};
        x += m.memoryTime + m.itemGap;
    }
    l.capture = centredSquare(m, x, m.chipHeight);
    x += m.chipHeight + m.framePad;
    l.memoryFrame = frameAt(m, x0, x - x0);
    return x - x0;
}

int buildQwerty(Layout& l, const Metrics& m, int x) {
    l.qwerty = {x, m.buttonY, m.buttonSize, m.buttonSize};
    return m.buttonSize;
}

// The caption and the load side by side.
int buildCpu(Layout& l, const Metrics& m, int x) {
    const int width = m.cpuPad + m.cpuTitle + m.itemGap + m.cpuValue + m.cpuPad;
    l.cpu = frameAt(m, x, width);
    l.cpuTitle = {x + m.cpuPad, m.rowY, m.cpuTitle, m.rowHeight};
    l.cpuValue = {l.cpuTitle.getRight() + m.itemGap, m.rowY, m.cpuValue, m.rowHeight};
    return width;
}

int buildOverflow(Layout& l, const Metrics& m, int x) {
    l.overflow = {x, m.buttonY, m.buttonSize, m.buttonSize};
    return m.buttonSize;
}

int build(Group group, Layout& l, const Metrics& m, int x) {
    switch (group) {
        case Group::Buttons:
            return buildButtons(l, m, x);
        case Group::Punch:
            return buildPunch(l, m, x);
        case Group::Tempo:
            return buildTempo(l, m, x);
        case Group::Cursor:
            return buildCursor(l, m, x);
        case Group::Selection:
            return buildSelection(l, m, x);
        case Group::Loop:
            return buildLoop(l, m, x);
        case Group::Grid:
            return buildGrid(l, m, x);
        case Group::Memory:
            return buildMemory(l, m, x);
        case Group::Qwerty:
            return buildQwerty(l, m, x);
        case Group::Cpu:
            return buildCpu(l, m, x);
        case Group::Overflow:
            return buildOverflow(l, m, x);
    }
    return 0;
}

// A scratch copy carries the visibility flags the builders read, and nothing
// else, so measuring cannot leak a rectangle into the real layout.
Layout flagsOf(const Layout& l) {
    Layout flags;
    flags.style = l.style;
    flags.navVisible = l.navVisible;
    flags.loopBackVisible = l.loopBackVisible;
    flags.punchVisible = l.punchVisible;
    flags.linkLetteringVisible = l.linkLetteringVisible;
    flags.clockVisible = l.clockVisible;
    flags.selLoopTimesVisible = l.selLoopTimesVisible;
    flags.rangeEndVisible = l.rangeEndVisible;
    flags.gridVisible = l.gridVisible;
    flags.memoryMeterVisible = l.memoryMeterVisible;
    flags.rightClusterVisible = l.rightClusterVisible;
    flags.overflowVisible = l.overflowVisible;
    return flags;
}

int measure(Group group, const Layout& l, const Metrics& m) {
    Layout scratch = flagsOf(l);
    return build(group, scratch, m, 0);
}

// The surviving groups in three runs: what anchors to the left, the time and
// tempo groups centred in the slack, and what anchors to the right.
struct Runs {
    std::vector<Group> left, centre, right;
};

Runs runsFor(const Layout& l) {
    Runs runs;
    runs.left.push_back(Group::Buttons);
    if (l.punchVisible)
        runs.left.push_back(Group::Punch);

    runs.centre.push_back(Group::Tempo);
    runs.centre.push_back(Group::Cursor);
    if (l.selLoopTimesVisible) {
        runs.centre.push_back(Group::Selection);
        runs.centre.push_back(Group::Loop);
    }

    if (l.gridVisible)
        runs.right.push_back(Group::Grid);
    if (l.rightClusterVisible) {
        runs.right.push_back(Group::Memory);
        runs.right.push_back(Group::Qwerty);
        runs.right.push_back(Group::Cpu);
    } else {
        runs.right.push_back(Group::Overflow);
    }
    return runs;
}

int packedWidth(const std::vector<Group>& run, const Layout& l, const Metrics& m) {
    int width = 0;
    for (auto group : run)
        width += measure(group, l, m);
    return width + (m.groupGap * juce::jmax(0, static_cast<int>(run.size()) - 1));
}

int requiredWidth(const Layout& l, const Metrics& m) {
    const auto runs = runsFor(l);
    return (2 * m.edgePad) + packedWidth(runs.left, l, m) + packedWidth(runs.centre, l, m) +
           packedWidth(runs.right, l, m) + (2 * m.groupGap);
}

void drop(Layout& l, Section section) {
    switch (section) {
        case Section::MemoryMeter:
            l.memoryMeterVisible = false;
            break;
        case Section::Clock:
            l.clockVisible = false;
            break;
        case Section::LinkLettering:
            l.linkLetteringVisible = false;
            break;
        case Section::RangeEnd:
            l.rangeEndVisible = false;
            break;
        case Section::RightCluster:
            l.rightClusterVisible = false;
            l.overflowVisible = true;
            break;
        case Section::Grid:
            l.gridVisible = false;
            break;
        case Section::Punch:
            l.punchVisible = false;
            break;
        case Section::LoopBack:
            l.loopBackVisible = false;
            break;
        case Section::Nav:
            l.navVisible = false;
            break;
        case Section::SelLoopTimes:
            l.selLoopTimesVisible = false;
            break;
    }
}

struct Placed {
    Group group;
    juce::Range<int> span;
};

int placeRun(const std::vector<Group>& run, Layout& l, const Metrics& m, int x,
             std::vector<Placed>& placed) {
    for (size_t i = 0; i < run.size(); ++i) {
        const int width = build(run[i], l, m, x);
        placed.push_back({run[i], {x, x + width}});
        x += width;
        if (i + 1 < run.size())
            x += m.groupGap;
    }
    return x;
}

// A hairline just before each group, half a packed gap out, unless a tile
// stands on either side of it.
void placeDividers(Layout& l, const Metrics& m, const std::vector<Placed>& placed) {
    for (size_t i = 1; i < placed.size(); ++i)
        if (!isTile(placed[i - 1].group) && !isTile(placed[i].group))
            l.dividerXs.push_back(placed[i].span.getStart() - ((m.groupGap + 1) / 2));
}

// The automation-write banner takes the widest gap between groups, and hides
// when no gap holds it.
void placeBanner(Layout& l, const Metrics& m, const std::vector<Placed>& placed) {
    juce::Range<int> widest;
    for (size_t i = 1; i < placed.size(); ++i) {
        const juce::Range<int> gap(placed[i - 1].span.getEnd(), placed[i].span.getStart());
        if (gap.getLength() > widest.getLength())
            widest = gap;
    }
    const juce::Range<int> inner(
        widest.getStart() + m.autoWriteGap,
        juce::jmax(widest.getStart() + m.autoWriteGap, widest.getEnd() - m.autoWriteGap));
    l.automationWriteLabelFits = inner.getLength() >= m.banner;
    if (l.automationWriteLabelFits)
        l.automationWriteLabel = {inner.getStart(), 0, inner.getLength(), m.height};
}

void arrange(Layout& l, const Metrics& m) {
    const auto runs = runsFor(l);
    const int slack = juce::jmax(0, m.width - requiredWidth(l, m));
    std::vector<Placed> placed;

    placeRun(runs.left, l, m, m.edgePad, placed);
    const int centreX = m.edgePad + packedWidth(runs.left, l, m) + m.groupGap + (slack / 2);
    placeRun(runs.centre, l, m, centreX, placed);
    const int rightWidth = packedWidth(runs.right, l, m);
    placeRun(runs.right, l, m, m.width - m.edgePad - rightWidth, placed);

    placeDividers(l, m, placed);
    placeBanner(l, m, placed);
}

}  // namespace

Layout compute(int width, int height, const TextWidths& text, float densityScale, Style style) {
    const Metrics m = metricsFor(width, height, text, densityScale);

    Layout l;
    l.style = style;
    for (auto section : kDropOrder) {
        if (requiredWidth(l, m) <= width)
            break;
        drop(l, section);
    }
    arrange(l, m);
    return l;
}

}  // namespace magda::daw::ui::transport
