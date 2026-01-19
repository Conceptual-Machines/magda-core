#include "UndoManager.hpp"

#include <iostream>

namespace magica {

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
        std::cout << "📝 UNDO: executeCommand called with null command!" << std::endl;
        return;
    }

    std::cout << "📝 UNDO: Executing command: " << command->getDescription() << std::endl;

    // Execute the command
    command->execute();

    // If in compound operation, collect commands instead of pushing to stack
    if (compoundDepth_ > 0) {
        compoundCommands_.push_back(std::move(command));
        return;
    }

    // Check if we can merge with the previous command
    if (!undoStack_.empty() && undoStack_.back()->canMergeWith(command.get())) {
        undoStack_.back()->mergeWith(command.get());
    } else {
        // Add to undo stack
        undoStack_.push_back(std::move(command));
        trimUndoStack();
    }

    // Clear redo stack (new action invalidates redo history)
    redoStack_.clear();

    notifyListeners();

    std::cout << "📝 UNDO: Executed command, stack size: " << undoStack_.size() << std::endl;
}

bool UndoManager::undo() {
    if (undoStack_.empty()) {
        return false;
    }

    // Pop from undo stack
    auto command = std::move(undoStack_.back());
    undoStack_.pop_back();

    std::cout << "📝 UNDO: Undoing '" << command->getDescription() << "'" << std::endl;

    // Undo the command
    command->undo();

    // Push to redo stack
    redoStack_.push_back(std::move(command));

    notifyListeners();

    return true;
}

bool UndoManager::redo() {
    if (redoStack_.empty()) {
        return false;
    }

    // Pop from redo stack
    auto command = std::move(redoStack_.back());
    redoStack_.pop_back();

    std::cout << "📝 UNDO: Redoing '" << command->getDescription() << "'" << std::endl;

    // Re-execute the command
    command->execute();

    // Push to undo stack
    undoStack_.push_back(std::move(command));

    notifyListeners();

    return true;
}

juce::String UndoManager::getUndoDescription() const {
    if (undoStack_.empty()) {
        return {};
    }
    return undoStack_.back()->getDescription();
}

juce::String UndoManager::getRedoDescription() const {
    if (redoStack_.empty()) {
        return {};
    }
    return redoStack_.back()->getDescription();
}

void UndoManager::clearHistory() {
    undoStack_.clear();
    redoStack_.clear();
    compoundCommands_.clear();
    compoundDepth_ = 0;
    notifyListeners();
}

void UndoManager::beginCompoundOperation(const juce::String& description) {
    if (compoundDepth_ == 0) {
        compoundDescription_ = description;
        compoundCommands_.clear();
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
        undoStack_.push_back(std::move(compound));
        trimUndoStack();

        // Clear redo stack
        redoStack_.clear();

        compoundCommands_.clear();
        notifyListeners();

        std::cout << "📝 UNDO: Completed compound operation '" << compoundDescription_ << "'"
                  << std::endl;
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
    for (auto it = commands_.rbegin(); it != commands_.rend(); ++it) {
        (*it)->undo();
    }
}

// ============================================================================
// CompoundOperationScope Implementation
// ============================================================================

CompoundOperationScope::CompoundOperationScope(const juce::String& description) {
    UndoManager::getInstance().beginCompoundOperation(description);
}

CompoundOperationScope::~CompoundOperationScope() {
    UndoManager::getInstance().endCompoundOperation();
}

}  // namespace magica
