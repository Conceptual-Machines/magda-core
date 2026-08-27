#include "PadCommands.hpp"

#include <memory>
#include <utility>

#include "../audio/AudioBridge.hpp"
#include "../engine/AudioEngine.hpp"
#include "RackInfo.hpp"

namespace magda {

namespace {

/// Flush the grid's live plugins into the model before it is snapshotted.
///
/// A pad plugin's patch reaches `DeviceInfo` only when the Drum Grid's state is
/// captured (`PluginManager::captureDrumGridPads`), and the ordinary sync does
/// not do it. Without this, undoing the removal of a pad whose sampler had been
/// edited since it was added restores the sampler as it was added, not as it
/// sounded. `RemoveDeviceByPathCommand` captures for the same reason.
void capturePadPluginStates(const ChainNodePath& gridPath) {
    auto& tm = TrackManager::getInstance();
    if (auto* engine = tm.getAudioEngine())
        if (auto* bridge = engine->getAudioBridge())
            bridge->getPluginManager().capturePluginState(gridPath);
}

PadRack snapshotPads(const ChainNodePath& gridPath) {
    PadRack pads;
    if (const auto* live = TrackManager::getInstance().getPads(gridPath))
        pads.reset(std::make_unique<RackInfo>(*live));
    return pads;
}

}  // namespace

EditPadsCommand::EditPadsCommand(const ChainNodePath& gridPath, juce::String description,
                                 std::function<void()> edit, juce::String mergeKey)
    : gridPath_(gridPath),
      description_(std::move(description)),
      edit_(std::move(edit)),
      mergeKey_(std::move(mergeKey)) {}

void EditPadsCommand::execute() {
    auto& tm = TrackManager::getInstance();

    // Redo restores the after-state rather than replaying the edit. Replaying
    // would run `addDeviceToPad()` again, and that allocates a fresh DeviceId
    // every time: the pad device would come back under a new id while every
    // later command in the redo chain still named the old one, and each of them
    // would silently do nothing. Restoring the snapshot brings the ids back
    // with it.
    if (executed_) {
        tm.setPads(gridPath_, padsAfter_);
        return;
    }

    if (!edit_)
        return;

    capturePadPluginStates(gridPath_);
    padsBefore_ = snapshotPads(gridPath_);

    edit_();

    padsAfter_ = snapshotPads(gridPath_);
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
    // The later state is where redo lands; the state to come back to stays the
    // one this command was made against, which is the point of merging. The
    // other command has already run by the time this is called.
    if (const auto* otherEdit = dynamic_cast<const EditPadsCommand*>(other))
        padsAfter_ = otherEdit->padsAfter_;
}

void editPads(const ChainNodePath& gridPath, const juce::String& description,
              std::function<void()> edit, const juce::String& mergeKey) {
    UndoManager::getInstance().executeCommand(
        std::make_unique<EditPadsCommand>(gridPath, description, std::move(edit), mergeKey));
}

}  // namespace magda
