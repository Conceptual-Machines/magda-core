#pragma once

#include "undo_api.hpp"

namespace magda {

/// Forwards every UndoApi call to UndoManager::getInstance().
class UndoApiLive : public UndoApi {
  public:
    void executeCommand(std::unique_ptr<UndoableCommand> command) override;
    void beginCompound(const juce::String& description) override;
    void endCompound() override;
};

}  // namespace magda
