// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include <memory>

#include "extensions/Extension.hpp"
#include "ui/windows/CommandIDs.hpp"
#include "ui/windows/MenuManager.hpp"

namespace magda::extensions::stub {

namespace {

/** @brief The stub's one command, which its Help item runs. */
constexpr juce::CommandID kStubCommand = CommandIDs::firstExtensionCommand;

/**
 * @brief Proves the extension hooks link and run. It is built only in CI, where it adds a menu,
 *        a Help item and a command (#3076).
 */
class StubExtension final : public Extension, private juce::ApplicationCommandTarget {
  public:
    const char* id() const override {
        return "stub";
    }

    void onStartup(AppContext&) override {
        auto& menus = MenuManager::getInstance();
        menus.addCommandTarget(this);
        menu_ = menus.addTopLevelMenu("Stub", 6, [](MenuItems& items) {
            items.addItem("Stub Action", [] { juce::Logger::writeToLog("[stub] menu item"); });
        });
        helpItems_ = menus.addItems(Menu::Help, MenuSection::End,
                                    [](MenuItems& items) { items.addCommandItem(kStubCommand); });
    }

    void onShutdown() override {
        auto& menus = MenuManager::getInstance();
        menus.removeContribution(helpItems_);
        menus.removeContribution(menu_);
        menus.removeCommandTarget(this);
    }

  private:
    juce::ApplicationCommandTarget* getNextCommandTarget() override {
        return nullptr;
    }

    void getAllCommands(juce::Array<juce::CommandID>& commands) override {
        commands.add(kStubCommand);
    }

    void getCommandInfo(juce::CommandID commandID, juce::ApplicationCommandInfo& result) override {
        if (commandID == kStubCommand)
            result.setInfo("Stub Command", "Proves extension commands run", "Extensions", 0);
    }

    bool perform(const InvocationInfo& info) override {
        if (info.commandID != kStubCommand)
            return false;
        juce::Logger::writeToLog("[stub] command");
        return true;
    }

    MenuManager::ContributionId menu_ = 0;
    MenuManager::ContributionId helpItems_ = 0;
};

[[maybe_unused]] const bool stubExtensionRegistered = registerExtension(
    []() -> std::unique_ptr<Extension> { return std::make_unique<StubExtension>(); });

}  // namespace

}  // namespace magda::extensions::stub
