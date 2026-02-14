#include "MidiNoteCommands.hpp"

#include <algorithm>
#include <cmath>

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

    clipManager.forceNotifyClipPropertyChanged(clipId_);
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

    clipManager.forceNotifyClipPropertyChanged(clipId_);
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

    clipManager.forceNotifyClipPropertyChanged(clipId_);
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

    clipManager.forceNotifyClipPropertyChanged(clipId_);
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

    clipManager.forceNotifyClipPropertyChanged(clipId_);
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

    clipManager.forceNotifyClipPropertyChanged(clipId_);
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

    clipManager.forceNotifyClipPropertyChanged(clipId_);
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

// ============================================================================
// SetMultipleNoteVelocitiesCommand
// ============================================================================

SetMultipleNoteVelocitiesCommand::SetMultipleNoteVelocitiesCommand(
    ClipId clipId, std::vector<std::pair<size_t, int>> noteVelocities)
    : clipId_(clipId), newVelocities_(std::move(noteVelocities)) {}

void SetMultipleNoteVelocitiesCommand::execute() {
    auto& clipManager = ClipManager::getInstance();
    auto* clip = clipManager.getClip(clipId_);

    if (!clip || clip->type != ClipType::MIDI) {
        return;
    }

    // Capture old velocities on first execute
    if (!executed_) {
        oldVelocities_.clear();
        oldVelocities_.reserve(newVelocities_.size());
        for (const auto& [index, newVel] : newVelocities_) {
            if (index < clip->midiNotes.size()) {
                oldVelocities_.emplace_back(index, clip->midiNotes[index].velocity);
            }
        }
    }

    // Apply new velocities
    for (const auto& [index, newVel] : newVelocities_) {
        if (index < clip->midiNotes.size()) {
            clip->midiNotes[index].velocity = newVel;
        }
    }

    clipManager.forceNotifyClipPropertyChanged(clipId_);
    executed_ = true;
}

void SetMultipleNoteVelocitiesCommand::undo() {
    if (!executed_) {
        return;
    }

    auto& clipManager = ClipManager::getInstance();
    auto* clip = clipManager.getClip(clipId_);

    if (!clip || clip->type != ClipType::MIDI) {
        return;
    }

    // Restore old velocities
    for (const auto& [index, oldVel] : oldVelocities_) {
        if (index < clip->midiNotes.size()) {
            clip->midiNotes[index].velocity = oldVel;
        }
    }

    clipManager.forceNotifyClipPropertyChanged(clipId_);
}

// ============================================================================
// MoveMidiNoteBetweenClipsCommand
// ============================================================================

MoveMidiNoteBetweenClipsCommand::MoveMidiNoteBetweenClipsCommand(ClipId sourceClipId,
                                                                 size_t noteIndex,
                                                                 ClipId destClipId,
                                                                 double newStartBeat,
                                                                 int newNoteNumber)
    : sourceClipId_(sourceClipId),
      destClipId_(destClipId),
      sourceNoteIndex_(noteIndex),
      newStartBeat_(newStartBeat),
      newNoteNumber_(newNoteNumber) {
    // Capture the note being moved
    const auto* sourceClip = ClipManager::getInstance().getClip(sourceClipId_);
    if (sourceClip && sourceNoteIndex_ < sourceClip->midiNotes.size()) {
        movedNote_ = sourceClip->midiNotes[sourceNoteIndex_];
    }
}

void MoveMidiNoteBetweenClipsCommand::execute() {
    auto& clipManager = ClipManager::getInstance();

    // Get source clip
    auto* sourceClip = clipManager.getClip(sourceClipId_);
    if (!sourceClip || sourceClip->type != ClipType::MIDI ||
        sourceNoteIndex_ >= sourceClip->midiNotes.size()) {
        DBG("MoveMidiNoteBetweenClipsCommand::execute() - validation failed");
        return;
    }

    // Get destination clip
    auto* destClip = clipManager.getClip(destClipId_);
    if (!destClip || destClip->type != ClipType::MIDI) {
        DBG("MoveMidiNoteBetweenClipsCommand::execute() - dest clip validation failed");
        return;
    }

    DBG("MoveMidiNoteBetweenClipsCommand::execute() - moving note from clip "
        << sourceClipId_ << " (index " << sourceNoteIndex_ << ") to clip " << destClipId_);
    DBG("  Source clip has " << sourceClip->midiNotes.size() << " notes before removal");

    // Create new note for destination clip
    MidiNote newNote = movedNote_;
    newNote.startBeat = newStartBeat_;
    newNote.noteNumber = newNoteNumber_;

    // Remove from source clip
    clipManager.removeMidiNote(sourceClipId_, static_cast<int>(sourceNoteIndex_));
    DBG("  Source clip has " << sourceClip->midiNotes.size() << " notes after removal");

    // Add to destination clip
    clipManager.addMidiNote(destClipId_, newNote);
    destNoteIndex_ = destClip->midiNotes.size() - 1;
    DBG("  Dest clip now has " << destClip->midiNotes.size() << " notes");

    executed_ = true;
}

