#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "core/DeviceInfo.hpp"
#include "ui/components/common/LinkableTextSlider.hpp"

namespace magda::daw::ui {

/**
 * @brief Custom UI for the Tracktion Engine 4-Band Equaliser
 *
 * Top half: frequency response curve visualization
 * Bottom half: 4 columns of band controls (freq, gain, Q)
 *
 * Automatable parameters use paramIndex-based callbacks through TrackManager.
 * The response curve is drawn by sampling getDBGainAtFrequency across 20-20kHz.
 */
class EqualiserUI : public juce::Component, public juce::Timer {
  public:
    EqualiserUI();
    ~EqualiserUI() override = default;

    void updateFromParameters(const std::vector<magda::ParameterInfo>& params);

    std::function<void(int paramIndex, float value)> onParameterChanged;
    // Called by DeviceSlotComponent to get live curve data
    std::function<float(float freq)> getDBGainAtFrequency;

    // Get all linkable sliders for mod/macro wiring (in parameter-index order)
    std::vector<LinkableTextSlider*> getLinkableSliders();

    void paint(juce::Graphics& g) override;
    void resized() override;
    void timerCallback() override;

  private:
    // Frequency response curve component (top half)
    class CurveDisplay : public juce::Component {
      public:
        CurveDisplay(EqualiserUI& owner);
        void paint(juce::Graphics& g) override;
        void mouseDown(const juce::MouseEvent& e) override;
        void mouseDrag(const juce::MouseEvent& e) override;
        void mouseUp(const juce::MouseEvent& e) override;
        void mouseMove(const juce::MouseEvent& e) override;

      private:
        EqualiserUI& owner_;
        int dragBand_ = -1;  // Which band is being dragged (-1 = none)

        // Coordinate mapping helpers
        float freqToX(float freq, float width) const;
        float dbToY(float db, float height) const;
        float xToFreq(float x, float width) const;
        float yToDb(float y, float height) const;
        int findBandAt(float x, float y) const;
    };

    // Band controls (bottom half) - 4 columns
    struct BandControls {
        juce::Label nameLabel;
        LinkableTextSlider freqSlider{TextSlider::Format::Decimal};
        LinkableTextSlider gainSlider{TextSlider::Format::Decibels};
        LinkableTextSlider qSlider{TextSlider::Format::Decimal};
        juce::Label freqLabel, gainLabel, qLabel;
    };

    BandControls bands_[4];
    CurveDisplay curveDisplay_;

    // Phase invert toggle (virtual param index 12)
    juce::Label phaseInvertLabel_;
    LinkableTextSlider phaseInvertSlider_;

    // Band colours for dots on curve
    static constexpr int kNumBands = 4;
    static constexpr int kBandParamCount = 3;  // freq, gain, Q per band
    juce::Colour bandColours_[kNumBands];

    // Current band parameter values (for drawing dots)
    float bandFreqs_[kNumBands] = {100.0f, 1000.0f, 3000.0f, 10000.0f};
    float bandGains_[kNumBands] = {0.0f, 0.0f, 0.0f, 0.0f};

    void setupBandControls(int bandIndex, const juce::String& name);
    void setupLabel(juce::Label& label, const juce::String& text);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EqualiserUI)
};

}  // namespace magda::daw::ui
