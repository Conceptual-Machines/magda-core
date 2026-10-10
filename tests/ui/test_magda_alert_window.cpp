#include <catch2/catch_test_macros.hpp>

#include "ui/components/common/MagdaAlertWindow.hpp"

namespace {

using Alert = magda::MagdaAlertWindow;
using Role = Alert::ButtonRole;

// Plain buttons with the keys createMagdaAlertWindow gives them: an alert is a desktop window,
// and CI's Linux display has no window manager to host one.
struct Buttons {
    juce::OwnedArray<juce::TextButton> owned;
    juce::Array<juce::Button*> inOrder;

    juce::Button& add(const juce::String& label, std::initializer_list<juce::KeyPress> keys) {
        auto* button = owned.add(new juce::TextButton(label));
        for (const auto& key : keys)
            button->addShortcut(key);
        inOrder.add(button);
        return *button;
    }
};

const juce::KeyPress kReturn(juce::KeyPress::returnKey);
const juce::KeyPress kEscape(juce::KeyPress::escapeKey);

}  // namespace

TEST_CASE("A destructive alert gives Return to Cancel", "[ui][alert]") {
    juce::ScopedJuceInitialiser_GUI gui;
    Buttons b;
    auto& remove = b.add("Delete", {kReturn});
    auto& cancel = b.add("Cancel", {kEscape});
    Alert::assignRoles(b.inOrder);

    CHECK_FALSE(remove.isRegisteredForShortcut(kReturn));
    CHECK(cancel.isRegisteredForShortcut(kReturn));
    CHECK(Alert::roleOf(remove) == Role::Destructive);
    CHECK(Alert::roleOf(cancel) == Role::Primary);
    CHECK(remove.isRegisteredForShortcut(
        juce::KeyPress(juce::KeyPress::backspaceKey, juce::ModifierKeys::commandModifier, 0)));
    const auto order = Alert::footerOrder(b.inOrder);
    CHECK(order.left.isEmpty());
    CHECK(order.right == juce::Array<juce::Button*>{&cancel, &remove});
}

TEST_CASE("The unsaved-changes alert saves on Return and keeps Don't Save apart", "[ui][alert]") {
    juce::ScopedJuceInitialiser_GUI gui;
    Buttons b;
    auto& save = b.add("Save", {kReturn});
    auto& discard = b.add("Don't Save", {});
    auto& cancel = b.add("Cancel", {kEscape});
    Alert::assignRoles(b.inOrder);

    CHECK(save.isRegisteredForShortcut(kReturn));
    CHECK(Alert::roleOf(save) == Role::Primary);
    CHECK(Alert::roleOf(discard) == Role::Discard);
    CHECK(discard.isRegisteredForShortcut(
        juce::KeyPress('d', juce::ModifierKeys::commandModifier, 0)));
    const auto order = Alert::footerOrder(b.inOrder);
    CHECK(order.left == juce::Array<juce::Button*>{&discard});
    CHECK(order.right == juce::Array<juce::Button*>{&cancel, &save});
}

TEST_CASE("A lone Cancel is primary but ignores Return", "[ui][alert]") {
    juce::ScopedJuceInitialiser_GUI gui;
    Buttons b;
    auto& cancel = b.add("Cancel", {kEscape});
    Alert::assignRoles(b.inOrder);

    CHECK_FALSE(cancel.isRegisteredForShortcut(kReturn));
    CHECK(Alert::roleOf(cancel) == Role::Primary);
}
