#include "undo_api_live.hpp"

#include "../core/UndoManager.hpp"

namespace magda {

void UndoApiLive::executeCommand(std::unique_ptr<UndoableCommand> command) {
    UndoManager::getInstance().executeCommand(std::move(command));
}

}  // namespace magda
