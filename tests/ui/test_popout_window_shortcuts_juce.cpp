#include <juce_gui_basics/juce_gui_basics.h>

#include "magda/daw/ui/windows/AppShortcuts.hpp"

/**
 * MAGDA's floating windows sit outside the main window's key chain; each falls back to
 * invokeAppShortcut, so Space plays and stops from the analyzer, the LFO window and the rest. The
 * manager has no first target, as in the app, so the shortcut only runs if it is invoked on the
 * main target itself. No real window: a desktop window under Xvfb has no window manager to take
 * the always-on-top property.
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
        juce::Component origin;

        beginTest("Without the app's shortcuts a key is passed on");
        magda::setAppShortcuts(nullptr, nullptr);
        expect(!magda::invokeAppShortcut(juce::KeyPress(juce::KeyPress::spaceKey), &origin));
        expectEquals(target.performed, 0);

        beginTest("Space runs the app's play shortcut on the main target");
        magda::setAppShortcuts(&manager, &target);
        expect(magda::invokeAppShortcut(juce::KeyPress(juce::KeyPress::spaceKey), &origin));
        expectEquals(target.performed, 1);

        beginTest("A key with no shortcut is left unhandled");
        expect(!magda::invokeAppShortcut(juce::KeyPress('q', juce::ModifierKeys::noModifiers, 0),
                                         &origin));
        expectEquals(target.performed, 1);

        magda::setAppShortcuts(nullptr, nullptr);
    }
};

PopoutWindowShortcutsTest popoutWindowShortcutsTest;

}  // namespace
