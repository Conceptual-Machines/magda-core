#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace magica {

/**
 * Preferences dialog for editing application configuration.
 * Displays organized sections for zoom, timeline, and transport settings.
 */
class PreferencesDialog : public juce::Component {
  public:
    PreferencesDialog();
    ~PreferencesDialog() override;

    void resized() override;
    void paint(juce::Graphics& g) override;

    // Apply current settings to Config
    void applySettings();

    // Static method to show as modal dialog
    static void showDialog(juce::Component* parent);

  private:
    // Zoom section
    juce::Slider zoomInSensitivitySlider;
    juce::Slider zoomOutSensitivitySlider;
    juce::Slider zoomShiftSensitivitySlider;

    // Timeline section
    juce::Slider timelineLengthSlider;
    juce::Slider viewDurationSlider;

    // Transport section
    juce::ToggleButton showBothFormatsToggle;
    juce::ToggleButton defaultBarsBeatsToggle;

    // Panel section
    juce::ToggleButton showLeftPanelToggle;
    juce::ToggleButton showRightPanelToggle;
    juce::ToggleButton showBottomPanelToggle;

    // Labels for each control
    juce::Label zoomInLabel;
    juce::Label zoomOutLabel;
    juce::Label zoomShiftLabel;
    juce::Label timelineLengthLabel;
    juce::Label viewDurationLabel;

    // Section headers
    juce::Label zoomHeader;
    juce::Label timelineHeader;
    juce::Label transportHeader;
    juce::Label panelsHeader;

    // Buttons
    juce::TextButton okButton;
    juce::TextButton cancelButton;
    juce::TextButton applyButton;

    // Parent window reference for closing
    juce::DialogWindow* dialogWindow = nullptr;

    void loadCurrentSettings();
    void setupSlider(juce::Slider& slider, juce::Label& label, const juce::String& labelText,
                     double min, double max, double interval, const juce::String& suffix = "");
    void setupToggle(juce::ToggleButton& toggle, const juce::String& text);
    void setupSectionHeader(juce::Label& header, const juce::String& text);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PreferencesDialog)
};

}  // namespace magica
