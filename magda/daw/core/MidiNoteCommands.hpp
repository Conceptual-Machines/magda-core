#pragma once

#include "ClipInfo.hpp"
#include "ClipManager.hpp"
#include "UndoManager.hpp"

namespace magda {

/**
 * @brief Mode for quantizing MIDI notes
 */
enum class QuantizeMode { StartOnly, LengthOnly, StartAndLength };

/**
 * @brief Command for adding a MIDI note to a clip
 */
class AddMidiNoteCommand : public UndoableCommand {
  public:
    AddMidiNoteCommand(ClipId clipId, double startBeat, int noteNumber, double lengthBeats,
                       int velocity);

    void execute() override;
    void undo() override;
    juce::String getDescription() const override {
        return "Add MIDI Note";
    }

  private:
    ClipId clipId_;
    MidiNote note_;
    size_t insertedIndex_ = 0;
    bool executed_ = false;
};

/**
 * @brief Command for moving a MIDI note (change start beat and/or note number)
 */
class MoveMidiNoteCommand : public UndoableCommand {
  public:
    MoveMidiNoteCommand(ClipId clipId, size_t noteIndex, double newStartBeat, int newNoteNumber);

    void execute() override;
    void undo() override;
    juce::String getDescription() const override {
        return "Move MIDI Note";
    }

    bool canMergeWith(const UndoableCommand* other) const override;
    void mergeWith(const UndoableCommand* other) override;

  private:
    ClipId clipId_;
    size_t noteIndex_;
    double oldStartBeat_;
    double newStartBeat_;
    int oldNoteNumber_;
    int newNoteNumber_;
    bool executed_ = false;
};

/**
 * @brief Command for resizing a MIDI note (change length)
 */
class ResizeMidiNoteCommand : public UndoableCommand {
  public:
    ResizeMidiNoteCommand(ClipId clipId, size_t noteIndex, double newLengthBeats);

    void execute() override;
    void undo() override;
    juce::String getDescription() const override {
        return "Resize MIDI Note";
    }

    bool canMergeWith(const UndoableCommand* other) const override;
    void mergeWith(const UndoableCommand* other) override;

  private:
    ClipId clipId_;
    size_t noteIndex_;
    double oldLengthBeats_;
    double newLengthBeats_;
    bool executed_ = false;
};

/**
 * @brief Command for deleting a MIDI note
 */
class DeleteMidiNoteCommand : public UndoableCommand {
  public:
    DeleteMidiNoteCommand(ClipId clipId, size_t noteIndex);

    void execute() override;
    void undo() override;
    juce::String getDescription() const override {
        return "Delete MIDI Note";
    }

  private:
    ClipId clipId_;
    size_t noteIndex_;
    MidiNote deletedNote_;
    bool executed_ = false;
};

/**
 * @brief Command for setting velocity of a MIDI note
 */
class SetMidiNoteVelocityCommand : public UndoableCommand {
  public:
    SetMidiNoteVelocityCommand(ClipId clipId, size_t noteIndex, int newVelocity);

    void execute() override;
    void undo() override;
    juce::String getDescription() const override {
        return "Set Note Velocity";
    }

    bool canMergeWith(const UndoableCommand* other) const override;
    void mergeWith(const UndoableCommand* other) override;

  private:
    ClipId clipId_;
    size_t noteIndex_;
    int oldVelocity_;
    int newVelocity_;
    bool executed_ = false;
};

/**
 * @brief Command for setting velocities of multiple MIDI notes at once
 */
class SetMultipleNoteVelocitiesCommand : public UndoableCommand {
  public:
    SetMultipleNoteVelocitiesCommand(ClipId clipId,
                                     std::vector<std::pair<size_t, int>> noteVelocities);

    void execute() override;
    void undo() override;
    juce::String getDescription() const override {
        return "Set Note Velocities";
    }

  private:
    ClipId clipId_;
    std::vector<std::pair<size_t, int>> newVelocities_;  // {index, newVel}
    std::vector<std::pair<size_t, int>> oldVelocities_;  // captured on first execute
    bool executed_ = false;
};

/**
 * @brief Command for moving multiple MIDI notes at once (single undo step)
 */
class MoveMultipleMidiNotesCommand : public UndoableCommand {
  public:
    struct NoteMove {
        size_t noteIndex;
        double newStartBeat;
        int newNoteNumber;
    };

