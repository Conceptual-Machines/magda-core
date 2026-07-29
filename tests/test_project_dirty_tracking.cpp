#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <stdexcept>

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

class MergeableValueCommand : public magda::UndoableCommand {
  public:
    MergeableValueCommand(int& value, int newValue)
        : value_(value), oldValue_(value), newValue_(newValue) {}

    void execute() override {
        value_ = newValue_;
    }

    void undo() override {
        value_ = oldValue_;
    }

    juce::String getDescription() const override {
        return "Set value";
    }

    bool canMergeWith(const magda::UndoableCommand* other) const override {
        const auto* otherValue = dynamic_cast<const MergeableValueCommand*>(other);
        return otherValue != nullptr && &otherValue->value_ == &value_;
    }

    void mergeWith(const magda::UndoableCommand* other) override {
        const auto* otherValue = dynamic_cast<const MergeableValueCommand*>(other);
        REQUIRE(otherValue != nullptr);
        newValue_ = otherValue->newValue_;
    }

  private:
    int& value_;
    int oldValue_;
    int newValue_;
};

// Throws out of execute() so the mutation scope has to unwind. If it does not,
// markDirty() stays suppressed for the rest of the process and the project
// silently never reports unsaved changes again.
class ThrowingCommand : public magda::UndoableCommand {
  public:
    void execute() override {
        throw std::runtime_error("command failed");
    }

    void undo() override {}

    juce::String getDescription() const override {
        return "Throwing";
    }
};

class TempProject {
  public:
    TempProject()
        : directory_(juce::File::getSpecialLocation(juce::File::tempDirectory)
                         .getNonexistentChildFile("magda_dirty_tracking", {})),
          file_(directory_.getChildFile("DirtyTracking.mgd")) {
        const auto result = directory_.createDirectory();
        INFO("Temporary project directory: " << directory_.getFullPathName());
        INFO("Directory creation error: " << result.getErrorMessage());
        REQUIRE(result.wasOk());
    }

    ~TempProject() {
        directory_.deleteRecursively();
    }

    const juce::File& file() const {
        return file_;
    }

  private:
    juce::File directory_;
    juce::File file_;
};

// Tests establish a known-clean checkpoint through the same save path used by
// the application. This also hands the shared singleton back clean so later
// project-boundary tests cannot raise a modal unsaved-changes prompt.
juce::File saveToCleanState(const juce::File& file) {
    auto& projectManager = magda::ProjectManager::getInstance();
    REQUIRE(projectManager.saveProjectAs(file));
    REQUIRE_FALSE(projectManager.isDirty());
    return projectManager.getCurrentProjectFile();
}

}  // namespace

TEST_CASE("Undoable commands mark the project dirty", "[project][undo]") {
    auto& projectManager = magda::ProjectManager::getInstance();
    auto& undoManager = magda::UndoManager::getInstance();
    TempProject tempProject;

    saveToCleanState(tempProject.file());
    undoManager.clearHistory();

    SECTION("executing a command") {
        undoManager.executeCommand(std::make_unique<NoOpCommand>());
        CHECK(projectManager.isDirty());
    }

    SECTION("undoing back to the saved checkpoint restores clean state") {
        undoManager.executeCommand(std::make_unique<NoOpCommand>());
        REQUIRE(projectManager.isDirty());

        REQUIRE(undoManager.undo());
        CHECK_FALSE(projectManager.isDirty());
    }

    SECTION("undoing away from the saved checkpoint marks dirty") {
        undoManager.executeCommand(std::make_unique<NoOpCommand>());
        saveToCleanState(tempProject.file());

        REQUIRE(undoManager.undo());
        CHECK(projectManager.isDirty());
    }

    SECTION("redoing back to the saved checkpoint restores clean state") {
        undoManager.executeCommand(std::make_unique<NoOpCommand>());
        saveToCleanState(tempProject.file());
        REQUIRE(undoManager.undo());
        REQUIRE(projectManager.isDirty());

        REQUIRE(undoManager.redo());
        CHECK_FALSE(projectManager.isDirty());
    }

    SECTION("branching from before the saved checkpoint remains dirty") {
        undoManager.executeCommand(std::make_unique<NoOpCommand>());
        saveToCleanState(tempProject.file());
        REQUIRE(undoManager.undo());

        undoManager.executeCommand(std::make_unique<NoOpCommand>());
        CHECK(projectManager.isDirty());
    }

    SECTION("command merging does not make the saved checkpoint unreachable") {
        int value = 0;
        undoManager.executeCommand(std::make_unique<MergeableValueCommand>(value, 1));
        saveToCleanState(tempProject.file());

        undoManager.executeCommand(std::make_unique<MergeableValueCommand>(value, 2));
        REQUIRE(value == 2);
        REQUIRE(undoManager.undo());

        CHECK(value == 1);
        CHECK_FALSE(projectManager.isDirty());
    }

    SECTION("external changes remain dirty when undo returns to the saved checkpoint") {
        projectManager.markDirty();
        undoManager.executeCommand(std::make_unique<NoOpCommand>());

        REQUIRE(undoManager.undo());
        CHECK(projectManager.isDirty());
    }

    SECTION("a throwing command leaves later external edits still detectable") {
        REQUIRE_THROWS(undoManager.executeCommand(std::make_unique<ThrowingCommand>()));

        projectManager.markDirty();
        CHECK(projectManager.isDirty());
    }

    SECTION("a command inside a compound operation still marks dirty") {
        undoManager.beginCompoundOperation("Compound");
        undoManager.executeCommand(std::make_unique<NoOpCommand>());
        CHECK(projectManager.isDirty());
        undoManager.endCompoundOperation();
    }

    saveToCleanState(tempProject.file());
    undoManager.clearHistory();
}

TEST_CASE("Opening a project drops the previous project's undo history", "[project][undo]") {
    auto& projectManager = magda::ProjectManager::getInstance();
    auto& undoManager = magda::UndoManager::getInstance();
    TempProject tempProject;

    auto file = saveToCleanState(tempProject.file());
    undoManager.clearHistory();

    // Save after the edit so the load below starts from a clean project and
    // does not raise the unsaved-changes prompt — the history must survive the
    // save and be dropped only by the project boundary.
    undoManager.executeCommand(std::make_unique<NoOpCommand>());
    saveToCleanState(tempProject.file());
    REQUIRE(undoManager.canUndo());

    REQUIRE(projectManager.loadProject(file));

    CHECK_FALSE(undoManager.canUndo());
    CHECK_FALSE(undoManager.canRedo());
    CHECK_FALSE(projectManager.isDirty());

    saveToCleanState(tempProject.file());
    undoManager.clearHistory();
}
