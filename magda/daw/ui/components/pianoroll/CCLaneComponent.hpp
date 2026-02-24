#pragma once

#include <functional>
#include <set>
#include <vector>

#include "core/ClipTypes.hpp"
#include "ui/components/common/curve/CurveEditorBase.hpp"

namespace magda {

/**
 * @brief CC / Pitchbend lane editor for MIDI clips using the curve editor system
 *
 * Subclasses CurveEditorBase to display and edit CC/Pitchbend data as continuous
 * curves with step interpolation. Supports all CurveEditorBase draw modes
 * (Select, Pencil, Line, Curve).
 *
 * CC values (0-127) and Pitchbend values (0-16383) are normalized to 0-1 for
 * the curve editor's Y coordinate.
 */
class CCLaneComponent : public CurveEditorBase {
  public:
    CCLaneComponent();
    ~CCLaneComponent() override = default;

    // Configuration
    void setClip(ClipId clipId);
    ClipId getClipId() const {
        return clipId_;
    }

    void setCCNumber(int ccNumber);
    int getCCNumber() const {
        return ccNumber_;
    }

    void setIsPitchBend(bool isPitchBend);
    bool getIsPitchBend() const {
        return isPitchBend_;
    }

    // Pitch bend range in semitones (default 2, common values: 2, 12, 24, 48)
    void setPitchBendRange(int semitones);
    int getPitchBendRange() const {
        return pitchBendRange_;
    }

    // Zoom and scroll
    void setPixelsPerBeat(double ppb);
    void setScrollOffset(int offsetX);
    void setLeftPadding(int padding);

    // Display mode
    void setRelativeMode(bool relative);
    void setClipStartBeats(double startBeats);
    void setClipLengthBeats(double lengthBeats);

    // Loop region
    void setLoopRegion(double offsetBeats, double lengthBeats, bool enabled);

    // Refresh from clip data
    void refreshEvents();

    // Get display name for this lane
    juce::String getLaneName() const;

    // CurveEditorBase coordinate interface
    double getPixelsPerX() const override {
        return pixelsPerBeat_;
    }
    double pixelToX(int px) const override;
    int xToPixel(double x) const override;

    // Value label formatting
    juce::String formatValueLabel(double y) const override;

    // CurveEditorBase data access
    const std::vector<CurvePoint>& getPoints() const override;

  protected:
    // CurveEditorBase overrides
    void paintGrid(juce::Graphics& g) override;

    // Data mutation callbacks
    void onPointAdded(double x, double y, CurveType curveType) override;
    void onPointMoved(uint32_t pointId, double newX, double newY) override;
    void onPointDeleted(uint32_t pointId) override;
    void onPointSelected(uint32_t pointId) override;
    void onTensionChanged(uint32_t pointId, double tension) override;
    void onHandlesChanged(uint32_t pointId, const CurveHandleData& inHandle,
                          const CurveHandleData& outHandle) override;
    void onDeleteSelectedPoints(const std::set<uint32_t>& pointIds) override;

  private:
    ClipId clipId_ = INVALID_CLIP_ID;
    int ccNumber_ = 1;  // Default: Mod wheel
    bool isPitchBend_ = false;
    int pitchBendRange_ = 2;  // Semitones (±)

    double pixelsPerBeat_ = 50.0;
    int scrollOffsetX_ = 0;
    int leftPadding_ = 2;
    bool relativeMode_ = true;
    double clipStartBeats_ = 0.0;
    double clipLengthBeats_ = 0.0;

    // Loop region
    double loopOffsetBeats_ = 0.0;
    double loopLengthBeats_ = 0.0;
    bool loopEnabled_ = false;

    // Cached curve points (converted from MIDI CC/PB data)
    mutable std::vector<CurvePoint> cachedPoints_;
    mutable bool pointsCacheDirty_ = true;

    void updatePointsCache() const;
    void invalidateCache();

    // Value range helpers
    int getMaxValue() const {
        return isPitchBend_ ? 16383 : 127;
    }

    // Convert normalized Y (0-1) to raw CC/PB value
    int normalizedToValue(double y) const;
    // Convert raw CC/PB value to normalized Y (0-1)
    double valueToNormalized(int value) const;

    // Find the index of a CC/PB event by its point ID
    size_t pointIdToEventIndex(uint32_t pointId) const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CCLaneComponent)
};

}  // namespace magda
