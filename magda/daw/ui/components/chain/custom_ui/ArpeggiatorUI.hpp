#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <vector>

#include "audio/plugins/ArpeggiatorPlugin.hpp"
#include "ui/components/common/LinkableTextSlider.hpp"
#include "ui/components/common/RampCurveDisplay.hpp"
#include "ui/themes/DarkTheme.hpp"
#include "ui/themes/FontManager.hpp"

namespace magda::daw::ui {

/**
 * The Arpeggiator is a MagdaDevice (#2299), so this UI is written against the
 * model: slot values arrive through updateFromParameters(), slot edits leave
 * through onParameterChanged. The device pointer stays for what is not a slot -
 * the ramp-cycles / quantize / hard-angle settings it publishes as model
 * document patches, and the play-step atomics the sweep animation reads.
 */
class ArpeggiatorUI : public juce::Component, private juce::Timer {
  public:
    ArpeggiatorUI();
    ~ArpeggiatorUI() override;

    void setArpeggiator(daw::audio::ArpeggiatorPlugin* device);
    void updateFromParameters(const std::vector<magda::ParameterInfo>& params);

    std::function<void(int paramIndex, float value)> onParameterChanged;

    /// Fired when a non-slot setting (ramp cycles, quantize, subdivision,
    /// hard angle) is edited, carrying ALL the settings as properties in the
    /// device's own state vocabulary (ArpeggiatorPlugin::SettingIDs). The UI
    /// does not write the device: the owner patches the model's state
    /// document, and the projection updates the live device (#2317).
    std::function<void(const juce::NamedValueSet&)> onSettingsEdited;

    std::vector<LinkableTextSlider*> getLinkableSliders();

    void lookAndFeelChanged() override;
    void paint(juce::Graphics& g) override;
    void resized() override;

  private:
    // Mirror of the curve display's hard-angle toggle, so settingsEdited() can
    // publish the full settings set from the controls alone.
    bool hardAngle_ = false;
    daw::audio::ArpeggiatorPlugin* plugin_ = nullptr;

    // Left column
    juce::Label patternLabel_;
    juce::ComboBox patternCombo_;
    juce::Label rateLabel_;
    LinkableTextSlider rateSlider_;
    juce::Label octavesLabel_;
    LinkableTextSlider octavesSlider_;
    juce::Label latchLabel_;
    juce::TextButton latchButton_{"OFF"};
    juce::Label rampLabel_;
    RampCurveDisplay rampCurveDisplay_;
    juce::Label depthLabel_;
    LinkableTextSlider depthSlider_;
    juce::Label skewLabel_;
    LinkableTextSlider skewSlider_;
    juce::Label cyclesLabel_;
    LinkableTextSlider cyclesSlider_;
    juce::Label quantizeLabel_;
    LinkableTextSlider quantizeSlider_;
    juce::Label quantizeSubLabel_;
    LinkableTextSlider quantizeSubSlider_;

    // Right column
    juce::Label gateLabel_;
    LinkableTextSlider gateSlider_;
    juce::Label swingLabel_;
    LinkableTextSlider swingSlider_;
    juce::Label velModeLabel_;
    juce::ComboBox velModeCombo_;
    juce::Label fixedVelLabel_;
    LinkableTextSlider fixedVelSlider_;

    int topSectionBottom_ = 0;  // Y boundary between two-column section and full-width RAMP

    void sendChange(int paramIndex, float value);
    void settingsEdited();
    void syncSettingsFromDevice();
    void setupLabel(juce::Label& label, const juce::String& text);
    void setupCombo(juce::ComboBox& combo);
    void setupSlider(LinkableTextSlider& slider, double min, double max, double step);

    // Timer — poll modulated values and the play step for the curve display
    void timerCallback() override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ArpeggiatorUI)
};

}  // namespace magda::daw::ui
