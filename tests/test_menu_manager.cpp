#include <catch2/catch_test_macros.hpp>

#include "magda/daw/ui/windows/CommandIDs.hpp"
#include "magda/daw/ui/windows/MenuManager.hpp"

namespace {

class MenuCallbacksGuard {
  public:
    explicit MenuCallbacksGuard(magda::MenuManager& menuManager) : menuManager_(menuManager) {}

    ~MenuCallbacksGuard() {
        menuManager_.initialize({});
    }

  private:
    magda::MenuManager& menuManager_;
};

}  // namespace

TEST_CASE("MenuManager dispatches File application commands to menu callbacks",
          "[commands][menu]") {
    auto& menuManager = magda::MenuManager::getInstance();
    MenuCallbacksGuard guard(menuManager);

    int saveProjectCalls = 0;
    int saveProjectAsCalls = 0;
    int exportAudioCalls = 0;

    magda::MenuManager::MenuCallbacks callbacks;
    callbacks.onSaveProject = [&] { ++saveProjectCalls; };
    callbacks.onSaveProjectAs = [&] { ++saveProjectAsCalls; };
    callbacks.onExportAudio = [&] { ++exportAudioCalls; };
    menuManager.initialize(callbacks);

    REQUIRE(menuManager.invokeApplicationCommand(magda::CommandIDs::saveProject));
    REQUIRE(menuManager.invokeApplicationCommand(magda::CommandIDs::saveProjectAs));
    REQUIRE(menuManager.invokeApplicationCommand(magda::CommandIDs::exportAudio));

    CHECK(saveProjectCalls == 1);
    CHECK(saveProjectAsCalls == 1);
    CHECK(exportAudioCalls == 1);
}

TEST_CASE("MenuManager rejects commands without a configured File callback", "[commands][menu]") {
    auto& menuManager = magda::MenuManager::getInstance();
    MenuCallbacksGuard guard(menuManager);
    menuManager.initialize({});

    CHECK_FALSE(menuManager.invokeApplicationCommand(magda::CommandIDs::saveProject));
    CHECK_FALSE(menuManager.invokeApplicationCommand(magda::CommandIDs::undo));
}
