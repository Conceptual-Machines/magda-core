#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <optional>
#include <vector>

#include "core/ClipInfo.hpp"

namespace magda {

enum class MidiEditTool { Pointer, Pencil, Slice, Glue, Erase };

/**
 * @brief The MIDI editor's active tool, shared by the piano roll, the drum grid and the toolbar.
 *
 * Keys 1-5 pick a tool; holding the key past kHoldMs makes the swap temporary.
 */
class MidiEditToolState : public juce::ChangeBroadcaster {
  public:
    static MidiEditToolState& getInstance();

    MidiEditTool getTool() const {
        return tool_;
    }
    void setTool(MidiEditTool tool);

    /** Returns true when @p key is one of the tool keys. */
    bool handleKeyPressed(const juce::KeyPress& key);
    /** Call from keyStateChanged so a held tool key reverts on release. */
    void handleKeyStateChanged();
    /** Option/Alt held over a grid swaps to the Pencil until it is released. */
    void setAltHeld(bool held);

  private:
    static constexpr juce::uint32 kHoldMs = 350;

    MidiEditTool tool_ = MidiEditTool::Pointer;
    MidiEditTool toolBeforeHold_ = MidiEditTool::Pointer;
    int heldKeyCode_ = 0;
    bool altSwapped_ = false;
    MidiEditTool toolBeforeAlt_ = MidiEditTool::Pointer;
    juce::uint32 heldSinceMs_ = 0;
};

/** @brief Cursor a tool shows over notes and the empty grid; nullopt keeps the pointer's own. */
std::optional<juce::MouseCursor> cursorForMidiEditTool(MidiEditTool tool);

/** @brief Undoable split of a clip's note at a clip beat. */
void splitMidiNoteWithUndo(ClipId clipId, size_t noteIndex, double clipBeat);
/** @brief Undoable join of a clip's note with the next note of its pitch. */
void glueMidiNoteWithUndo(ClipId clipId, size_t noteIndex);
/** @brief The "slice into equal parts" popup for selected notes, opened at the mouse. */
void showSliceNotesPopup(ClipId clipId, std::vector<size_t> noteIndices);

}  // namespace magda
