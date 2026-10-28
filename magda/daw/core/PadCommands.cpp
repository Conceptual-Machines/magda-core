// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include "PadCommands.hpp"

#include <juce_audio_basics/juce_audio_basics.h>

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

/// What a pad edit can change on the grid's channels: a pad's fader and
/// switches are its channel's (#3010).
std::vector<EditPadsCommand::ChannelMix> snapshotChannelMix(const ChainNodePath& gridPath) {
    std::vector<EditPadsCommand::ChannelMix> mixes;
    for (const auto& track : TrackManager::getInstance().getTracks())
        if (track.padLink && track.padLink->gridId == gridPath.getDeviceId())
            mixes.push_back({track.id, track.volume, track.pan, track.muted, track.soloed});
    return mixes;
}

void restoreChannelMix(const std::vector<EditPadsCommand::ChannelMix>& mixes) {
    auto& tm = TrackManager::getInstance();
    for (const auto& mix : mixes) {
        const auto* track = tm.getTrack(mix.trackId);
        if (track == nullptr)
            continue;
        if (track->volume != mix.volume)
            tm.setTrackVolume(mix.trackId, mix.volume);
        if (track->pan != mix.pan)
            tm.setTrackPan(mix.trackId, mix.pan);
        if (track->muted != mix.muted)
            tm.setTrackMuted(mix.trackId, mix.muted);
        if (track->soloed != mix.soloed)
            tm.setTrackSoloed(mix.trackId, mix.soloed);
    }
}

/// A pad's level in dB or its pan, from its channel once it has one.
float padFaderValue(const ChainNodePath& gridPath, int padIndex,
                    SetPadFaderCommand::Target target) {
    auto& tm = TrackManager::getInstance();
    const auto volume = target == SetPadFaderCommand::Target::Volume;
    if (const auto* channel = tm.getPadChannel(gridPath, padIndex))
        return volume ? juce::Decibels::gainToDecibels(channel->volume, -60.0f) : channel->pan;
    const auto* pad = tm.getPad(gridPath, padIndex);
    return pad == nullptr ? 0.0f : volume ? pad->volume : pad->pan;
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
        restoreChannelMix(mixAfter_);
        return;
    }

    if (!edit_)
        return;

    // Native pad snapshots do not yet have a child-instance capture path, so
    // this captures the grid plugin only.
    PluginService::getInstance().capturePluginStateAt(gridPath_);
    padsBefore_ = snapshotPads(gridPath_);
    mixBefore_ = snapshotChannelMix(gridPath_);

    edit_();

    padsAfter_ = snapshotPads(gridPath_);
    mixAfter_ = snapshotChannelMix(gridPath_);
    executed_ = true;
}

void EditPadsCommand::undo() {
    if (!executed_)
        return;

    TrackManager::getInstance().setPads(gridPath_, padsBefore_);
    restoreChannelMix(mixBefore_);
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
    if (TrackManager::getInstance().getPad(gridPath_, padIndex_) != nullptr) {
        oldValue_ = padFaderValue(gridPath_, padIndex_, target_);
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

}  // namespace magda
