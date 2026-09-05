#include "UndoManager.hpp"

#include <ranges>

#include "../project/ProjectManager.hpp"

namespace magda {

// ============================================================================
// UndoManager Implementation
// ============================================================================

UndoManager& UndoManager::getInstance() {
    static UndoManager instance;
    return instance;
}

UndoManager::UndoManager() = default;

void UndoManager::executeCommand(std::unique_ptr<UndoableCommand> command) {
    if (!command) {
        DBG("UNDO: executeCommand called with null command!");
        return;
    }

    const auto beforeStateId = currentStateId_;
    {
        ProjectManager::UndoableMutationScope mutationScope;
        command->execute();
    }
    currentStateId_ = nextStateId_++;

    // If in compound operation, collect commands instead of pushing to stack
    if (compoundDepth_ > 0) {
        compoundCommands_.push_back(std::move(command));
        updateProjectDirtyState();
        return;
    }

    // Check if we can merge with the previous command
    // Do not merge across the saved checkpoint: doing so would make the exact
    // on-disk state unreachable by undo.
    if (!undoStack_.empty() && beforeStateId != savedStateId_ &&
        undoStack_.back().command->canMergeWith(command.get())) {
        undoStack_.back().command->mergeWith(command.get());
        undoStack_.back().afterStateId = currentStateId_;
    } else {
        // Add to undo stack
        undoStack_.push_back({std::move(command), beforeStateId, currentStateId_});
        trimUndoStack();
    }

    // Clear redo stack (new action invalidates redo history)
    redoStack_.clear();

    updateProjectDirtyState();
    notifyListeners();
}

bool UndoManager::undo() {
    if (undoStack_.empty()) {
        return false;
    }

    // Pop from undo stack
    auto entry = std::move(undoStack_.back());
    undoStack_.pop_back();

    DBG("UNDO: Undoing '" << entry.command->getDescription() << "'");

    {
        ProjectManager::UndoableMutationScope mutationScope;
        entry.command->undo();
    }
    currentStateId_ = entry.beforeStateId;

    // Push to redo stack
    redoStack_.push_back(std::move(entry));

    updateProjectDirtyState();
    notifyListeners();

    return true;
}

bool UndoManager::redo() {
    if (redoStack_.empty()) {
        return false;
    }

    // Pop from redo stack
    auto entry = std::move(redoStack_.back());
    redoStack_.pop_back();

    DBG("UNDO: Redoing '" << entry.command->getDescription() << "'");

    {
        ProjectManager::UndoableMutationScope mutationScope;
        entry.command->execute();
    }
    currentStateId_ = entry.afterStateId;

    // Push to undo stack
    undoStack_.push_back(std::move(entry));

    updateProjectDirtyState();
    notifyListeners();

    return true;
}

juce::String UndoManager::getUndoDescription() const {
    if (undoStack_.empty()) {
        return {};
    }
    return undoStack_.back().command->getDescription();
}

juce::String UndoManager::getRedoDescription() const {
    if (redoStack_.empty()) {
        return {};
    }
    return redoStack_.back().command->getDescription();
}

void UndoManager::clearHistory() {
    // The state ids deliberately survive this. Dropping the stacks removes the
    // route back to the saved state but not the fact that current state differs
    // from it, so undoHistoryDirty_ stays correct and a later save still clears
    // it through markCurrentStateSaved().
    undoStack_.clear();
    redoStack_.clear();
    compoundCommands_.clear();
    compoundDepth_ = 0;
    notifyListeners();
}

void UndoManager::markCurrentStateSaved() {
    savedStateId_ = currentStateId_;
    updateProjectDirtyState();
}

void UndoManager::beginCompoundOperation(const juce::String& description) {
    if (compoundDepth_ == 0) {
        compoundDescription_ = description;
        compoundCommands_.clear();
        compoundBeforeStateId_ = currentStateId_;
    }
    compoundDepth_++;
}

void UndoManager::endCompoundOperation() {
    if (compoundDepth_ <= 0) {
        return;
    }

    compoundDepth_--;

    if (compoundDepth_ == 0 && !compoundCommands_.empty()) {
        // Create compound command and add to undo stack
        auto compound =
            std::make_unique<CompoundCommand>(compoundDescription_, std::move(compoundCommands_));
        undoStack_.push_back({std::move(compound), compoundBeforeStateId_, currentStateId_});
        trimUndoStack();

        // Clear redo stack
        redoStack_.clear();

        compoundCommands_.clear();
        notifyListeners();

        DBG("UNDO: Completed compound operation '" << compoundDescription_ << "'");
    }
}

void UndoManager::addListener(UndoManagerListener* listener) {
    if (listener && std::find(listeners_.begin(), listeners_.end(), listener) == listeners_.end()) {
        listeners_.push_back(listener);
    }
}

void UndoManager::removeListener(UndoManagerListener* listener) {
    listeners_.erase(std::remove(listeners_.begin(), listeners_.end(), listener), listeners_.end());
}

void UndoManager::notifyListeners() {
    for (auto* listener : listeners_) {
        listener->undoStateChanged();
    }
}

void UndoManager::trimUndoStack() {
    while (undoStack_.size() > maxUndoSteps_) {
        undoStack_.pop_front();
    }
}

void UndoManager::updateProjectDirtyState() {
    ProjectManager::getInstance().setUndoHistoryDirty(currentStateId_ != savedStateId_);
}

// ============================================================================
// CompoundCommand Implementation
// ============================================================================

CompoundCommand::CompoundCommand(const juce::String& description,
                                 std::vector<std::unique_ptr<UndoableCommand>> commands)
    : description_(description), commands_(std::move(commands)) {}

void CompoundCommand::execute() {
    // Execute all commands in order
    for (auto& cmd : commands_) {
        cmd->execute();
    }
}

void CompoundCommand::undo() {
    // Undo all commands in reverse order
    for (auto& command : std::views::reverse(commands_)) {
        command->undo();
    }
}

// ============================================================================
// CompoundOperationScope Implementation
// ============================================================================

CompoundOperationScope::CompoundOperationScope(const juce::String& description) {
    UndoManager::getInstance().beginCompoundOperation(description);
}

CompoundOperationScope::~CompoundOperationScope() {
    try {
        UndoManager::getInstance().endCompoundOperation();
    } catch (...) {
        // best-effort compound-command flush; a listener throw must not terminate
    }
}

}  // namespace magda