void MoveMidiNoteBetweenClipsCommand::undo() {
    if (!executed_) {
        return;
    }

    auto& clipManager = ClipManager::getInstance();

    // Remove from destination clip
    clipManager.removeMidiNote(destClipId_, static_cast<int>(destNoteIndex_));

    // Re-add to source clip at original position
    auto* sourceClip = clipManager.getClip(sourceClipId_);
    if (!sourceClip || sourceClip->type != ClipType::MIDI) {
        return;
    }

    size_t insertPos = std::min(sourceNoteIndex_, sourceClip->midiNotes.size());
    sourceClip->midiNotes.insert(
        sourceClip->midiNotes.begin() + static_cast<std::ptrdiff_t>(insertPos), movedNote_);

    clipManager.forceNotifyClipPropertyChanged(sourceClipId_);
}

// ============================================================================
// QuantizeMidiNotesCommand
// ============================================================================

QuantizeMidiNotesCommand::QuantizeMidiNotesCommand(ClipId clipId, std::vector<size_t> noteIndices,
                                                   double gridResolution, QuantizeMode mode)
    : clipId_(clipId),
      noteIndices_(std::move(noteIndices)),
      gridResolution_(gridResolution),
      mode_(mode) {}

void QuantizeMidiNotesCommand::execute() {
    auto& clipManager = ClipManager::getInstance();
    auto* clip = clipManager.getClip(clipId_);

    if (!clip || clip->type != ClipType::MIDI) {
        return;
    }

    // Capture old values on first execute
    if (!executed_) {
        oldValues_.clear();
        oldValues_.reserve(noteIndices_.size());
        for (size_t index : noteIndices_) {
            if (index < clip->midiNotes.size()) {
                oldValues_.push_back(
                    {clip->midiNotes[index].startBeat, clip->midiNotes[index].lengthBeats});
            }
        }
    }

    // Apply quantization
    for (size_t i = 0; i < noteIndices_.size(); ++i) {
        size_t index = noteIndices_[i];
        if (index >= clip->midiNotes.size()) {
            continue;
        }

        auto& note = clip->midiNotes[index];

        if (mode_ == QuantizeMode::StartOnly || mode_ == QuantizeMode::StartAndLength) {
            note.startBeat = std::round(note.startBeat / gridResolution_) * gridResolution_;
        }

        if (mode_ == QuantizeMode::LengthOnly || mode_ == QuantizeMode::StartAndLength) {
            double quantizedLength =
                std::round(note.lengthBeats / gridResolution_) * gridResolution_;
            note.lengthBeats = juce::jmax(gridResolution_, quantizedLength);
        }
    }

    clipManager.forceNotifyClipPropertyChanged(clipId_);
    executed_ = true;
}

void QuantizeMidiNotesCommand::undo() {
    if (!executed_) {
        return;
    }

    auto& clipManager = ClipManager::getInstance();
    auto* clip = clipManager.getClip(clipId_);

    if (!clip || clip->type != ClipType::MIDI) {
        return;
    }

    // Restore old values
    for (size_t i = 0; i < noteIndices_.size() && i < oldValues_.size(); ++i) {
        size_t index = noteIndices_[i];
        if (index < clip->midiNotes.size()) {
            clip->midiNotes[index].startBeat = oldValues_[i].startBeat;
            clip->midiNotes[index].lengthBeats = oldValues_[i].lengthBeats;
        }
    }

    clipManager.forceNotifyClipPropertyChanged(clipId_);
}

}  // namespace magda
