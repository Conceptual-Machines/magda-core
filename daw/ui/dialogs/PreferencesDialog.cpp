#include "PreferencesDialog.hpp"

#include "../themes/DarkTheme.hpp"
#include "core/Config.hpp"

namespace magica {

PreferencesDialog::PreferencesDialog() {
    // Setup section headers
    setupSectionHeader(zoomHeader, "Zoom");
    setupSectionHeader(timelineHeader, "Timeline");
    setupSectionHeader(transportHeader, "Transport Display");

    // Setup zoom sliders
    setupSlider(zoomInSensitivitySlider, zoomInLabel, "Zoom In Sensitivity", 5.0, 100.0, 1.0);
    setupSlider(zoomOutSensitivitySlider, zoomOutLabel, "Zoom Out Sensitivity", 5.0, 100.0, 1.0);
    setupSlider(zoomShiftSensitivitySlider, zoomShiftLabel, "Shift+Zoom Sensitivity", 1.0, 50.0,
                0.5);

    // Setup timeline sliders
    setupSlider(timelineLengthSlider, timelineLengthLabel, "Default Length (sec)", 60.0, 1800.0,
                10.0, " sec");
    setupSlider(viewDurationSlider, viewDurationLabel, "Default View Duration", 10.0, 300.0, 5.0,
                " sec");

    // Setup transport toggles
    setupToggle(showBothFormatsToggle, "Show both time formats");
    setupToggle(defaultBarsBeatsToggle, "Default to Bars/Beats (vs Seconds)");

    // Setup panel section
    setupSectionHeader(panelsHeader, "Panels (Default Visibility)");
    setupToggle(showLeftPanelToggle, "Show Left Panel (Browser)");
    setupToggle(showRightPanelToggle, "Show Right Panel (Inspector)");
    setupToggle(showBottomPanelToggle, "Show Bottom Panel (Mixer)");

    // Setup buttons
    okButton.setButtonText("OK");
    okButton.onClick = [this]() {
        applySettings();
        if (auto* dw = findParentComponentOfClass<juce::DialogWindow>()) {
            dw->exitModalState(1);
        }
    };
    addAndMakeVisible(okButton);

    cancelButton.setButtonText("Cancel");
    cancelButton.onClick = [this]() {
        if (auto* dw = findParentComponentOfClass<juce::DialogWindow>()) {
            dw->exitModalState(0);
        }
    };
    addAndMakeVisible(cancelButton);

    applyButton.setButtonText("Apply");
    applyButton.onClick = [this]() { applySettings(); };
    addAndMakeVisible(applyButton);

    // Load current settings
    loadCurrentSettings();

    // Set preferred size (increased height for panels section)
    setSize(450, 580);
}

PreferencesDialog::~PreferencesDialog() = default;

void PreferencesDialog::paint(juce::Graphics& g) {
    g.fillAll(DarkTheme::getColour(DarkTheme::PANEL_BACKGROUND));
}

void PreferencesDialog::resized() {
    auto bounds = getLocalBounds().reduced(20);
    const int rowHeight = 32;
    const int labelWidth = 180;
    const int sliderHeight = 24;
    const int toggleHeight = 24;
    const int headerHeight = 28;
    const int sectionSpacing = 16;
    const int buttonHeight = 28;
    const int buttonWidth = 80;
    const int buttonSpacing = 10;

    // Zoom section
    auto zoomHeaderBounds = bounds.removeFromTop(headerHeight);
    zoomHeader.setBounds(zoomHeaderBounds);
    bounds.removeFromTop(4);

    // Zoom In Sensitivity
    auto row = bounds.removeFromTop(rowHeight);
    zoomInLabel.setBounds(row.removeFromLeft(labelWidth));
    zoomInSensitivitySlider.setBounds(row.reduced(0, (rowHeight - sliderHeight) / 2));
    bounds.removeFromTop(4);

    // Zoom Out Sensitivity
    row = bounds.removeFromTop(rowHeight);
    zoomOutLabel.setBounds(row.removeFromLeft(labelWidth));
    zoomOutSensitivitySlider.setBounds(row.reduced(0, (rowHeight - sliderHeight) / 2));
    bounds.removeFromTop(4);

    // Shift+Zoom Sensitivity
    row = bounds.removeFromTop(rowHeight);
    zoomShiftLabel.setBounds(row.removeFromLeft(labelWidth));
    zoomShiftSensitivitySlider.setBounds(row.reduced(0, (rowHeight - sliderHeight) / 2));

    bounds.removeFromTop(sectionSpacing);

    // Timeline section
    auto timelineHeaderBounds = bounds.removeFromTop(headerHeight);
    timelineHeader.setBounds(timelineHeaderBounds);
    bounds.removeFromTop(4);

    // Default Length
    row = bounds.removeFromTop(rowHeight);
    timelineLengthLabel.setBounds(row.removeFromLeft(labelWidth));
    timelineLengthSlider.setBounds(row.reduced(0, (rowHeight - sliderHeight) / 2));
    bounds.removeFromTop(4);

    // Default View Duration
    row = bounds.removeFromTop(rowHeight);
    viewDurationLabel.setBounds(row.removeFromLeft(labelWidth));
    viewDurationSlider.setBounds(row.reduced(0, (rowHeight - sliderHeight) / 2));

    bounds.removeFromTop(sectionSpacing);

    // Transport section
    auto transportHeaderBounds = bounds.removeFromTop(headerHeight);
    transportHeader.setBounds(transportHeaderBounds);
    bounds.removeFromTop(4);

    // Show both formats toggle
    row = bounds.removeFromTop(toggleHeight + 8);
    showBothFormatsToggle.setBounds(row.reduced(0, 4));
    bounds.removeFromTop(4);

    // Default bars/beats toggle
    row = bounds.removeFromTop(toggleHeight + 8);
    defaultBarsBeatsToggle.setBounds(row.reduced(0, 4));

    bounds.removeFromTop(sectionSpacing);

    // Panels section
    auto panelsHeaderBounds = bounds.removeFromTop(headerHeight);
    panelsHeader.setBounds(panelsHeaderBounds);
    bounds.removeFromTop(4);

    // Show left panel toggle
    row = bounds.removeFromTop(toggleHeight + 8);
    showLeftPanelToggle.setBounds(row.reduced(0, 4));
    bounds.removeFromTop(4);

    // Show right panel toggle
    row = bounds.removeFromTop(toggleHeight + 8);
    showRightPanelToggle.setBounds(row.reduced(0, 4));
    bounds.removeFromTop(4);

    // Show bottom panel toggle
    row = bounds.removeFromTop(toggleHeight + 8);
    showBottomPanelToggle.setBounds(row.reduced(0, 4));

    // Button row at bottom
    auto buttonArea = getLocalBounds().reduced(20).removeFromBottom(buttonHeight);

    // Right-align buttons
    auto buttonsWidth = buttonWidth * 3 + buttonSpacing * 2;
    buttonArea.removeFromLeft(buttonArea.getWidth() - buttonsWidth);

    cancelButton.setBounds(buttonArea.removeFromLeft(buttonWidth));
    buttonArea.removeFromLeft(buttonSpacing);
    applyButton.setBounds(buttonArea.removeFromLeft(buttonWidth));
    buttonArea.removeFromLeft(buttonSpacing);
    okButton.setBounds(buttonArea.removeFromLeft(buttonWidth));
}

void PreferencesDialog::loadCurrentSettings() {
    auto& config = Config::getInstance();

    // Load zoom settings
    zoomInSensitivitySlider.setValue(config.getZoomInSensitivity(), juce::dontSendNotification);
    zoomOutSensitivitySlider.setValue(config.getZoomOutSensitivity(), juce::dontSendNotification);
    zoomShiftSensitivitySlider.setValue(config.getZoomInSensitivityShift(),
                                        juce::dontSendNotification);

    // Load timeline settings
    timelineLengthSlider.setValue(config.getDefaultTimelineLength(), juce::dontSendNotification);
    viewDurationSlider.setValue(config.getDefaultZoomViewDuration(), juce::dontSendNotification);

    // Load transport settings
    showBothFormatsToggle.setToggleState(config.getTransportShowBothFormats(),
                                         juce::dontSendNotification);
    defaultBarsBeatsToggle.setToggleState(config.getTransportDefaultBarsBeats(),
                                          juce::dontSendNotification);

    // Load panel visibility settings
    showLeftPanelToggle.setToggleState(config.getShowLeftPanel(), juce::dontSendNotification);
    showRightPanelToggle.setToggleState(config.getShowRightPanel(), juce::dontSendNotification);
    showBottomPanelToggle.setToggleState(config.getShowBottomPanel(), juce::dontSendNotification);
}

void PreferencesDialog::applySettings() {
    auto& config = Config::getInstance();

    // Apply zoom settings
    config.setZoomInSensitivity(zoomInSensitivitySlider.getValue());
    config.setZoomOutSensitivity(zoomOutSensitivitySlider.getValue());
    config.setZoomInSensitivityShift(zoomShiftSensitivitySlider.getValue());
    config.setZoomOutSensitivityShift(
        zoomShiftSensitivitySlider.getValue());  // Use same value for both shift sensitivities

    // Apply timeline settings
    config.setDefaultTimelineLength(timelineLengthSlider.getValue());
    config.setDefaultZoomViewDuration(viewDurationSlider.getValue());

    // Apply transport settings
    config.setTransportShowBothFormats(showBothFormatsToggle.getToggleState());
    config.setTransportDefaultBarsBeats(defaultBarsBeatsToggle.getToggleState());

    // Apply panel visibility settings
    config.setShowLeftPanel(showLeftPanelToggle.getToggleState());
    config.setShowRightPanel(showRightPanelToggle.getToggleState());
    config.setShowBottomPanel(showBottomPanelToggle.getToggleState());
}

void PreferencesDialog::showDialog(juce::Component* parent) {
    auto* dialog = new PreferencesDialog();

    juce::DialogWindow::LaunchOptions options;
    options.dialogTitle = "Preferences";
    options.dialogBackgroundColour = DarkTheme::getColour(DarkTheme::PANEL_BACKGROUND);
    options.content.setOwned(dialog);
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = false;

    options.launchAsync();
}

void PreferencesDialog::setupSlider(juce::Slider& slider, juce::Label& label,
                                    const juce::String& labelText, double min, double max,
                                    double interval, const juce::String& suffix) {
    label.setText(labelText, juce::dontSendNotification);
    label.setColour(juce::Label::textColourId, DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
    label.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(label);

    slider.setRange(min, max, interval);
    slider.setSliderStyle(juce::Slider::LinearHorizontal);
    slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 60, 20);
    slider.setTextValueSuffix(suffix);
    slider.setColour(juce::Slider::backgroundColourId, DarkTheme::getColour(DarkTheme::SURFACE));
    slider.setColour(juce::Slider::thumbColourId, DarkTheme::getColour(DarkTheme::ACCENT_BLUE));
    slider.setColour(juce::Slider::trackColourId,
                     DarkTheme::getColour(DarkTheme::ACCENT_BLUE).darker(0.3f));
    slider.setColour(juce::Slider::textBoxTextColourId,
                     DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
    slider.setColour(juce::Slider::textBoxBackgroundColourId,
                     DarkTheme::getColour(DarkTheme::SURFACE));
    slider.setColour(juce::Slider::textBoxOutlineColourId, DarkTheme::getColour(DarkTheme::BORDER));
    addAndMakeVisible(slider);
}

void PreferencesDialog::setupToggle(juce::ToggleButton& toggle, const juce::String& text) {
    toggle.setButtonText(text);
    toggle.setColour(juce::ToggleButton::textColourId,
                     DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
    toggle.setColour(juce::ToggleButton::tickColourId,
                     DarkTheme::getColour(DarkTheme::ACCENT_BLUE));
    toggle.setColour(juce::ToggleButton::tickDisabledColourId,
                     DarkTheme::getColour(DarkTheme::TEXT_DIM));
    addAndMakeVisible(toggle);
}

void PreferencesDialog::setupSectionHeader(juce::Label& header, const juce::String& text) {
    header.setText(text, juce::dontSendNotification);
    header.setColour(juce::Label::textColourId, DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
    header.setFont(juce::Font(14.0f, juce::Font::bold));
    header.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(header);
}

}  // namespace magica
