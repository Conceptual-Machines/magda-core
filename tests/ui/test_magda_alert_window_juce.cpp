#include <juce_gui_basics/juce_gui_basics.h>

#include "magda/daw/ui/components/common/MagdaAlertWindow.hpp"

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

// Alerts are top-level windows, so these run where the JUCE tests have a display.
class MagdaAlertWindowTest final : public juce::UnitTest {
  public:
    MagdaAlertWindowTest() : juce::UnitTest("Magda Alert Window Tests", "magda") {}

    void runTest() override {
        beginTest("A destructive alert gives Return to Cancel");
        {
            std::unique_ptr<juce::AlertWindow> alert(
                magda::createMagdaAlertWindow("Delete 3 tracks?", "", "Delete", "Cancel", {},
                                              juce::MessageBoxIconType::WarningIcon, 2, nullptr));
            auto* remove = buttonNamed(*alert, "Delete");
            auto* cancel = buttonNamed(*alert, "Cancel");
            expect(remove != nullptr && cancel != nullptr);
            if (remove == nullptr || cancel == nullptr)
                return;
            expect(!takesReturn(*remove));
            expect(takesReturn(*cancel));
            expect(magda::MagdaAlertWindow::roleOf(*remove) == Role::Destructive);
            expect(magda::MagdaAlertWindow::roleOf(*cancel) == Role::Primary);
            expect(remove->isRegisteredForShortcut(juce::KeyPress(
                juce::KeyPress::backspaceKey, juce::ModifierKeys::commandModifier, 0)));
            expect(cancel->getRight() < remove->getX(), "Delete sits last, right of Cancel");
        }

        beginTest("The unsaved-changes alert saves on Return and keeps Don't Save apart");
        {
            std::unique_ptr<juce::AlertWindow> alert(
                magda::createMagdaAlertWindow("Save changes?", "", "Save", "Don't Save", "Cancel",
                                              juce::MessageBoxIconType::QuestionIcon, 3, nullptr));
            auto* save = buttonNamed(*alert, "Save");
            auto* discard = buttonNamed(*alert, "Don't Save");
            auto* cancel = buttonNamed(*alert, "Cancel");
            expect(save != nullptr && discard != nullptr && cancel != nullptr);
            if (save == nullptr || discard == nullptr || cancel == nullptr)
                return;
            expect(takesReturn(*save));
            expect(magda::MagdaAlertWindow::roleOf(*save) == Role::Primary);
            expect(magda::MagdaAlertWindow::roleOf(*discard) == Role::Discard);
            expect(discard->isRegisteredForShortcut(
                juce::KeyPress('d', juce::ModifierKeys::commandModifier, 0)));
            expect(discard->getRight() < cancel->getX());
            expect(cancel->getRight() < save->getX());
        }

        beginTest("A lone Cancel is primary but ignores Return");
        {
            std::unique_ptr<juce::AlertWindow> alert(magda::createMagdaAlertWindow(
                "Capturing", "", {}, {}, {}, juce::MessageBoxIconType::InfoIcon, 0, nullptr));
            alert->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
            auto* cancel = buttonNamed(*alert, "Cancel");
            expect(cancel != nullptr);
            if (cancel == nullptr)
                return;
            expect(!takesReturn(*cancel));
            expect(magda::MagdaAlertWindow::roleOf(*cancel) == Role::Primary);
        }
    }
};

static MagdaAlertWindowTest magdaAlertWindowTest;
