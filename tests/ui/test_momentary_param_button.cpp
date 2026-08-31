#include <catch2/catch_test_macros.hpp>
#include <vector>

#include "magda/daw/ui/components/chain/params/MomentaryParamButton.hpp"

using magda::daw::ui::MomentaryParamButton;

namespace {

juce::MouseEvent mouseEventFor(juce::Component& component) {
    const auto now = juce::Time::getCurrentTime();
    return {juce::Desktop::getInstance().getMainMouseSource(),
            {5.0f, 5.0f},
            juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier),
            1.0f,
            0.0f,
            0.0f,
            0.0f,
            0.0f,
            &component,
            &component,
            now,
            {5.0f, 5.0f},
            now,
            1,
            false};
}

}  // namespace

TEST_CASE("MomentaryParamButton emits press and release values once", "[faust][momentary_button]") {
    juce::ScopedJuceInitialiser_GUI gui;
    MomentaryParamButton button;
    std::vector<double> values;
    button.setValueChangedCallback([&values](double value) { values.push_back(value); });

    button.setState(juce::Button::buttonDown);
    button.setState(juce::Button::buttonDown);
    button.setState(juce::Button::buttonNormal);

    REQUIRE(values == std::vector<double>{1.0, 0.0});
}

TEST_CASE("MomentaryParamButton forced release returns the value to zero",
          "[faust][momentary_button]") {
    juce::ScopedJuceInitialiser_GUI gui;
    MomentaryParamButton button;
    std::vector<double> values;
    button.setValueChangedCallback([&values](double value) { values.push_back(value); });

    button.setState(juce::Button::buttonDown);
    button.release();
    button.release();

    REQUIRE(values == std::vector<double>{1.0, 0.0});
    REQUIRE(button.getState() == juce::Button::buttonNormal);
}

TEST_CASE("MomentaryParamButton stays active until mouse up", "[faust][momentary_button]") {
    juce::ScopedJuceInitialiser_GUI gui;
    MomentaryParamButton button;
    std::vector<double> values;
    button.setValueChangedCallback([&values](double value) { values.push_back(value); });

    auto mouseEvent = mouseEventFor(button);
    button.mouseDown(mouseEvent);
    button.setState(juce::Button::buttonNormal);  // Pointer moved outside while still held.
    REQUIRE(values == std::vector<double>{1.0});

    button.mouseUp(mouseEvent);
    REQUIRE(values == std::vector<double>{1.0, 0.0});
}
