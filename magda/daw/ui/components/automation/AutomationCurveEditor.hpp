#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <vector>

#include "core/AutomationInfo.hpp"
#include "core/AutomationManager.hpp"
#include "core/AutomationTypes.hpp"
#include "core/SelectionManager.hpp"
#include "ui/components/common/curve/CurveEditorBase.hpp"

namespace magda {

/**
 * @brief Curve editing surface for automation data
 *
 * Renders automation curves (linear, bezier, step) and manages
 * point components. Supports drawing tools: Select, Pencil, Line.
 * Double-click to add point, Delete to remove.
 *
 * Extends CurveEditorBase with automation-specific functionality:
 * - Time-based X coordinate (seconds)
 * - Integration with AutomationManager for data persistence
 * - SelectionManager integration for multi-point selection
 */
class AutomationCurveEditor : public CurveEditorBase,
                              public AutomationManagerListener,
                              public SelectionManagerListener {
  public:
    explicit AutomationCurveEditor(AutomationLaneId laneId);
    ~AutomationCurveEditor() override;

    // Component
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void paintOverChildren(juce::Graphics& g) override;

    // AutomationManagerListener
    void automationLanesChanged() override;
    void automationLanePropertyChanged(AutomationLaneId laneId) override;
    void automationPointsChanged(AutomationLaneId laneId) override;
    void automationPointDragPreview(AutomationLaneId laneId, AutomationPointId pointId,
                                    double previewTime, double previewValue) override;

    // SelectionManagerListener
    void selectionTypeChanged(SelectionType newType) override;
    void automationPointSelectionChanged(const AutomationPointSelection& selection) override;

    // Syncs the curve colour with lane state (purple when active, grey when
    // bypassed/overridden). Called from listener callbacks — no polling.
    void refreshCurveColour();

    // Configuration
    void setLaneId(AutomationLaneId laneId);
    AutomationLaneId getLaneId() const {
        return laneId_;
    }

    // Draw mode using automation-specific type (delegates to base)
    void setDrawMode(AutomationDrawMode mode);
    AutomationDrawMode getAutomationDrawMode() const;

    // Coordinate conversion
    void setPixelsPerSecond(double pps) {
        pixelsPerSecond_ = pps;
    }
    double getPixelsPerSecond() const {
        return pixelsPerSecond_;
    }
    void setPixelsPerBeat(double ppb);
    void setTempoBPM(double bpm) {
        tempoBPM_ = bpm;
    }

    // CurveEditorBase coordinate interface
    double getPixelsPerX() const override {
        return pixelsPerBeat_;
    }
    double pixelToX(int px) const override;
    int xToPixel(double x) const override;

    // Snapping (uses base class snapXToGrid)
    std::function<double(double)> snapTimeToGrid;
    std::function<double()> getGridSpacingBeats;  // Grid step in beats

    // Clip mode (for clip-based automation)
    void setClipId(AutomationClipId clipId) {
        clipId_ = clipId;
    }
    AutomationClipId getClipId() const {
        return clipId_;
    }
    void setClipOffset(double offset) {
        clipOffset_ = offset;
    }

    // CurveEditorBase data access
    const std::vector<CurvePoint>& getPoints() const override;
    juce::String formatValueLabel(double y) const override;

  protected:
    // CurveEditorBase data mutation callbacks
    void onSelectedPointsMoved(
        const std::map<uint32_t, std::pair<double, double>>& finalPositions) override;
    void onPointAdded(double x, double y, CurveType curveType) override;
    void onPointMoved(uint32_t pointId, double newX, double newY) override;
    void onPointDragPreview(uint32_t pointId, double newX, double newY) override;
    void onPointDeleted(uint32_t pointId) override;
    void onPointSelected(uint32_t pointId) override;
    void onTensionChanged(uint32_t pointId, double tension) override;
    void onHandlesChanged(uint32_t pointId, const CurveHandleData& inHandle,
                          const CurveHandleData& outHandle) override;

    void onDeleteSelectedPoints(const std::set<uint32_t>& pointIds) override;
    void onStepStamped(double gridStart, double gridEnd, double y, uint32_t prevPointId,
                       double prevValue) override;
    void paintGrid(juce::Graphics& g) override;
    void syncSelectionState() override;
    void rebuildPointComponents() override;

  private:
    AutomationLaneId laneId_;
    AutomationClipId clipId_ = INVALID_AUTOMATION_CLIP_ID;
    double clipOffset_ = 0.0;
    double pixelsPerSecond_ = 100.0;
    double pixelsPerBeat_ = 10.0;
    double tempoBPM_ = 120.0;

    // Cached curve points (converted from AutomationPoints)
    mutable std::vector<CurvePoint> cachedPoints_;
    mutable bool pointsCacheDirty_ = true;
    bool isRightClickPending_ = false;

    void updatePointsCache() const;
    void deleteSelectedPoints();
    // Shown on right-click anywhere in the curve body, or forwarded from a
    // CurvePointComponent right-click so the menu isn't swallowed by points.
    void showContextMenu();
    void paintOverrideOverlay(juce::Graphics& g);

    // Quantize a normalized value to the parameter's natural grid when
    // the lane's snapValue flag is enabled.
    double applyValueSnap(double normalized) const;

    // Convert between AutomationCurveType and CurveType
    static CurveType toCurveType(AutomationCurveType type);
    static AutomationCurveType toAutomationCurveType(CurveType type);
};

}  // namespace magda
