#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <functional>

#include "core/TempoUtils.hpp"

namespace magda {

/**
 * A composite label displaying bars.beats.ticks with three independently
 * draggable segments separated by dot separators.
 *
 * Internal value is stored in beats (same unit as DraggableValueLabel BarsBeats).
 * Ticks quantize to 16th-note steps (multiples of 120/480) by default;
 * holding Shift allows free-form (unquantized) dragging.
 */
class BarsBeatsTicksLabel : public juce::Component {
  public:
    // The size the digits are drawn at. Published so anything sizing a box
    // around this label measures the same font it will render with.
    static constexpr float kTextFontSize = 10.0f;

    /** Width a label needs to show its widest value without clipping, across a
     *  whole range of time signatures: the bar number is widest at the fewest
     *  beats per bar, the beat number at the most. Static so a caller can size
     *  a box before the label exists; it reads the same font and segment
     *  widths that resized() lays out with, so the two cannot disagree. */
    static int preferredWidthForRange(double maxValue, int minBeatsPerBar, int maxBeatsPerBar,
                                      bool isPosition);

    /** Width each segment needs for the widest value it can show, in the order
     *  bars, beats, ticks. preferredWidthForRange() adds these up; the live
     *  layout shares the strip out by the digits on screen instead (see
     *  resized()), so a box sized for this holds the widest value while a
     *  typical one reads balanced. */
    static std::array<int, 3> segmentWidthsFor(double maxValue, int minBeatsPerBar,
                                               int maxBeatsPerBar, bool isPosition);

    BarsBeatsTicksLabel();
    ~BarsBeatsTicksLabel() override;

    // Value range
    void setRange(double min, double max, double defaultValue = 0.0);
    void setValue(double newValue, juce::NotificationType notification = juce::sendNotification);
    double getValue() const {
        return value_;
    }

    // Beats per bar for display decomposition
    void setBeatsPerBar(int beatsPerBar);

    // Whether display is 1-indexed position (true) or 0-indexed duration (false)
    void setBarsBeatsIsPosition(bool isPosition);

    // Reset to default on double-click (instead of edit mode)
    void setDoubleClickResetsValue(bool shouldReset) {
        doubleClickResets_ = shouldReset;
    }

    // Custom text colour (default: uses TEXT_PRIMARY from theme)
    void setTextColour(juce::Colour colour);
    juce::Colour getTextColour() const;

    // Overlay label drawn at top-left corner in tiny font
    void setOverlayLabel(const juce::String& label);

    // Whether to draw background fill + border (default: true)
    void setDrawBackground(bool draw);

    /** Width at the right end of the label its glyphs keep clear, for a caption
     *  or icon the owner draws over the box's end. The owner measures that
     *  decoration and adds the same number to the box it gives this label
     *  (TransportLayout does), so the digits never sit under it. The glyphs
     *  already stop kEdgeInset + kSegmentPad / 2 short of the edge, so only
     *  what the decoration needs beyond that comes off the strip. Zero by
     *  default. */
    void setTrailingInset(int px);

    // Geometry shared by resized() and preferredWidthForRange(). Public, like
    // kTextFontSize, so a caller sizing a box around this label can reason
    // about where its glyphs stop.
    static constexpr int kEdgeInset = 2;   // left/right inset of the segment strip
    static constexpr int kDotWidth = 6;    // gap between two segments, where the dot is drawn
    static constexpr int kSegmentPad = 4;  // air around a segment's digits

    // Text override: when set, displays this text instead of bars.beats.ticks segments
    void setTextOverride(const juce::String& text);
    void clearTextOverride();

    // Whether any segment is currently being dragged
    bool isDragging() const;

    // Callback when value changes
    std::function<void()> onValueChange;

    // Component overrides
    void paint(juce::Graphics& g) override;
    void resized() override;

  private:
    // Width each segment needs for the digits it is showing right now; the
    // strip is shared out in these proportions.
    std::array<int, 3> shownSegmentWidths() const;
    // Width of a run of `digits` of the widest digit, plus the segment's air.
    static int widthOfDigits(int digits);

    int trailingInset_ = 0;
    // Digit counts of the bar and beat numbers last laid out for, so a value
    // that gains or loses a digit relayouts rather than just repaints.
    std::array<int, 2> laidOutDigits_{0, 0};

    static constexpr int TICKS_PER_BEAT = 480;
    static constexpr int TICKS_PER_16TH = 120;  // 480 / 4

    enum class SegmentType { Bars, Beats, Ticks };

    // Forward declaration
    class SegmentLabel;

    double value_ = 0.0;
    double minValue_ = 0.0;
    double maxValue_ = 1.0;
    double defaultValue_ = 0.0;
    int beatsPerBar_ = DEFAULT_TIME_SIGNATURE_NUMERATOR;
    bool barsBeatsIsPosition_ = true;
    bool doubleClickResets_ = true;
    juce::Colour customTextColour_;
    bool hasCustomTextColour_ = false;
    juce::String overlayLabel_;
    bool drawBackground_ = true;
    juce::String textOverride_;

    std::unique_ptr<SegmentLabel> barsSegment_;
    std::unique_ptr<SegmentLabel> beatsSegment_;
    std::unique_ptr<SegmentLabel> ticksSegment_;

    // Decompose value into bars, beats, ticks for display
    void decompose(int& bars, int& beats, int& ticks) const;

    // Recompose value from segment display values
    double recompose(int bars, int beats, int ticks) const;

    // Called by SegmentLabel when its value changes
    void onSegmentChanged();

    // Update all segment display texts from current value_
    void updateSegmentTexts();

    // =========================================================================
    // SegmentLabel — private inner class
    // =========================================================================
    class SegmentLabel : public juce::Component {
      public:
        SegmentLabel(BarsBeatsTicksLabel& owner, SegmentType type);
        ~SegmentLabel() override;

        void setDisplayValue(int val);
        int getDisplayValue() const {
            return displayValue_;
        }

        void paint(juce::Graphics& g) override;
        void mouseDown(const juce::MouseEvent& e) override;
        void mouseDrag(const juce::MouseEvent& e) override;
        void mouseUp(const juce::MouseEvent& e) override;
        void mouseDoubleClick(const juce::MouseEvent& e) override;
        void mouseWheelMove(const juce::MouseEvent& e,
                            const juce::MouseWheelDetails& wheel) override;

        bool isDragging() const {
            return isDragging_;
        }

      private:
        BarsBeatsTicksLabel& owner_;
        SegmentType type_;
        int displayValue_ = 0;

        // Drag state
        bool isDragging_ = false;
        int dragStartY_ = 0;
        double dragAccumulator_ = 0.0;

        // Edit state
        bool isEditing_ = false;
        std::unique_ptr<juce::TextEditor> editor_;

        juce::String formatDisplay() const;
        void startEditing();
        void finishEditing();
        void cancelEditing();

        // Get drag/wheel increments
        double getDefaultIncrement(bool shift) const;
        double getDragPixelsPerStep() const;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SegmentLabel)
    };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BarsBeatsTicksLabel)
};

}  // namespace magda
