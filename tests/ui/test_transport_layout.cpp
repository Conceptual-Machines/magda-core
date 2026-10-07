// Transport bar layout (#2071 / #2074). The decision about what fits is pure
// arithmetic over (width, height, measured text, spacing density, style), so it
// can be asserted without a window. The companion test_transport_layout_juce.cpp
// runs the same assertions against the widths the shipped fonts actually produce.

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <cstdlib>
#include <vector>

#include "magda/daw/ui/layout/LayoutConfig.hpp"
#include "magda/daw/ui/panels/TransportLayout.hpp"

using magda::LayoutConfig;
using namespace magda::daw::ui::transport;

namespace {

// Roughly what the shipped Inter measures at the default font scale. The tests
// that matter sweep around these rather than leaning on the exact numbers.
TextWidths nominalText() {
    TextWidths text;
    text.timecodeBox = 104;
    text.timecodeOverlay = 11;
    text.stackTimecodeBox = 91;
    text.headlineTimecodeBox = 141;
    text.timecodeCaption = 20;
    text.timecodeGlyphInset = 4;
    text.tempo = 49;
    text.timeSigNumerator = 20;
    text.timeSigDenominator = 16;
    text.keyRoot = 19;
    text.keyQuality = 17;
    text.rangeChip = 26;
    text.memoryCaption = 19;
    text.memoryTime = 65;
    text.keep = 25;
    text.cpuTitle = 17;
    text.cpuValue = 40;
    text.gridDivision = 18;
    text.gridToggle = 26;
    text.banner = 106;
    return text;
}

constexpr std::array<Style, 3> kStyles{Style::Anchored, Style::MemoryFill, Style::Justified};
constexpr int kWide = 1600;

int defaultTransportHeight() {
    return LayoutConfig::getInstance().transportHeight;
}

// Everything on except, at most, the master buffer meter.
bool nothingButTheMeterCollapsed(const Layout& l) {
    return l.navVisible && l.loopBackVisible && l.punchVisible && l.selLoopTimesVisible &&
           l.gridVisible && l.rightClusterVisible && !l.overflowVisible;
}

// The horizontal extent of every group on screen, in left-to-right order.
std::vector<juce::Rectangle<int>> groupsOf(const Layout& l) {
    juce::Rectangle<int> buttons = l.play;
    for (const auto& r :
         {l.home, l.prev, l.next, l.stop, l.record, l.automationWrite, l.loop, l.backToArrangement})
        if (!r.isEmpty())
            buttons = buttons.getUnion(r);

    std::vector<juce::Rectangle<int>> groups{buttons};
    for (const auto& r : {l.punchFrame, l.tempoFrame, l.cursorFrame, l.rangeFrame, l.stackFrame,
                          l.gridFrame, l.memoryFrame, l.qwerty, l.cpu, l.overflow})
        if (!r.isEmpty())
            groups.push_back(r);
    std::sort(groups.begin(), groups.end(),
              [](const auto& a, const auto& b) { return a.getX() < b.getX(); });
    return groups;
}

std::vector<int> gapsBetween(const std::vector<juce::Rectangle<int>>& groups) {
    std::vector<int> gaps;
    for (size_t i = 1; i < groups.size(); ++i)
        gaps.push_back(groups[i].getX() - groups[i - 1].getRight());
    return gaps;
}

}  // namespace

TEST_CASE("Style keys round-trip through the config word", "[ui][transport-layout]") {
    for (auto style : kStyles)
        REQUIRE(styleFromKey(styleKey(style)) == style);
    REQUIRE(styleFromKey("") == Style::Anchored);
    REQUIRE(styleFromKey("nonsense") == Style::Anchored);
}

// ---------------------------------------------------------------------------
// What fits. #2071 was the overflow button showing from the first frame.
// ---------------------------------------------------------------------------

