#include "PreferencesDialog.hpp"

#include "../themes/DarkTheme.hpp"
#include "core/Config.hpp"

namespace magda {

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

    // Setup layout section
    setupSectionHeader(layoutHeader, "Layout");
    setupToggle(leftHandedLayoutToggle, "Headers on Right");

    // Setup rendering section
    setupSectionHeader(renderHeader, "Rendering");

    renderFolderLabel.setText("Render Output Folder", juce::dontSendNotification);
    renderFolderLabel.setColour(juce::Label::textColourId,
                                DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
    renderFolderLabel.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(renderFolderLabel);

    renderFolderValue.setText("Default (beside source file)", juce::dontSendNotification);
    renderFolderValue.setColour(juce::Label::textColourId,
                                DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
    renderFolderValue.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(renderFolderValue);

    renderFolderBrowseButton.setButtonText("Browse...");
    renderFolderBrowseButton.onClick = [this]() {
        fileChooser_ = std::make_unique<juce::FileChooser>("Select Render Output Folder");
        fileChooser_->launchAsync(
            juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
            [this](const juce::FileChooser& fc) {
                auto result = fc.getResult();
                if (result.exists()) {
                    renderFolderValue.setText(result.getFullPathName(), juce::dontSendNotification);
                    renderFolderValue.setTooltip(result.getFullPathName());
                }
            });
    };
    addAndMakeVisible(renderFolderBrowseButton);

    renderFolderClearButton.setButtonText("Clear");
    renderFolderClearButton.onClick = [this]() {
        renderFolderValue.setText("Default (beside source file)", juce::dontSendNotification);
        renderFolderValue.setTooltip("");
    };
    addAndMakeVisible(renderFolderClearButton);

    // Setup keyboard shortcuts section
    setupSectionHeader(shortcutsHeader, "Keyboard Shortcuts");
#if JUCE_MAC
    setupShortcutLabel(addTrackShortcut, "Add Track", juce::String::fromUTF8("\u2318T"));
    setupShortcutLabel(deleteTrackShortcut, "Delete Track", juce::String::fromUTF8("\u232B"));
    setupShortcutLabel(duplicateTrackShortcut, "Duplicate Track",
                       juce::String::fromUTF8("\u2318D"));
#else
    setupShortcutLabel(addTrackShortcut, "Add Track", "Ctrl+T");
    setupShortcutLabel(deleteTrackShortcut, "Delete Track", "Delete");
    setupShortcutLabel(duplicateTrackShortcut, "Duplicate Track", "Ctrl+D");
#endif
    setupShortcutLabel(muteTrackShortcut, "Mute Track", "M");
    setupShortcutLabel(soloTrackShortcut, "Solo Track", "S");

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

    // Set preferred size (increased height for panels, layout, rendering and shortcuts sections)
    setSize(450, 940);
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

    bounds.removeFromTop(sectionSpacing);

    // Layout section
    auto layoutHeaderBounds = bounds.removeFromTop(headerHeight);
    layoutHeader.setBounds(layoutHeaderBounds);
    bounds.removeFromTop(4);

    // Left-handed layout toggle
    row = bounds.removeFromTop(toggleHeight + 8);
    leftHandedLayoutToggle.setBounds(row.reduced(0, 4));

    bounds.removeFromTop(sectionSpacing);

    // Rendering section
    auto renderHeaderBounds = bounds.removeFromTop(headerHeight);
    renderHeader.setBounds(renderHeaderBounds);
    bounds.removeFromTop(4);

    // Render folder label
    row = bounds.removeFromTop(rowHeight);
    renderFolderLabel.setBounds(row);
    bounds.removeFromTop(4);

    // Render folder value + buttons
    row = bounds.removeFromTop(rowHeight);
    {
        auto buttonsArea = row.removeFromRight(140);
        renderFolderValue.setBounds(row);
        renderFolderClearButton.setBounds(buttonsArea.removeFromRight(60).reduced(0, 2));
        buttonsArea.removeFromRight(4);
        renderFolderBrowseButton.setBounds(buttonsArea.reduced(0, 2));
    }

    bounds.removeFromTop(sectionSpacing);

    // Keyboard Shortcuts section
    auto shortcutsHeaderBounds = bounds.removeFromTop(headerHeight);
    shortcutsHeader.setBounds(shortcutsHeaderBounds);
    bounds.removeFromTop(4);

    // Add Track shortcut
    row = bounds.removeFromTop(rowHeight);
    addTrackShortcut.setBounds(row);
    bounds.removeFromTop(4);

    // Delete Track shortcut
    row = bounds.removeFromTop(rowHeight);
    deleteTrackShortcut.setBounds(row);
    bounds.removeFromTop(4);

    // Duplicate Track shortcut
    row = bounds.removeFromTop(rowHeight);
    duplicateTrackShortcut.setBounds(row);
    bounds.removeFromTop(4);

    // Mute Track shortcut
    row = bounds.removeFromTop(rowHeight);
    muteTrackShortcut.setBounds(row);
    bounds.removeFromTop(4);

    // Solo Track shortcut
    row = bounds.removeFromTop(rowHeight);
    soloTrackShortcut.setBounds(row);

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

    // Load layout settings
    leftHandedLayoutToggle.setToggleState(config.getScrollbarOnLeft(), juce::dontSendNotification);

    // Load render folder setting
    auto folder = config.getRenderFolder();
    if (folder.empty()) {
        renderFolderValue.setText("Default (beside source file)", juce::dontSendNotification);
        renderFolderValue.setTooltip("");
    } else {
        renderFolderValue.setText(juce::String(folder), juce::dontSendNotification);
        renderFolderValue.setTooltip(juce::String(folder));
    }
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

    // Apply layout settings
    config.setScrollbarOnLeft(leftHandedLayoutToggle.getToggleState());

    // Apply render folder setting
    auto folderText = renderFolderValue.getText();
    if (folderText == "Default (beside source file)") {
        config.setRenderFolder("");
    } else {
        config.setRenderFolder(folderText.toStdString());
    }
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

void PreferencesDialog::setupShortcutLabel(juce::Label& label, const juce::String& action,
                                           const juce::String& shortcut) {
    label.setText(action + ":  " + shortcut, juce::dontSendNotification);
    label.setColour(juce::Label::textColourId, DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
    label.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(label);
}

}  // namespace magda
