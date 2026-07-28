#include <catch2/catch_test_macros.hpp>

#include <memory>

#include "magda/daw/core/UndoManager.hpp"
#include "magda/daw/project/ProjectManager.hpp"

namespace {

// A command with no side effects beyond the undo stack itself, so these tests
// exercise the dirty/history plumbing rather than any manager's state.
class NoOpCommand : public magda::UndoableCommand {
  public:
    void execute() override {}
    void undo() override {}

    juce::String getDescription() const override {
        return "No-op";
    }
};

// Saving is the only public path that clears the dirty flag. Tests need it both
// to establish a known-clean starting point and to hand the shared singleton
// back clean — a dirty ProjectManager would make a later test's newProject() or
// loadProject() raise the modal unsaved-changes prompt and stall the run.
juce::File saveToCleanState() {
    auto& projectManager = magda::ProjectManager::getInstance();
    auto file = juce::File::getSpecialLocation(juce::File::tempDirectory)
                    .getChildFile("magda_dirty_tracking")
                    .getChildFile("DirtyTracking.mgd");
    REQUIRE(projectManager.saveProjectAs(file));
    REQUIRE_FALSE(projectManager.isDirty());
    return projectManager.getCurrentProjectFile();
}

}  // namespace

TEST_CASE("Undoable commands mark the project dirty", "[project][undo]") {
    auto& projectManager = magda::ProjectManager::getInstance();
    auto& undoManager = magda::UndoManager::getInstance();

    saveToCleanState();
    undoManager.clearHistory();

    SECTION("executing a command") {
        undoManager.executeCommand(std::make_unique<NoOpCommand>());
        CHECK(projectManager.isDirty());
    }

    SECTION("undoing moves state away from the saved file too") {
        undoManager.executeCommand(std::make_unique<NoOpCommand>());
        saveToCleanState();

        REQUIRE(undoManager.undo());
        CHECK(projectManager.isDirty());
    }

    SECTION("redoing does as well") {
        undoManager.executeCommand(std::make_unique<NoOpCommand>());
        REQUIRE(undoManager.undo());
        saveToCleanState();

        REQUIRE(undoManager.redo());
        CHECK(projectManager.isDirty());
    }

    SECTION("a command inside a compound operation still marks dirty") {
        undoManager.beginCompoundOperation("Compound");
        undoManager.executeCommand(std::make_unique<NoOpCommand>());
        CHECK(projectManager.isDirty());
        undoManager.endCompoundOperation();
    }

    saveToCleanState();
    undoManager.clearHistory();
}

TEST_CASE("Opening a project drops the previous project's undo history",
          "[project][undo]") {
    auto& projectManager = magda::ProjectManager::getInstance();
    auto& undoManager = magda::UndoManager::getInstance();

    auto file = saveToCleanState();
    undoManager.clearHistory();

    // Save after the edit so the load below starts from a clean project and
    // does not raise the unsaved-changes prompt — the history must survive the
    // save and be dropped only by the project boundary.
    undoManager.executeCommand(std::make_unique<NoOpCommand>());
    saveToCleanState();
    REQUIRE(undoManager.canUndo());

    REQUIRE(projectManager.loadProject(file));

    CHECK_FALSE(undoManager.canUndo());
    CHECK_FALSE(undoManager.canRedo());
    CHECK_FALSE(projectManager.isDirty());

    saveToCleanState();
    undoManager.clearHistory();
}
