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
 * Retrospect-style anchor point. Smooth anchors are circles; hard-angle
 * anchors are squares. Shows bezier handles when selected.
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

    // Size constants. POINT_SIZE/POINT_SIZE_SELECTED are the base (inline)
    // diameters; they are scaled up by pointScale() in the large popped-out
    // editor. HIT_SIZE is the fixed component footprint, sized to contain the
    // largest scaled anchor (incl. its selection ring) without clipping.
    static constexpr int POINT_SIZE = 5;
    static constexpr int POINT_SIZE_SELECTED = 6;
    static constexpr int HIT_SIZE = 18;

  private:
    uint32_t pointId_;
    CurveEditorBase* parentEditor_;
    CurvePoint point_;

    bool isSelected_ = false;
    bool isHovered_ = false;
    bool isDragging_ = false;
    bool handlesVisible_ = false;
    bool isRightClickPending_ = false;

    juce::Point<int> dragStartPos_;
    double dragStartX_ = 0.0;
    double dragStartY_ = 0.0;

    std::unique_ptr<CurveBezierHandle> inHandle_;
    std::unique_ptr<CurveBezierHandle> outHandle_;

    // Scale anchors with the editor height so the popped-out (external) editor
    // gets bigger, easier-to-grab points than the small inline preview.
    float pointScale() const;

    void createHandles();
    void updateHandlePositions();
    void onHandleChanged(CurveBezierHandle::HandleType type, double x, double y, bool linked);
};

}  // namespace magda
