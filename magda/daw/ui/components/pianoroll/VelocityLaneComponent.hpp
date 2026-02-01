#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <unordered_map>
#include <vector>

#include "core/ClipTypes.hpp"

namespace magda {

/**
 * @brief Velocity lane editor for MIDI notes
 *
 * Displays vertical bars representing note velocities.
 * Users can click and drag to adjust velocity values.
 */
class VelocityLaneComponent : public juce::Component {
  public:
    VelocityLaneComponent();
    ~VelocityLaneComponent() override = default;

    // Set the clip to display/edit
    void setClip(ClipId clipId);
    void setClipIds(const std::vector<ClipId>& clipIds);
    ClipId getClipId() const {
        return clipId_;
    }

    // Zoom and scroll settings
    void setPixelsPerBeat(double ppb);
    void setScrollOffset(int offsetX);
    void setLeftPadding(int padding);

    // Display mode
    void setRelativeMode(bool relative);
    void setClipStartBeats(double startBeats);
    void setClipLengthBeats(double lengthBeats);

    // Loop region
    void setLoopRegion(double offsetBeats, double lengthBeats, bool enabled);

    // Refresh from clip data
    void refreshNotes();

    // Set preview position for a note during drag (for syncing with grid)
    void setNotePreviewPosition(size_t noteIndex, double previewBeat, bool isDragging);

    // Callback for velocity changes
    std::function<void(ClipId, size_t noteIndex, int newVelocity)> onVelocityChanged;

    // Component overrides
    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;

  private:
    ClipId clipId_ = INVALID_CLIP_ID;
    std::vector<ClipId> clipIds_;
    double pixelsPerBeat_ = 50.0;
    int scrollOffsetX_ = 0;
    int leftPadding_ = 2;
    bool relativeMode_ = true;
    double clipStartBeats_ = 0.0;
    double clipLengthBeats_ = 0.0;

    // Loop region
    double loopOffsetBeats_ = 0.0;
    double loopLengthBeats_ = 0.0;
    bool loopEnabled_ = false;

    // Drag state
    size_t draggingNoteIndex_ = SIZE_MAX;
    int dragStartVelocity_ = 0;
    int currentDragVelocity_ = 0;
    bool isDragging_ = false;

    // Preview positions for notes being dragged in the grid
    std::unordered_map<size_t, double> notePreviewPositions_;

    // Coordinate conversion
    int beatToPixel(double beat) const;
    double pixelToBeat(int x) const;
    int velocityToY(int velocity) const;
    int yToVelocity(int y) const;

    // Find note at given x coordinate
    size_t findNoteAtX(int x) const;

    // Get clip color
    juce::Colour getClipColour() const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VelocityLaneComponent)
};

}  // namespace magda
