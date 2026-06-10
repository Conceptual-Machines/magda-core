#include "undo_api_live.hpp"

#include "../core/UndoManager.hpp"

namespace magda {

void UndoApiLive::executeCommand(std::unique_ptr<UndoableCommand> command) {
    UndoManager::getInstance().executeCommand(std::move(command));
}

void UndoApiLive::beginCompound(const juce::String& description) {
    UndoManager::getInstance().beginCompoundOperation(description);
}

void UndoApiLive::endCompound() {
    UndoManager::getInstance().endCompoundOperation();
}

}  // namespace magda
