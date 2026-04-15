#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <cmath>
#include <functional>
#include <map>
#include <memory>
#include <set>
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
    void paintOverChildren(juce::Graphics& g) override;
    void resized() override;

    // Mouse interaction
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseDoubleClick(const juce::MouseEvent& e) override;
    bool keyPressed(const juce::KeyPress& key) override;
    void modifierKeysChanged(const juce::ModifierKeys& modifiers) override;

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

    // Format a value label for a given normalized Y (0-1). Override in subclasses.
    virtual juce::String formatValueLabel(double y) const {
        return juce::String(juce::roundToInt(y * 100)) + "%";
    }

    // Data access - must be implemented by subclasses
    virtual const std::vector<CurvePoint>& getPoints() const = 0;

    // Selection
    void clearSelection();
    bool isPointSelected(uint32_t pointId) const;
    const std::set<uint32_t>& getSelectedPointIds() const {
        return selectedPointIds_;
    }

    // Snapping
    std::function<double(double)> snapXToGrid;
    std::function<double(double)> snapYToGrid;  // Snap a normalized Y (0-1) to grid
    std::function<double()> getGridSpacingX;    // Returns grid step size in X units

  protected:
    CurveDrawMode drawMode_ = CurveDrawMode::Select;
    juce::Colour curveColour_{0xFF6688CC};  // Default curve color
    int padding_ = 5;                       // Content area padding (>= half of point size)

    // Components
    std::vector<std::unique_ptr<CurvePointComponent>> pointComponents_;
    std::vector<std::unique_ptr<CurveBezierHandle>> handleComponents_;
    std::vector<std::unique_ptr<CurveTensionHandle>> tensionHandles_;

    // Selection state
    std::set<uint32_t> selectedPointIds_;

    // Effective draw mode (resolved from modifiers at mouseDown)
    CurveDrawMode activeDrawMode_ = CurveDrawMode::Select;

    // Drawing state
    bool isDrawing_ = false;
    std::vector<juce::Point<int>> drawingPath_;
    juce::Point<int> lineStartPoint_;

    // Lasso selection state
    bool isLassoActive_ = false;
    juce::Rectangle<int> lassoRect_;
    juce::Point<int> lassoAnchor_;

    // Hovered point for value tooltip
    uint32_t hoveredPointId_ = INVALID_CURVE_POINT_ID;

    // Drag preview state
    uint32_t previewPointId_ = INVALID_CURVE_POINT_ID;
    double previewX_ = 0.0;
    double previewY_ = 0.0;

    // Multi-point drag: start positions (snapshotted when drag begins) and
    // current preview positions for all selected points except the lead.
    std::map<uint32_t, std::pair<double, double>> multiDragStartPositions_;
    std::map<uint32_t, std::pair<double, double>> multiPreviewPositions_;

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

    // Batch selection callback for lasso - override in subclasses
    virtual void onPointsSelected(const std::vector<uint32_t>& pointIds) {
        juce::ignoreUnused(pointIds);
    }

    // Batch delete callback - override in subclasses to execute delete commands
    virtual void onDeleteSelectedPoints(const std::set<uint32_t>& pointIds) {
        juce::ignoreUnused(pointIds);
    }

    // Batch move callback — called instead of onPointMoved when multiple
    // points are selected and the user drags any one of them. The map
    // contains the final {x, y} for every selected point (including the lead).
    // Default falls back to individual onPointMoved calls for each entry.
    virtual void onSelectedPointsMoved(
        const std::map<uint32_t, std::pair<double, double>>& finalPositions) {
        for (const auto& [id, pos] : finalPositions)
            onPointMoved(id, pos.first, pos.second);
    }

    // Step stamp callback — creates a Serum-style "dip" cell:
    //   - Left edge: cliff from prevValue down/up to y at gridStart
    //   - Cell holds at y across one grid division
    //   - Right edge: cliff back from y to prevValue at gridEnd
    // Subclasses override to group the mutations into one undo step AND
    // flip the preceding point's curveType to Step so its outgoing
    // segment becomes the left-edge cliff instead of a linear fade.
    // prevPointId is INVALID_CURVE_POINT_ID when nothing exists before
    // the stamp position; in that case there is no baseline to return to,
    // so the cell is stamped as a single point and extends rightward.
    virtual void onStepStamped(double gridStart, double gridEnd, double y, uint32_t prevPointId,
                               double prevValue);

    // Constrain point position during drag (override to pin edge points, etc.)
    virtual void constrainPointPosition(uint32_t pointId, double& x, double& y) {
        juce::ignoreUnused(pointId, x, y);
    }

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

    // Sync selection state — re-apply selectedPointIds_ to point components after rebuild
    virtual void syncSelectionState();

  private:
    // Curve rendering helper
    void renderCurveSegment(juce::Path& path, const CurvePoint& p1, const CurvePoint& p2,
                            double effectiveTension);
};

}  // namespace magda
