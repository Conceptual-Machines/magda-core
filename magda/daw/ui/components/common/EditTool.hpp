#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <optional>

namespace magda {

/// Keys 1-5 pick the first five; Line and Flat are the gain envelope's (#3064), Marker places
/// the audio editor's warp markers.
enum class EditTool { Pointer, Pencil, Slice, Glue, Erase, Line, Flat, Marker };

/**
 * @brief One surface's active edit tool: the MIDI editor and the arrangement each own one.
 *
 * Keys 1-5 pick a tool; holding the key past kHoldMs makes the swap temporary.
 */
class EditToolState : public juce::ChangeBroadcaster {
  public:
    /** Shared by the piano roll, the drum grid and their toolbar. */
    static EditToolState& midiEditor();
    static EditToolState& arrangement();
    /** The audio editor's envelope tools (#3064). */
    static EditToolState& audioEditor();
    /** The audio editor's sample view: the pointer and the warp-marker tool. */
    static EditToolState& sampleEditor();

    EditTool getTool() const {
        return tool_;
    }
    void setTool(EditTool tool);

    /** Returns true when @p key is one of the tool keys. */
    bool handleKeyPressed(const juce::KeyPress& key);
    /** Call from keyStateChanged so a held tool key reverts on release. */
    void handleKeyStateChanged();
    /** Option/Alt held over a grid swaps to the Pencil until it is released. */
    void setAltHeld(bool held);

  private:
    static constexpr juce::uint32 kHoldMs = 350;

    EditTool tool_ = EditTool::Pointer;
    EditTool toolBeforeHold_ = EditTool::Pointer;
    int heldKeyCode_ = 0;
    bool altSwapped_ = false;
    EditTool toolBeforeAlt_ = EditTool::Pointer;
    juce::uint32 heldSinceMs_ = 0;
};

/** @brief Cursor a tool shows over its targets; nullopt keeps the pointer's own. */
std::optional<juce::MouseCursor> cursorForEditTool(EditTool tool);

}  // namespace magda
