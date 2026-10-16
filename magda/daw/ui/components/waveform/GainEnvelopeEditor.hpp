#pragma once

#include <optional>
#include <utility>
#include <vector>

#include "core/ClipInfo.hpp"
#include "ui/components/common/EditTool.hpp"
#include "ui/components/common/curve/CurveEditorBase.hpp"

namespace magda::daw::ui {

/**
 * @brief What a gain envelope editor sits over: its x axis and how that axis meets the source.
 *
 * Pixels are the editor's own. x is linear in them; the source behind it need not be.
 */
class GainEnvelopeHost {
  public:
    virtual ~GainEnvelopeHost() = default;

    virtual double pixelsPerX() const = 0;
    virtual double xToPixel(double x) const = 0;
    virtual double pixelToX(double px) const = 0;

    virtual double sourceAt(double x) const = 0;
    /// Every x @p sourceSeconds plays at: one, or one per loop pass.
    virtual std::vector<double> xsOf(double sourceSeconds) const = 0;
    virtual std::pair<double, double> xRange() const = 0;

    virtual bool snapsToGrid() const = 0;
    virtual double snapToGrid(double x) const = 0;
    virtual double gridSpacing() const = 0;

    /// A press the host keeps for itself, such as a trim edge.
    virtual bool keepsPress(int /*x*/, int /*y*/) const {
        return false;
    }
    /// Any press on the envelope, before it edits.
    virtual void envelopePressed() {}
    /// Right-click: true when the host shows its own menu instead.
    virtual bool showContextMenu() {
        return false;
    }
    virtual bool showsLevelGuides() const {
        return true;
    }
};

/**
 * @brief A clip's gain envelope, drawn and edited over its waveform (#3064).
 *
 * Points are stored in source seconds and converted at the edges through the host, and the curve
 * is drawn by sampling each pixel's source position, so what is drawn is what plays through
 * reverse, loops and warp.
 */
class GainEnvelopeEditor : public magda::CurveEditorBase, private juce::ChangeListener {
  public:
    explicit GainEnvelopeEditor(GainEnvelopeHost& host);
    ~GainEnvelopeEditor() override;

    bool hitTest(int x, int y) override;

    void setClip(magda::ClipId clipId);

    /// Re-read the clip's points and place them for the grid's current mapping.
    void refresh();

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;

    double getPixelsPerX() const override;
    double pixelToX(int px) const override;
    int xToPixel(double x) const override;
    double xToPixelF(double x) const override;
    juce::String formatValueLabel(double y) const override;
    const std::vector<magda::CurvePoint>& getPoints() const override;
    magda::CurveDrawMode plainPressMode() const override;

  protected:
    void paintGrid(juce::Graphics& g) override;
    void paintCurve(juce::Graphics& g) override;
    void rebuildPointComponents() override;
    void paintLevelGuides(juce::Graphics& g, int left, int right);
    void updateCursorForModifiers(const juce::ModifierKeys& modifiers) override;

    void onPointAdded(double x, double y, magda::CurveType curveType) override;
    void onPointMoved(uint32_t pointId, double newX, double newY) override;
    void onPointDeleted(uint32_t pointId) override;
    void onPointSelected(uint32_t pointId) override;
    void onTensionChanged(uint32_t pointId, double tension) override;
    void onHandlesChanged(uint32_t pointId, const magda::CurveHandleData& inHandle,
                          const magda::CurveHandleData& outHandle) override;
    void onSegmentShaperChanged(uint32_t leftPointId, const magda::CurveHandleData& leftInHandle,
                                const magda::CurveHandleData& leftOutHandle, uint32_t rightPointId,
                                const magda::CurveHandleData& rightInHandle,
                                const magda::CurveHandleData& rightOutHandle,
                                bool isPreview) override;
    void onPointCurveTypeChanged(uint32_t pointId, magda::CurveType newType) override;
    void onDeleteSelectedPoints(const std::set<uint32_t>& pointIds) override;
    void onSelectedPointsMoved(
        const std::map<uint32_t, std::pair<double, double>>& finalPositions) override;
    void constrainPointPosition(uint32_t pointId, double& x, double& y) override;
    void onStepStamped(double gridStart, double gridEnd, double y, uint32_t prevPointId,
                       double prevValue) override;
    void onStrokeDrawn(const std::vector<std::pair<double, double>>& stroke,
                       magda::CurveType curveType) override;

  private:
    void changeListenerCallback(juce::ChangeBroadcaster*) override;

    const magda::AudioEvent* event() const;
    std::vector<sdk::AutomationPoint> modelPoints() const;
    /// The model point a curve point shows; one point may show once per loop pass.
    sdk::AutomationPoint* find(std::vector<sdk::AutomationPoint>& points, uint32_t curveId) const;
    double sourceAt(double x) const;
    double placeOf(uint32_t curveId) const;
    sdk::BezierHandle toSource(double displayX, const magda::CurveHandleData& handle) const;

    /// One undo step.
    void commit(std::vector<sdk::AutomationPoint> points);

    magda::EditTool tool() const;
    bool drawsLine(const juce::MouseEvent& e) const;
    void commitLine(juce::Point<int> from, juce::Point<int> to);

    GainEnvelopeHost& host_;

    /// The Line tool's drag, in this component's pixels.
    std::optional<std::pair<juce::Point<int>, juce::Point<int>>> line_;
    magda::ClipId clipId_ = magda::INVALID_CLIP_ID;

    mutable std::vector<magda::CurvePoint> cachedPoints_;
    mutable bool pointsCacheDirty_ = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GainEnvelopeEditor)
};

}  // namespace magda::daw::ui