TEST_CASE("The transport fits the window MAGDA opens at in every style", "[ui][transport-layout]") {
    const auto style = GENERATE(Style::Anchored, Style::MemoryFill, Style::Justified);
    const auto l = compute(LayoutConfig::defaultWindowWidth, defaultTransportHeight(),
                           nominalText(), 1.0f, style);
    INFO("style " << styleKey(style));
    REQUIRE(nothingButTheMeterCollapsed(l));
    REQUIRE(l.overflow.isEmpty());
}

TEST_CASE("A wide bar shows the master buffer meter in every style", "[ui][transport-layout]") {
    const auto style = GENERATE(Style::Anchored, Style::MemoryFill, Style::Justified);
    const auto l = compute(kWide, defaultTransportHeight(), nominalText(), 1.0f, style);
    INFO("style " << styleKey(style));
    REQUIRE(nothingButTheMeterCollapsed(l));
    REQUIRE(l.memoryMeterVisible);
    REQUIRE_FALSE(l.memoryMeter.isEmpty());
}

// ---------------------------------------------------------------------------
// The drop order.
// ---------------------------------------------------------------------------

TEST_CASE("Sections drop in the declared order as the panel narrows", "[ui][transport-layout]") {
    const auto style = GENERATE(Style::Anchored, Style::MemoryFill, Style::Justified);
    INFO("style " << styleKey(style));
    std::vector<Section> dropped;
    for (int width = kWide; width >= 200; --width) {
        const auto l = compute(width, defaultTransportHeight(), nominalText(), 1.0f, style);
        for (auto section : kDropOrder)
            if (!l.isVisible(section) &&
                std::find(dropped.begin(), dropped.end(), section) == dropped.end())
                dropped.push_back(section);
    }
    REQUIRE(dropped == std::vector<Section>(kDropOrder.begin(), kDropOrder.end()));
}

TEST_CASE("A section that is on stays on as the panel widens", "[ui][transport-layout]") {
    const auto style = GENERATE(Style::Anchored, Style::MemoryFill, Style::Justified);
    INFO("style " << styleKey(style));
    const int height = defaultTransportHeight();
    Layout narrower = compute(200, height, nominalText(), 1.0f, style);
    for (int width = 201; width <= kWide; ++width) {
        const auto wider = compute(width, height, nominalText(), 1.0f, style);
        INFO("width " << width);
        for (auto section : kDropOrder)
            REQUIRE((wider.isVisible(section) || !narrower.isVisible(section)));
        narrower = wider;
    }
}

TEST_CASE("The overflow button stands in for the right-hand cluster", "[ui][transport-layout]") {
    for (auto style : kStyles)
        for (int width = 300; width <= kWide; width += 3) {
            const auto l = compute(width, defaultTransportHeight(), nominalText(), 1.0f, style);
            INFO("style " << styleKey(style) << " width " << width);
            REQUIRE(l.overflowVisible == !l.rightClusterVisible);
            REQUIRE(l.overflow.isEmpty() == l.rightClusterVisible);
        }
}

TEST_CASE("A dropped section leaves no rectangle behind", "[ui][transport-layout]") {
    const auto style = GENERATE(Style::Anchored, Style::MemoryFill, Style::Justified);
    const auto l = compute(400, defaultTransportHeight(), nominalText(), 1.0f, style);

    REQUIRE(l.overflowVisible);
    REQUIRE(l.home.isEmpty());
    REQUIRE(l.loop.isEmpty());
    REQUIRE(l.punchFrame.isEmpty());
    REQUIRE(l.selectionStart.isEmpty());
    REQUIRE(l.selChip.isEmpty());
    REQUIRE(l.stackFrame.isEmpty());
    REQUIRE(l.autoGrid.isEmpty());
    REQUIRE(l.memoryFrame.isEmpty());
    REQUIRE(l.keep.isEmpty());
    REQUIRE(l.cpu.isEmpty());
    REQUIRE(l.qwerty.isEmpty());

    // The transport is still a transport.
    REQUIRE_FALSE(l.play.isEmpty());
    REQUIRE_FALSE(l.tempo.isEmpty());
    REQUIRE_FALSE(l.key.isEmpty());
    REQUIRE_FALSE(l.playhead.isEmpty());
}

