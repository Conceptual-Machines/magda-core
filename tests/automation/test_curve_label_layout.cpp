// Tests for the curve-view label clamp (#2072): readouts that track a dot or a
// handle must stay inside the plot instead of hanging over the border once the
// tracked point nears either end. Pure geometry, runs headless.

#include <catch2/catch_test_macros.hpp>

#include "magda/daw/ui/utils/CurveLabelLayout.hpp"

using magda::daw::ui::CurveLabelLayout::aboveAnchor;
using magda::daw::ui::CurveLabelLayout::centredIn;

namespace {

// Stands in for an EQ plot area: 8px inset inside a 300x120 view.
const juce::Rectangle<float> kPlot{8.0f, 8.0f, 284.0f, 104.0f};

// The EQ readout box: 120 wide, parked just under the top edge of the plot.
juce::Rectangle<float> readoutAt(float dotX) {
    return centredIn(kPlot, dotX, kPlot.getY() + 2.0f, 120.0f, 14.0f);
}

bool containedIn(juce::Rectangle<float> box, juce::Rectangle<float> area) {
    return box.getX() >= area.getX() && box.getRight() <= area.getRight() &&
           box.getY() >= area.getY() && box.getBottom() <= area.getBottom();
}

}  // namespace

TEST_CASE("Curve label centres on the tracked point away from the edges", "[ui][curve-label]") {
    const float centreX = kPlot.getCentreX();
    const auto box = readoutAt(centreX);

    REQUIRE(box.getWidth() == 120.0f);
    REQUIRE(box.getCentreX() == centreX);
    REQUIRE(box.getY() == kPlot.getY() + 2.0f);
    REQUIRE(containedIn(box, kPlot));
}

TEST_CASE("Curve label stops at the plot edges instead of following the dot", "[ui][curve-label]") {
    SECTION("dot at the low end of the frequency range") {
        const auto box = readoutAt(kPlot.getX());
        REQUIRE(containedIn(box, kPlot));
        REQUIRE(box.getX() == kPlot.getX());
        REQUIRE(box.getWidth() == 120.0f);
    }

    SECTION("dot at the high end of the frequency range") {
        const auto box = readoutAt(kPlot.getRight());
        REQUIRE(containedIn(box, kPlot));
        REQUIRE(box.getRight() == kPlot.getRight());
        REQUIRE(box.getWidth() == 120.0f);
    }

    SECTION("dot past the edge still leaves the box inside") {
        REQUIRE(containedIn(readoutAt(kPlot.getX() - 500.0f), kPlot));
        REQUIRE(containedIn(readoutAt(kPlot.getRight() + 500.0f), kPlot));
    }
}

TEST_CASE("Curve label slides continuously as the dot approaches an edge", "[ui][curve-label]") {
    // Inside the clamp zone the box must stay put rather than jump: two dots
    // 1px apart near the edge share the same box, and the handover from
    // tracking to sliding happens exactly where the centred box first touches
    // the border.
    const float firstTracked = kPlot.getX() + 60.0f;  // half of 120
    REQUIRE(readoutAt(firstTracked).getX() == kPlot.getX());
    REQUIRE(readoutAt(firstTracked + 1.0f).getX() == kPlot.getX() + 1.0f);
    REQUIRE(readoutAt(firstTracked - 1.0f).getX() == kPlot.getX());
}

TEST_CASE("Curve label shrinks when the plot is narrower than the box", "[ui][curve-label]") {
    // A collapsed or very narrow device panel must not produce a box that
    // overflows on both sides.
    const juce::Rectangle<float> narrow{8.0f, 8.0f, 40.0f, 10.0f};
    const auto box = centredIn(narrow, narrow.getCentreX(), narrow.getY() + 2.0f, 120.0f, 14.0f);

    REQUIRE(box.getWidth() == narrow.getWidth());
    REQUIRE(box.getHeight() == narrow.getHeight());
    REQUIRE(containedIn(box, narrow));
}

TEST_CASE("Curve label is clamped vertically as well", "[ui][curve-label]") {
    // The EQ band number rides 16px above its dot; at max gain the dot sits on
    // the top edge, which would otherwise put the number outside the view.
    const auto box = centredIn(kPlot, kPlot.getCentreX(), kPlot.getY() - 16.0f, 10.0f, 12.0f);
    REQUIRE(box.getY() == kPlot.getY());
    REQUIRE(containedIn(box, kPlot));

    const auto low = centredIn(kPlot, kPlot.getCentreX(), kPlot.getBottom() + 4.0f, 10.0f, 12.0f);
    REQUIRE(low.getBottom() == kPlot.getBottom());
    REQUIRE(containedIn(low, kPlot));
}

// The EQ band number: 10x12, 6px clear of the dot, with the active band's
// readout occupying the top 16px of the plot.
namespace {

constexpr float kNumberWidth = 10.0f;
constexpr float kNumberHeight = 12.0f;
constexpr float kNumberGap = 6.0f;

juce::Rectangle<float> numberFor(float dotY, bool bandIsActive) {
    const float readoutBottom = kPlot.getY() + 2.0f + 14.0f;
    return aboveAnchor(kPlot, kPlot.getCentreX(), dotY, kNumberWidth, kNumberHeight, kNumberGap,
                       bandIsActive ? readoutBottom : kPlot.getY());
}

}  // namespace

TEST_CASE("Anchored label sits above its dot when there is room", "[ui][curve-label]") {
    const float dotY = kPlot.getCentreY();

    for (const bool active : {false, true}) {
        const auto box = numberFor(dotY, active);
        REQUIRE(box.getBottom() == dotY - kNumberGap);
        REQUIRE(containedIn(box, kPlot));
    }
}

TEST_CASE("Anchored label flips below its dot rather than leaving the plot", "[ui][curve-label]") {
    // A band at max gain puts its dot on the top edge.
    const auto box = numberFor(kPlot.getY(), false);
    REQUIRE(box.getY() == kPlot.getY() + kNumberGap);
    REQUIRE(containedIn(box, kPlot));
}

TEST_CASE("Anchored label keeps clear of the active band's readout", "[ui][curve-label]") {
    const float readoutBottom = kPlot.getY() + 2.0f + 14.0f;

    SECTION("a dot high enough to collide pushes the number below") {
        // Above-placement would land inside the readout strip even though it
        // is still inside the plot, so it must flip.
        const float dotY = readoutBottom + kNumberGap + kNumberHeight - 1.0f;
        const auto box = numberFor(dotY, true);
        REQUIRE(box.getY() == dotY + kNumberGap);
        REQUIRE(box.getY() >= readoutBottom);
    }

    SECTION("the same dot keeps the upper spot when the band is idle") {
        const float dotY = readoutBottom + kNumberGap + kNumberHeight - 1.0f;
        const auto box = numberFor(dotY, false);
        REQUIRE(box.getBottom() == dotY - kNumberGap);
    }

    SECTION("a dot just clear of the strip still sits above") {
        const float dotY = readoutBottom + kNumberGap + kNumberHeight;
        const auto box = numberFor(dotY, true);
        REQUIRE(box.getY() == readoutBottom);
        REQUIRE(box.getBottom() == dotY - kNumberGap);
    }
}
