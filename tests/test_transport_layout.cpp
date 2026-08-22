// Transport bar layout (#2071 / #2074). The decision about what fits is pure
// arithmetic over (width, height, measured text, spacing density), so it can be
// asserted without a window. The companion test_transport_layout_juce.cpp runs
// the same assertions against the widths the shipped fonts actually produce.

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <vector>

#include "magda/daw/ui/layout/LayoutConfig.hpp"
#include "magda/daw/ui/panels/TransportLayout.hpp"

using magda::LayoutConfig;
using namespace magda::daw::ui::transport;

namespace {

// What the shipped Inter measures at the default font scale, as reported by
// test_transport_layout_juce.cpp. The tests that matter sweep around these
// rather than leaning on the exact numbers, so a font change moves them
// without invalidating the test.
TextWidths nominalText() {
    TextWidths text;
    text.timecodeBox = 93;
    text.tempo = 43;
    text.timeSigNumerator = 20;
    text.timeSigDenominator = 16;
    text.cpuTitle = 17;
    text.cpuValue = 23;
    text.gridDivision = 18;
    text.gridToggle = 26;
    return text;
}

// The transport is as tall as the window gives it, which at launch is
// LayoutConfig's own default.
int defaultTransportHeight() {
    return LayoutConfig::getInstance().defaultTransportHeight;
}

bool nothingCollapsed(const Layout& l) {
    return l.navVisible && l.loopBackVisible && l.punchVisible && l.selLoopTimesVisible &&
           l.gridVisible && l.rightClusterVisible && !l.overflowVisible;
}

}  // namespace

// ---------------------------------------------------------------------------
// The regression from #2071: the overflow button was on from the first frame at
// the size the app opens itself at.
// ---------------------------------------------------------------------------

TEST_CASE("The whole transport fits the window MAGDA opens at", "[ui][transport-layout]") {
    const auto l =
        compute(LayoutConfig::defaultWindowWidth, defaultTransportHeight(), nominalText(), 1.0f);

    REQUIRE(nothingCollapsed(l));
    REQUIRE(l.cpu.getWidth() > 0);
    REQUIRE(l.overflow.isEmpty());
}

TEST_CASE("It keeps fitting as the text around it grows", "[ui][transport-layout]") {
    // Roughly 15% wider strings than the shipped font draws, which covers a
    // different UI font family or a nudged font scale. Past that the sections
    // start collapsing, which is what the overflow menu is for.
    for (int extra = 0; extra <= 3; ++extra) {
        auto text = nominalText();
        text.timecodeBox += extra * 4;
        text.tempo += extra * 2;
        text.timeSigNumerator += extra;
        text.timeSigDenominator += extra;
        text.cpuTitle += extra;
        text.cpuValue += extra;
        text.gridDivision += extra;
        text.gridToggle += extra * 2;

        INFO("text widths grown by " << extra << "px");
        const auto l =
            compute(LayoutConfig::defaultWindowWidth, defaultTransportHeight(), text, 1.0f);
        REQUIRE(nothingCollapsed(l));
    }
}

TEST_CASE("It fits at every height the transport can be dragged to", "[ui][transport-layout]") {
    // The icon buttons are square, so a taller transport is also a wider one.
    const auto& config = LayoutConfig::getInstance();
    for (int height = config.minTransportHeight; height <= config.maxTransportHeight; ++height) {
        INFO("transport height " << height);
        const auto l = compute(LayoutConfig::defaultWindowWidth, height, nominalText(), 1.0f);
        REQUIRE(nothingCollapsed(l));
    }
}

// ---------------------------------------------------------------------------
// The drop order, which is the part that used to be six hand-written copies of
// the same two lines.
// ---------------------------------------------------------------------------

TEST_CASE("Sections drop in the declared order as the panel narrows", "[ui][transport-layout]") {
    const int height = defaultTransportHeight();
    std::vector<Section> dropped;

    for (int width = LayoutConfig::defaultWindowWidth; width >= 200; --width) {
        const auto l = compute(width, height, nominalText(), 1.0f);
        for (auto section : kDropOrder)
            if (!l.isVisible(section) &&
                std::find(dropped.begin(), dropped.end(), section) == dropped.end())
                dropped.push_back(section);
    }

    REQUIRE(dropped == std::vector<Section>(kDropOrder.begin(), kDropOrder.end()));
}

TEST_CASE("A section that is on stays on as the panel widens", "[ui][transport-layout]") {
    const int height = defaultTransportHeight();
    Layout narrower = compute(200, height, nominalText(), 1.0f);

    for (int width = 201; width <= 1600; ++width) {
        const auto wider = compute(width, height, nominalText(), 1.0f);
        INFO("width " << width);
        for (auto section : kDropOrder)
            REQUIRE((wider.isVisible(section) || !narrower.isVisible(section)));
        narrower = wider;
    }
}

// ---------------------------------------------------------------------------
// The invariant the hardcoded estimates could not hold: what the fit decision
// counted and what the placement occupies are the same width.
// ---------------------------------------------------------------------------