// ---------------------------------------------------------------------------
// Placement: what the fit decision counted is what the groups occupy.
// ---------------------------------------------------------------------------

TEST_CASE("Groups never overlap and stay inside the bar", "[ui][transport-layout]") {
    for (auto style : kStyles)
        for (int width = 600; width <= kWide; width += 7) {
            const auto l = compute(width, defaultTransportHeight(), nominalText(), 1.0f, style);
            INFO("style " << styleKey(style) << " width " << width);
            const auto groups = groupsOf(l);
            REQUIRE(groups.front().getX() >= 0);
            REQUIRE(groups.back().getRight() <= width);
            for (int gap : gapsBetween(groups))
                REQUIRE(gap >= 0);
        }
}

TEST_CASE("The automation-write banner sits in a gap", "[ui][transport-layout]") {
    for (auto style : kStyles)
        for (int width = 600; width <= 2400; width += 13) {
            const auto l = compute(width, defaultTransportHeight(), nominalText(), 1.0f, style);
            INFO("style " << styleKey(style) << " width " << width);
            if (!l.automationWriteLabelFits) {
                REQUIRE(l.automationWriteLabel.isEmpty());
                continue;
            }
            REQUIRE(l.automationWriteLabel.getWidth() >= nominalText().banner);
            for (const auto& group : groupsOf(l))
                REQUIRE_FALSE(group.intersects(l.automationWriteLabel));
        }
}

TEST_CASE("Anchored centres the time and tempo cluster in the slack", "[ui][transport-layout]") {
    const auto l = compute(kWide, defaultTransportHeight(), nominalText(), 1.0f, Style::Anchored);
    const int leftEdge = l.punchFrame.getRight();
    const int centreStart = l.tempoFrame.getX();
    const int centreEnd = l.rangeFrame.getRight();
    const int rightEdge = l.gridFrame.getX();
    REQUIRE(std::abs((centreStart - leftEdge) - (rightEdge - centreEnd)) <= 1);
    REQUIRE(centreStart - leftEdge > 20);
}

TEST_CASE("MemoryFill hands the slack to the meter", "[ui][transport-layout]") {
    const int height = defaultTransportHeight();
    const auto narrow = compute(1400, height, nominalText(), 1.0f, Style::MemoryFill);
    const auto wide = compute(1800, height, nominalText(), 1.0f, Style::MemoryFill);
    REQUIRE(narrow.memoryMeterVisible);
    REQUIRE(wide.memoryMeter.getWidth() - narrow.memoryMeter.getWidth() == 400);

    // Everything else stays packed against its neighbours.
    for (const auto* l : {&narrow, &wide}) {
        const auto gaps = gapsBetween(groupsOf(*l));
        REQUIRE(*std::max_element(gaps.begin(), gaps.end()) <=
                *std::min_element(gaps.begin(), gaps.end()) + 1);
    }
}

TEST_CASE("Justified spaces its groups evenly", "[ui][transport-layout]") {
    const auto l = compute(kWide, defaultTransportHeight(), nominalText(), 1.0f, Style::Justified);
    // The QWERTY toggle and CPU ride as one; their own gap stays packed.
    auto groups = groupsOf(l);
    groups[groups.size() - 2] = groups[groups.size() - 2].getUnion(groups.back());
    groups.pop_back();
    const auto gaps = gapsBetween(groups);
    REQUIRE(*std::max_element(gaps.begin(), gaps.end()) -
                *std::min_element(gaps.begin(), gaps.end()) <=
            1);
    REQUIRE(groups.back().getRight() == kWide - 5);
}

// ---------------------------------------------------------------------------
// The time readouts.
// ---------------------------------------------------------------------------