    MoveMultipleMidiNotesCommand(ClipId clipId, std::vector<NoteMove> moves);

    void execute() override;
    void undo() override;
    juce::String getDescription() const override {
        return "Move MIDI Notes";
    }

  private:
    ClipId clipId_;
    std::vector<NoteMove> moves_;
    struct OldValues {
        double startBeat;
        int noteNumber;
    };
    std::vector<OldValues> oldValues_;
    bool executed_ = false;
};

/**
 * @brief Command for resizing multiple MIDI notes at once (single undo step)
 */
class ResizeMultipleMidiNotesCommand : public UndoableCommand {
  public:
    ResizeMultipleMidiNotesCommand(ClipId clipId,
                                   std::vector<std::pair<size_t, double>> noteLengths);

    void execute() override;
    void undo() override;
    juce::String getDescription() const override {
        return "Resize MIDI Notes";
    }

  private:
    ClipId clipId_;
    std::vector<std::pair<size_t, double>> newLengths_;  // {index, newLength}
    std::vector<std::pair<size_t, double>> oldLengths_;  // captured on first execute
    bool executed_ = false;
};

/**
 * @brief Command for moving a MIDI note between clips
 * Removes note from source clip and adds it to destination clip
 */
class MoveMidiNoteBetweenClipsCommand : public UndoableCommand {
  public:
    MoveMidiNoteBetweenClipsCommand(ClipId sourceClipId, size_t noteIndex, ClipId destClipId,
                                    double newStartBeat, int newNoteNumber);

    void execute() override;
    void undo() override;
    juce::String getDescription() const override {
        return "Move Note Between Clips";
    }

  private:
    ClipId sourceClipId_;
    ClipId destClipId_;
    size_t sourceNoteIndex_;
    size_t destNoteIndex_ = 0;  // Where it was inserted in dest clip
    MidiNote movedNote_;
    double newStartBeat_;
    int newNoteNumber_;
    bool executed_ = false;
};

/**
 * @brief Command for quantizing multiple MIDI notes to grid
 */
class QuantizeMidiNotesCommand : public UndoableCommand {
  public:
    QuantizeMidiNotesCommand(ClipId clipId, std::vector<size_t> noteIndices, double gridResolution,
                             QuantizeMode mode);

    void execute() override;
    void undo() override;
    juce::String getDescription() const override {
        return "Quantize MIDI Notes";
    }

  private:
    ClipId clipId_;
    std::vector<size_t> noteIndices_;
    double gridResolution_;
    QuantizeMode mode_;

    struct OldValues {
        double startBeat;
        double lengthBeats;
    };
    std::vector<OldValues> oldValues_;
    bool executed_ = false;
};

/**
 * @brief Command for deleting multiple MIDI notes at once
 */
class DeleteMultipleMidiNotesCommand : public UndoableCommand {
  public:
    DeleteMultipleMidiNotesCommand(ClipId clipId, std::vector<size_t> noteIndices);

    void execute() override;
    void undo() override;
    juce::String getDescription() const override {
        return "Delete MIDI Notes";
    }

  private:
    ClipId clipId_;
    std::vector<size_t> noteIndices_;  // Original indices (sorted descending for removal)
    std::vector<std::pair<size_t, MidiNote>> deleted_;  // {originalIndex, note} for undo
    bool executed_ = false;
};

/**
 * @brief Command for adding multiple MIDI notes at once (used by paste/duplicate)
 */
class AddMultipleMidiNotesCommand : public UndoableCommand {
  public:
    AddMultipleMidiNotesCommand(ClipId clipId, std::vector<MidiNote> notes,
                                juce::String description);

    void execute() override;
    void undo() override;
    juce::String getDescription() const override {
        return description_;
    }

    const std::vector<size_t>& getInsertedIndices() const {
        return insertedIndices_;
    }

  private:
    ClipId clipId_;
    std::vector<MidiNote> notes_;
    juce::String description_;
    std::vector<size_t> insertedIndices_;  // Indices of inserted notes after execute
    bool executed_ = false;
};

/**
 * @brief Command for transposing all notes in a MIDI clip by a given number of semitones
 */
class TransposeMidiClipCommand : public UndoableCommand {
  public:
    TransposeMidiClipCommand(ClipId clipId, int semitones);

