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

    int newProjectCalls = 0;
    int openProjectCalls = 0;
    int saveProjectCalls = 0;
    int saveProjectAsCalls = 0;
    int exportAudioCalls = 0;
    int closeProjectCalls = 0;
    int projectSettingsCalls = 0;
    int collectFilesCalls = 0;
    int exportMidiCalls = 0;
    int importDawProjectCalls = 0;
    int exportDawProjectCalls = 0;

    magda::MenuManager::MenuCallbacks callbacks;
    callbacks.onNewProject = [&] { ++newProjectCalls; };
    callbacks.onOpenProject = [&] { ++openProjectCalls; };
    callbacks.onSaveProject = [&] { ++saveProjectCalls; };
    callbacks.onSaveProjectAs = [&] { ++saveProjectAsCalls; };
    callbacks.onExportAudio = [&] { ++exportAudioCalls; };
    callbacks.onCloseProject = [&] { ++closeProjectCalls; };
    callbacks.onProjectSettings = [&] { ++projectSettingsCalls; };
    callbacks.onCollectFiles = [&] { ++collectFilesCalls; };
    callbacks.onExportMidi = [&] { ++exportMidiCalls; };
    callbacks.onImportDawProject = [&] { ++importDawProjectCalls; };
    callbacks.onExportDawProject = [&] { ++exportDawProjectCalls; };
    menuManager.initialize(callbacks);

    REQUIRE(menuManager.invokeApplicationCommand(magda::CommandIDs::newProject));
    REQUIRE(menuManager.invokeApplicationCommand(magda::CommandIDs::openProject));
    REQUIRE(menuManager.invokeApplicationCommand(magda::CommandIDs::saveProject));
    REQUIRE(menuManager.invokeApplicationCommand(magda::CommandIDs::saveProjectAs));
    REQUIRE(menuManager.invokeApplicationCommand(magda::CommandIDs::exportAudio));
    REQUIRE(menuManager.invokeApplicationCommand(magda::CommandIDs::closeProject));
    REQUIRE(menuManager.invokeApplicationCommand(magda::CommandIDs::projectSettings));
    REQUIRE(menuManager.invokeApplicationCommand(magda::CommandIDs::collectFiles));
    REQUIRE(menuManager.invokeApplicationCommand(magda::CommandIDs::exportMidi));
    REQUIRE(menuManager.invokeApplicationCommand(magda::CommandIDs::importDawProject));
    REQUIRE(menuManager.invokeApplicationCommand(magda::CommandIDs::exportDawProject));

    CHECK(newProjectCalls == 1);
    CHECK(openProjectCalls == 1);
    CHECK(saveProjectCalls == 1);
    CHECK(saveProjectAsCalls == 1);
    CHECK(exportAudioCalls == 1);
    CHECK(closeProjectCalls == 1);
    CHECK(projectSettingsCalls == 1);
    CHECK(collectFilesCalls == 1);
    CHECK(exportMidiCalls == 1);
    CHECK(importDawProjectCalls == 1);
    CHECK(exportDawProjectCalls == 1);
}

TEST_CASE("MenuManager rejects commands without a configured File callback", "[commands][menu]") {
    auto& menuManager = magda::MenuManager::getInstance();
    MenuCallbacksGuard guard(menuManager);
    menuManager.initialize({});

    CHECK_FALSE(menuManager.invokeApplicationCommand(magda::CommandIDs::saveProject));
    CHECK_FALSE(menuManager.invokeApplicationCommand(magda::CommandIDs::undo));
}