TEST_CASE("Selection and loop share rows behind the chips except in the stack",
          "[ui][transport-layout]") {
    const int height = defaultTransportHeight();
    for (auto style : {Style::Anchored, Style::MemoryFill}) {
        const auto l = compute(kWide, height, nominalText(), 1.0f, style);
        REQUIRE(l.selectionAndLoopShareRows());
        REQUIRE(l.selectionStart == l.loopStart);
        REQUIRE(l.selectionEnd == l.loopEnd);
        REQUIRE_FALSE(l.selChip.isEmpty());
        REQUIRE(l.editCursor.isEmpty());
        REQUIRE(l.playhead.getHeight() > l.selectionStart.getHeight());
    }

    const auto stack = compute(kWide, height, nominalText(), 1.0f, Style::Justified);
    REQUIRE_FALSE(stack.selectionAndLoopShareRows());
    REQUIRE(stack.selChip.isEmpty());
    REQUIRE(stack.cursorFrame.isEmpty());
    REQUIRE_FALSE(stack.editCursor.isEmpty());
    REQUIRE(stack.selectionStart.getY() < stack.loopStart.getY());
    REQUIRE(stack.loopStart.getY() < stack.playhead.getY());
    REQUIRE(stack.playhead.getBottom() <= stack.stackFrame.getBottom());
}

TEST_CASE("The stack falls back to the headline playhead once SEL / LOOP drop",
          "[ui][transport-layout]") {
    for (int width = kWide; width >= 200; --width) {
        const auto l =
            compute(width, defaultTransportHeight(), nominalText(), 1.0f, Style::Justified);
        if (l.selLoopTimesVisible)
            continue;
        REQUIRE(l.stackFrame.isEmpty());
        REQUIRE_FALSE(l.cursorFrame.isEmpty());
        REQUIRE(l.editCursor.isEmpty());
        return;
    }
    FAIL("SEL / LOOP never dropped");
}

TEST_CASE("Readouts are wider than their digits by what is drawn over their end",
          "[ui][transport-layout]") {
    const auto text = nominalText();
    for (auto style : kStyles)
        for (int width = 600; width <= kWide; width += 50) {
            const auto l = compute(width, defaultTransportHeight(), text, 1.0f, style);
            INFO("style " << styleKey(style) << " width " << width);

            if (!l.cursorFrame.isEmpty()) {
                REQUIRE(l.cursorTrailingInset >= text.timecodeCaption);
                REQUIRE(l.playhead.getWidth() >=
                        text.headlineTimecodeBox + l.cursorTrailingInset - text.timecodeGlyphInset);
            }
            if (l.punchVisible) {
                REQUIRE(l.punchIn.getRight() <= l.punchStart.getRight());
                REQUIRE(l.punchIn.getX() >= l.punchStart.getRight() - l.punchTrailingInset);
                REQUIRE(l.punchOut.getX() >= l.punchEnd.getRight() - l.punchTrailingInset);
            }
        }
}

// ---------------------------------------------------------------------------
// Density.
// ---------------------------------------------------------------------------

TEST_CASE("Spacing density moves the layout with it", "[ui][transport-layout]") {
    const int height = defaultTransportHeight();
    const auto style = GENERATE(Style::Anchored, Style::MemoryFill, Style::Justified);
    const auto compact =
        compute(LayoutConfig::defaultWindowWidth, height, nominalText(), 0.6f, style);
    const auto normal =
        compute(LayoutConfig::defaultWindowWidth, height, nominalText(), 1.0f, style);
    // The widest spacing pushes the right-hand cluster into the overflow menu at
    // the smallest window; it holds everything once the window is wider.
    const auto spacious = compute(1400, height, nominalText(), 1.4f, style);

    REQUIRE(nothingButTheMeterCollapsed(compact));
    REQUIRE(nothingButTheMeterCollapsed(normal));
    REQUIRE(nothingButTheMeterCollapsed(spacious));

    REQUIRE(compact.home.getX() < normal.home.getX());
    REQUIRE(normal.home.getX() < spacious.home.getX());

    const auto framePad = [](const Layout& l) { return l.tempo.getX() - l.tempoFrame.getX(); };
    REQUIRE(framePad(compact) < framePad(normal));
    REQUIRE(framePad(normal) < framePad(spacious));
}
