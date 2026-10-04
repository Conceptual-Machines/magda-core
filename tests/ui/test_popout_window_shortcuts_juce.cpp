#include <juce_gui_basics/juce_gui_basics.h>

#include "magda/daw/ui/components/common/FloatingHostWindow.hpp"
#include "magda/daw/ui/windows/AppShortcuts.hpp"

/**
 * MAGDA's floating windows sit outside the main window's key chain, so a key their content leaves
 * unhandled must still run the app's shortcut: Space plays and stops from the analyzer, the LFO
 * window and the rest. The manager has no first target, as in the app, so the shortcut only runs
 * if the window invokes it on the main target itself.
 */

namespace {

constexpr juce::CommandID kPlay = 1;

struct Target final : juce::ApplicationCommandTarget {
    int performed = 0;

    juce::ApplicationCommandTarget* getNextCommandTarget() override {
        return nullptr;
    }
    void getAllCommands(juce::Array<juce::CommandID>& commands) override {
        commands.add(kPlay);
    }
    void getCommandInfo(juce::CommandID, juce::ApplicationCommandInfo& info) override {
        info.setInfo("Play", "Play or stop", "Transport", 0);
        info.addDefaultKeypress(juce::KeyPress::spaceKey, 0);
    }
    bool perform(const InvocationInfo&) override {
        ++performed;
        return true;
    }
};

class PopoutWindowShortcutsTest final : public juce::UnitTest {
  public:
    PopoutWindowShortcutsTest() : juce::UnitTest("Popout window shortcuts", "magda") {}

    void runTest() override {
        Target target;
        juce::ApplicationCommandManager manager;
        manager.registerAllCommandsForTarget(&target);
        manager.setFirstCommandTarget(nullptr);
        magda::daw::ui::FloatingHostWindow window("Popout");

        beginTest("Without the app's shortcuts a popout passes keys on");
        magda::setAppShortcuts(nullptr, nullptr);
        expect(!window.keyPressed(juce::KeyPress(juce::KeyPress::spaceKey)));
        expectEquals(target.performed, 0);

        beginTest("Space in a popout runs the app's play shortcut on the main target");
        magda::setAppShortcuts(&manager, &target);
        expect(window.keyPressed(juce::KeyPress(juce::KeyPress::spaceKey)));
        expectEquals(target.performed, 1);

        beginTest("A key with no shortcut is left unhandled");
        expect(!window.keyPressed(juce::KeyPress('q', juce::ModifierKeys::noModifiers, 0)));
        expectEquals(target.performed, 1);

        magda::setAppShortcuts(nullptr, nullptr);
    }
};

PopoutWindowShortcutsTest popoutWindowShortcutsTest;

}  // namespace
