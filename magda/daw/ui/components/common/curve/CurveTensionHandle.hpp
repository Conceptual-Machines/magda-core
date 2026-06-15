#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <cstdint>
#include <functional>

namespace magda {

/**
 * @brief Draggable segment shaper handle between points
 *
 * Appears on a curve segment. Dragging moves the actual Retrospect-style
 * shaper point in X/Y; right-click toggles the segment shape.
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

    void setSlopeGoesDown(bool goesDown) {
        juce::ignoreUnused(goesDown);
    }

    // Callbacks
    std::function<void(uint32_t, double)> onTensionChanged;
    std::function<void(uint32_t, double)> onTensionDragPreview;
    std::function<void(uint32_t, double, double)> onShaperChanged;
    std::function<void(uint32_t, double, double)> onShaperDragPreview;
    std::function<void(uint32_t)> onRightClick;

    static constexpr int HANDLE_SIZE = 9;

  private:
    uint32_t pointId_;
    double tension_ = 0.0;
    bool isDragging_ = false;
    bool isHovered_ = false;
    double dragOffsetX_ = 0.0;
    double dragOffsetY_ = 0.0;
    double dragStartTension_ = 0.0;
};

}  // namespace magda
