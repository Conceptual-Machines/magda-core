#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <cmath>
#include <functional>
#include <memory>
#include <vector>

#include "CurveBezierHandle.hpp"
#include "CurvePointComponent.hpp"
#include "CurveTensionHandle.hpp"
#include "CurveTypes.hpp"

namespace magda {

/**
 * @brief Abstract base class for curve editing surfaces
 *
 * Provides common functionality for rendering and editing curves with:
 * - Linear, bezier, and step interpolation
 * - Tension-based curve shaping
 * - Point and handle component management
 * - Drawing tools (select, pencil, line, curve)
 * - Preview state during drag operations
 *
 * Subclasses implement:
 * - Data source access (getPoints, mutation callbacks)
 * - Coordinate conversion (x/y to pixel and back)
 * - Edge behavior (loop for LFO, extend for automation)
 */
class CurveEditorBase : public juce::Component {
  public:
    CurveEditorBase();
    ~CurveEditorBase() override;

    // Component
    void paint(juce::Graphics& g) override;
    void resized() override;

    // Mouse interaction
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseDoubleClick(const juce::MouseEvent& e) override;
    bool keyPressed(const juce::KeyPress& key) override;

    // Configuration
    void setDrawMode(CurveDrawMode mode) {
        drawMode_ = mode;
    }
    CurveDrawMode getDrawMode() const {
        return drawMode_;
    }

    void setCurveColour(juce::Colour colour) {
        curveColour_ = colour;
    }
    juce::Colour getCurveColour() const {
        return curveColour_;
    }

    // Padding for content area
    void setPadding(int padding) {
        padding_ = padding;
    }
    int getPadding() const {
        return padding_;
    }

    // Get content bounds (area minus padding)
    juce::Rectangle<int> getContentBounds() const {
        return getLocalBounds().reduced(padding_);
    }

    // Coordinate conversion - must be implemented by subclasses
    virtual double getPixelsPerX() const = 0;  // Pixels per X unit (time or phase)
    virtual double getPixelsPerY() const {
        auto content = getContentBounds();
        return content.getHeight() > 0 ? static_cast<double>(content.getHeight()) : 100.0;
    }

    virtual double pixelToX(int px) const = 0;  // Convert pixel to X coordinate
    virtual int xToPixel(double x) const = 0;   // Convert X to pixel
    double pixelToY(int py) const;              // Convert pixel to Y (value 0-1)
    int yToPixel(double y) const;               // Convert Y to pixel

    // Loop behavior - override for LFO to enable seamless looping
    virtual bool shouldLoop() const {
        return false;
    }

    // Data access - must be implemented by subclasses
    virtual const std::vector<CurvePoint>& getPoints() const = 0;

    // Snapping
    std::function<double(double)> snapXToGrid;

  protected:
    CurveDrawMode drawMode_ = CurveDrawMode::Select;
    juce::Colour curveColour_{0xFF6688CC};  // Default curve color
    int padding_ = 2;                       // Content area padding

    // Components
    std::vector<std::unique_ptr<CurvePointComponent>> pointComponents_;
    std::vector<std::unique_ptr<CurveBezierHandle>> handleComponents_;
    std::vector<std::unique_ptr<CurveTensionHandle>> tensionHandles_;

    // Drawing state
    bool isDrawing_ = false;
    std::vector<juce::Point<int>> drawingPath_;
    juce::Point<int> lineStartPoint_;

    // Drag preview state
    uint32_t previewPointId_ = INVALID_CURVE_POINT_ID;
    double previewX_ = 0.0;
    double previewY_ = 0.0;

    // Tension preview state
    uint32_t tensionPreviewPointId_ = INVALID_CURVE_POINT_ID;
    double tensionPreviewValue_ = 0.0;

    // Rebuild components from data
    virtual void rebuildPointComponents();
    virtual void updatePointPositions();
    void updateTensionHandlePositions();

    // Drawing
    virtual void paintCurve(juce::Graphics& g);
    virtual void paintGrid(juce::Graphics& g);
    void paintDrawingPreview(juce::Graphics& g);

    // Data mutation callbacks - must be implemented by subclasses
    virtual void onPointAdded(double x, double y, CurveType curveType) = 0;
    virtual void onPointMoved(uint32_t pointId, double newX, double newY) = 0;
    virtual void onPointDeleted(uint32_t pointId) = 0;
    virtual void onPointSelected(uint32_t pointId) = 0;
    virtual void onTensionChanged(uint32_t pointId, double tension) = 0;
    virtual void onHandlesChanged(uint32_t pointId, const CurveHandleData& inHandle,
                                  const CurveHandleData& outHandle) = 0;

    // Preview callbacks for fluid updates during drag (optional override)
    virtual void onPointDragPreview(uint32_t pointId, double newX, double newY) {
        juce::ignoreUnused(pointId, newX, newY);
    }
    virtual void onTensionDragPreview(uint32_t pointId, double tension) {
        juce::ignoreUnused(pointId, tension);
    }

    // Helper to get effective position during preview
    std::pair<double, double> getEffectivePosition(const CurvePoint& p) const;

    // Pencil drawing
    void createPointsFromDrawingPath();

    // Sync selection state (subclass can override)
    virtual void syncSelectionState() {}

  private:
    // Curve rendering helper
    void renderCurveSegment(juce::Path& path, const CurvePoint& p1, const CurvePoint& p2,
                            double effectiveTension);
};

}  // namespace magda
