// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include <catch2/catch_test_macros.hpp>
#include <vector>

#include "magda/daw/ui/components/pianoroll/PianoRollKeyboard.hpp"

namespace {

struct Preview {
    int note;
    bool on;
};

juce::MouseEvent mouseAt(magda::PianoRollKeyboard& keys, float x, float y, float downY) {
    const auto now = juce::Time::getCurrentTime();
    return {juce::Desktop::getInstance().getMainMouseSource(),
            {x, y},
            juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier),
            1.0f,
            0.0f,
            0.0f,
            0.0f,
            0.0f,
            &keys,
            &keys,
            now,
            {10.0f, downY},
            now,
            1,
            false};
}

}  // namespace

TEST_CASE("A drag on the piano roll keys plays the keys it crosses", "[ui][pianoroll][3429]") {
    juce::ScopedJuceInitialiser_GUI gui;
    magda::PianoRollKeyboard keys;
    keys.setBounds(0, 0, 60, 240);
    keys.setNoteHeight(12);

    std::vector<Preview> previews;
    keys.onNotePreview = [&previews](int note, int, bool on) { previews.push_back({note, on}); };
    int zooms = 0;
    int scrolls = 0;
    keys.onZoomChanged = [&zooms](int, int, int) { ++zooms; };
    keys.onScrollRequested = [&scrolls](int) { ++scrolls; };

    keys.mouseDown(mouseAt(keys, 10.0f, 6.0f, 6.0f));
    REQUIRE(previews.size() == 1);
    const int first = previews.front().note;
    CHECK(previews.front().on);

    // Sideways movement within the key keeps the note sounding.
    keys.mouseDrag(mouseAt(keys, 40.0f, 8.0f, 6.0f));
    CHECK(previews.size() == 1);

    // Reaching another key hands the note over to it.
    keys.mouseDrag(mouseAt(keys, 40.0f, 30.0f, 6.0f));
    REQUIRE(previews.size() == 3);
    CHECK(previews[1].note == first);
    CHECK_FALSE(previews[1].on);
    CHECK(previews[2].note != first);
    CHECK(previews[2].on);

    keys.mouseUp(mouseAt(keys, 40.0f, 30.0f, 6.0f));
    REQUIRE(previews.size() == 4);
    CHECK(previews[3].note == previews[2].note);
    CHECK_FALSE(previews[3].on);

    CHECK(zooms == 0);
    CHECK(scrolls == 0);
}