    void execute() override;
    void undo() override;
    juce::String getDescription() const override {
        return "Transpose MIDI Clip";
    }

    bool canMergeWith(const UndoableCommand* other) const override;
    void mergeWith(const UndoableCommand* other) override;

  private:
    ClipId clipId_;
    int semitones_;
    std::vector<int> oldNoteNumbers_;
    bool executed_ = false;
};

// ============================================================================
// MIDI CC Commands
// ============================================================================

/**
 * @brief Command for adding a MIDI CC event to a clip
 */
class AddMidiCCEventCommand : public UndoableCommand {
  public:
    AddMidiCCEventCommand(ClipId clipId, MidiCCData event);

    void execute() override;
    void undo() override;
    juce::String getDescription() const override {
        return "Add MIDI CC Event";
    }

  private:
    ClipId clipId_;
    MidiCCData event_;
    bool executed_ = false;
};

/**
 * @brief Command for editing a MIDI CC event value
 */
class EditMidiCCEventCommand : public UndoableCommand {
  public:
    EditMidiCCEventCommand(ClipId clipId, size_t eventIndex, int newValue);

    void execute() override;
    void undo() override;
    juce::String getDescription() const override {
        return "Edit MIDI CC Event";
    }

    bool canMergeWith(const UndoableCommand* other) const override;
    void mergeWith(const UndoableCommand* other) override;

  private:
    ClipId clipId_;
    size_t eventIndex_;
    int oldValue_;
    int newValue_;
    bool executed_ = false;
};

/**
 * @brief Command for deleting a MIDI CC event
 */
class DeleteMidiCCEventCommand : public UndoableCommand {
  public:
    DeleteMidiCCEventCommand(ClipId clipId, size_t eventIndex);

    void execute() override;
    void undo() override;
    juce::String getDescription() const override {
        return "Delete MIDI CC Event";
    }

  private:
    ClipId clipId_;
    size_t eventIndex_;
    MidiCCData deletedEvent_;
    bool executed_ = false;
};

/**
 * @brief Command for batch-adding MIDI CC events (freehand draw)
 */
class DrawMidiCCEventsCommand : public UndoableCommand {
  public:
    DrawMidiCCEventsCommand(ClipId clipId, std::vector<MidiCCData> events);

    void execute() override;
    void undo() override;
    juce::String getDescription() const override {
        return "Draw MIDI CC Events";
    }

  private:
    ClipId clipId_;
    std::vector<MidiCCData> events_;
    size_t insertStartIndex_ = 0;
    bool executed_ = false;
};

/**
 * @brief Command for moving a MIDI CC event (change beat position and/or value)
 */
class MoveMidiCCEventCommand : public UndoableCommand {
  public:
    MoveMidiCCEventCommand(ClipId clipId, size_t eventIndex, double newBeatPosition, int newValue);

    void execute() override;
    void undo() override;
    juce::String getDescription() const override {
        return "Move MIDI CC Event";
    }

    bool canMergeWith(const UndoableCommand* other) const override;
    void mergeWith(const UndoableCommand* other) override;

  private:
    ClipId clipId_;
    size_t eventIndex_;
    double oldBeatPosition_;
    double newBeatPosition_;
    int oldValue_;
    int newValue_;
    bool executed_ = false;
};

/**
 * @brief Command for moving a MIDI pitch bend event (change beat position and/or value)
 */
class MoveMidiPitchBendEventCommand : public UndoableCommand {
  public:
    MoveMidiPitchBendEventCommand(ClipId clipId, size_t eventIndex, double newBeatPosition,
                                  int newValue);

    void execute() override;
    void undo() override;
    juce::String getDescription() const override {
        return "Move Pitch Bend Event";
    }

    bool canMergeWith(const UndoableCommand* other) const override;
    void mergeWith(const UndoableCommand* other) override;

  private:
    ClipId clipId_;
    size_t eventIndex_;
    double oldBeatPosition_;
    double newBeatPosition_;
    int oldValue_;
    int newValue_;
    bool executed_ = false;
};

/**
 * @brief Command for deleting multiple MIDI CC events at once
 */
class DeleteMultipleMidiCCEventsCommand : public UndoableCommand {
  public:
    DeleteMultipleMidiCCEventsCommand(ClipId clipId, std::vector<size_t> eventIndices);