TEST_CASE("The arranged sections stay inside the width they were measured for",
          "[ui][transport-layout]") {
    const int height = defaultTransportHeight();

    for (int width = 600; width <= 1600; width += 7) {
        const auto l = compute(width, height, nominalText(), 1.0f);
        INFO("width " << width);

        // Right edge of the left-to-right flow.
        const int flowRight = l.gridVisible ? l.snap.getRight() : l.editCursor.getRight();
        // Left edge of whatever is pinned to the right.
        const int clusterLeft = l.overflowVisible ? l.overflow.getX() : l.qwerty.getX();

        REQUIRE(flowRight <= clusterLeft);
        REQUIRE(l.cpu.getRight() <= width);
        REQUIRE(l.overflow.getRight() <= width);
    }
}

TEST_CASE("A dropped section leaves no rectangle behind", "[ui][transport-layout]") {
    // Narrow enough that everything collapsible is gone.
    const auto l = compute(400, defaultTransportHeight(), nominalText(), 1.0f);

    REQUIRE(l.overflowVisible);
    REQUIRE(l.home.isEmpty());
    REQUIRE(l.loop.isEmpty());
    REQUIRE(l.punchStart.isEmpty());
    REQUIRE(l.selectionStart.isEmpty());
    REQUIRE(l.autoGrid.isEmpty());
    REQUIRE(l.cpu.isEmpty());
    REQUIRE(l.qwerty.isEmpty());

    // The transport is still a transport.
    REQUIRE_FALSE(l.play.isEmpty());
    REQUIRE_FALSE(l.tempo.isEmpty());
    REQUIRE_FALSE(l.playhead.isEmpty());
}

TEST_CASE("The overflow button appears exactly when something is hidden",
          "[ui][transport-layout]") {
    const int height = defaultTransportHeight();

    for (int width = 300; width <= 1600; width += 3) {
        const auto l = compute(width, height, nominalText(), 1.0f);
        const bool anythingHidden = !nothingCollapsed(l);
        INFO("width " << width);
        REQUIRE(l.overflowVisible == anythingHidden);
    }
}

// ---------------------------------------------------------------------------
// Density, which the old hardcoded widths ignored entirely.
// ---------------------------------------------------------------------------

TEST_CASE("Spacing density moves the layout with it", "[ui][transport-layout]") {
    const int height = defaultTransportHeight();
    const auto compact = compute(LayoutConfig::defaultWindowWidth, height, nominalText(), 0.6f);
    const auto normal = compute(LayoutConfig::defaultWindowWidth, height, nominalText(), 1.0f);
    const auto spacious = compute(LayoutConfig::defaultWindowWidth, height, nominalText(), 1.4f);

    // Everything still fits at every density the preference offers.
    REQUIRE(nothingCollapsed(compact));
    REQUIRE(nothingCollapsed(normal));
    REQUIRE(nothingCollapsed(spacious));

    // ...and the spacing actually moves, rather than density being ignored.
    // Read it off the pads themselves: the section widths also carry the
    // leftover width shared out to the readouts, which moves the other way.
    REQUIRE(compact.play.getX() < normal.play.getX());
    REQUIRE(normal.play.getX() < spacious.play.getX());

    const auto metroPad = [](const Layout& l) { return l.tempo.getX() - l.transportRight; };
    REQUIRE(metroPad(compact) < metroPad(normal));
    REQUIRE(metroPad(normal) < metroPad(spacious));
}

// ---------------------------------------------------------------------------
// The readouts share out whatever width is left over.
// ---------------------------------------------------------------------------

TEST_CASE("Spare width goes to the timecode readouts, up to a cap", "[ui][transport-layout]") {
    const int height = defaultTransportHeight();
    const auto text = nominalText();

    // The narrowest width that still holds everything, where there is no spare
    // to share out and the readouts sit at their intrinsic size.
    int snug = 0;
    for (int width = 400; width <= 2000 && snug == 0; ++width)
        if (nothingCollapsed(compute(width, height, text, 1.0f)))
            snug = width;
    REQUIRE(snug > 0);

    const auto tight = compute(snug, height, text, 1.0f);
    const auto roomy = compute(snug + 120, height, text, 1.0f);
    const auto huge = compute(snug + 1200, height, text, 1.0f);

    REQUIRE(tight.playhead.getWidth() == text.timecodeBox);
    REQUIRE(roomy.playhead.getWidth() > tight.playhead.getWidth());
    REQUIRE(huge.playhead.getWidth() == roomy.playhead.getWidth());

    // Every readout is the same width, whichever group it belongs to.
    REQUIRE(roomy.selectionStart.getWidth() == roomy.playhead.getWidth());
    REQUIRE(roomy.loopEnd.getWidth() == roomy.playhead.getWidth());
    REQUIRE(roomy.punchStart.getWidth() == roomy.playhead.getWidth());
}
