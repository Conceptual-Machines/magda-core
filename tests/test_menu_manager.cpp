#include <array>
#include <catch2/catch_test_macros.hpp>

#include "magda/daw/ui/windows/CommandIDs.hpp"
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
    std::array<int, 8> calls{};
    MenuCallbacksGuard guard(menuManager);

    magda::MenuManager::MenuCallbacks callbacks;
    callbacks.onNewProject = [&] { ++calls[0]; };
    callbacks.onOpenProject = [&] { ++calls[1]; };
    callbacks.onSaveProject = [&] { ++calls[2]; };
    callbacks.onSaveProjectAs = [&] { ++calls[3]; };
    callbacks.onExportAudio = [&] { ++calls[4]; };
    callbacks.onDeleteTrack = [&] { ++calls[5]; };
    callbacks.onToggleArrangeSession = [&] { ++calls[6]; };
    callbacks.onAbout = [&] { ++calls[7]; };
    menuManager.initialize(callbacks);

    const std::array commandIDs{
        magda::CommandIDs::newProject,           magda::CommandIDs::openProject,
        magda::CommandIDs::saveProject,          magda::CommandIDs::saveProjectAs,
        magda::CommandIDs::exportAudio,          magda::CommandIDs::deleteTrack,
        magda::CommandIDs::toggleArrangeSession, magda::CommandIDs::about,
    };

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
