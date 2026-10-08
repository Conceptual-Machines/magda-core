#include "PadCommands.hpp"

#include <memory>
#include <utility>

#include "../engine/PluginService.hpp"
#include "DrumGridPads.hpp"
#include "RackInfo.hpp"

namespace magda {

namespace {

PadRack snapshotPads(const ChainNodePath& gridPath) {
    PadRack pads;
    if (const auto* live = TrackManager::getInstance().getPads(gridPath))
        pads.reset(std::make_unique<RackInfo>(*live));
    return pads;
}

}  // namespace

EditPadsCommand::EditPadsCommand(ChainNodePath gridPath, juce::String description,
                                 std::function<void()> edit)
    : gridPath_(std::move(gridPath)),
      description_(std::move(description)),
      edit_(std::move(edit)) {}

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

    // Native pad snapshots do not yet have a child-instance capture path, so
    // this captures the grid plugin only.
    PluginService::getInstance().capturePluginStateAt(gridPath_);
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

void editPads(const ChainNodePath& gridPath, const juce::String& description,
              std::function<void()> edit) {
    UndoManager::getInstance().executeCommand(
        std::make_unique<EditPadsCommand>(gridPath, description, std::move(edit)));
}

// ============================================================================
// SetPadFaderCommand
// ============================================================================

SetPadFaderCommand::SetPadFaderCommand(ChainNodePath gridPath, int padIndex, Target target,
                                       float value, int gesture)
    : gridPath_(std::move(gridPath)),
      padIndex_(padIndex),
      target_(target),
      newValue_(value),
      gesture_(gesture) {
    if (const auto* pad = TrackManager::getInstance().getPad(gridPath_, padIndex_)) {
        oldValue_ = target_ == Target::Volume ? pad->volume : pad->pan;
        valid_ = true;
    }
}

void SetPadFaderCommand::apply(float value) const {
    auto& tm = TrackManager::getInstance();
    if (target_ == Target::Volume)
        tm.setPadVolume(gridPath_, padIndex_, value);
    else
        tm.setPadPan(gridPath_, padIndex_, value);
}

void SetPadFaderCommand::execute() {
    if (valid_)
        apply(newValue_);
}

void SetPadFaderCommand::undo() {
    if (valid_)
        apply(oldValue_);
}

juce::String SetPadFaderCommand::getDescription() const {
    return target_ == Target::Volume ? "Set Pad Level" : "Set Pad Pan";
}

bool SetPadFaderCommand::canMergeWith(const UndoableCommand* other) const {
    const auto* otherFader = dynamic_cast<const SetPadFaderCommand*>(other);
    return otherFader != nullptr && otherFader->gridPath_ == gridPath_ &&
           otherFader->padIndex_ == padIndex_ && otherFader->target_ == target_ &&
           otherFader->gesture_ == gesture_;
}

void SetPadFaderCommand::mergeWith(const UndoableCommand* other) {
    // The value to redo to moves on; the one to come back to stays where the
    // gesture started, which is the point of merging.
    if (const auto* otherFader = dynamic_cast<const SetPadFaderCommand*>(other))
        newValue_ = otherFader->newValue_;
}

void setPadFader(const ChainNodePath& gridPath, int padIndex, SetPadFaderCommand::Target target,
                 float value, int gesture) {
    UndoManager::getInstance().executeCommand(
        std::make_unique<SetPadFaderCommand>(gridPath, padIndex, target, value, gesture));
}

// ============================================================================
// SetPadLayerCommand
// ============================================================================

SetPadLayerCommand::SetPadLayerCommand(ChainNodePath gridPath, ChainId layerId,
                                       PadLayerSettings settings, int gesture)
    : gridPath_(std::move(gridPath)), layerId_(layerId), newSettings_(settings), gesture_(gesture) {
    auto& tm = TrackManager::getInstance();
    const auto* pads = tm.getPads(gridPath_);
    const auto* pad = pads != nullptr ? padOfLayer(*pads, layerId_) : nullptr;
    if (const auto* layer = pad != nullptr ? findPadLayer(*pad, layerId_) : nullptr) {
        oldSettings_ = {layer->volume, layer->pan, layer->zones};
        valid_ = true;
    }
}

void SetPadLayerCommand::apply(const PadLayerSettings& settings) const {
    auto& tm = TrackManager::getInstance();
    tm.setPadLayerVolume(gridPath_, layerId_, settings.volume);
    tm.setPadLayerPan(gridPath_, layerId_, settings.pan);
    tm.setPadLayerZones(gridPath_, layerId_, settings.zones);
}

void SetPadLayerCommand::execute() {
    if (valid_)
        apply(newSettings_);
}

void SetPadLayerCommand::undo() {
    if (valid_)
        apply(oldSettings_);
}

bool SetPadLayerCommand::canMergeWith(const UndoableCommand* other) const {
    const auto* otherLayer = dynamic_cast<const SetPadLayerCommand*>(other);
    return otherLayer != nullptr && otherLayer->gridPath_ == gridPath_ &&
           otherLayer->layerId_ == layerId_ && otherLayer->gesture_ == gesture_;
}

void SetPadLayerCommand::mergeWith(const UndoableCommand* other) {
    if (const auto* otherLayer = dynamic_cast<const SetPadLayerCommand*>(other))
        newSettings_ = otherLayer->newSettings_;
}

void setPadLayer(const ChainNodePath& gridPath, ChainId layerId, const PadLayerSettings& settings,
                 int gesture) {
    UndoManager::getInstance().executeCommand(
        std::make_unique<SetPadLayerCommand>(gridPath, layerId, settings, gesture));
}

}  // namespace magda