    void execute() override;
    void undo() override;
    juce::String getDescription() const override {
        return "Delete MIDI CC Events";
    }

  private:
    ClipId clipId_;
    std::vector<size_t> eventIndices_;
    std::vector<std::pair<size_t, MidiCCData>> deleted_;
    bool executed_ = false;
};

// ============================================================================
// MIDI Pitch Bend Commands
// ============================================================================

/**
 * @brief Command for adding a MIDI pitch bend event to a clip
 */
class AddMidiPitchBendEventCommand : public UndoableCommand {
  public:
    AddMidiPitchBendEventCommand(ClipId clipId, MidiPitchBendData event);

    void execute() override;
    void undo() override;
    juce::String getDescription() const override {
        return "Add Pitch Bend Event";
    }

  private:
    ClipId clipId_;
    MidiPitchBendData event_;
    bool executed_ = false;
};

/**
 * @brief Command for editing a MIDI pitch bend event value
 */
class EditMidiPitchBendEventCommand : public UndoableCommand {
  public:
    EditMidiPitchBendEventCommand(ClipId clipId, size_t eventIndex, int newValue);

    void execute() override;
    void undo() override;
    juce::String getDescription() const override {
        return "Edit Pitch Bend Event";
    }

    bool canMergeWith(const UndoableCommand* other) const override;
    void mergeWith(const UndoableCommand* other) override;

  private:
    ClipId clipId_;
    size_t eventIndex_;
    int oldValue_;
    int newValue_;
    bool executed_ = false;
};

/**
 * @brief Command for deleting a MIDI pitch bend event
 */
class DeleteMidiPitchBendEventCommand : public UndoableCommand {
  public:
    DeleteMidiPitchBendEventCommand(ClipId clipId, size_t eventIndex);

    void execute() override;
    void undo() override;
    juce::String getDescription() const override {
        return "Delete Pitch Bend Event";
    }

  private:
    ClipId clipId_;
    size_t eventIndex_;
    MidiPitchBendData deletedEvent_;
    bool executed_ = false;
};

/**
 * @brief Command for batch-adding MIDI pitch bend events (freehand draw)
 */
class DrawMidiPitchBendEventsCommand : public UndoableCommand {
  public:
    DrawMidiPitchBendEventsCommand(ClipId clipId, std::vector<MidiPitchBendData> events);

    void execute() override;
    void undo() override;
    juce::String getDescription() const override {
        return "Draw Pitch Bend Events";
    }

  private:
    ClipId clipId_;
    std::vector<MidiPitchBendData> events_;
    size_t insertStartIndex_ = 0;
    bool executed_ = false;
};

/**
 * @brief Command for deleting multiple MIDI pitch bend events at once
 */
class DeleteMultipleMidiPitchBendEventsCommand : public UndoableCommand {
  public:
    DeleteMultipleMidiPitchBendEventsCommand(ClipId clipId, std::vector<size_t> eventIndices);

    void execute() override;
    void undo() override;
    juce::String getDescription() const override {
        return "Delete Pitch Bend Events";
    }

  private:
    ClipId clipId_;
    std::vector<size_t> eventIndices_;
    std::vector<std::pair<size_t, MidiPitchBendData>> deleted_;
    bool executed_ = false;
};

// ============================================================================
// MIDI CC/PB Curve Shape Commands
// ============================================================================

/**
 * @brief Command for setting tension on a MIDI CC event
 */
class SetMidiCCEventTensionCommand : public UndoableCommand {
  public:
    SetMidiCCEventTensionCommand(ClipId clipId, size_t eventIndex, double tension);

    void execute() override;
    void undo() override;
    juce::String getDescription() const override {
        return "Set CC Event Tension";
    }

    bool canMergeWith(const UndoableCommand* other) const override;
    void mergeWith(const UndoableCommand* other) override;

  private:
    ClipId clipId_;
    size_t eventIndex_;
    double oldTension_;
    double newTension_;
    bool executed_ = false;
};

/**
 * @brief Command for setting bezier handles on a MIDI CC event
 */
class SetMidiCCEventHandlesCommand : public UndoableCommand {
  public:
    SetMidiCCEventHandlesCommand(ClipId clipId, size_t eventIndex, MidiCurveHandle inHandle,
                                 MidiCurveHandle outHandle);

    void execute() override;
    void undo() override;
    juce::String getDescription() const override {
        return "Set CC Event Handles";
    }

