// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include <array>
#include <catch2/catch_test_macros.hpp>

#include "magda/daw/ui/components/chord/CircleOfFifths.hpp"

using namespace magda::daw::ui::circle_of_fifths;
using magda::music::ChordRoot;

TEST_CASE("Circle of fifths: majors a fifth apart, minors their relatives", "[ui][chord]") {
    using R = ChordRoot;
    const std::array<ChordRoot, 12> majors{R::C,      R::G,      R::D,      R::A,
                                           R::E,      R::B,      R::FSharp, R::CSharp,
                                           R::GSharp, R::DSharp, R::ASharp, R::F};
    const std::array<ChordRoot, 12> minors{R::A,      R::E,      R::B,      R::FSharp,
                                           R::CSharp, R::GSharp, R::DSharp, R::ASharp,
                                           R::F,      R::C,      R::G,      R::D};
    for (int step = 0; step < kSegments; ++step) {
        CHECK(majorRootAt(step) == majors[static_cast<size_t>(step)]);
        CHECK(minorRootAt(step) == minors[static_cast<size_t>(step)]);
    }
    CHECK(majorRootAt(-1) == R::F);
    CHECK(majorRootAt(13) == R::G);
}

TEST_CASE("Circle of fifths: a chord's step finds its segment", "[ui][chord]") {
    for (int root = 0; root < 12; ++root) {
        const auto r = static_cast<ChordRoot>(root);
        CHECK(majorRootAt(stepOf(r, false)) == r);
        CHECK(minorRootAt(stepOf(r, true)) == r);
    }
    CHECK(stepOf(ChordRoot::A, true) == stepOf(ChordRoot::C, false));
}

TEST_CASE("Circle of fifths: a segment is a major outside, a minor inside", "[ui][chord]") {
    using magda::music::ChordQuality;
    CHECK(rootAt({Ring::Outer, 1}) == ChordRoot::G);
    CHECK(qualityAt({Ring::Outer, 1}) == ChordQuality::Major);
    CHECK(rootAt({Ring::Inner, 1}) == ChordRoot::E);
    CHECK(qualityAt({Ring::Inner, 1}) == ChordQuality::Minor);
}

TEST_CASE("Circle of fifths: hit testing follows rings and rotation", "[ui][chord]") {
    const Radii radii{40.0f, 80.0f, 120.0f};

    SECTION("the top segment is the one turned there") {
        CHECK(segmentAt(0.0f, -100.0f, 0.0f, radii) == Segment{Ring::Outer, 0});
        CHECK(segmentAt(0.0f, -60.0f, 0.0f, radii) == Segment{Ring::Inner, 0});
        CHECK(segmentAt(0.0f, -100.0f, 5.0f, radii) == Segment{Ring::Outer, 5});
        CHECK(segmentAt(0.0f, -100.0f, 14.0f, radii) == Segment{Ring::Outer, 2});
    }

    SECTION("segments run clockwise") {
        CHECK(segmentAt(100.0f, 0.0f, 0.0f, radii) == Segment{Ring::Outer, 3});
        CHECK(segmentAt(0.0f, 100.0f, 0.0f, radii) == Segment{Ring::Outer, 6});
        CHECK(segmentAt(-100.0f, 0.0f, 0.0f, radii) == Segment{Ring::Outer, 9});
        CHECK(segmentAt(-100.0f, 0.0f, 1.0f, radii) == Segment{Ring::Outer, 10});
    }

    SECTION("the hub and beyond the rim are no segment") {
        CHECK(segmentAt(0.0f, -20.0f, 0.0f, radii).ring == Ring::None);
        CHECK(segmentAt(0.0f, -130.0f, 0.0f, radii).ring == Ring::None);
    }
}
