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

}  // namespace magda
