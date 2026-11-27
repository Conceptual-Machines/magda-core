// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include <array>
#include <catch2/catch_test_macros.hpp>

#include "magda/daw/ui/windows/CommandIDs.hpp"
#include "magda/daw/ui/windows/ContributedCommands.hpp"
#include "magda/daw/ui/windows/MenuManager.hpp"

namespace {

class MenuCallbacksGuard {
  public:
    explicit MenuCallbacksGuard(magda::MenuManager& menuManager) : menuManager_(menuManager) {}

    ~MenuCallbacksGuard() {
        // This test target has no application-owned callbacks to restore, so
        // return the process-wide singleton to its known empty baseline.
        menuManager_.initialize({});
    }

  private:
    magda::MenuManager& menuManager_;
};

}  // namespace

TEST_CASE("MenuManager dispatches registered application commands to menu callbacks",
          "[commands][menu]") {
    auto& menuManager = magda::MenuManager::getInstance();
    std::array<int, 16> calls{};
    MenuCallbacksGuard guard(menuManager);

    magda::MenuManager::MenuCallbacks callbacks;
    callbacks.onNewProject = [&] { ++calls[0]; };
    callbacks.onOpenProject = [&] { ++calls[1]; };
    callbacks.onSaveProject = [&] { ++calls[2]; };
    callbacks.onSaveProjectAs = [&] { ++calls[3]; };
    callbacks.onExportAudio = [&] { ++calls[4]; };
    callbacks.onCloseProject = [&] { ++calls[5]; };
    callbacks.onProjectSettings = [&] { ++calls[6]; };
    callbacks.onCollectFiles = [&] { ++calls[7]; };
    callbacks.onExportMidi = [&] { ++calls[8]; };
    callbacks.onImportDawProject = [&] { ++calls[9]; };
    callbacks.onExportDawProject = [&] { ++calls[10]; };
    callbacks.onDeleteTrack = [&] { ++calls[11]; };
    callbacks.onToggleArrangeSession = [&] { ++calls[12]; };
    callbacks.onAbout = [&] { ++calls[13]; };
    callbacks.onPreferences = [&] { ++calls[14]; };
    callbacks.onAudioSettings = [&] { ++calls[15]; };
    menuManager.initialize(callbacks);

    const std::array commandIDs{
        magda::CommandIDs::newProject,           magda::CommandIDs::openProject,
        magda::CommandIDs::saveProject,          magda::CommandIDs::saveProjectAs,
        magda::CommandIDs::exportAudio,          magda::CommandIDs::closeProject,
        magda::CommandIDs::projectSettings,      magda::CommandIDs::collectFiles,
        magda::CommandIDs::exportMidi,           magda::CommandIDs::importDawProject,
        magda::CommandIDs::exportDawProject,     magda::CommandIDs::deleteTrack,
        magda::CommandIDs::toggleArrangeSession, magda::CommandIDs::about,
        magda::CommandIDs::openPreferences,      magda::CommandIDs::openAudioSettings,
    };
    static_assert(calls.size() == commandIDs.size());

    for (const auto commandID : commandIDs)
        REQUIRE(menuManager.invokeApplicationCommand(commandID));

    for (size_t i = 0; i < calls.size(); ++i) {
        CAPTURE(i, commandIDs[i]);
        CHECK(calls[i] == 1);
    }
}

TEST_CASE("MenuManager rejects commands outside its application callback dispatcher",
          "[commands][menu]") {
    auto& menuManager = magda::MenuManager::getInstance();
    MenuCallbacksGuard guard(menuManager);
    menuManager.initialize({});

    CHECK_FALSE(menuManager.invokeApplicationCommand(magda::CommandIDs::undo));
    CHECK_FALSE(menuManager.invokeApplicationCommand(magda::CommandIDs::zoom));
    CHECK_FALSE(menuManager.invokeApplicationCommand(magda::CommandIDs::showHelp));
}

namespace {

/** @brief The bar through its MenuBarModel face, as JUCE sees it. */
juce::MenuBarModel& barOf(magda::MenuManager& menuManager) {
    return *menuManager.getMenuBarModel();
}

/** @brief Pick the item with @p text in @p menu, as a click would. */
bool pick(magda::MenuManager& menuManager, int menuIndex, const juce::String& text) {
    auto& bar = barOf(menuManager);
    const auto menu = bar.getMenuForIndex(menuIndex, {});
    for (juce::PopupMenu::MenuItemIterator it(menu); it.next();) {
        if (it.getItem().text == text) {
            bar.menuItemSelected(it.getItem().itemID, menuIndex);
            return true;
        }
    }
    return false;
}

/** @brief Removes a contribution when the test ends, pass or fail. */
struct Contributed {
    magda::MenuManager& menus;
    magda::MenuManager::ContributionId id;
    ~Contributed() {
        menus.removeContribution(id);
    }
};

}  // namespace

