#include "MidiNoteCommands.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

#include "ClipOperations.hpp"
#include "audio/transport/StepClock.hpp"

namespace magda {

std::vector<MidiNoteStartBeat> collectMidiNoteStartBeats(const ClipInfo& clip,
                                                         const std::vector<size_t>& noteIndices) {
    std::vector<MidiNoteStartBeat> starts;
    if (!clip.isMidi())
        return starts;

    starts.reserve(noteIndices.size());
    for (size_t index : noteIndices) {
        if (index < clip.midiNotes.size())
            starts.push_back({index, clip.midiNotes[index].startBeat});
    }
    return starts;
}

std::vector<MidiNoteStartBeat> calculateBentMidiNoteStartBeats(
    const ClipInfo& clip, const std::vector<MidiNoteStartBeat>& originalStartBeats, float depth,
    float skew, int cycles, float quantize, int quantizeSub, bool hardAngle) {
    std::vector<MidiNoteStartBeat> validStarts;
    if (!clip.isMidi())
        return validStarts;

    validStarts.reserve(originalStartBeats.size());
    for (const auto& original : originalStartBeats) {
        if (original.noteIndex < clip.midiNotes.size())
            validStarts.push_back(original);
    }

    if (validStarts.size() < 2)
        return {};

    // Order the selection as a sequence of events: by onset, then by pitch so a
    // chord strums low -> high. Each note then samples the curve at its own
    // ordinal slot, so notes stacked on one onset (a chord) fan out in time
    // instead of moving as a rigid block.
    std::sort(validStarts.begin(), validStarts.end(),
              [&clip](const MidiNoteStartBeat& a, const MidiNoteStartBeat& b) {
                  if (a.startBeat != b.startBeat)
                      return a.startBeat < b.startBeat;
                  return clip.midiNotes[a.noteIndex].noteNumber <
                         clip.midiNotes[b.noteIndex].noteNumber;
              });

    const double minBeat = validStarts.front().startBeat;
    const double maxBeat = validStarts.back().startBeat;
    double span = maxBeat - minBeat;

    // Lone chord (all notes share an onset): no grid to warp, so give the strum
    // a window to spread into, derived from the shortest selected note so it
    // scales with the part rather than a fixed amount.
    if (span < 1e-9) {
        double minLen = std::numeric_limits<double>::max();
        for (const auto& start : validStarts)
            minLen = std::min(minLen, clip.midiNotes[start.noteIndex].lengthBeats);
        if (minLen >= std::numeric_limits<double>::max() || minLen < 1e-9)
            return {};
        span = minLen;
    }

    const int noteCount = static_cast<int>(validStarts.size());
    const double ordinalDenom = static_cast<double>(noteCount - 1);

    std::vector<MidiNoteStartBeat> bentStarts;
    bentStarts.reserve(validStarts.size());
    for (int i = 0; i < noteCount; ++i) {
        const auto& start = validStarts[static_cast<size_t>(i)];
        const double t = static_cast<double>(i) / ordinalDenom;
        const double tEased =
            daw::audio::StepClock::applyRampCurveWithCycles(t, depth, skew, cycles, hardAngle);
        // Shift each note by how far the curve pushes its ordinal slot, anchored
        // on its true onset. Depth 0 -> tEased == t -> identity; a uniform mono
        // line reduces to minBeat + curve(t) * span (the prior behaviour).
        double newBeat = start.startBeat + (tEased - t) * span;

        if (quantize > 0.0f && quantizeSub > 0) {
            const double gridSpacing = span / static_cast<double>(quantizeSub);
            const double snapped =
                std::round((newBeat - minBeat) / gridSpacing) * gridSpacing + minBeat;
            newBeat += (snapped - newBeat) * static_cast<double>(quantize);
        }

        auto constrainedNote = clip.midiNotes[start.noteIndex];
        constrainedNote.startBeat = newBeat;
        if (ClipOperations::constrainMidiNoteToVisibleRange(clip, constrainedNote))
            newBeat = constrainedNote.startBeat;

        bentStarts.push_back({start.noteIndex, newBeat});
    }

    return bentStarts;
}

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

    if (!clip) {
        DBG("AddMidiNoteCommand: clip " + juce::String(clipId_) + " NOT FOUND — note dropped");
        return;
    }
    if (!clip->isMidi()) {
        DBG("AddMidiNoteCommand: clip " + juce::String(clipId_) +
            " type=" + juce::String(static_cast<int>(clip->getType())) + " != MIDI — note dropped");
        return;
    }

    size_t oldSize = clip->midiNotes.size();
    if (!clipManager.addMidiNote(clipId_, note_) || clip->midiNotes.size() <= oldSize)
        return;

    insertedIndex_ = oldSize;
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

    if (!clip || !clip->isMidi() || noteIndex_ >= clip->midiNotes.size()) {
        return;
    }

    auto movedNote = clip->midiNotes[noteIndex_];
    movedNote.startBeat = newStartBeat_;
    movedNote.noteNumber = newNoteNumber_;
    if (!ClipOperations::constrainMidiNoteToVisibleRange(*clip, movedNote))
        return;

    clip->midiNotes[noteIndex_] = movedNote;

    clipManager.forceNotifyClipPropertyChanged(clipId_);
    executed_ = true;
}

