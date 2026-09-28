#include <catch2/catch_test_macros.hpp>

#include "magda/daw/ui/views/ClipSlotButton.hpp"

namespace {

juce::MouseEvent clickAt(magda::ClipSlotButton& slot, float x) {
    const auto now = juce::Time::getCurrentTime();
    return {juce::Desktop::getInstance().getMainMouseSource(),
            {x, 10.0f},
            juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier),
            1.0f,
            0.0f,
            0.0f,
            0.0f,
            0.0f,
            &slot,
            &slot,
            now,
            {x, 10.0f},
            now,
            1,
            false};
}

}  // namespace

TEST_CASE("An active Session recording stops from anywhere in its slot",
          "[ui][session][recording][2553]") {
    juce::ScopedJuceInitialiser_GUI gui;
    magda::ClipSlotButton slot;
    slot.setBounds(0, 0, 160, 24);
    slot.trackIsRecordArmed = true;
    slot.slotIsRecording = true;

    int stopClicks = 0;
    int contentClicks = 0;
    slot.onEmptySlotRecordClick = [&stopClicks] { ++stopClicks; };
    slot.onSingleClick = [&contentClicks](const juce::MouseEvent&) { ++contentClicks; };

    slot.mouseUp(clickAt(slot, 100.0f));

    REQUIRE(stopClicks == 1);
    REQUIRE(contentClicks == 0);
}
