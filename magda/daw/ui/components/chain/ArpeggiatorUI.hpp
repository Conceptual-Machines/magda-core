#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "audio/ArpeggiatorPlugin.hpp"
#include "ui/themes/DarkTheme.hpp"
#include "ui/themes/FontManager.hpp"

namespace magda::daw::ui {

/**
 * @brief Two-column UI for the ArpeggiatorPlugin.
 *
 * Left column:  Pattern (combo), Rate (combo), Octaves (combo), Latch (toggle)
 * Right column: Gate (slider), Swing (slider), Velocity Mode (combo), Fixed Vel (slider)
 *
 * Reads/writes CachedValues on the plugin's ValueTree directly.
 */
/** Small component that draws the ramp bezier curve. */
class RampCurveDisplay : public juce::Component {
  public:
    RampCurveDisplay() = default;

    void setValues(float depth, float skew) {
        if (std::abs(depth_ - depth) > 0.001f || std::abs(skew_ - skew) > 0.001f) {
            depth_ = depth;
            skew_ = skew;
            repaint();
        }
    }

    float getDepth() const {
        return depth_;
    }
    float getSkew() const {
        return skew_;
    }

    /** Called whenever depth or skew change. */
    std::function<void(float depth, float skew)> onCurveChanged;

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseDoubleClick(const juce::MouseEvent& e) override;

  private:
    float depth_ = 0.0f;
    float skew_ = 0.5f;
    // Offset from mouseDown position to handle centre — prevents handle jumping on click
    float handleOffsetX_ = 0.0f;
    float handleOffsetY_ = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RampCurveDisplay)
};

class ArpeggiatorUI : public juce::Component, private juce::ValueTree::Listener {
  public:
    ArpeggiatorUI();
    ~ArpeggiatorUI() override;

    void setArpeggiator(daw::audio::ArpeggiatorPlugin* plugin);

    void paint(juce::Graphics& g) override;
    void resized() override;

  private:
    daw::audio::ArpeggiatorPlugin* plugin_ = nullptr;
    juce::ValueTree watchedState_;

    // Left column
    juce::Label patternLabel_;
    juce::ComboBox patternCombo_;
    juce::Label rateLabel_;
    juce::Slider rateSlider_;
    juce::Label octavesLabel_;
    juce::Slider octavesSlider_;
    juce::Label latchLabel_;
    juce::TextButton latchButton_{"OFF"};
    juce::Label rampLabel_;
    RampCurveDisplay rampCurveDisplay_;

    // Right column
    juce::Label gateLabel_;
    juce::Slider gateSlider_;
    juce::Label swingLabel_;
    juce::Slider swingSlider_;
    juce::Label velModeLabel_;
    juce::ComboBox velModeCombo_;
    juce::Label fixedVelLabel_;
    juce::Slider fixedVelSlider_;

    void syncFromPlugin();
    void setupLabel(juce::Label& label, const juce::String& text);
    void setupCombo(juce::ComboBox& combo);
    void setupSlider(juce::Slider& slider, double min, double max, double step);

    // ValueTree::Listener — sync UI when plugin state changes externally
    void valueTreePropertyChanged(juce::ValueTree& tree, const juce::Identifier& property) override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ArpeggiatorUI)
};

}  // namespace magda::daw::ui
