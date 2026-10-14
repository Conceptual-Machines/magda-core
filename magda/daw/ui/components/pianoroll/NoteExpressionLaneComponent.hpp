#pragma once

#include <optional>
#include <set>
#include <vector>

#include "core/ClipInfo.hpp"
#include "core/ClipTypes.hpp"
#include "ui/components/common/curve/CurveEditorBase.hpp"

namespace magda {

/**
 * @brief One MPE dimension, slide or pressure, of the selected note (#1571).
 *
 * The lane edits the single selected note, inside its own span; every other
 * note's curve is drawn faintly behind it. With no note or several selected it
 * only shows them: under a chord, a beat names no one note.
 */
class NoteExpressionLaneComponent : public CurveEditorBase {
  public:
    explicit NoteExpressionLaneComponent(NoteExpression dimension);
    ~NoteExpressionLaneComponent() override = default;

    void setClip(ClipId clipId);
    void setSelectedNoteIndices(const std::vector<size_t>& indices);

    void setPixelsPerBeat(double ppb);
    void setScrollOffset(int offsetX);
    void setLeftPadding(int padding);
    void setRelativeMode(bool relative);
    void setClipStartBeats(double startBeats);

    void refreshNotes();

    juce::String getLaneName() const {
        return dimension_ == NoteExpression::Timbre ? "Slide" : "Pressure";
    }

    double getPixelsPerX() const override {
        return pixelsPerBeat_;
    }
    double pixelToX(int px) const override;
    int xToPixel(double x) const override;
    juce::String formatValueLabel(double y) const override;
    const std::vector<CurvePoint>& getPoints() const override;

  protected:
    void paintGrid(juce::Graphics& g) override;
    void paintCurve(juce::Graphics& g) override;

    void onPointAdded(double x, double y, CurveType curveType) override;
    void onPointMoved(uint32_t pointId, double newX, double newY) override;
    void onPointDeleted(uint32_t pointId) override;
    void onPointSelected(uint32_t pointId) override;
    void onTensionChanged(uint32_t pointId, double tension) override;
    void onHandlesChanged(uint32_t pointId, const CurveHandleData& inHandle,
                          const CurveHandleData& outHandle) override;
    void onDeleteSelectedPoints(const std::set<uint32_t>& pointIds) override;
    void onSelectedPointsMoved(
        const std::map<uint32_t, std::pair<double, double>>& finalPositions) override;
    void constrainPointPosition(uint32_t pointId, double& x, double& y) override;

  private:
    const MidiNote* editableNote() const;

    /// Where a note's beat 0 sits on this lane's x axis.
    double noteOrigin(const MidiNote& note) const;

    /// The edited note's lane, or nothing to edit: one undo step per call.
    void commit(std::vector<MidiNoteExpressionPoint> points);

    void reload();

    NoteExpression dimension_;
    ClipId clipId_ = INVALID_CLIP_ID;
    std::optional<size_t> noteIndex_;

    double pixelsPerBeat_ = 50.0;
    int scrollOffsetX_ = 0;
    int leftPadding_ = 2;
    bool relativeMode_ = true;
    double clipStartBeats_ = 0.0;

    mutable std::vector<CurvePoint> cachedPoints_;
    mutable bool pointsCacheDirty_ = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NoteExpressionLaneComponent)
};

}  // namespace magda
