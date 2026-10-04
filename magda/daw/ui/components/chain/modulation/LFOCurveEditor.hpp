#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <optional>
#include <vector>

#include "core/ChainNodePath.hpp"
#include "core/ModInfo.hpp"
#include "magda/sdk/curveedit/CurveEditor.hpp"
#include "magda/sdk/display/DisplayList.hpp"
#include "ui/themes/ActiveTheme.hpp"

namespace magda {

/**
 * @brief The LFO and sidechain curve editor: a JUCE shell over sdk::CurveEditor.
 *
 * The core owns the points, gestures and drawing (magda-sdk docs/curve-editor.md); this shell
 * feeds it mouse and key events, writes its results to the ModInfo, records one undo step per
 * gesture and draws its display list with the active theme.
 */
class LFOCurveEditor : public juce::Component, private juce::Timer {
  public:
    LFOCurveEditor();
    ~LFOCurveEditor() override;

    void setModInfo(ModInfo* mod);
    ModInfo* getModInfo() const {
        return modInfo_;
    }
    void setUndoTarget(const ChainNodePath& ownerPath, int modIndex);

    /// Follows another editor's drag: values only, no reload.
    void syncFromModInfo();

    /// After an edit commits (one undo step).
    std::function<void()> onWaveformChanged;
    /// While a gesture moves the curve or the loop region.
    std::function<void()> onDragPreview;

    void setCurveColour(juce::Colour colour) {
        curveColourRole_.reset();
        curveColour_ = colour;
        repaint();
    }
    void setCurveColour(ColourRole role) {
        curveColourRole_ = role;
        repaint();
    }
    juce::Colour getCurveColour() const {
        return curveColourRole_ ? ActiveTheme::getColour(*curveColourRole_) : curveColour_;
    }

    void setPadding(int padding) {
        core_.setPadding(padding);
        repaint();
    }
    int getPadding() const {
        return core_.padding();
    }

    /// A 1px frame around the curve field; the Sidechain faceplate turns it on.
    void setDrawContentBorder(bool draw) {
        core_.setDrawContentBorder(draw);
        repaint();
    }

    void setShowCrosshair(bool show) {
        core_.setShowCrosshair(show);
    }
    bool getShowCrosshair() const {
        return core_.showCrosshair();
    }

    void setGridDivisionsX(int divisions);
    int getGridDivisionsX() const {
        return core_.gridDivisionsX();
    }
    void setGridDivisionsY(int divisions);
    int getGridDivisionsY() const {
        return core_.gridDivisionsY();
    }

    void setSnapX(bool snap);
    bool getSnapX() const {
        return snapX_;
    }
    void setSnapY(bool snap);
    bool getSnapY() const {
        return snapY_;
    }
    /// Loop markers snap to the X grid while dragging.
    void setSnapLoop(bool snap);
    bool getSnapLoop() const {
        return snapLoop_;
    }

    void setShowLoopRegion(bool show) {
        showLoopRegion_ = show;
        repaint();
    }
    bool getShowLoopRegion() const {
        return showLoopRegion_;
    }

    void loadPreset(CurvePreset preset);
    void loadCurvePoints(const std::vector<CurvePointData>& points);

    /// The committed curve.
    std::vector<CurvePointData> getCurvePoints() const {
        return core_.points();
    }

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseDoubleClick(const juce::MouseEvent& e) override;
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseEnter(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent& e) override;
    bool keyPressed(const juce::KeyPress& key) override;
    void modifierKeysChanged(const juce::ModifierKeys& modifiers) override;

  private:
    void timerCallback() override;
    /// Pushes the ModInfo's live state (loop region, indicator) into the core.
    void syncSurface();
    void apply(const sdk::CurveEditorResponse& response);
    void updateCursor(const juce::ModifierKeys& modifiers);
    void notifyWaveformChanged();
    void commitUndoableCurveEdit(const std::vector<CurvePointData>& beforePoints,
                                 CurvePreset beforePreset, const juce::String& description);
    juce::Colour roleColour(sdk::display::ColourRole role) const;

    sdk::CurveEditor core_;
    sdk::display::DisplayList displayList_;

    ModInfo* modInfo_ = nullptr;
    ChainNodePath undoOwnerPath_;
    int undoModIndex_ = -1;

    juce::Colour curveColour_{ActiveTheme::getColour(ActiveTheme::AUTOMATION_BEZIER)};
    std::optional<ColourRole> curveColourRole_{ActiveTheme::AUTOMATION_BEZIER};

    bool snapX_ = false;
    bool snapY_ = false;
    bool snapLoop_ = true;
    bool showLoopRegion_ = false;

    float lastPhase_ = 0.0f;
    float lastValue_ = 0.0f;
    uint32_t lastSeenTriggerCount_ = 0;
    int triggerHoldFrames_ = 0;
};

}  // namespace magda
