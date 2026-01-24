#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <cstdint>
#include <functional>

namespace magda {

/**
 * @brief Draggable handle for adjusting curve tension between points
 *
 * Appears at the midpoint of a curve segment. Dragging up/down adjusts
 * the tension from concave (-1) through linear (0) to convex (+1).
 * With Shift held, extends to extreme range (-3 to +3).
 */
class CurveTensionHandle : public juce::Component {
  public:
    explicit CurveTensionHandle(uint32_t pointId);
    ~CurveTensionHandle() override = default;

    void paint(juce::Graphics& g) override;

    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseEnter(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent& e) override;

    uint32_t getPointId() const {
        return pointId_;
    }

    void setTension(double tension) {
        tension_ = tension;
        repaint();
    }
    double getTension() const {
        return tension_;
    }

    // Set whether the curve segment goes downward (y2 < y1)
    // When true, drag direction is inverted so "up" always bends outward
    void setSlopeGoesDown(bool goesDown) {
        slopeGoesDown_ = goesDown;
    }

    // Callbacks
    std::function<void(uint32_t, double)> onTensionChanged;
    std::function<void(uint32_t, double)> onTensionDragPreview;

    static constexpr int HANDLE_SIZE = 10;

  private:
    uint32_t pointId_;
    double tension_ = 0.0;
    bool isDragging_ = false;
    bool isHovered_ = false;
    bool slopeGoesDown_ = false;  // True if curve segment goes downward
    int dragStartY_ = 0;
    double dragStartTension_ = 0.0;
};

}  // namespace magda
