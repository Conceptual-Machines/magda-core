#pragma once

#include <memory>

namespace juce {
class String;
}

namespace magda {

class UndoableCommand;

/// Abstract view onto UndoManager — agent code only enqueues commands.
class UndoApi {
  public:
    virtual ~UndoApi() = default;

    /**
     * @brief Execute a command and take ownership of it.
     *
     * Implementations must keep the command alive rather than destroying it.
     * Callers throughout MAGDA hold a raw pointer across this call to read what
     * the command allocated — `getCreatedTrackId`, `getCreatedClipId` — which is
     * only valid because `UndoManager` retains it on the undo stack or in the
     * open compound. A stub that drops the command turns every one of those
     * call sites into a use-after-free.
     */
    virtual void executeCommand(std::unique_ptr<UndoableCommand> command) = 0;

    /// Group every command enqueued until endCompound() into one undo step.
    /// Nestable; the outermost begin/end pair forms the step.
    virtual void beginCompound(const juce::String& description) = 0;
    virtual void endCompound() = 0;
};

}  // namespace magda
