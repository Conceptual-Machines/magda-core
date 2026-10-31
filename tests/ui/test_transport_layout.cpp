// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

// Transport bar layout (#2071 / #2074, one row in #3206). The decision about
// what fits is pure arithmetic over (width, height, measured text, spacing
// density), so it can be asserted without a window. The companion
// test_transport_layout_juce.cpp runs the same assertions against the widths
// the shipped fonts actually produce.

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
    text.headlineTimecodeBox = 141;
    text.clock = 50;
    text.tempo = 49;
    text.timeSigNumerator = 20;
    text.timeSigDenominator = 16;
    text.rangeCaption = 22;
    text.memoryCaption = 19;
    text.memoryTime = 65;
    text.cpuTitle = 17;
    text.cpuValue = 40;
    text.gridDivision = 30;
    text.gridToggle = 26;
    text.banner = 106;
    return text;
}

constexpr int kWide = 2000;

int defaultTransportHeight() {
    return LayoutConfig::getInstance().transportHeight;
}

Layout at(int width, float density = 1.0f) {
    return compute(width, defaultTransportHeight(), nominalText(), density);
}

bool everythingShowing(const Layout& l) {
    for (auto section : kDropOrder)
        if (!l.isVisible(section))
            return false;
    return !l.overflowVisible;
}

