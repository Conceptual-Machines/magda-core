#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "core/ClipInfo.hpp"
#include "core/ClipManager.hpp"
#include "core/ClipTypes.hpp"

namespace magica {

// Forward declarations
class TrackContentPanel;

/**
 * @brief Visual representation of a clip in the arrange view
 *
 * Handles:
 * - Clip rendering (different styles for Audio vs MIDI)
 * - Drag to move (horizontally and to other tracks)
 * - Resize handles (left/right edges)
 * - Selection
 */
class ClipComponent : public juce::Component, public ClipManagerListener {
  public:
    explicit ClipComponent(ClipId clipId, TrackContentPanel* parent);
    ~ClipComponent() override;

    ClipId getClipId() const {
        return clipId_;
    }

    // Component overrides
    void paint(juce::Graphics& g) override;
    void resized() override;

    // Mouse handling
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent& e) override;
    void mouseDoubleClick(const juce::MouseEvent& e) override;

    // ClipManagerListener
    void clipsChanged() override;
    void clipPropertyChanged(ClipId clipId) override;
    void clipSelectionChanged(ClipId clipId) override;

    // Selection state
    bool isSelected() const {
        return isSelected_;
    }
    void setSelected(bool selected);

    // Drag state (for parent to check)
    bool isCurrentlyDragging() const {
        return isDragging_;
    }

    // Callbacks
    std::function<void(ClipId, double)> onClipMoved;          // clipId, newStartTime
    std::function<void(ClipId, TrackId)> onClipMovedToTrack;  // clipId, newTrackId
    std::function<void(ClipId, double, bool)> onClipResized;  // clipId, newLength, fromStart
    std::function<void(ClipId)> onClipSelected;
    std::function<void(ClipId)> onClipDoubleClicked;
    std::function<double(double)> snapTimeToGrid;  // Optional grid snapping

  private:
    ClipId clipId_;
    TrackContentPanel* parentPanel_;
    bool isSelected_ = false;

    // Interaction state
    enum class DragMode { None, Move, ResizeLeft, ResizeRight };
    DragMode dragMode_ = DragMode::None;

    // Drag state
    juce::Point<int> dragStartPos_;
    juce::Point<int> dragStartBoundsPos_;  // Original bounds position at drag start
    double dragStartTime_ = 0.0;
    double dragStartLength_ = 0.0;
    TrackId dragStartTrackId_ = INVALID_TRACK_ID;

    // Preview state during drag (visual only, not committed until mouseUp)
    double previewStartTime_ = 0.0;
    double previewLength_ = 0.0;
    bool isDragging_ = false;

    // Magnetic snap threshold in pixels (higher = snappier)
    static constexpr int SNAP_THRESHOLD_PIXELS = 15;

    // Hover state for resize handles
    bool hoverLeftEdge_ = false;
    bool hoverRightEdge_ = false;

    // Visual constants
    static constexpr int RESIZE_HANDLE_WIDTH = 6;
    static constexpr int CORNER_RADIUS = 4;
    static constexpr int HEADER_HEIGHT = 16;
    static constexpr int MIN_WIDTH_FOR_NAME = 40;

    // Painting helpers
    void paintAudioClip(juce::Graphics& g, const ClipInfo& clip, juce::Rectangle<int> bounds);
    void paintMidiClip(juce::Graphics& g, const ClipInfo& clip, juce::Rectangle<int> bounds);
    void paintClipHeader(juce::Graphics& g, const ClipInfo& clip, juce::Rectangle<int> bounds);
    void paintResizeHandles(juce::Graphics& g, juce::Rectangle<int> bounds);

    // Interaction helpers
    bool isOnLeftEdge(int x) const;
    bool isOnRightEdge(int x) const;
    void updateCursor();

    // Helper to get current clip info
    const ClipInfo* getClipInfo() const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ClipComponent)
};

}  // namespace magica
