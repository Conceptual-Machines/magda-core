#include "PadCommands.hpp"

#include <memory>
#include <utility>

#include "RackInfo.hpp"

namespace magda {

EditPadsCommand::EditPadsCommand(const ChainNodePath& gridPath, juce::String description,
                                 std::function<void()> edit, juce::String mergeKey)
    : gridPath_(gridPath),
      description_(std::move(description)),
      edit_(std::move(edit)),
      mergeKey_(std::move(mergeKey)) {}

void EditPadsCommand::execute() {
    if (!edit_)
        return;

    auto& tm = TrackManager::getInstance();

    // Taken before the edit and only on the first run: redo re-runs the edit,
    // so the state to come back to is the one this command was made against.
    if (!executed_) {
        if (const auto* pads = tm.getPads(gridPath_))
            padsBefore_.reset(std::make_unique<RackInfo>(*pads));
    }

    edit_();
    executed_ = true;
}

void EditPadsCommand::undo() {
    if (!executed_)
        return;

    TrackManager::getInstance().setPads(gridPath_, padsBefore_);
}

bool EditPadsCommand::canMergeWith(const UndoableCommand* other) const {
    if (mergeKey_.isEmpty())
        return false;

    const auto* otherEdit = dynamic_cast<const EditPadsCommand*>(other);
    return otherEdit != nullptr && otherEdit->gridPath_ == gridPath_ &&
           otherEdit->mergeKey_ == mergeKey_;
}

void EditPadsCommand::mergeWith(const UndoableCommand* other) {
    // The later edit is what redo replays; the state to come back to stays the
    // one this command was made against, which is the point of merging.
    if (const auto* otherEdit = dynamic_cast<const EditPadsCommand*>(other))
        edit_ = otherEdit->edit_;
}

void editPads(const ChainNodePath& gridPath, const juce::String& description,
              std::function<void()> edit, const juce::String& mergeKey) {
    UndoManager::getInstance().executeCommand(
        std::make_unique<EditPadsCommand>(gridPath, description, std::move(edit), mergeKey));
}

}  // namespace magda
