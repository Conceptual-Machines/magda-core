#pragma once

#include <juce_core/juce_core.h>

#include <cstdint>
#include <deque>
#include <memory>
#include <vector>

namespace magda {

/**
 * @brief Base class for all undoable commands
 *
 * Implement this interface for each operation that should support undo/redo.
 * Commands should capture all state needed to both execute and undo the operation.
 */
class UndoableCommand {
  public:
    virtual ~UndoableCommand() = default;

    /**
     * Execute the command (do the operation).
     * Called when the command is first performed and on redo.
     */
    virtual void execute() = 0;

    /**
     * Undo the command (reverse the operation).
     * Called when the user triggers undo.
     */
    virtual void undo() = 0;

    /**
     * Get a human-readable description of this command.
     * Used for UI display (e.g., "Undo Split Clip").
     */
    virtual juce::String getDescription() const = 0;

    /**
     * Check if this command can be merged with another command.
     * Used for coalescing rapid repeated operations (e.g., multiple small moves).
     * Default: no merging.
     */
    virtual bool canMergeWith(const UndoableCommand* /*other*/) const {
        return false;
    }

    /**
     * Merge another command into this one.
     * Only called if canMergeWith returned true.
     */
    virtual void mergeWith(const UndoableCommand* /*other*/) {}
};

/**
 * @brief Listener interface for undo state changes
 */
class UndoManagerListener {
  public:
    virtual ~UndoManagerListener() = default;

    /**
     * Called when the undo/redo state changes.
     * UI can use this to update menu items, buttons, etc.
     */
    virtual void undoStateChanged() = 0;
};

/**
 * @brief Central manager for undo/redo operations
 *
 * All undoable operations should go through this manager.
 * Usage:
 *   auto cmd = std::make_unique<SplitClipCommand>(clipId, splitTime);
 *   UndoManager::getInstance().executeCommand(std::move(cmd));
 *
 * The command is executed immediately and added to the undo stack.
 */
class UndoManager {
  public:
    static UndoManager& getInstance();

    // Prevent copying
    UndoManager(const UndoManager&) = delete;
    UndoManager& operator=(const UndoManager&) = delete;

    /**
     * Execute a command and add it to the undo stack.
     * The command's execute() method is called immediately.
     */
    void executeCommand(std::unique_ptr<UndoableCommand> command);

    /**
     * Undo the last command.
     * @return true if undo was performed, false if nothing to undo
     */
    bool undo();

    /**
     * Redo the last undone command.
     * @return true if redo was performed, false if nothing to redo
     */
    bool redo();

    /**
     * Check if undo is available.
     */
    bool canUndo() const {
        return !undoStack_.empty();
    }

    /**
     * Check if redo is available.
     */
    bool canRedo() const {
        return !redoStack_.empty();
    }

    /**
     * Get description of the command that would be undone.
     */
    juce::String getUndoDescription() const;

    /**
     * Get description of the command that would be redone.
     */
    juce::String getRedoDescription() const;

    /**
     * Clear all undo/redo history.
     */
    void clearHistory();

    /**
     * Record the current history state as matching the project on disk.
     */
    void markCurrentStateSaved();

    /**
     * Begin a compound operation (groups multiple commands as one undo step).
     * All commands executed until endCompoundOperation() are grouped.
     */
    void beginCompoundOperation(const juce::String& description);

    /**
     * End a compound operation.
     */
    void endCompoundOperation();

    /**
     * Check if currently in a compound operation.
     */
    bool isInCompoundOperation() const {
        return compoundDepth_ > 0;
    }

    /**
     * Set maximum number of undo steps to keep.
     */
    void setMaxUndoSteps(size_t maxSteps) {
        maxUndoSteps_ = maxSteps;
        trimUndoStack();
    }

    /**
     * Get maximum number of undo steps.
     */
    size_t getMaxUndoSteps() const {
        return maxUndoSteps_;
    }

    // Listener management
    void addListener(UndoManagerListener* listener);
    void removeListener(UndoManagerListener* listener);

  private:
    struct HistoryEntry {
        std::unique_ptr<UndoableCommand> command;
        std::uint64_t beforeStateId = 0;
        std::uint64_t afterStateId = 0;
    };

    UndoManager();
    ~UndoManager() = default;

    void notifyListeners();
    void trimUndoStack();
    void updateProjectDirtyState();

    std::deque<HistoryEntry> undoStack_;
    std::deque<HistoryEntry> redoStack_;

    // Compound operation support
    int compoundDepth_ = 0;
    juce::String compoundDescription_;
    std::vector<std::unique_ptr<UndoableCommand>> compoundCommands_;
    std::uint64_t compoundBeforeStateId_ = 0;

    size_t maxUndoSteps_ = 100;

    // Stable state identities let undo/redo recognise the exact state that was
    // last saved, including after branching or command merging.
    std::uint64_t currentStateId_ = 0;
    std::uint64_t savedStateId_ = 0;
    std::uint64_t nextStateId_ = 1;

    std::vector<UndoManagerListener*> listeners_;
};

/**
 * @brief Compound command that groups multiple commands as one undo step
 */
class CompoundCommand : public UndoableCommand {
  public:
    explicit CompoundCommand(const juce::String& description,
                             std::vector<std::unique_ptr<UndoableCommand>> commands);

    void execute() override;
    void undo() override;
    juce::String getDescription() const override {
        return description_;
    }

  private:
    juce::String description_;
    std::vector<std::unique_ptr<UndoableCommand>> commands_;
};

/**
 * @brief RAII helper for compound operations
 *
 * Usage:
 *   {
 *       CompoundOperationScope scope("Move Multiple Clips");
 *       // Execute multiple commands here...
 *   } // Automatically ends compound operation
 */
class CompoundOperationScope {
  public:
    explicit CompoundOperationScope(const juce::String& description);
    ~CompoundOperationScope();

    // Prevent copying/moving
    CompoundOperationScope(const CompoundOperationScope&) = delete;
    CompoundOperationScope& operator=(const CompoundOperationScope&) = delete;
};

}  // namespace magda
