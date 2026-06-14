#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <set>
#include <vector>

#include "core/ChainNodePath.hpp"
#include "core/ModInfo.hpp"
#include "ui/components/common/curve/CurveEditorBase.hpp"

namespace magda {

/**
 * @brief Curve editor for LFO waveform editing
 *
 * Extends CurveEditorBase with LFO-specific functionality:
 * - Phase-based X coordinate (0 to 1)
 * - Seamless looping (last point connects to first)
 * - Integration with ModInfo for waveform storage
 * - Animated phase indicator showing current LFO position
 *
 * Used in the modulator editor panel for custom LFO shapes.
 */
class LFOCurveEditor : public CurveEditorBase, private juce::Timer {
  public:
    LFOCurveEditor();
    ~LFOCurveEditor() override;

    // Set the mod info to edit
    void setModInfo(ModInfo* mod);
    ModInfo* getModInfo() const {
        return modInfo_;
    }
    void setUndoTarget(const ChainNodePath& ownerPath, int modIndex);

    // Sync local points from modInfo (for external editor sync without rebuild)
    void syncFromModInfo();

    // CurveEditorBase coordinate interface
    double getPixelsPerX() const override;
    double getPixelsPerY() const override;
    double pixelToX(int px) const override;
    int xToPixel(double x) const override;
    double xToPixelF(double x) const override;
    double pixelToY(int py) const override;
    int yToPixel(double y) const override;
    double yToPixelF(double y) const override;

    // LFO loops seamlessly
    bool shouldLoop() const override {
        return true;
    }

    // Adding a point is a deliberate double-click; a single click on empty
    // canvas just clears the selection (avoids stray points).
    bool addsPointOnSingleClick() const override {
        return false;
    }

    // CurveEditorBase data access
    const std::vector<CurvePoint>& getPoints() const override;

    // Callback when waveform changes (on drag end)
    std::function<void()> onWaveformChanged;

    // Callback during drag for real-time preview sync
    std::function<void()> onDragPreview;

    // Phase indicator crosshair toggle
    void setShowCrosshair(bool show) {
        showCrosshair_ = show;
    }
    bool getShowCrosshair() const {
        return showCrosshair_;
    }

    // Grid settings
    void setGridDivisionsX(int divisions) {
        gridDivisionsX_ = juce::jmax(1, divisions);
        repaint();
    }
    int getGridDivisionsX() const {
        return gridDivisionsX_;
    }

    void setGridDivisionsY(int divisions) {
        gridDivisionsY_ = juce::jmax(1, divisions);
        repaint();
    }
    int getGridDivisionsY() const {
        return gridDivisionsY_;
    }

    // Snap settings
    void setSnapX(bool snap) {
        snapX_ = snap;
    }
    bool getSnapX() const {
        return snapX_;
    }

    void setSnapY(bool snap) {
        snapY_ = snap;
    }
    bool getSnapY() const {
        return snapY_;
    }

    // Snap loop-region markers to the X grid divisions while dragging.
    void setSnapLoop(bool snap) {
        snapLoop_ = snap;
    }
    bool getSnapLoop() const {
        return snapLoop_;
    }

    // Show/hide loop region markers
    void setShowLoopRegion(bool show) {
        showLoopRegion_ = show;
        repaint();
    }
    bool getShowLoopRegion() const {
        return showLoopRegion_;
    }

    // Load a preset curve shape
    void loadPreset(CurvePreset preset);

    // Load an arbitrary saved curve point set
    void loadCurvePoints(const std::vector<CurvePointData>& points);

  protected:
    // CurveEditorBase data mutation callbacks
    void onPointAdded(double x, double y, CurveType curveType) override;
    void onPointMoved(uint32_t pointId, double newX, double newY) override;
    void onPointDeleted(uint32_t pointId) override;
    void onDeleteSelectedPoints(const std::set<uint32_t>& pointIds) override;
    void onStepStamped(double gridStart, double gridEnd, double y, uint32_t prevPointId,
                       double prevValue) override;
    void onPointSelected(uint32_t pointId) override;
    void onTensionChanged(uint32_t pointId, double tension) override;
    void onHandlesChanged(uint32_t pointId, const CurveHandleData& inHandle,
                          const CurveHandleData& outHandle) override;
    void onSegmentShaperChanged(uint32_t leftPointId, const CurveHandleData& leftInHandle,
                                const CurveHandleData& leftOutHandle, uint32_t rightPointId,
                                const CurveHandleData& rightInHandle,
                                const CurveHandleData& rightOutHandle, bool isPreview) override;
    void onPointCurveTypeChanged(uint32_t pointId, CurveType newType) override;

    // Constrain edge points to x=0 and x=1
    void constrainPointPosition(uint32_t pointId, double& x, double& y) override;

    // Preview callbacks for fluid mini waveform updates
    void onPointDragPreview(uint32_t pointId, double newX, double newY) override;
    void onTensionDragPreview(uint32_t pointId, double tension) override;

    void paintGrid(juce::Graphics& g) override;
    void paint(juce::Graphics& g) override;

    // Handle C key for crosshair toggle
    bool keyPressed(const juce::KeyPress& key) override;

    // Intercept loop-region marker drags before the base class point editing.
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;

  private:
    void timerCallback() override;
    void paintPhaseIndicator(juce::Graphics& g);
    juce::Rectangle<int> getIndicatorBounds() const;

    ModInfo* modInfo_ = nullptr;
    ChainNodePath undoOwnerPath_;
    int undoModIndex_ = -1;
    bool segmentShaperUndoActive_ = false;
    CurvePreset segmentShaperUndoBeforePreset_ = CurvePreset::Custom;
    std::vector<CurvePointData> segmentShaperUndoBeforePoints_;

    // Local curve points for custom waveform
    mutable std::vector<CurvePoint> points_;
    uint32_t nextPointId_ = 1;

    // Selected point (local selection, not using SelectionManager)
    uint32_t selectedPointId_ = INVALID_CURVE_POINT_ID;

    // Phase indicator state
    bool showCrosshair_ = false;
    float lastPhase_ = 0.0f;
    float lastValue_ = 0.0f;

    // Trigger indicator state
    int lastSeenTriggerCount_ = 0;
    int triggerHoldFrames_ = 0;

    // Grid settings
    int gridDivisionsX_ = 4;  // Vertical lines (phase divisions)
    int gridDivisionsY_ = 4;  // Horizontal lines (value divisions)

    // Snap settings
    bool snapX_ = false;
    bool snapY_ = false;
    bool snapLoop_ = true;  // loop markers snap to the X grid by default

    // Loop region display + drag state (0 = none, 1 = start marker, 2 = end)
    bool showLoopRegion_ = false;
    int draggingLoopMarker_ = 0;
    // Hit-test the loop markers at a pixel position (grabbed via the handles in
    // the top strip so the rest of the curve stays free for points); 0/1/2.
    int loopMarkerAtPixel(int px, int py) const;

    void notifyWaveformChanged();
    std::vector<CurvePointData> snapshotCurvePoints() const;
    void commitUndoableCurveEdit(const std::vector<CurvePointData>& beforePoints,
                                 CurvePreset beforePreset, const juce::String& description);
    void paintLoopRegion(juce::Graphics& g);
};

}  // namespace magda
