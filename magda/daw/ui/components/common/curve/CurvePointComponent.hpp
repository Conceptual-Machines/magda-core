#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>

#include "CurveBezierHandle.hpp"
#include "CurveTypes.hpp"

namespace magda {

class CurveEditorBase;

/**
 * @brief A single draggable point on an editable curve
 *
 * 8px circle normally, 10px when selected. Shows bezier handles when selected.
 * Drag to move position.
 */
class CurvePointComponent : public juce::Component {
  public:
    CurvePointComponent(uint32_t pointId, CurveEditorBase* parent);
    ~CurvePointComponent() override;

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
    uint32_t getPointId() const {
        return pointId_;
    }
    bool isSelected() const {
        return isSelected_;
    }
    void setSelected(bool selected);

    // Update from data
    void updateFromPoint(const CurvePoint& point);
    CurvePoint getPoint() const {
        return point_;
    }

    // Show/hide bezier handles
    void showHandles(bool show);
    bool handlesVisible() const {
        return handlesVisible_;
    }

    // Access to parent editor for coordinate conversion
    CurveEditorBase* getParentEditor() const {
        return parentEditor_;
    }

    // Callbacks
    std::function<void(uint32_t)> onPointSelected;
    std::function<void(uint32_t, double, double)> onPointMoved;  // id, newX, newY
    std::function<void(uint32_t, double, double)> onPointDragPreview;
    std::function<void(uint32_t)> onPointDeleted;
    std::function<void(uint32_t, const CurveHandleData&, const CurveHandleData&)> onHandlesChanged;
    std::function<void(uint32_t, bool)> onPointHovered;  // id, isHovered

    // Size constants
    static constexpr int POINT_SIZE = 6;
    static constexpr int POINT_SIZE_SELECTED = 8;
    static constexpr int HIT_SIZE = 16;

  private:
    uint32_t pointId_;
    CurveEditorBase* parentEditor_;
    CurvePoint point_;

    bool isSelected_ = false;
    bool isHovered_ = false;
    bool isDragging_ = false;
    bool handlesVisible_ = false;

    juce::Point<int> dragStartPos_;
    double dragStartX_ = 0.0;
    double dragStartY_ = 0.0;

    std::unique_ptr<CurveBezierHandle> inHandle_;
    std::unique_ptr<CurveBezierHandle> outHandle_;

    void createHandles();
    void updateHandlePositions();
    void onHandleChanged(CurveBezierHandle::HandleType type, double x, double y, bool linked);
};

}  // namespace magda
