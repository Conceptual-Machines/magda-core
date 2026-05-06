#pragma once

#include <cstddef>
#include <memory>

#include "UndoManager.hpp"

namespace magda {

/**
 * RAII helper for applying one inspector/edit operation to multiple clips.
 *
 * Single-clip edits are executed normally so existing command merge behaviour
 * is preserved. Multi-clip edits are grouped into one undo step.
 */
class ClipBatchEdit {
  public:
    ClipBatchEdit(const juce::String& description, std::size_t itemCount)
        : batching_(itemCount > 1) {
        if (batching_)
            UndoManager::getInstance().beginCompoundOperation(description);
    }

    ~ClipBatchEdit() {
        if (batching_)
            UndoManager::getInstance().endCompoundOperation();
    }

    ClipBatchEdit(const ClipBatchEdit&) = delete;
    ClipBatchEdit& operator=(const ClipBatchEdit&) = delete;

    void execute(std::unique_ptr<UndoableCommand> command) {
        UndoManager::getInstance().executeCommand(std::move(command));
    }

    bool isBatching() const {
        return batching_;
    }

  private:
    bool batching_ = false;
};

}  // namespace magda
