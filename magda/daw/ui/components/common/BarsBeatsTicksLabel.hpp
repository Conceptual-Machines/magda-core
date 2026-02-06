#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

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

    // Callback when value changes
    std::function<void()> onValueChange;

    // Component overrides
    void paint(juce::Graphics& g) override;
    void resized() override;

  private:
    static constexpr int TICKS_PER_BEAT = 480;
    static constexpr int TICKS_PER_16TH = 120;  // 480 / 4

    enum class SegmentType { Bars, Beats, Ticks };

    // Forward declaration
    class SegmentLabel;

    double value_ = 0.0;
    double minValue_ = 0.0;
    double maxValue_ = 1.0;
    double defaultValue_ = 0.0;
    int beatsPerBar_ = 4;
    bool barsBeatsIsPosition_ = true;
    bool doubleClickResets_ = true;
    juce::Colour customTextColour_;
    bool hasCustomTextColour_ = false;
    juce::String overlayLabel_;
    bool drawBackground_ = true;

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
