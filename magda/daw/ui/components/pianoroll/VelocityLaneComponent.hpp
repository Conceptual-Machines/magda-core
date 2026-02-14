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

    // Selection awareness
    void setSelectedNoteIndices(const std::vector<size_t>& indices);

    // Callback for velocity changes
    std::function<void(ClipId, size_t noteIndex, int newVelocity)> onVelocityChanged;

    // Callback for batch velocity changes (Alt+drag ramp / curve)
    std::function<void(ClipId, std::vector<std::pair<size_t, int>>)> onMultiVelocityChanged;

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

    // Selected note indices (synced from SelectionManager)
    std::vector<size_t> selectedNoteIndices_;

    // Alt+drag ramp state
    bool isRampDragging_ = false;
    int rampStartVelocity_ = 0;
    int rampEndVelocity_ = 0;
    std::vector<size_t> sortedSelectedIndices_;  // sorted by beat position

    // Curve handle state
    bool isCurveHandleVisible_ = false;
    bool isCurveHandleDragging_ = false;
    float curveAmount_ = 0.0f;  // -1.0 to 1.0 (0 = linear)
    int curveHandleX_ = 0;
    int curveHandleY_ = 0;
    int curveHandleDragStartY_ = 0;
    float curveHandleDragStartAmount_ = 0.0f;
    static constexpr int CURVE_HANDLE_SIZE = 8;

    // Preview velocities during ramp/curve drag
    std::unordered_map<size_t, int> previewVelocities_;

    // Internal helpers for ramp/curve
    std::vector<std::pair<size_t, int>> computeRampVelocities() const;
    int interpolateVelocity(float t) const;
    bool hitTestCurveHandle(int x, int y) const;
    void updateCurveHandle();
    void updatePreviewVelocities();

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
