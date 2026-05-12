#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>

#include "BezierHandleComponent.hpp"
#include "core/AutomationInfo.hpp"

namespace magda {

class AutomationCurveEditor;

/**
 * @brief A single draggable point on an automation curve
 *
 * 8px circle normally, 10px when selected. Shows bezier handles when selected.
 * Drag to move beat/value position.
 */
class AutomationPointComponent : public juce::Component {
  public:
    AutomationPointComponent(AutomationPointId pointId, AutomationCurveEditor* parent);
    ~AutomationPointComponent() override;

    // Component
    void paint(juce::Graphics& g) override;
    void resized() override;
    bool hitTest(int x, int y) override;

    // Mouse interaction
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseEnter(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent& e) override;
    void mouseDoubleClick(const juce::MouseEvent& e) override;

    // State
    AutomationPointId getPointId() const {
        return pointId_;
    }
    bool isSelected() const {
        return isSelected_;
    }
    void setSelected(bool selected);

    // Update from data
    void updateFromPoint(const AutomationPoint& point);
    AutomationPoint getPoint() const {
        return point_;
    }

    // Show/hide bezier handles
    void showHandles(bool show);
    bool handlesVisible() const {
        return handlesVisible_;
    }

    // Callbacks
    std::function<void(AutomationPointId)> onPointSelected;
    std::function<void(AutomationPointId, double, double)> onPointMoved;  // id, beat, value
    std::function<void(AutomationPointId, double, double)> onPointDragPreview;
    std::function<void(AutomationPointId)> onPointDeleted;
    std::function<void(AutomationPointId, const BezierHandle&, const BezierHandle&)>
        onHandlesChanged;

    // Size constants
    static constexpr int POINT_SIZE = 8;
    static constexpr int POINT_SIZE_SELECTED = 10;
    static constexpr int HIT_SIZE = 16;

  private:
    AutomationPointId pointId_;
    AutomationCurveEditor* parentEditor_;
    AutomationPoint point_;

    bool isSelected_ = false;
    bool isHovered_ = false;
    bool isDragging_ = false;
    bool handlesVisible_ = false;

    juce::Point<int> dragStartPos_;
    double dragStartBeatPosition_ = 0.0;
    double dragStartValue_ = 0.0;

    std::unique_ptr<BezierHandleComponent> inHandle_;
    std::unique_ptr<BezierHandleComponent> outHandle_;

    void createHandles();
    void updateHandlePositions();
    void onHandleChanged(BezierHandleComponent::HandleType type, const BezierHandle& handle);
};

}  // namespace magda
