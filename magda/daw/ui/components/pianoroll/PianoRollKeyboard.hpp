#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace magda {

/**
 * Piano keyboard component for the piano roll.
 * Displays note names and responds to vertical scroll offset.
 * Supports vertical zoom by dragging up/down.
 */
class PianoRollKeyboard : public juce::Component {
  public:
    PianoRollKeyboard();
    ~PianoRollKeyboard() override = default;

    void paint(juce::Graphics& g) override;

    // Mouse interaction for zoom and scroll
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;
    void mouseWheelMove(const juce::MouseEvent& event,
                        const juce::MouseWheelDetails& wheel) override;

    // Configuration
    void setNoteHeight(int height);
    void setNoteRange(int minNote, int maxNote);
    void setScrollOffset(int offsetY);

    int getNoteHeight() const {
        return noteHeight_;
    }

    // Callbacks
    std::function<void(int, int, int)> onZoomChanged;   // newNoteHeight, anchorNote, anchorScreenY
    std::function<void(int)> onScrollRequested;         // deltaY scroll amount
    std::function<void(int, int, bool)> onNotePreview;  // noteNumber, velocity, isNoteOn

  private:
    int noteHeight_ = 12;
    int minNote_ = 21;   // A0
    int maxNote_ = 108;  // C8
    int scrollOffsetY_ = 0;

    // Drag state (zoom or scroll)
    enum class DragMode { None, Zooming, Scrolling };
    DragMode dragMode_ = DragMode::None;
    int mouseDownX_ = 0;
    int mouseDownY_ = 0;
    int lastDragY_ = 0;
    int zoomStartHeight_ = 0;
    int zoomAnchorNote_ = 0;
    static constexpr int DRAG_THRESHOLD = 3;

    // Note preview state
    int currentPlayingNote_ = -1;
    bool isPlayingNote_ = false;

    bool isBlackKey(int noteNumber) const;
    juce::String getNoteName(int noteNumber) const;
    int yToNoteNumber(int y) const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PianoRollKeyboard)
};

}  // namespace magda