void MoveMidiNoteCommand::undo() {
    if (!executed_) {
        return;
    }

    auto& clipManager = ClipManager::getInstance();
    auto* clip = clipManager.getClip(clipId_);

    if (!clip || !clip->isMidi() || noteIndex_ >= clip->midiNotes.size()) {
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

    if (!clip || !clip->isMidi() || noteIndex_ >= clip->midiNotes.size()) {
        return;
    }

    auto resizedNote = clip->midiNotes[noteIndex_];
    resizedNote.lengthBeats = newLengthBeats_;
    if (!ClipOperations::constrainMidiNoteToVisibleRange(*clip, resizedNote))
        return;

    clip->midiNotes[noteIndex_] = resizedNote;

    clipManager.forceNotifyClipPropertyChanged(clipId_);
    executed_ = true;
}

void ResizeMidiNoteCommand::undo() {
    if (!executed_) {
        return;
    }

    auto& clipManager = ClipManager::getInstance();
    auto* clip = clipManager.getClip(clipId_);

    if (!clip || !clip->isMidi() || noteIndex_ >= clip->midiNotes.size()) {
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
    auto* clip = clipManager.getClip(clipId_);
    if (!clip || !clip->isMidi() || noteIndex_ >= clip->midiNotes.size()) {
        return;
    }

    deletedNote_ = clip->midiNotes[noteIndex_];
    const auto oldSize = clip->midiNotes.size();
    clipManager.removeMidiNote(clipId_, static_cast<int>(noteIndex_));
    executed_ = clip->midiNotes.size() + 1 == oldSize;
}

void DeleteMidiNoteCommand::undo() {
    if (!executed_) {
        return;
    }

    auto& clipManager = ClipManager::getInstance();
    auto* clip = clipManager.getClip(clipId_);

    if (!clip || !clip->isMidi()) {
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

    if (!clip || !clip->isMidi() || noteIndex_ >= clip->midiNotes.size()) {
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

    if (!clip || !clip->isMidi() || noteIndex_ >= clip->midiNotes.size()) {
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
// SetNotePitchExpressionCommand
// ============================================================================

SetNotePitchExpressionCommand::SetNotePitchExpressionCommand(
    ClipId clipId, size_t noteIndex, std::vector<MidiPitchExpressionPoint> newPoints)
    : clipId_(clipId), noteIndex_(noteIndex), newPoints_(std::move(newPoints)) {}

void SetNotePitchExpressionCommand::execute() {
    auto& clipManager = ClipManager::getInstance();
    auto* clip = clipManager.getClip(clipId_);
    if (!clip || !clip->isMidi() || noteIndex_ >= clip->midiNotes.size())
        return;

    if (!executed_)
        oldPoints_ = clip->midiNotes[noteIndex_].pitchExpression;

    clipManager.setMidiNotePitchExpression(clipId_, noteIndex_, newPoints_);
    executed_ = true;
}

void SetNotePitchExpressionCommand::undo() {
    if (!executed_)
        return;

    ClipManager::getInstance().setMidiNotePitchExpression(clipId_, noteIndex_, oldPoints_);
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

    if (!clip || !clip->isMidi()) {
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

    if (!clip || !clip->isMidi()) {
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
// MoveMultipleMidiNotesCommand
// ============================================================================

MoveMultipleMidiNotesCommand::MoveMultipleMidiNotesCommand(ClipId clipId,
                                                           std::vector<NoteMove> moves)
    : clipId_(clipId), moves_(std::move(moves)) {}

void MoveMultipleMidiNotesCommand::execute() {
    auto& clipManager = ClipManager::getInstance();
    auto* clip = clipManager.getClip(clipId_);

    if (!clip || !clip->isMidi()) {
        return;
    }

    // Capture old values on first execute
    if (!executed_) {
        oldValues_.clear();
        oldValues_.reserve(moves_.size());
        for (const auto& move : moves_) {
            if (move.noteIndex < clip->midiNotes.size()) {
                oldValues_.push_back({move.noteIndex, clip->midiNotes[move.noteIndex].startBeat,
                                      clip->midiNotes[move.noteIndex].noteNumber});
            }
        }
    }

    // Apply moves
    for (const auto& move : moves_) {
        if (move.noteIndex < clip->midiNotes.size()) {
            auto movedNote = clip->midiNotes[move.noteIndex];
            movedNote.startBeat = move.newStartBeat;
            movedNote.noteNumber = move.newNoteNumber;
            if (ClipOperations::constrainMidiNoteToVisibleRange(*clip, movedNote))
                clip->midiNotes[move.noteIndex] = movedNote;
        }
    }

    clipManager.forceNotifyClipPropertyChanged(clipId_);
    executed_ = true;
}

void MoveMultipleMidiNotesCommand::undo() {
    if (!executed_) {
        return;
    }

    auto& clipManager = ClipManager::getInstance();
    auto* clip = clipManager.getClip(clipId_);

    if (!clip || !clip->isMidi()) {
        return;
    }

    // Restore old values
    for (const auto& oldValue : oldValues_) {
        size_t index = oldValue.noteIndex;
        if (index < clip->midiNotes.size()) {
            clip->midiNotes[index].startBeat = oldValue.startBeat;
            clip->midiNotes[index].noteNumber = oldValue.noteNumber;
        }
    }

    clipManager.forceNotifyClipPropertyChanged(clipId_);
}

// ============================================================================
// ResizeMultipleMidiNotesCommand
// ============================================================================

ResizeMultipleMidiNotesCommand::ResizeMultipleMidiNotesCommand(
    ClipId clipId, std::vector<std::pair<size_t, double>> noteLengths)
    : clipId_(clipId), newLengths_(std::move(noteLengths)) {}

void ResizeMultipleMidiNotesCommand::execute() {
    auto& clipManager = ClipManager::getInstance();
    auto* clip = clipManager.getClip(clipId_);

    if (!clip || !clip->isMidi()) {
        return;
    }

    // Capture old lengths on first execute
    if (!executed_) {
        oldLengths_.clear();
        oldLengths_.reserve(newLengths_.size());
        for (const auto& [index, newLen] : newLengths_) {
            if (index < clip->midiNotes.size()) {
                oldLengths_.emplace_back(index, clip->midiNotes[index].lengthBeats);
            }
        }
    }

    // Apply new lengths
    for (const auto& [index, newLen] : newLengths_) {
        if (index < clip->midiNotes.size()) {
            auto resizedNote = clip->midiNotes[index];
            resizedNote.lengthBeats = newLen;
            if (ClipOperations::constrainMidiNoteToVisibleRange(*clip, resizedNote))
                clip->midiNotes[index] = resizedNote;
        }
    }

    clipManager.forceNotifyClipPropertyChanged(clipId_);
    executed_ = true;
}

void ResizeMultipleMidiNotesCommand::undo() {
    if (!executed_) {
        return;
    }

    auto& clipManager = ClipManager::getInstance();
    auto* clip = clipManager.getClip(clipId_);

    if (!clip || !clip->isMidi()) {
        return;
    }

    // Restore old lengths
    for (const auto& [index, oldLen] : oldLengths_) {
        if (index < clip->midiNotes.size()) {
            clip->midiNotes[index].lengthBeats = oldLen;
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
    if (!sourceClip || !sourceClip->isMidi() || sourceNoteIndex_ >= sourceClip->midiNotes.size()) {
        DBG("MoveMidiNoteBetweenClipsCommand::execute() - validation failed");
        return;
    }

    // Get destination clip
    auto* destClip = clipManager.getClip(destClipId_);
    if (!destClip || !destClip->isMidi()) {
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
    if (!ClipOperations::constrainMidiNoteToVisibleRange(*destClip, newNote))
        return;

    size_t oldDestSize = destClip->midiNotes.size();
    if (!clipManager.addMidiNote(destClipId_, newNote) || destClip->midiNotes.size() <= oldDestSize)
        return;
    destNoteIndex_ = destClip->midiNotes.size() - 1;
    DBG("  Dest clip now has " << destClip->midiNotes.size() << " notes");

    // Remove from source clip only after destination insertion succeeded.
    clipManager.removeMidiNote(sourceClipId_, static_cast<int>(sourceNoteIndex_));
    DBG("  Source clip has " << sourceClip->midiNotes.size() << " notes after removal");

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
    if (!sourceClip || !sourceClip->isMidi()) {
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

    if (!clip || !clip->isMidi()) {
        return;
    }

    // Capture old values on first execute
    if (!executed_) {
        oldValues_.clear();
        oldValues_.reserve(noteIndices_.size());
        for (size_t index : noteIndices_) {
            if (index < clip->midiNotes.size()) {
                oldValues_.push_back(
                    {index, clip->midiNotes[index].startBeat, clip->midiNotes[index].lengthBeats});
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

    if (!clip || !clip->isMidi()) {
        return;
    }

    // Restore old values
    for (const auto& oldValue : oldValues_) {
        size_t index = oldValue.noteIndex;
        if (index < clip->midiNotes.size()) {
            clip->midiNotes[index].startBeat = oldValue.startBeat;
            clip->midiNotes[index].lengthBeats = oldValue.lengthBeats;
        }
    }

    clipManager.forceNotifyClipPropertyChanged(clipId_);
}

// ============================================================================
// DeleteMultipleMidiNotesCommand
// ============================================================================

DeleteMultipleMidiNotesCommand::DeleteMultipleMidiNotesCommand(ClipId clipId,
                                                               std::vector<size_t> noteIndices)
    : clipId_(clipId), noteIndices_(std::move(noteIndices)) {
    // Sort descending so we remove from the end first (avoids index shifting)
    std::sort(noteIndices_.begin(), noteIndices_.end(), std::greater<size_t>());

    // Capture note data for undo
    const auto* clip = ClipManager::getInstance().getClip(clipId_);
    if (clip && clip->isMidi()) {
        for (size_t idx : noteIndices_) {
            if (idx < clip->midiNotes.size()) {
                deleted_.emplace_back(idx, clip->midiNotes[idx]);
            }
        }
    }
}

void DeleteMultipleMidiNotesCommand::execute() {
    auto& clipManager = ClipManager::getInstance();
    auto* clip = clipManager.getClip(clipId_);

    if (!clip || !clip->isMidi()) {
        return;
    }

    // Remove in descending index order
    for (size_t idx : noteIndices_) {
        if (idx < clip->midiNotes.size()) {
            clip->midiNotes.erase(clip->midiNotes.begin() + static_cast<std::ptrdiff_t>(idx));
        }
    }

    clipManager.forceNotifyClipPropertyChanged(clipId_);
    executed_ = true;
}

void DeleteMultipleMidiNotesCommand::undo() {
    if (!executed_) {
        return;
    }

    auto& clipManager = ClipManager::getInstance();
    auto* clip = clipManager.getClip(clipId_);

    if (!clip || !clip->isMidi()) {
        return;
    }

    // Re-insert in reverse order (ascending index) to restore original positions
    for (auto it = deleted_.rbegin(); it != deleted_.rend(); ++it) {
        size_t insertPos = std::min(it->first, clip->midiNotes.size());
        clip->midiNotes.insert(clip->midiNotes.begin() + static_cast<std::ptrdiff_t>(insertPos),
                               it->second);
    }

    clipManager.forceNotifyClipPropertyChanged(clipId_);
}

// ============================================================================
// AddMultipleMidiNotesCommand
// ============================================================================

AddMultipleMidiNotesCommand::AddMultipleMidiNotesCommand(ClipId clipId, std::vector<MidiNote> notes,
                                                         juce::String description)
    : clipId_(clipId), notes_(std::move(notes)), description_(std::move(description)) {}

void AddMultipleMidiNotesCommand::execute() {
    auto& clipManager = ClipManager::getInstance();
    auto* clip = clipManager.getClip(clipId_);

    if (!clip || !clip->isMidi()) {
        return;
    }

    insertedIndices_.clear();
    for (const auto& note : notes_) {
        auto clippedNote = note;
        if (!ClipOperations::clipMidiNoteToVisibleRange(*clip, clippedNote))
            continue;

        size_t idx = clip->midiNotes.size();
        clip->midiNotes.push_back(clippedNote);
        insertedIndices_.push_back(idx);
    }

    if (insertedIndices_.empty())
        return;

    clipManager.forceNotifyClipPropertyChanged(clipId_);
    executed_ = true;
}

void AddMultipleMidiNotesCommand::undo() {
    if (!executed_) {
        return;
    }

    auto& clipManager = ClipManager::getInstance();
    auto* clip = clipManager.getClip(clipId_);

    if (!clip || !clip->isMidi()) {
        return;
    }

    // Remove in descending index order
    std::vector<size_t> sorted = insertedIndices_;
    std::sort(sorted.begin(), sorted.end(), std::greater<size_t>());
    for (size_t idx : sorted) {
        if (idx < clip->midiNotes.size()) {
            clip->midiNotes.erase(clip->midiNotes.begin() + static_cast<std::ptrdiff_t>(idx));
        }
    }

    clipManager.forceNotifyClipPropertyChanged(clipId_);
}

// ============================================================================
// SliceMidiNotesCommand
// ============================================================================

SliceMidiNotesCommand::SliceMidiNotesCommand(ClipId clipId, std::vector<size_t> noteIndices,
                                             int subdivisions)
    : clipId_(clipId),
      noteIndices_(std::move(noteIndices)),
      subdivisions_(std::max(2, subdivisions)) {}

void SliceMidiNotesCommand::execute() {
    auto& clipManager = ClipManager::getInstance();
    auto* clip = clipManager.getClip(clipId_);

    if (!clip || !clip->isMidi() || noteIndices_.empty()) {
        return;
    }

    if (!executed_)
        originalNotes_ = clip->midiNotes;

    std::sort(noteIndices_.begin(), noteIndices_.end());
    noteIndices_.erase(std::unique(noteIndices_.begin(), noteIndices_.end()), noteIndices_.end());

    std::vector<MidiNote> slicedNotes;
    slicedNotes.reserve(originalNotes_.size() * static_cast<size_t>(subdivisions_));
    slicedNoteIndices_.clear();
    slicedNoteIndices_.reserve(noteIndices_.size() * static_cast<size_t>(subdivisions_));

    for (size_t index = 0; index < originalNotes_.size(); ++index) {
        const auto& note = originalNotes_[index];
        if (!std::binary_search(noteIndices_.begin(), noteIndices_.end(), index) ||
            note.lengthBeats <= 0.0) {
            slicedNotes.push_back(note);
            continue;
        }

        const double sliceLength = note.lengthBeats / static_cast<double>(subdivisions_);
        for (int slice = 0; slice < subdivisions_; ++slice) {
            auto sliced = note;
            sliced.startBeat = note.startBeat + sliceLength * static_cast<double>(slice);
            sliced.lengthBeats = sliceLength;
            slicedNoteIndices_.push_back(slicedNotes.size());
            slicedNotes.push_back(sliced);
        }
    }

    clip->midiNotes = std::move(slicedNotes);
    clipManager.forceNotifyClipPropertyChanged(clipId_);
    executed_ = true;
}

void SliceMidiNotesCommand::undo() {
    if (!executed_) {
        return;
    }

    auto& clipManager = ClipManager::getInstance();
    auto* clip = clipManager.getClip(clipId_);

    if (!clip || !clip->isMidi()) {
        return;
    }

    clip->midiNotes = originalNotes_;
    slicedNoteIndices_.clear();
    clipManager.forceNotifyClipPropertyChanged(clipId_);
}

// ============================================================================
// TransposeMidiClipCommand
// ============================================================================

TransposeMidiClipCommand::TransposeMidiClipCommand(ClipId clipId, int semitones)
    : clipId_(clipId), semitones_(semitones) {}

void TransposeMidiClipCommand::execute() {
    auto& clipManager = ClipManager::getInstance();
    auto* clip = clipManager.getClip(clipId_);

    if (!clip || !clip->isMidi() || clip->midiNotes.empty()) {
        return;
    }

    // Capture old note numbers on first execute
    if (!executed_) {
        oldNoteNumbers_.clear();
        oldNoteNumbers_.reserve(clip->midiNotes.size());
        for (const auto& note : clip->midiNotes) {
            oldNoteNumbers_.push_back(note.noteNumber);
        }
    }

    // Apply transpose, clamping to MIDI range
    for (auto& note : clip->midiNotes) {
        note.noteNumber = juce::jlimit(0, 127, note.noteNumber + semitones_);
    }

    clipManager.forceNotifyClipPropertyChanged(clipId_);
    executed_ = true;
}

void TransposeMidiClipCommand::undo() {
    if (!executed_) {
        return;
    }

    auto& clipManager = ClipManager::getInstance();
    auto* clip = clipManager.getClip(clipId_);

    if (!clip || !clip->isMidi()) {
        return;
    }

    // Restore old note numbers
    for (size_t i = 0; i < oldNoteNumbers_.size() && i < clip->midiNotes.size(); ++i) {
        clip->midiNotes[i].noteNumber = oldNoteNumbers_[i];
    }

    clipManager.forceNotifyClipPropertyChanged(clipId_);
}

bool TransposeMidiClipCommand::canMergeWith(const UndoableCommand* other) const {
    auto* otherTranspose = dynamic_cast<const TransposeMidiClipCommand*>(other);
    return otherTranspose && otherTranspose->clipId_ == clipId_;
}

void TransposeMidiClipCommand::mergeWith(const UndoableCommand* other) {
    auto* otherTranspose = dynamic_cast<const TransposeMidiClipCommand*>(other);
    if (otherTranspose) {
        semitones_ += otherTranspose->semitones_;
    }
}

// ============================================================================
// AddMidiCCEventCommand
// ============================================================================

AddMidiCCEventCommand::AddMidiCCEventCommand(ClipId clipId, MidiCCData event)
    : clipId_(clipId), event_(event) {}

void AddMidiCCEventCommand::execute() {
    auto& clipManager = ClipManager::getInstance();
    auto* clip = clipManager.getClip(clipId_);
    if (!clip || !clip->isMidi())
        return;

    clip->midiCCData.push_back(event_);
    clipManager.forceNotifyClipPropertyChanged(clipId_);
    executed_ = true;
}

void AddMidiCCEventCommand::undo() {
    if (!executed_)
        return;
    auto& clipManager = ClipManager::getInstance();
    auto* clip = clipManager.getClip(clipId_);
    if (!clip || !clip->isMidi() || clip->midiCCData.empty())
        return;

    clip->midiCCData.pop_back();
    clipManager.forceNotifyClipPropertyChanged(clipId_);
}

// ============================================================================
// EditMidiCCEventCommand
// ============================================================================

EditMidiCCEventCommand::EditMidiCCEventCommand(ClipId clipId, size_t eventIndex, int newValue)
    : clipId_(clipId), eventIndex_(eventIndex), newValue_(newValue) {
    const auto* clip = ClipManager::getInstance().getClip(clipId_);
    if (clip && eventIndex_ < clip->midiCCData.size()) {
        oldValue_ = clip->midiCCData[eventIndex_].value;
    }
}

void EditMidiCCEventCommand::execute() {
    auto& clipManager = ClipManager::getInstance();
    auto* clip = clipManager.getClip(clipId_);
    if (!clip || !clip->isMidi() || eventIndex_ >= clip->midiCCData.size())
        return;

    clip->midiCCData[eventIndex_].value = newValue_;
    clipManager.forceNotifyClipPropertyChanged(clipId_);
    executed_ = true;
}

void EditMidiCCEventCommand::undo() {
    if (!executed_)
        return;
    auto& clipManager = ClipManager::getInstance();
    auto* clip = clipManager.getClip(clipId_);
    if (!clip || !clip->isMidi() || eventIndex_ >= clip->midiCCData.size())
        return;

    clip->midiCCData[eventIndex_].value = oldValue_;
    clipManager.forceNotifyClipPropertyChanged(clipId_);
}

bool EditMidiCCEventCommand::canMergeWith(const UndoableCommand* other) const {
    auto* o = dynamic_cast<const EditMidiCCEventCommand*>(other);
    return o && o->clipId_ == clipId_ && o->eventIndex_ == eventIndex_;
}

void EditMidiCCEventCommand::mergeWith(const UndoableCommand* other) {
    auto* o = dynamic_cast<const EditMidiCCEventCommand*>(other);
    if (o)
        newValue_ = o->newValue_;
}

// ============================================================================
// DeleteMidiCCEventCommand
// ============================================================================

DeleteMidiCCEventCommand::DeleteMidiCCEventCommand(ClipId clipId, size_t eventIndex)
    : clipId_(clipId), eventIndex_(eventIndex) {
    const auto* clip = ClipManager::getInstance().getClip(clipId_);
    if (clip && eventIndex_ < clip->midiCCData.size()) {
        deletedEvent_ = clip->midiCCData[eventIndex_];
    }
}

void DeleteMidiCCEventCommand::execute() {
    auto& clipManager = ClipManager::getInstance();
    auto* clip = clipManager.getClip(clipId_);
    if (!clip || !clip->isMidi() || eventIndex_ >= clip->midiCCData.size())
        return;

    clip->midiCCData.erase(clip->midiCCData.begin() + static_cast<std::ptrdiff_t>(eventIndex_));
    clipManager.forceNotifyClipPropertyChanged(clipId_);
    executed_ = true;
}

void DeleteMidiCCEventCommand::undo() {
    if (!executed_)
        return;
    auto& clipManager = ClipManager::getInstance();
    auto* clip = clipManager.getClip(clipId_);
    if (!clip || !clip->isMidi())
        return;

    size_t insertPos = std::min(eventIndex_, clip->midiCCData.size());
    clip->midiCCData.insert(clip->midiCCData.begin() + static_cast<std::ptrdiff_t>(insertPos),
                            deletedEvent_);
    clipManager.forceNotifyClipPropertyChanged(clipId_);
}

// ============================================================================
// DrawMidiCCEventsCommand
// ============================================================================

DrawMidiCCEventsCommand::DrawMidiCCEventsCommand(ClipId clipId, std::vector<MidiCCData> events)
    : clipId_(clipId), events_(std::move(events)) {}

void DrawMidiCCEventsCommand::execute() {
    auto& clipManager = ClipManager::getInstance();
    auto* clip = clipManager.getClip(clipId_);
    if (!clip || !clip->isMidi())
        return;

    insertStartIndex_ = clip->midiCCData.size();
    for (const auto& event : events_) {
        clip->midiCCData.push_back(event);
    }
    clipManager.forceNotifyClipPropertyChanged(clipId_);
    executed_ = true;
}

void DrawMidiCCEventsCommand::undo() {
    if (!executed_)
        return;
    auto& clipManager = ClipManager::getInstance();
    auto* clip = clipManager.getClip(clipId_);
    if (!clip || !clip->isMidi())
        return;

    if (insertStartIndex_ <= clip->midiCCData.size()) {
        clip->midiCCData.erase(clip->midiCCData.begin() +
                                   static_cast<std::ptrdiff_t>(insertStartIndex_),
                               clip->midiCCData.end());
    }
    clipManager.forceNotifyClipPropertyChanged(clipId_);
}

// ============================================================================
// MoveMidiCCEventCommand
// ============================================================================

MoveMidiCCEventCommand::MoveMidiCCEventCommand(ClipId clipId, size_t eventIndex,
                                               double newBeatPosition, int newValue)
    : clipId_(clipId),
      eventIndex_(eventIndex),
      newBeatPosition_(newBeatPosition),
      newValue_(newValue) {
    const auto* clip = ClipManager::getInstance().getClip(clipId_);
    if (clip && eventIndex_ < clip->midiCCData.size()) {
        oldBeatPosition_ = clip->midiCCData[eventIndex_].beatPosition;
        oldValue_ = clip->midiCCData[eventIndex_].value;
    }
}

void MoveMidiCCEventCommand::execute() {
    auto& clipManager = ClipManager::getInstance();
    auto* clip = clipManager.getClip(clipId_);
    if (!clip || !clip->isMidi() || eventIndex_ >= clip->midiCCData.size())
        return;

    clip->midiCCData[eventIndex_].beatPosition = newBeatPosition_;
    clip->midiCCData[eventIndex_].value = newValue_;
    clipManager.forceNotifyClipPropertyChanged(clipId_);
    executed_ = true;
}

void MoveMidiCCEventCommand::undo() {
    if (!executed_)
        return;
    auto& clipManager = ClipManager::getInstance();
    auto* clip = clipManager.getClip(clipId_);
    if (!clip || !clip->isMidi() || eventIndex_ >= clip->midiCCData.size())
        return;

    clip->midiCCData[eventIndex_].beatPosition = oldBeatPosition_;
    clip->midiCCData[eventIndex_].value = oldValue_;
    clipManager.forceNotifyClipPropertyChanged(clipId_);
}

bool MoveMidiCCEventCommand::canMergeWith(const UndoableCommand* other) const {
    auto* o = dynamic_cast<const MoveMidiCCEventCommand*>(other);
    return o && o->clipId_ == clipId_ && o->eventIndex_ == eventIndex_;
}

void MoveMidiCCEventCommand::mergeWith(const UndoableCommand* other) {
    auto* o = dynamic_cast<const MoveMidiCCEventCommand*>(other);
    if (o) {
        newBeatPosition_ = o->newBeatPosition_;
        newValue_ = o->newValue_;
    }
}

// ============================================================================
// MoveMidiPitchBendEventCommand
// ============================================================================

MoveMidiPitchBendEventCommand::MoveMidiPitchBendEventCommand(ClipId clipId, size_t eventIndex,
                                                             double newBeatPosition, int newValue)
    : clipId_(clipId),
      eventIndex_(eventIndex),
      newBeatPosition_(newBeatPosition),
      newValue_(newValue) {
    const auto* clip = ClipManager::getInstance().getClip(clipId_);
    if (clip && eventIndex_ < clip->midiPitchBendData.size()) {
        oldBeatPosition_ = clip->midiPitchBendData[eventIndex_].beatPosition;
        oldValue_ = clip->midiPitchBendData[eventIndex_].value;
    }
}

void MoveMidiPitchBendEventCommand::execute() {
    auto& clipManager = ClipManager::getInstance();
    auto* clip = clipManager.getClip(clipId_);
    if (!clip || !clip->isMidi() || eventIndex_ >= clip->midiPitchBendData.size())
        return;

    clip->midiPitchBendData[eventIndex_].beatPosition = newBeatPosition_;
    clip->midiPitchBendData[eventIndex_].value = newValue_;
    clipManager.forceNotifyClipPropertyChanged(clipId_);
    executed_ = true;
}

void MoveMidiPitchBendEventCommand::undo() {
    if (!executed_)
        return;
    auto& clipManager = ClipManager::getInstance();
    auto* clip = clipManager.getClip(clipId_);
    if (!clip || !clip->isMidi() || eventIndex_ >= clip->midiPitchBendData.size())
        return;

    clip->midiPitchBendData[eventIndex_].beatPosition = oldBeatPosition_;
    clip->midiPitchBendData[eventIndex_].value = oldValue_;
    clipManager.forceNotifyClipPropertyChanged(clipId_);
}

bool MoveMidiPitchBendEventCommand::canMergeWith(const UndoableCommand* other) const {
    auto* o = dynamic_cast<const MoveMidiPitchBendEventCommand*>(other);
    return o && o->clipId_ == clipId_ && o->eventIndex_ == eventIndex_;
}

void MoveMidiPitchBendEventCommand::mergeWith(const UndoableCommand* other) {
    auto* o = dynamic_cast<const MoveMidiPitchBendEventCommand*>(other);
    if (o) {
        newBeatPosition_ = o->newBeatPosition_;
        newValue_ = o->newValue_;
    }
}

// ============================================================================
// DeleteMultipleMidiCCEventsCommand
// ============================================================================

DeleteMultipleMidiCCEventsCommand::DeleteMultipleMidiCCEventsCommand(
    ClipId clipId, std::vector<size_t> eventIndices)
    : clipId_(clipId), eventIndices_(std::move(eventIndices)) {
    // Sort descending so we remove from the end first
    std::sort(eventIndices_.begin(), eventIndices_.end(), std::greater<size_t>());

    // Capture event data for undo
    const auto* clip = ClipManager::getInstance().getClip(clipId_);
    if (clip && clip->isMidi()) {
        for (size_t idx : eventIndices_) {
            if (idx < clip->midiCCData.size()) {
                deleted_.emplace_back(idx, clip->midiCCData[idx]);
            }
        }
    }
}

void DeleteMultipleMidiCCEventsCommand::execute() {
    auto& clipManager = ClipManager::getInstance();
    auto* clip = clipManager.getClip(clipId_);
    if (!clip || !clip->isMidi())
        return;

    for (size_t idx : eventIndices_) {
        if (idx < clip->midiCCData.size()) {
            clip->midiCCData.erase(clip->midiCCData.begin() + static_cast<std::ptrdiff_t>(idx));
        }
    }

    clipManager.forceNotifyClipPropertyChanged(clipId_);
    executed_ = true;
}

void DeleteMultipleMidiCCEventsCommand::undo() {
    if (!executed_)
        return;
    auto& clipManager = ClipManager::getInstance();
    auto* clip = clipManager.getClip(clipId_);
    if (!clip || !clip->isMidi())
        return;

    // Re-insert in reverse order (ascending index) to restore original positions
    for (auto it = deleted_.rbegin(); it != deleted_.rend(); ++it) {
        size_t insertPos = std::min(it->first, clip->midiCCData.size());
        clip->midiCCData.insert(clip->midiCCData.begin() + static_cast<std::ptrdiff_t>(insertPos),
                                it->second);
    }

    clipManager.forceNotifyClipPropertyChanged(clipId_);
}

// ============================================================================
// AddMidiPitchBendEventCommand
// ============================================================================

AddMidiPitchBendEventCommand::AddMidiPitchBendEventCommand(ClipId clipId, MidiPitchBendData event)
    : clipId_(clipId), event_(event) {}

void AddMidiPitchBendEventCommand::execute() {
    auto& clipManager = ClipManager::getInstance();
    auto* clip = clipManager.getClip(clipId_);
    if (!clip || !clip->isMidi())
        return;

    clip->midiPitchBendData.push_back(event_);
    clipManager.forceNotifyClipPropertyChanged(clipId_);
    executed_ = true;
}

void AddMidiPitchBendEventCommand::undo() {
    if (!executed_)
        return;
    auto& clipManager = ClipManager::getInstance();
    auto* clip = clipManager.getClip(clipId_);
    if (!clip || !clip->isMidi() || clip->midiPitchBendData.empty())
        return;

    clip->midiPitchBendData.pop_back();
    clipManager.forceNotifyClipPropertyChanged(clipId_);
}

// ============================================================================
// EditMidiPitchBendEventCommand
// ============================================================================

EditMidiPitchBendEventCommand::EditMidiPitchBendEventCommand(ClipId clipId, size_t eventIndex,
                                                             int newValue)
    : clipId_(clipId), eventIndex_(eventIndex), newValue_(newValue) {
    const auto* clip = ClipManager::getInstance().getClip(clipId_);
    if (clip && eventIndex_ < clip->midiPitchBendData.size()) {
        oldValue_ = clip->midiPitchBendData[eventIndex_].value;
    }
}

void EditMidiPitchBendEventCommand::execute() {
    auto& clipManager = ClipManager::getInstance();
    auto* clip = clipManager.getClip(clipId_);
    if (!clip || !clip->isMidi() || eventIndex_ >= clip->midiPitchBendData.size())
        return;

    clip->midiPitchBendData[eventIndex_].value = newValue_;
    clipManager.forceNotifyClipPropertyChanged(clipId_);
    executed_ = true;
}

void EditMidiPitchBendEventCommand::undo() {
    if (!executed_)
        return;
    auto& clipManager = ClipManager::getInstance();
    auto* clip = clipManager.getClip(clipId_);
    if (!clip || !clip->isMidi() || eventIndex_ >= clip->midiPitchBendData.size())
        return;

    clip->midiPitchBendData[eventIndex_].value = oldValue_;
    clipManager.forceNotifyClipPropertyChanged(clipId_);
}

bool EditMidiPitchBendEventCommand::canMergeWith(const UndoableCommand* other) const {
    auto* o = dynamic_cast<const EditMidiPitchBendEventCommand*>(other);
    return o && o->clipId_ == clipId_ && o->eventIndex_ == eventIndex_;
}

void EditMidiPitchBendEventCommand::mergeWith(const UndoableCommand* other) {
    auto* o = dynamic_cast<const EditMidiPitchBendEventCommand*>(other);
    if (o)
        newValue_ = o->newValue_;
}

// ============================================================================
// DeleteMidiPitchBendEventCommand
// ============================================================================

DeleteMidiPitchBendEventCommand::DeleteMidiPitchBendEventCommand(ClipId clipId, size_t eventIndex)
    : clipId_(clipId), eventIndex_(eventIndex) {
    const auto* clip = ClipManager::getInstance().getClip(clipId_);
    if (clip && eventIndex_ < clip->midiPitchBendData.size()) {
        deletedEvent_ = clip->midiPitchBendData[eventIndex_];
    }
}

void DeleteMidiPitchBendEventCommand::execute() {
    auto& clipManager = ClipManager::getInstance();
    auto* clip = clipManager.getClip(clipId_);
    if (!clip || !clip->isMidi() || eventIndex_ >= clip->midiPitchBendData.size())
        return;

    clip->midiPitchBendData.erase(clip->midiPitchBendData.begin() +
                                  static_cast<std::ptrdiff_t>(eventIndex_));
    clipManager.forceNotifyClipPropertyChanged(clipId_);
    executed_ = true;
}

void DeleteMidiPitchBendEventCommand::undo() {
    if (!executed_)
        return;
    auto& clipManager = ClipManager::getInstance();
    auto* clip = clipManager.getClip(clipId_);
    if (!clip || !clip->isMidi())
        return;

    size_t insertPos = std::min(eventIndex_, clip->midiPitchBendData.size());
    clip->midiPitchBendData.insert(
        clip->midiPitchBendData.begin() + static_cast<std::ptrdiff_t>(insertPos), deletedEvent_);
    clipManager.forceNotifyClipPropertyChanged(clipId_);
}

// ============================================================================
// DrawMidiPitchBendEventsCommand
// ============================================================================

DrawMidiPitchBendEventsCommand::DrawMidiPitchBendEventsCommand(
    ClipId clipId, std::vector<MidiPitchBendData> events)
    : clipId_(clipId), events_(std::move(events)) {}

void DrawMidiPitchBendEventsCommand::execute() {
    auto& clipManager = ClipManager::getInstance();
    auto* clip = clipManager.getClip(clipId_);
    if (!clip || !clip->isMidi())
        return;

    insertStartIndex_ = clip->midiPitchBendData.size();
    for (const auto& event : events_) {
        clip->midiPitchBendData.push_back(event);
    }
    clipManager.forceNotifyClipPropertyChanged(clipId_);
    executed_ = true;
}

void DrawMidiPitchBendEventsCommand::undo() {
    if (!executed_)
        return;
    auto& clipManager = ClipManager::getInstance();
    auto* clip = clipManager.getClip(clipId_);
    if (!clip || !clip->isMidi())
        return;

    if (insertStartIndex_ <= clip->midiPitchBendData.size()) {
        clip->midiPitchBendData.erase(clip->midiPitchBendData.begin() +
                                          static_cast<std::ptrdiff_t>(insertStartIndex_),
                                      clip->midiPitchBendData.end());
    }
    clipManager.forceNotifyClipPropertyChanged(clipId_);
}

// ============================================================================
// DeleteMultipleMidiPitchBendEventsCommand
// ============================================================================

DeleteMultipleMidiPitchBendEventsCommand::DeleteMultipleMidiPitchBendEventsCommand(
    ClipId clipId, std::vector<size_t> eventIndices)
    : clipId_(clipId), eventIndices_(std::move(eventIndices)) {
    // Sort descending so we remove from the end first
    std::sort(eventIndices_.begin(), eventIndices_.end(), std::greater<size_t>());

    // Capture event data for undo
    const auto* clip = ClipManager::getInstance().getClip(clipId_);
    if (clip && clip->isMidi()) {
        for (size_t idx : eventIndices_) {
            if (idx < clip->midiPitchBendData.size()) {
                deleted_.emplace_back(idx, clip->midiPitchBendData[idx]);
            }
        }
    }
}

void DeleteMultipleMidiPitchBendEventsCommand::execute() {
    auto& clipManager = ClipManager::getInstance();
    auto* clip = clipManager.getClip(clipId_);
    if (!clip || !clip->isMidi())
        return;

    for (size_t idx : eventIndices_) {
        if (idx < clip->midiPitchBendData.size()) {
            clip->midiPitchBendData.erase(clip->midiPitchBendData.begin() +
                                          static_cast<std::ptrdiff_t>(idx));
        }
    }

    clipManager.forceNotifyClipPropertyChanged(clipId_);
    executed_ = true;
}

void DeleteMultipleMidiPitchBendEventsCommand::undo() {
    if (!executed_)
        return;
    auto& clipManager = ClipManager::getInstance();
    auto* clip = clipManager.getClip(clipId_);
    if (!clip || !clip->isMidi())
        return;

    // Re-insert in reverse order (ascending index) to restore original positions
    for (auto it = deleted_.rbegin(); it != deleted_.rend(); ++it) {
        size_t insertPos = std::min(it->first, clip->midiPitchBendData.size());
        clip->midiPitchBendData.insert(
            clip->midiPitchBendData.begin() + static_cast<std::ptrdiff_t>(insertPos), it->second);
    }

    clipManager.forceNotifyClipPropertyChanged(clipId_);
}

// ============================================================================
// SetMidiCCEventTensionCommand
// ============================================================================

SetMidiCCEventTensionCommand::SetMidiCCEventTensionCommand(ClipId clipId, size_t eventIndex,
                                                           double tension)
    : clipId_(clipId), eventIndex_(eventIndex), newTension_(tension) {}

void SetMidiCCEventTensionCommand::execute() {
    auto* clip = ClipManager::getInstance().getClip(clipId_);
    if (!clip || eventIndex_ >= clip->midiCCData.size())
        return;
    if (!executed_)
        oldTension_ = clip->midiCCData[eventIndex_].tension;
    clip->midiCCData[eventIndex_].tension = newTension_;
    executed_ = true;
    ClipManager::getInstance().forceNotifyClipPropertyChanged(clipId_);
}

void SetMidiCCEventTensionCommand::undo() {
    auto* clip = ClipManager::getInstance().getClip(clipId_);
    if (!clip || eventIndex_ >= clip->midiCCData.size())
        return;
    clip->midiCCData[eventIndex_].tension = oldTension_;
    ClipManager::getInstance().forceNotifyClipPropertyChanged(clipId_);
}

bool SetMidiCCEventTensionCommand::canMergeWith(const UndoableCommand* other) const {
    auto* o = dynamic_cast<const SetMidiCCEventTensionCommand*>(other);
    return o && o->clipId_ == clipId_ && o->eventIndex_ == eventIndex_;
}

void SetMidiCCEventTensionCommand::mergeWith(const UndoableCommand* other) {
    auto* o = dynamic_cast<const SetMidiCCEventTensionCommand*>(other);
    if (o)
        newTension_ = o->newTension_;
}

// ============================================================================
// SetMidiCCEventHandlesCommand
// ============================================================================

SetMidiCCEventHandlesCommand::SetMidiCCEventHandlesCommand(ClipId clipId, size_t eventIndex,
                                                           MidiCurveHandle inHandle,
                                                           MidiCurveHandle outHandle)
    : clipId_(clipId), eventIndex_(eventIndex), newInHandle_(inHandle), newOutHandle_(outHandle) {}

void SetMidiCCEventHandlesCommand::execute() {
    auto* clip = ClipManager::getInstance().getClip(clipId_);
    if (!clip || eventIndex_ >= clip->midiCCData.size())
        return;
    if (!executed_) {
        oldInHandle_ = clip->midiCCData[eventIndex_].inHandle;
        oldOutHandle_ = clip->midiCCData[eventIndex_].outHandle;
    }
    clip->midiCCData[eventIndex_].inHandle = newInHandle_;
    clip->midiCCData[eventIndex_].outHandle = newOutHandle_;
    executed_ = true;
    ClipManager::getInstance().forceNotifyClipPropertyChanged(clipId_);
}

void SetMidiCCEventHandlesCommand::undo() {
    auto* clip = ClipManager::getInstance().getClip(clipId_);
    if (!clip || eventIndex_ >= clip->midiCCData.size())
        return;
    clip->midiCCData[eventIndex_].inHandle = oldInHandle_;
    clip->midiCCData[eventIndex_].outHandle = oldOutHandle_;
    ClipManager::getInstance().forceNotifyClipPropertyChanged(clipId_);
}

// ============================================================================
// SetMidiPitchBendEventTensionCommand
// ============================================================================

SetMidiPitchBendEventTensionCommand::SetMidiPitchBendEventTensionCommand(ClipId clipId,
                                                                         size_t eventIndex,
                                                                         double tension)
    : clipId_(clipId), eventIndex_(eventIndex), newTension_(tension) {}

void SetMidiPitchBendEventTensionCommand::execute() {
    auto* clip = ClipManager::getInstance().getClip(clipId_);
    if (!clip || eventIndex_ >= clip->midiPitchBendData.size())
        return;
    if (!executed_)
        oldTension_ = clip->midiPitchBendData[eventIndex_].tension;
    clip->midiPitchBendData[eventIndex_].tension = newTension_;
    executed_ = true;
    ClipManager::getInstance().forceNotifyClipPropertyChanged(clipId_);
}

void SetMidiPitchBendEventTensionCommand::undo() {
    auto* clip = ClipManager::getInstance().getClip(clipId_);
    if (!clip || eventIndex_ >= clip->midiPitchBendData.size())
        return;
    clip->midiPitchBendData[eventIndex_].tension = oldTension_;
    ClipManager::getInstance().forceNotifyClipPropertyChanged(clipId_);
}

bool SetMidiPitchBendEventTensionCommand::canMergeWith(const UndoableCommand* other) const {
    auto* o = dynamic_cast<const SetMidiPitchBendEventTensionCommand*>(other);
    return o && o->clipId_ == clipId_ && o->eventIndex_ == eventIndex_;
}

void SetMidiPitchBendEventTensionCommand::mergeWith(const UndoableCommand* other) {
    auto* o = dynamic_cast<const SetMidiPitchBendEventTensionCommand*>(other);
    if (o)
        newTension_ = o->newTension_;
}

// ============================================================================
// SetMidiPitchBendEventHandlesCommand
// ============================================================================

SetMidiPitchBendEventHandlesCommand::SetMidiPitchBendEventHandlesCommand(ClipId clipId,
                                                                         size_t eventIndex,
                                                                         MidiCurveHandle inHandle,
                                                                         MidiCurveHandle outHandle)
    : clipId_(clipId), eventIndex_(eventIndex), newInHandle_(inHandle), newOutHandle_(outHandle) {}

void SetMidiPitchBendEventHandlesCommand::execute() {
    auto* clip = ClipManager::getInstance().getClip(clipId_);
    if (!clip || eventIndex_ >= clip->midiPitchBendData.size())
        return;
    if (!executed_) {
        oldInHandle_ = clip->midiPitchBendData[eventIndex_].inHandle;
        oldOutHandle_ = clip->midiPitchBendData[eventIndex_].outHandle;
    }
    clip->midiPitchBendData[eventIndex_].inHandle = newInHandle_;
    clip->midiPitchBendData[eventIndex_].outHandle = newOutHandle_;
    executed_ = true;
    ClipManager::getInstance().forceNotifyClipPropertyChanged(clipId_);
}

void SetMidiPitchBendEventHandlesCommand::undo() {
    auto* clip = ClipManager::getInstance().getClip(clipId_);
    if (!clip || eventIndex_ >= clip->midiPitchBendData.size())
        return;
    clip->midiPitchBendData[eventIndex_].inHandle = oldInHandle_;
    clip->midiPitchBendData[eventIndex_].outHandle = oldOutHandle_;
    ClipManager::getInstance().forceNotifyClipPropertyChanged(clipId_);
}

// ============================================================================
// BendNoteTimingCommand
// ============================================================================

BendNoteTimingCommand::BendNoteTimingCommand(ClipId clipId, std::vector<size_t> noteIndices,
                                             float depth, float skew, int cycles, float quantize,
                                             int quantizeSub, bool hardAngle)
    : clipId_(clipId),
      noteIndices_(std::move(noteIndices)),
      depth_(depth),
      skew_(skew),
      cycles_(std::max(1, cycles)),
      quantize_(quantize),
      quantizeSub_(quantizeSub),
      hardAngle_(hardAngle) {}

void BendNoteTimingCommand::execute() {
    auto& clipManager = ClipManager::getInstance();
    auto* clip = clipManager.getClip(clipId_);

    if (!clip || !clip->isMidi() || noteIndices_.size() < 2)
        return;

    // Capture old values on first execute
    if (!executed_) {
        oldStartBeats_ = collectMidiNoteStartBeats(*clip, noteIndices_);
    }

    const auto bentStarts = calculateBentMidiNoteStartBeats(
        *clip, oldStartBeats_, depth_, skew_, cycles_, quantize_, quantizeSub_, hardAngle_);
    if (bentStarts.empty())
        return;

    for (const auto& bent : bentStarts) {
        if (bent.noteIndex >= clip->midiNotes.size())
            continue;
        clip->midiNotes[bent.noteIndex].startBeat = bent.startBeat;
    }

    clipManager.forceNotifyClipPropertyChanged(clipId_);
    executed_ = true;
}

void BendNoteTimingCommand::undo() {
    if (!executed_)
        return;

    auto& clipManager = ClipManager::getInstance();
    auto* clip = clipManager.getClip(clipId_);

    if (!clip || !clip->isMidi())
        return;

    for (const auto& oldStart : oldStartBeats_) {
        if (oldStart.noteIndex < clip->midiNotes.size())
            clip->midiNotes[oldStart.noteIndex].startBeat = oldStart.startBeat;
    }

    clipManager.forceNotifyClipPropertyChanged(clipId_);
}

}  // namespace magda
