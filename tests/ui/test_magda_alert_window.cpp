#include <catch2/catch_test_macros.hpp>

#include "ui/components/common/MagdaAlertWindow.hpp"

namespace {

using Role = magda::MagdaAlertWindow::ButtonRole;

juce::Button* buttonNamed(juce::Component& alert, const juce::String& text) {
    for (auto* child : alert.getChildren())
        if (auto* button = dynamic_cast<juce::TextButton*>(child);
            button != nullptr && button->getButtonText() == text)
            return button;
    return nullptr;
}

bool takesReturn(const juce::Button& button) {
    return button.isRegisteredForShortcut(juce::KeyPress(juce::KeyPress::returnKey));
}

}  // namespace

TEST_CASE("A destructive alert gives Return to Cancel", "[ui][alert]") {
    juce::ScopedJuceInitialiser_GUI gui;
    std::unique_ptr<juce::AlertWindow> alert(
        magda::createMagdaAlertWindow("Delete 3 tracks?", "", "Delete", "Cancel", {},
                                      juce::MessageBoxIconType::WarningIcon, 2, nullptr));
    auto* remove = buttonNamed(*alert, "Delete");
    auto* cancel = buttonNamed(*alert, "Cancel");
    REQUIRE(remove != nullptr);
    REQUIRE(cancel != nullptr);
    CHECK_FALSE(takesReturn(*remove));
    CHECK(takesReturn(*cancel));
    CHECK(magda::MagdaAlertWindow::roleOf(*remove) == Role::Destructive);
    CHECK(magda::MagdaAlertWindow::roleOf(*cancel) == Role::Primary);
    CHECK(remove->isRegisteredForShortcut(
        juce::KeyPress(juce::KeyPress::backspaceKey, juce::ModifierKeys::commandModifier, 0)));
    // Cancel sits left of Delete; Delete is last.
    CHECK(cancel->getRight() < remove->getX());
}

TEST_CASE("The unsaved-changes alert saves on Return and keeps Don't Save apart", "[ui][alert]") {
    juce::ScopedJuceInitialiser_GUI gui;
    std::unique_ptr<juce::AlertWindow> alert(
        magda::createMagdaAlertWindow("Save changes?", "", "Save", "Don't Save", "Cancel",
                                      juce::MessageBoxIconType::QuestionIcon, 3, nullptr));
    auto* save = buttonNamed(*alert, "Save");
    auto* discard = buttonNamed(*alert, "Don't Save");
    auto* cancel = buttonNamed(*alert, "Cancel");
    REQUIRE(save != nullptr);
    REQUIRE(discard != nullptr);
    REQUIRE(cancel != nullptr);
    CHECK(takesReturn(*save));
    CHECK(magda::MagdaAlertWindow::roleOf(*save) == Role::Primary);
    CHECK(magda::MagdaAlertWindow::roleOf(*discard) == Role::Discard);
    CHECK(discard->isRegisteredForShortcut(
        juce::KeyPress('d', juce::ModifierKeys::commandModifier, 0)));
    CHECK(discard->getRight() < cancel->getX());
    CHECK(cancel->getRight() < save->getX());
}
