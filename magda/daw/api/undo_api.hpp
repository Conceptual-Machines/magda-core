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

    virtual void executeCommand(std::unique_ptr<UndoableCommand> command) = 0;

    /// Group every command enqueued until endCompound() into one undo step.
    /// Nestable; the outermost begin/end pair forms the step.
    virtual void beginCompound(const juce::String& description) = 0;
    virtual void endCompound() = 0;
};

}  // namespace magda