TEST_CASE("An extension's top-level menu sits where it asked and runs its items",
          "[commands][menu][extensions]") {
    juce::ScopedJuceInitialiser_GUI gui;  // the bar posts its rebuilds
    auto& menus = magda::MenuManager::getInstance();
    const auto builtIn = barOf(menus).getMenuBarNames();

    int runs = 0;
    Contributed account{menus,
                        menus.addTopLevelMenu("Account", 6, [&runs](magda::MenuItems& items) {
                            items.addItem("Sign In", [&runs] { ++runs; });
                        })};

    const auto names = barOf(menus).getMenuBarNames();
    REQUIRE(names.size() == builtIn.size() + 1);
    CHECK(names[6] == "Account");
    CHECK(names[7] == builtIn[6]);

    REQUIRE(pick(menus, 6, "Sign In"));
    CHECK(runs == 1);
}

TEST_CASE("Items an extension adds to a built-in menu leave its own items as they were",
          "[commands][menu][extensions]") {
    juce::ScopedJuceInitialiser_GUI gui;  // the bar posts its rebuilds
    auto& menus = magda::MenuManager::getInstance();
    const int help = barOf(menus).getMenuBarNames().size() - 1;
    const auto before = barOf(menus).getMenuForIndex(help, {}).getNumItems();

    int runs = 0;
    {
        Contributed extra{menus, menus.addItems(magda::Menu::Help, magda::MenuSection::End,
                                                [&runs](magda::MenuItems& items) {
                                                    items.addItem("Extension Help",
                                                                  [&runs] { ++runs; });
                                                })};
        // getNumItems() leaves the separator out.
        CHECK(barOf(menus).getMenuForIndex(help, {}).getNumItems() == before + 1);
        REQUIRE(pick(menus, help, "Extension Help"));
        CHECK(runs == 1);
    }

    CHECK(barOf(menus).getMenuBarNames().size() == help + 1);
    CHECK(barOf(menus).getMenuForIndex(help, {}).getNumItems() == before);
}

TEST_CASE("A contributed menu is built each time it opens, from current state",
          "[commands][menu][extensions]") {
    juce::ScopedJuceInitialiser_GUI gui;  // the bar posts its rebuilds
    auto& menus = magda::MenuManager::getInstance();
    bool signedIn = false;
    Contributed account{menus,
                        menus.addTopLevelMenu("Account", 99, [&signedIn](magda::MenuItems& items) {
                            items.addItem(signedIn ? "Sign Out" : "Sign In", [] {});
                        })};
    const int index = barOf(menus).getMenuBarNames().indexOf("Account");
    REQUIRE(index == barOf(menus).getMenuBarNames().size() - 1);

    CHECK(pick(menus, index, "Sign In"));
    signedIn = true;
    menus.refresh();
    CHECK(pick(menus, index, "Sign Out"));
    CHECK_FALSE(pick(menus, index, "Sign In"));
}

TEST_CASE("Contributed commands run on the target that lists them", "[commands][extensions]") {
    struct Target final : juce::ApplicationCommandTarget {
        explicit Target(juce::CommandID ownCommand) : command(ownCommand) {}
        juce::ApplicationCommandTarget* getNextCommandTarget() override {
            return nullptr;
        }
        void getAllCommands(juce::Array<juce::CommandID>& commands) override {
            commands.add(command);
        }
        void getCommandInfo(juce::CommandID, juce::ApplicationCommandInfo& result) override {
            result.setInfo("Target", {}, "Extensions", 0);
        }
        bool perform(const InvocationInfo&) override {
            ++runs;
            return true;
        }
        juce::CommandID command;
        int runs = 0;
    };

    const auto first = magda::CommandIDs::firstExtensionCommand;
    Target a(first), b(first + 1);
    magda::ContributedCommands commands;
    commands.add(&a);
    commands.add(&b);

    juce::Array<juce::CommandID> all;
    commands.getAllCommands(all);
    CHECK(all.size() == 2);

    CHECK(commands.perform(juce::ApplicationCommandTarget::InvocationInfo(first + 1)));
    CHECK(a.runs == 0);
    CHECK(b.runs == 1);

    commands.remove(&b);
    CHECK_FALSE(commands.perform(juce::ApplicationCommandTarget::InvocationInfo(first + 1)));
}