// Nothing has gone into the overflow menu; collapsed sections still show.
bool nothingDropped(const Layout& l) {
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
    for (const auto& r : {l.punchFrame, l.tempoFrame, l.cursorFrame, l.selectionFrame, l.loopFrame,
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

// ---------------------------------------------------------------------------
// What fits. #2071 was the overflow button showing from the first frame.
// ---------------------------------------------------------------------------

TEST_CASE("Nothing drops into the overflow menu at the window MAGDA opens at",
          "[ui][transport-layout]") {
    const auto l = at(LayoutConfig::defaultWindowWidth);
    REQUIRE(nothingDropped(l));
    REQUIRE(l.overflow.isEmpty());
}

TEST_CASE("A wide bar shows everything at full size", "[ui][transport-layout]") {
    const auto l = at(kWide);
    REQUIRE(everythingShowing(l));
    REQUIRE_FALSE(l.memoryMeter.isEmpty());
    REQUIRE_FALSE(l.clock.isEmpty());
    REQUIRE_FALSE(l.selectionEnd.isEmpty());
    REQUIRE_FALSE(l.loopEnd.isEmpty());
}

TEST_CASE("Everything sits on one row", "[ui][transport-layout]") {
    const int height = defaultTransportHeight();
    const auto l = at(kWide);
    for (const auto& r :
         {l.play,        l.punchIn,     l.punchOut,         l.link,           l.tempo,
          l.playhead,    l.clock,       l.selectionCaption, l.selectionStart, l.selectionEnd,
          l.loopCaption, l.loopStart,   l.loopEnd,          l.gridDivision,   l.autoGrid,
          l.snap,        l.memoryMeter, l.capture,          l.qwerty,         l.cpuTitle,
          l.cpuValue}) {
        REQUIRE(r.getY() >= 0);
        REQUIRE(r.getBottom() <= height);
    }
    REQUIRE(l.selectionStart.getY() == l.loopStart.getY());
    REQUIRE(l.selectionEnd.getRight() <= l.loopCaption.getX());
    REQUIRE(l.selectionCaption.getRight() <= l.selectionStart.getX());
    REQUIRE(l.loopStart.getRight() <= l.loopEnd.getX());
    REQUIRE(l.autoGrid.getY() == l.snap.getY());
    REQUIRE(l.playhead.getRight() <= l.clock.getX());
    REQUIRE(l.cpuTitle.getRight() <= l.cpuValue.getX());
}

// ---------------------------------------------------------------------------
// The drop order.
// ---------------------------------------------------------------------------

TEST_CASE("Sections collapse and drop in the declared order as the panel narrows",
          "[ui][transport-layout]") {
    std::vector<Section> dropped;
    for (int width = kWide; width >= 200; --width) {
        const auto l = at(width);
        for (auto section : kDropOrder)
            if (!l.isVisible(section) &&
                std::find(dropped.begin(), dropped.end(), section) == dropped.end())
                dropped.push_back(section);
    }
    REQUIRE(dropped == std::vector<Section>(kDropOrder.begin(), kDropOrder.end()));
}

TEST_CASE("A section that is on stays on as the panel widens", "[ui][transport-layout]") {
    Layout narrower = at(200);
    for (int width = 201; width <= kWide; ++width) {
        const auto wider = at(width);
        INFO("width " << width);
        for (auto section : kDropOrder)
            REQUIRE((wider.isVisible(section) || !narrower.isVisible(section)));
        narrower = wider;
    }
}

TEST_CASE("The overflow button stands in for the right-hand cluster", "[ui][transport-layout]") {
    for (int width = 300; width <= kWide; width += 3) {
        const auto l = at(width);
        INFO("width " << width);
        REQUIRE(l.overflowVisible == !l.rightClusterVisible);
        REQUIRE(l.overflow.isEmpty() == l.rightClusterVisible);
    }
}

TEST_CASE("A dropped section leaves no rectangle behind", "[ui][transport-layout]") {
    const auto l = at(400);

    REQUIRE(l.overflowVisible);
    REQUIRE(l.home.isEmpty());
    REQUIRE(l.loop.isEmpty());
    REQUIRE(l.punchFrame.isEmpty());
    REQUIRE(l.punchIn.isEmpty());
    REQUIRE(l.selectionStart.isEmpty());
    REQUIRE(l.loopStart.isEmpty());
    REQUIRE(l.autoGrid.isEmpty());
    REQUIRE(l.memoryFrame.isEmpty());
    REQUIRE(l.capture.isEmpty());
    REQUIRE(l.cpu.isEmpty());
    REQUIRE(l.qwerty.isEmpty());
    REQUIRE(l.clock.isEmpty());

    // The transport is still a transport.
    REQUIRE_FALSE(l.play.isEmpty());
    REQUIRE_FALSE(l.tempo.isEmpty());
    REQUIRE_FALSE(l.playhead.isEmpty());
}

// ---------------------------------------------------------------------------
// What a collapsed section keeps.
// ---------------------------------------------------------------------------

TEST_CASE("Collapsed sections keep their controls", "[ui][transport-layout]") {
    for (int width = kWide; width >= 400; --width) {
        const auto l = at(width);
        INFO("width " << width);
        if (l.punchVisible) {
            REQUIRE_FALSE(l.punchIn.isEmpty());
            REQUIRE_FALSE(l.punchOut.isEmpty());
        }
        if (l.selLoopTimesVisible) {
            REQUIRE_FALSE(l.selectionStart.isEmpty());
            REQUIRE_FALSE(l.loopStart.isEmpty());
            REQUIRE(l.selectionEnd.isEmpty() == !l.rangeEndVisible);
            REQUIRE(l.loopEnd.isEmpty() == !l.rangeEndVisible);
        }
        REQUIRE(l.clock.isEmpty() == !l.clockVisible);
        REQUIRE_FALSE(l.link.isEmpty());
        const int lettered = link_chip::widthForHeight(l.link.getHeight());
        REQUIRE((l.link.getWidth() >= lettered) == l.linkLetteringVisible);
    }
}

// ---------------------------------------------------------------------------
// Placement: what the fit decision counted is what the groups occupy.
// ---------------------------------------------------------------------------

TEST_CASE("Groups never overlap and stay inside the bar", "[ui][transport-layout]") {
    for (int width = 600; width <= kWide; width += 7) {
        const auto l = at(width);
        INFO("width " << width);
        const auto groups = groupsOf(l);
        REQUIRE(groups.front().getX() >= 0);
        REQUIRE(groups.back().getRight() <= width);
        for (int gap : gapsBetween(groups))
            REQUIRE(gap >= 0);
    }
}

TEST_CASE("The automation-write banner sits in a gap", "[ui][transport-layout]") {
    for (int width = 600; width <= 2400; width += 13) {
        const auto l = at(width);
        INFO("width " << width);
        if (!l.automationWriteLabelFits) {
            REQUIRE(l.automationWriteLabel.isEmpty());
            continue;
        }
        REQUIRE(l.automationWriteLabel.getWidth() >= nominalText().banner);
        for (const auto& group : groupsOf(l))
            REQUIRE_FALSE(group.intersects(l.automationWriteLabel));
    }
}

TEST_CASE("The time and tempo groups are centred in the slack", "[ui][transport-layout]") {
    const auto l = at(kWide);
    const int leftEdge = l.punchFrame.getRight();
    const int centreStart = l.tempoFrame.getX();
    const int centreEnd = l.loopFrame.getRight();
    const int rightEdge = l.gridFrame.getX();
    REQUIRE(std::abs((centreStart - leftEdge) - (rightEdge - centreEnd)) <= 1);
    REQUIRE(centreStart - leftEdge > 20);
}

// ---------------------------------------------------------------------------
// Density.
// ---------------------------------------------------------------------------

TEST_CASE("Spacing density moves the layout with it", "[ui][transport-layout]") {
    const auto compact = at(LayoutConfig::defaultWindowWidth, 0.6f);
    const auto normal = at(LayoutConfig::defaultWindowWidth, 1.0f);
    // The widest spacing needs a wider window than the one MAGDA opens at.
    const auto spacious = at(1400, 1.4f);

    REQUIRE(nothingDropped(compact));
    REQUIRE(nothingDropped(normal));
    REQUIRE(nothingDropped(spacious));

    REQUIRE(compact.home.getX() < normal.home.getX());
    REQUIRE(normal.home.getX() < spacious.home.getX());

    const auto framePad = [](const Layout& l) { return l.link.getX() - l.tempoFrame.getX(); };
    REQUIRE(framePad(compact) < framePad(normal));
    REQUIRE(framePad(normal) < framePad(spacious));
}

TEST_CASE("The Link chip leads the tempo group", "[ui][transport-layout]") {
    const auto l = at(kWide);

    REQUIRE_FALSE(l.link.isEmpty());
    REQUIRE(l.tempoFrame.contains(l.link));
    REQUIRE(l.link.getRight() < l.tempo.getX());
}