  private:
    ClipId clipId_;
    size_t eventIndex_;
    MidiCurveHandle oldInHandle_;
    MidiCurveHandle oldOutHandle_;
    MidiCurveHandle newInHandle_;
    MidiCurveHandle newOutHandle_;
    bool executed_ = false;
};

/**
 * @brief Command for setting tension on a MIDI pitch bend event
 */
class SetMidiPitchBendEventTensionCommand : public UndoableCommand {
  public:
    SetMidiPitchBendEventTensionCommand(ClipId clipId, size_t eventIndex, double tension);

    void execute() override;
    void undo() override;
    juce::String getDescription() const override {
        return "Set Pitch Bend Tension";
    }

    bool canMergeWith(const UndoableCommand* other) const override;
    void mergeWith(const UndoableCommand* other) override;

  private:
    ClipId clipId_;
    size_t eventIndex_;
    double oldTension_;
    double newTension_;
    bool executed_ = false;
};

/**
 * @brief Command for setting bezier handles on a MIDI pitch bend event
 */
class SetMidiPitchBendEventHandlesCommand : public UndoableCommand {
  public:
    SetMidiPitchBendEventHandlesCommand(ClipId clipId, size_t eventIndex, MidiCurveHandle inHandle,
                                        MidiCurveHandle outHandle);

    void execute() override;
    void undo() override;
    juce::String getDescription() const override {
        return "Set Pitch Bend Handles";
    }

  private:
    ClipId clipId_;
    size_t eventIndex_;
    MidiCurveHandle oldInHandle_;
    MidiCurveHandle oldOutHandle_;
    MidiCurveHandle newInHandle_;
    MidiCurveHandle newOutHandle_;
    bool executed_ = false;
};

/**
 * @brief Command for setting chordGroup IDs on multiple notes (used by chord detection)
 */
class SetNoteChordGroupsCommand : public UndoableCommand {
  public:
    SetNoteChordGroupsCommand(ClipId clipId, std::vector<std::pair<size_t, int>> noteGroups)
        : clipId_(clipId), newGroups_(std::move(noteGroups)) {}

    void execute() override {
        auto* clip = ClipManager::getInstance().getClip(clipId_);
        if (!clip)
            return;

        oldGroups_.clear();
        oldGroups_.reserve(newGroups_.size());
        for (const auto& [idx, group] : newGroups_) {
            if (idx < clip->midiNotes.size()) {
                oldGroups_.emplace_back(idx, clip->midiNotes[idx].chordGroup);
                clip->midiNotes[idx].chordGroup = group;
            }
        }
        ClipManager::getInstance().forceNotifyClipPropertyChanged(clipId_);
    }

    void undo() override {
        auto* clip = ClipManager::getInstance().getClip(clipId_);
        if (!clip)
            return;

        for (const auto& [idx, group] : oldGroups_) {
            if (idx < clip->midiNotes.size())
                clip->midiNotes[idx].chordGroup = group;
        }
        ClipManager::getInstance().forceNotifyClipPropertyChanged(clipId_);
    }

    juce::String getDescription() const override {
        return "Set chord groups";
    }

  private:
    ClipId clipId_;
    std::vector<std::pair<size_t, int>> newGroups_;  // {noteIndex, newChordGroup}
    std::vector<std::pair<size_t, int>>
        oldGroups_;  // {noteIndex, oldChordGroup} captured on execute
};

/**
 * @brief Command for applying time bend curve to selected notes' timing.
 *
 * Redistributes note start times within their original span using the
 * ramp curve function (same as arpeggiator/step sequencer time bend).
 */
class BendNoteTimingCommand : public UndoableCommand {
  public:
    BendNoteTimingCommand(ClipId clipId, std::vector<size_t> noteIndices, float depth, float skew,
                          int cycles = 1, float quantize = 0.0f, int quantizeSub = 64,
                          bool hardAngle = false);

    void execute() override;
    void undo() override;
    juce::String getDescription() const override {
        return "Time Bend MIDI Notes";
    }

  private:
    ClipId clipId_;
    std::vector<size_t> noteIndices_;
    float depth_, skew_;
    int cycles_;
    float quantize_;
    int quantizeSub_;
    bool hardAngle_;
    std::vector<double> oldStartBeats_;
    bool executed_ = false;
};

}  // namespace magda
