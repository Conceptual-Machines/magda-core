#pragma once

#include <memory>

namespace magda {

class UndoableCommand;

/// Abstract view onto UndoManager — agent code only enqueues commands.
class UndoApi {
  public:
    virtual ~UndoApi() = default;

    virtual void executeCommand(std::unique_ptr<UndoableCommand> command) = 0;
};

}  // namespace magda
