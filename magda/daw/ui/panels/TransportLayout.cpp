#include "TransportLayout.hpp"

#include <algorithm>
#include <vector>

namespace magda::daw::ui::transport {

bool Layout::isVisible(Section section) const {
    switch (section) {
        case Section::MemoryMeter:
            return memoryMeterVisible;
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
        case Style::Anchored:
            return "anchored";
        case Style::MemoryFill:
            return "memory";
        case Style::Justified:
            return "justified";
    }
    return "anchored";
}

Style styleFromKey(const std::string& key) {
    if (key == "memory")
        return Style::MemoryFill;
    if (key == "justified")
        return Style::Justified;
    return Style::Anchored;
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
constexpr int kChipPad = 4;     // inside a SEL / LOOP / KEEP chip, each side
constexpr int kDividerPad = 5;  // each side of the line before the key
constexpr int kDashWidth = 12;  // between start and end in the Justified stack
constexpr int kCpuPad = 8;      // inside the CPU frame, each side
constexpr int kAutoWriteGap = 8;

// Geometry that follows the panel height or a glyph relationship rather than
// the spacing density.
constexpr int kFrameMargin = 3;        // vertical inset of every group frame
constexpr int kFrameInsetY = 2;        // rows inside a frame, top and bottom
constexpr int kMinButtonSize = 20;     //
constexpr int kMaxButtonSize = 28;     // a taller bar gives the readouts room, not the icons
constexpr int kButtonPercent = 66;     // icon tiles against the frame height
constexpr int kRowGap = 2;             // between stacked readout rows
constexpr int kTimeSigOverlap = 4;     // the denominator tucks under the numerator's slash
constexpr int kKeyGap = 3;             // between the key root and its quality
constexpr int kPunchIconInset = 4;     // punch icons ride the right end of their box
constexpr int kMemoryDot = 8;          //
constexpr int kMemoryMeterMin = 56;    // the meter's width before it takes any slack
constexpr int kCpuHeaderPercent = 30;  // share of the CPU frame taken by the title row

struct Metrics {
    int width = 0;
    int height = 0;

    // Vertical
    int frameY = 0;
    int frameHeight = 0;
    int buttonSize = 0;
    int buttonY = 0;
    int rowHeight = 0;  // two rows to a frame
    int rowY1 = 0;
    int rowY2 = 0;
    int stackRowHeight = 0;  // three rows to a frame
    int icon = 0;            // metronome
    int countInIcon = 0;     // the count-in dot, smaller than the icons beside it
    int punchIcon = 0;

    // Density-scaled spacing
    int edgePad = 0;
    int buttonGap = 0;
    int groupGap = 0;
    int framePad = 0;
    int itemGap = 0;
    int dividerPad = 0;
    int dashWidth = 0;
    int autoWriteGap = 0;

    // Measured widths
    int timeBox = 0;
    int stackBox = 0;
    int punchBox = 0;
    int headlineBox = 0;
    int captionColumn = 0;
    int tempoCell = 0;
    int timeSigNum = 0;
    int timeSigDen = 0;
    int key = 0;
    int keyRoot = 0;
    int chip = 0;
    int memoryCaption = 0;
    int memoryTime = 0;
    int keep = 0;
    int gridDivision = 0;
    int gridToggle = 0;
    int cpu = 0;
    int banner = 0;

    // Slack the MemoryFill style hands the meter.
    int memoryMeterExtra = 0;
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
    m.rowHeight = (m.frameHeight - (2 * kFrameInsetY) - kRowGap) / 2;
    m.rowY1 = m.frameY + kFrameInsetY;
    m.rowY2 = m.rowY1 + m.rowHeight + kRowGap;
    m.stackRowHeight = (m.frameHeight - (2 * kFrameInsetY)) / 3;
    m.icon = juce::jmax(12, m.rowHeight - 3);
    m.countInIcon = juce::jmax(8, (m.icon * 2) / 3);
    m.punchIcon = (m.rowHeight / 2) + 2;

    m.edgePad = scaled(kEdgePad, densityScale);
    m.buttonGap = scaled(kButtonGap, densityScale);
    m.groupGap = scaled(kGroupGap, densityScale);
    m.framePad = scaled(kFramePad, densityScale);
    m.itemGap = scaled(kItemGap, densityScale);
    m.dividerPad = scaled(kDividerPad, densityScale);
    m.dashWidth = scaled(kDashWidth, densityScale);
    m.autoWriteGap = scaled(kAutoWriteGap, densityScale);

    const int cellPad = scaled(kCellPad, densityScale);
    const int chipPad = scaled(kChipPad, densityScale);

    // A readout grows by what a decoration over its right end needs beyond
    // the inset its glyphs keep anyway.
    const auto grownBy = [&](int inset) { return juce::jmax(0, inset - text.timecodeGlyphInset); };
    m.timeBox = text.timecodeBox + text.timecodeOverlay;
    m.stackBox = text.stackTimecodeBox;
    m.punchBox = m.timeBox + grownBy(m.punchIcon + kPunchIconInset);
    m.headlineBox = text.headlineTimecodeBox + grownBy(text.timecodeCaption);
    m.captionColumn = text.timecodeCaption;

    m.tempoCell = text.tempo + (2 * cellPad);
    m.timeSigNum = text.timeSigNumerator + (2 * cellPad);
    m.timeSigDen = text.timeSigDenominator + (2 * cellPad);
    m.keyRoot = text.keyRoot;
    m.key = text.keyRoot + kKeyGap + text.keyQuality + (2 * cellPad);
    m.chip = text.rangeChip + (2 * chipPad);
    m.memoryCaption = text.memoryCaption;
    m.memoryTime = text.memoryTime;
    m.keep = text.keep + (2 * chipPad);
    m.gridDivision = text.gridDivision + (2 * cellPad);
    m.gridToggle = text.gridToggle + (2 * cellPad);
    m.cpu = juce::jmax(text.cpuTitle, text.cpuValue) + (2 * scaled(kCpuPad, densityScale));
    m.banner = text.banner;
    return m;
}

juce::Rectangle<int> frameAt(const Metrics& m, int x, int width) {
    return {x, m.frameY, width, m.frameHeight};
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
    Range,
    Stack,
    Grid,
    Memory,
    Qwerty,
    Cpu,
    Overflow,
};

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

int buildPunch(Layout& l, const Metrics& m, int x) {
    const int width = m.framePad + m.punchBox + m.framePad;
    l.punchFrame = frameAt(m, x, width);
    const int boxX = x + m.framePad;
    l.punchStart = {boxX, m.rowY1, m.punchBox, m.rowHeight};
    l.punchEnd = {boxX, m.rowY2, m.punchBox, m.rowHeight};
    const int iconX = boxX + m.punchBox - m.punchIcon - kPunchIconInset;
    const int iconOffset = (m.rowHeight - m.punchIcon) / 2;
    l.punchIn = {iconX, m.rowY1 + iconOffset, m.punchIcon, m.punchIcon};
    l.punchOut = {iconX, m.rowY2 + iconOffset, m.punchIcon, m.punchIcon};
    return width;
}

// BPM, count-in, meter and metronome on one line, then the key past a divider.
int buildTempo(Layout& l, const Metrics& m, int x) {
    const int x0 = x;
    const int iconY = m.frameY + ((m.frameHeight - m.icon) / 2);
    x += m.framePad;

    l.tempo = {x, m.frameY, m.tempoCell, m.frameHeight};
    x += m.tempoCell + m.itemGap;
    l.countIn = {x, m.frameY + ((m.frameHeight - m.countInIcon) / 2), m.countInIcon, m.countInIcon};
    x += m.countInIcon + m.itemGap;
    l.timeSigNumerator = {x, m.frameY, m.timeSigNum, m.frameHeight};
    x += m.timeSigNum - kTimeSigOverlap;
    l.timeSigDenominator = {x, m.frameY, m.timeSigDen, m.frameHeight};
    x += m.timeSigDen + m.itemGap;
    l.metronome = {x, iconY, m.icon, m.icon};
    x += m.icon + m.dividerPad;
    l.keyDividerX = x;
    x += 1 + m.dividerPad;
    l.key = {x, m.frameY, m.key, m.frameHeight};
    x += m.key + m.framePad;

    l.tempoFrame = frameAt(m, x0, x - x0);
    return x - x0;
}

// The playhead alone, at headline size, with CUR over its right end.
int buildCursor(Layout& l, const Metrics& m, int x) {
    const int width = m.framePad + m.headlineBox + m.framePad;
    l.cursorFrame = frameAt(m, x, width);
    l.playhead = {x + m.framePad, m.rowY1, m.headlineBox, m.frameHeight - (2 * kFrameInsetY)};
    return width;
}

// SEL / LOOP chips stacked on the left, the start and end of whichever is
// chosen on the right. Both pairs of readouts take the same rows.
int buildRange(Layout& l, const Metrics& m, int x) {
    const int width = m.framePad + m.chip + m.itemGap + m.timeBox + m.framePad;
    l.rangeFrame = frameAt(m, x, width);
    const int chipX = x + m.framePad;
    l.selChip = {chipX, m.rowY1, m.chip, m.rowHeight};
    l.loopChip = {chipX, m.rowY2, m.chip, m.rowHeight};
    const int boxX = chipX + m.chip + m.itemGap;
    l.selectionStart = l.loopStart = {boxX, m.rowY1, m.timeBox, m.rowHeight};
    l.selectionEnd = l.loopEnd = {boxX, m.rowY2, m.timeBox, m.rowHeight};
    return width;
}

// SEL, LOOP and CUR as three lines of caption, start and end.
int buildStack(Layout& l, const Metrics& m, int x) {
    const int width = m.framePad + m.captionColumn + m.itemGap + m.stackBox + m.dashWidth +
                      m.stackBox + m.framePad;
    l.stackFrame = frameAt(m, x, width);

    const int captionX = x + m.framePad;
    const int startX = captionX + m.captionColumn + m.itemGap;
    const int endX = startX + m.stackBox + m.dashWidth;
    const auto row = [&](int index, juce::Rectangle<int>& caption, juce::Rectangle<int>& start,
                         juce::Rectangle<int>& end) {
        const int y = m.rowY1 + (index * m.stackRowHeight);
        caption = {captionX, y, m.captionColumn, m.stackRowHeight};
        start = {startX, y, m.stackBox, m.stackRowHeight};
        end = {endX, y, m.stackBox, m.stackRowHeight};
    };
    row(0, l.selCaption, l.selectionStart, l.selectionEnd);
    row(1, l.loopCaption, l.loopStart, l.loopEnd);
    row(2, l.cursorCaption, l.playhead, l.editCursor);
    return width;
}

int buildGrid(Layout& l, const Metrics& m, int x) {
    const int width = m.framePad + m.gridDivision + m.itemGap + m.gridToggle + m.framePad;
    l.gridFrame = frameAt(m, x, width);
    const int divisionX = x + m.framePad;
    l.gridDivision = {divisionX, m.rowY1, m.gridDivision, (m.rowHeight * 2) + kRowGap};
    const int toggleX = divisionX + m.gridDivision + m.itemGap;
    l.autoGrid = {toggleX, m.rowY1, m.gridToggle, m.rowHeight};
    l.snap = {toggleX, m.rowY2, m.gridToggle, m.rowHeight};
    return width;
}

// The master buffer: a dot, then (while the meter is on) its caption, level
// history and length, then KEEP.
int buildMemory(Layout& l, const Metrics& m, int x) {
    const int x0 = x;
    const int centreY = m.frameY + (m.frameHeight / 2);
    x += m.framePad;
    l.memoryDot = {x, centreY - (kMemoryDot / 2), kMemoryDot, kMemoryDot};
    x += kMemoryDot + m.itemGap;
    if (l.memoryMeterVisible) {
        l.memoryCaption = {x, m.frameY, m.memoryCaption, m.frameHeight};
        x += m.memoryCaption + m.itemGap;
        const int meterWidth = kMemoryMeterMin + m.memoryMeterExtra;
        l.memoryMeter = {x, m.rowY1, meterWidth, m.frameHeight - (2 * kFrameInsetY)};
        x += meterWidth + m.itemGap;
        l.memoryTime = {x, m.frameY, m.memoryTime, m.frameHeight};
        x += m.memoryTime + m.itemGap;
    }
    l.keep = {x, centreY - (m.rowHeight / 2), m.keep, m.rowHeight};
    x += m.keep + m.framePad;
    l.memoryFrame = frameAt(m, x0, x - x0);
    return x - x0;
}

int buildQwerty(Layout& l, const Metrics& m, int x) {
    l.qwerty = {x, m.buttonY, m.buttonSize, m.buttonSize};
    return m.buttonSize;
}

int buildCpu(Layout& l, const Metrics& m, int x) {
    l.cpu = frameAt(m, x, m.cpu);
    auto inner = l.cpu.reduced(2, kFrameInsetY);
    l.cpuTitle = inner.removeFromTop((inner.getHeight() * kCpuHeaderPercent) / 100);
    l.cpuValue = inner;
    return m.cpu;
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
        case Group::Range:
            return buildRange(l, m, x);
        case Group::Stack:
            return buildStack(l, m, x);
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
    flags.selLoopTimesVisible = l.selLoopTimesVisible;
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

// The surviving groups in three runs: what anchors to the left, what the
// styles move about in the middle, and what anchors to the right.
struct Runs {
    std::vector<Group> left, centre, right;
};

Runs runsFor(const Layout& l) {
    Runs runs;
    runs.left.push_back(Group::Buttons);
    if (l.punchVisible)
        runs.left.push_back(Group::Punch);

    runs.centre.push_back(Group::Tempo);
    if (l.style == Style::Justified && l.selLoopTimesVisible) {
        runs.centre.push_back(Group::Stack);
    } else {
        runs.centre.push_back(Group::Cursor);
        if (l.selLoopTimesVisible)
            runs.centre.push_back(Group::Range);
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

// Places a run left to right from x with `gap` between groups, recording each
// group's span so the banner can find the widest gap afterwards.
int placeRun(const std::vector<Group>& run, Layout& l, const Metrics& m, int x, int gap,
             std::vector<juce::Range<int>>& spans) {
    for (size_t i = 0; i < run.size(); ++i) {
        const int width = build(run[i], l, m, x);
        spans.emplace_back(x, x + width);
        x += width;
        if (i + 1 < run.size())
            x += gap;
    }
    return x;
}

// The automation-write banner takes the widest gap between groups, and hides
// when no gap holds it.
void placeBanner(Layout& l, const Metrics& m, std::vector<juce::Range<int>> spans) {
    std::sort(spans.begin(), spans.end(),
              [](auto a, auto b) { return a.getStart() < b.getStart(); });
    juce::Range<int> widest;
    for (size_t i = 1; i < spans.size(); ++i) {
        const juce::Range<int> gap(spans[i - 1].getEnd(), spans[i].getStart());
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

void arrange(Layout& l, Metrics& m) {
    const auto runs = runsFor(l);
    const int slack = juce::jmax(0, m.width - requiredWidth(l, m));
    std::vector<juce::Range<int>> spans;

    switch (l.style) {
        case Style::Anchored: {
            placeRun(runs.left, l, m, m.edgePad, m.groupGap, spans);
            const int rightWidth = packedWidth(runs.right, l, m);
            placeRun(runs.right, l, m, m.width - m.edgePad - rightWidth, m.groupGap, spans);
            const int centreX = m.edgePad + packedWidth(runs.left, l, m) + m.groupGap + (slack / 2);
            placeRun(runs.centre, l, m, centreX, m.groupGap, spans);
            break;
        }
        case Style::MemoryFill: {
            // The meter takes the slack while it is showing; otherwise the
            // slack sits before the right-hand cluster.
            if (l.rightClusterVisible && l.memoryMeterVisible)
                m.memoryMeterExtra = slack;
            std::vector<Group> flow = runs.left;
            flow.insert(flow.end(), runs.centre.begin(), runs.centre.end());
            int x = placeRun(flow, l, m, m.edgePad, m.groupGap, spans);
            x += m.groupGap + (m.memoryMeterExtra > 0 ? 0 : slack);
            placeRun(runs.right, l, m, x, m.groupGap, spans);
            break;
        }
        case Style::Justified: {
            // Every group spaced evenly, the qwerty toggle and CPU riding as one.
            std::vector<Group> all = runs.left;
            all.insert(all.end(), runs.centre.begin(), runs.centre.end());
            all.insert(all.end(), runs.right.begin(), runs.right.end());
            if (l.rightClusterVisible)
                all.pop_back();
            const int gaps = juce::jmax(1, static_cast<int>(all.size()) - 1);
            int x = m.edgePad;
            for (size_t i = 0; i < all.size(); ++i) {
                const int width = build(all[i], l, m, x);
                spans.emplace_back(x, x + width);
                x += width + m.groupGap + (slack * static_cast<int>(i + 1) / gaps) -
                     (slack * static_cast<int>(i) / gaps);
            }
            if (l.rightClusterVisible) {
                const int cpuX = m.width - m.edgePad - m.cpu;
                build(Group::Cpu, l, m, cpuX);
                spans.back().setEnd(cpuX + m.cpu);
            }
            break;
        }
    }

    placeBanner(l, m, spans);
}

}  // namespace

Layout compute(int width, int height, const TextWidths& text, float densityScale, Style style) {
    Metrics m = metricsFor(width, height, text, densityScale);

    Layout l;
    l.style = style;
    for (auto section : kDropOrder) {
        if (requiredWidth(l, m) <= width)
            break;
        drop(l, section);
    }

    l.punchTrailingInset = m.punchIcon + kPunchIconInset;
    l.cursorTrailingInset = text.timecodeCaption;
    arrange(l, m);
    return l;
}

}  // namespace magda::daw::ui::transport
