#include <catch2/catch_test_macros.hpp>

#include "../../magda/daw/ui/components/pianoroll/PianoRollShownContent.hpp"

using magda::ClipView;
using magda::PianoRollShownContent;

TEST_CASE("The piano roll fits its view only to content it was not showing", "[pianoroll][view]") {
    std::optional<PianoRollShownContent> shown;

    SECTION("The first content fits") {
        CHECK(magda::takeNewContent(
            shown, PianoRollShownContent::of(1, ClipView::Arrangement, false, {10})));
    }

    SECTION("In absolute mode the track's timeline is the content, whichever clip is picked") {
        magda::takeNewContent(shown,
                              PianoRollShownContent::of(1, ClipView::Arrangement, false, {10}));
        CHECK_FALSE(magda::takeNewContent(
            shown, PianoRollShownContent::of(1, ClipView::Arrangement, false, {10})));
        CHECK_FALSE(magda::takeNewContent(
            shown, PianoRollShownContent::of(1, ClipView::Arrangement, false, {11, 12})));
        CHECK(magda::takeNewContent(
            shown, PianoRollShownContent::of(2, ClipView::Arrangement, false, {20})));
        CHECK(magda::takeNewContent(shown,
                                    PianoRollShownContent::of(2, ClipView::Session, false, {20})));
    }

    SECTION("In relative mode the clips shown are the content, in any order") {
        magda::takeNewContent(shown,
                              PianoRollShownContent::of(1, ClipView::Session, true, {10, 11}));
        CHECK_FALSE(magda::takeNewContent(
            shown, PianoRollShownContent::of(1, ClipView::Session, true, {11, 10})));
        CHECK(magda::takeNewContent(shown,
                                    PianoRollShownContent::of(1, ClipView::Session, true, {12})));
    }

    SECTION("Switching to relative mode is new content") {
        magda::takeNewContent(shown,
                              PianoRollShownContent::of(1, ClipView::Arrangement, false, {10}));
        CHECK(magda::takeNewContent(
            shown, PianoRollShownContent::of(1, ClipView::Arrangement, true, {10})));
    }
}
