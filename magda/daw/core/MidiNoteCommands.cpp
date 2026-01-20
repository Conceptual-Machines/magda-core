#include "MidiNoteCommands.hpp"

#include <algorithm>

namespace magda {

// ============================================================================
// AddMidiNoteCommand
// ============================================================================

AddMidiNoteCommand::AddMidiNoteCommand(ClipId clipId, double startBeat, int noteNumber,
                                       double lengthBeats, int velocity)
    : clipId_(clipId) {
    note_.startBeat = startBeat;
    note_.noteNumber = noteNumber;
    note_.lengthBeats = lengthBeats;
    note_.velocity = velocity;
}

void AddMidiNoteCommand::execute() {
    auto& clipManager = ClipManager::getInstance();
    auto* clip = clipManager.getClip(clipId_);

    if (!clip || clip->type != ClipType::MIDI) {
        return;
    }

    // Add note via ClipManager API
    clipManager.addMidiNote(clipId_, note_);

    // The note was added at the end, so its index is size - 1
    insertedIndex_ = clip->midiNotes.size() - 1;
    executed_ = true;
}

void AddMidiNoteCommand::undo() {
    if (!executed_) {
        return;
    }

    auto& clipManager = ClipManager::getInstance();
    clipManager.removeMidiNote(clipId_, static_cast<int>(insertedIndex_));
}

// ============================================================================
// MoveMidiNoteCommand
// ============================================================================

MoveMidiNoteCommand::MoveMidiNoteCommand(ClipId clipId, size_t noteIndex, double newStartBeat,
                                         int newNoteNumber)
    : clipId_(clipId),
      noteIndex_(noteIndex),
      newStartBeat_(newStartBeat),
      newNoteNumber_(newNoteNumber) {
    // Capture old values
    const auto* clip = ClipManager::getInstance().getClip(clipId_);
    if (clip && noteIndex_ < clip->midiNotes.size()) {
        oldStartBeat_ = clip->midiNotes[noteIndex_].startBeat;
        oldNoteNumber_ = clip->midiNotes[noteIndex_].noteNumber;
    }
}

void MoveMidiNoteCommand::execute() {
    auto& clipManager = ClipManager::getInstance();
    auto* clip = clipManager.getClip(clipId_);

    if (!clip || clip->type != ClipType::MIDI || noteIndex_ >= clip->midiNotes.size()) {
        return;
    }

    clip->midiNotes[noteIndex_].startBeat = newStartBeat_;
    clip->midiNotes[noteIndex_].noteNumber = newNoteNumber_;

    clipManager.forceNotifyClipsChanged();
    executed_ = true;
}

void MoveMidiNoteCommand::undo() {
    if (!executed_) {
        return;
    }

    auto& clipManager = ClipManager::getInstance();
    auto* clip = clipManager.getClip(clipId_);

    if (!clip || clip->type != ClipType::MIDI || noteIndex_ >= clip->midiNotes.size()) {
        return;
    }

    clip->midiNotes[noteIndex_].startBeat = oldStartBeat_;
    clip->midiNotes[noteIndex_].noteNumber = oldNoteNumber_;

    clipManager.forceNotifyClipsChanged();
}

bool MoveMidiNoteCommand::canMergeWith(const UndoableCommand* other) const {
    auto* otherMove = dynamic_cast<const MoveMidiNoteCommand*>(other);
    return otherMove && otherMove->clipId_ == clipId_ && otherMove->noteIndex_ == noteIndex_;
}

void MoveMidiNoteCommand::mergeWith(const UndoableCommand* other) {
    auto* otherMove = dynamic_cast<const MoveMidiNoteCommand*>(other);
    if (otherMove) {
        newStartBeat_ = otherMove->newStartBeat_;
        newNoteNumber_ = otherMove->newNoteNumber_;
    }
}

// ============================================================================
// ResizeMidiNoteCommand
// ============================================================================

ResizeMidiNoteCommand::ResizeMidiNoteCommand(ClipId clipId, size_t noteIndex, double newLengthBeats)
    : clipId_(clipId), noteIndex_(noteIndex), newLengthBeats_(newLengthBeats) {
    // Capture old value
    const auto* clip = ClipManager::getInstance().getClip(clipId_);
    if (clip && noteIndex_ < clip->midiNotes.size()) {
        oldLengthBeats_ = clip->midiNotes[noteIndex_].lengthBeats;
    }
}

void ResizeMidiNoteCommand::execute() {
    auto& clipManager = ClipManager::getInstance();
    auto* clip = clipManager.getClip(clipId_);

    if (!clip || clip->type != ClipType::MIDI || noteIndex_ >= clip->midiNotes.size()) {
        return;
    }

    clip->midiNotes[noteIndex_].lengthBeats = newLengthBeats_;

    clipManager.forceNotifyClipsChanged();
    executed_ = true;
}

void ResizeMidiNoteCommand::undo() {
    if (!executed_) {
        return;
    }

    auto& clipManager = ClipManager::getInstance();
    auto* clip = clipManager.getClip(clipId_);

    if (!clip || clip->type != ClipType::MIDI || noteIndex_ >= clip->midiNotes.size()) {
        return;
    }

    clip->midiNotes[noteIndex_].lengthBeats = oldLengthBeats_;

    clipManager.forceNotifyClipsChanged();
}

bool ResizeMidiNoteCommand::canMergeWith(const UndoableCommand* other) const {
    auto* otherResize = dynamic_cast<const ResizeMidiNoteCommand*>(other);
    return otherResize && otherResize->clipId_ == clipId_ && otherResize->noteIndex_ == noteIndex_;
}

void ResizeMidiNoteCommand::mergeWith(const UndoableCommand* other) {
    auto* otherResize = dynamic_cast<const ResizeMidiNoteCommand*>(other);
    if (otherResize) {
        newLengthBeats_ = otherResize->newLengthBeats_;
    }
}

// ============================================================================
// DeleteMidiNoteCommand
// ============================================================================

DeleteMidiNoteCommand::DeleteMidiNoteCommand(ClipId clipId, size_t noteIndex)
    : clipId_(clipId), noteIndex_(noteIndex) {
    // Capture note data for undo
    const auto* clip = ClipManager::getInstance().getClip(clipId_);
    if (clip && noteIndex_ < clip->midiNotes.size()) {
        deletedNote_ = clip->midiNotes[noteIndex_];
    }
}

void DeleteMidiNoteCommand::execute() {
    auto& clipManager = ClipManager::getInstance();
    clipManager.removeMidiNote(clipId_, static_cast<int>(noteIndex_));
    executed_ = true;
}

void DeleteMidiNoteCommand::undo() {
    if (!executed_) {
        return;
    }

    auto& clipManager = ClipManager::getInstance();
    auto* clip = clipManager.getClip(clipId_);

    if (!clip || clip->type != ClipType::MIDI) {
        return;
    }

    // Re-insert at original position (or at end if index is now out of range)
    size_t insertPos = std::min(noteIndex_, clip->midiNotes.size());
    clip->midiNotes.insert(clip->midiNotes.begin() + static_cast<std::ptrdiff_t>(insertPos),
                           deletedNote_);

    clipManager.forceNotifyClipsChanged();
}

// ============================================================================
// SetMidiNoteVelocityCommand
// ============================================================================

SetMidiNoteVelocityCommand::SetMidiNoteVelocityCommand(ClipId clipId, size_t noteIndex,
                                                       int newVelocity)
    : clipId_(clipId), noteIndex_(noteIndex), newVelocity_(newVelocity) {
    // Capture old value
    const auto* clip = ClipManager::getInstance().getClip(clipId_);
    if (clip && noteIndex_ < clip->midiNotes.size()) {
        oldVelocity_ = clip->midiNotes[noteIndex_].velocity;
    }
}

void SetMidiNoteVelocityCommand::execute() {
    auto& clipManager = ClipManager::getInstance();
    auto* clip = clipManager.getClip(clipId_);

    if (!clip || clip->type != ClipType::MIDI || noteIndex_ >= clip->midiNotes.size()) {
        return;
    }

    clip->midiNotes[noteIndex_].velocity = newVelocity_;

    clipManager.forceNotifyClipsChanged();
    executed_ = true;
}

void SetMidiNoteVelocityCommand::undo() {
    if (!executed_) {
        return;
    }

    auto& clipManager = ClipManager::getInstance();
    auto* clip = clipManager.getClip(clipId_);

    if (!clip || clip->type != ClipType::MIDI || noteIndex_ >= clip->midiNotes.size()) {
        return;
    }

    clip->midiNotes[noteIndex_].velocity = oldVelocity_;

    clipManager.forceNotifyClipsChanged();
}

bool SetMidiNoteVelocityCommand::canMergeWith(const UndoableCommand* other) const {
    auto* otherVelocity = dynamic_cast<const SetMidiNoteVelocityCommand*>(other);
    return otherVelocity && otherVelocity->clipId_ == clipId_ &&
           otherVelocity->noteIndex_ == noteIndex_;
}

void SetMidiNoteVelocityCommand::mergeWith(const UndoableCommand* other) {
    auto* otherVelocity = dynamic_cast<const SetMidiNoteVelocityCommand*>(other);
    if (otherVelocity) {
        newVelocity_ = otherVelocity->newVelocity_;
    }
}

}  // namespace magda
