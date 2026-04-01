#include "ExportMidiDialog.hpp"

#include "../themes/DarkTheme.hpp"
#include "../themes/DialogLookAndFeel.hpp"
#include "../themes/FontManager.hpp"

namespace magda {

ExportMidiDialog::ExportMidiDialog() {
    setLookAndFeel(&daw::ui::DialogLookAndFeel::getInstance());

    // MIDI format selection
    formatLabel_.setText("MIDI Format:", juce::dontSendNotification);
    formatLabel_.setFont(FontManager::getInstance().getUIFontBold(14.0f));
    addAndMakeVisible(formatLabel_);

    formatComboBox_.addItem("Type 0 (Single Track)", 1);
    formatComboBox_.addItem("Type 1 (Multi-Track)", 2);
    formatComboBox_.setSelectedId(2, juce::dontSendNotification);
    addAndMakeVisible(formatComboBox_);

    // Export range
    rangeLabel_.setText("Export Range:", juce::dontSendNotification);
    rangeLabel_.setFont(FontManager::getInstance().getUIFontBold(14.0f));
    addAndMakeVisible(rangeLabel_);

    exportEntireSongButton_.setButtonText("Entire Song");
    exportEntireSongButton_.setRadioGroupId(1);
    exportEntireSongButton_.setToggleState(true, juce::dontSendNotification);
    addAndMakeVisible(exportEntireSongButton_);

    exportTimeSelectionButton_.setButtonText("Time Selection");
    exportTimeSelectionButton_.setRadioGroupId(1);
    exportTimeSelectionButton_.setEnabled(false);
    addAndMakeVisible(exportTimeSelectionButton_);

    exportLoopRegionButton_.setButtonText("Loop Region");
    exportLoopRegionButton_.setRadioGroupId(1);
    exportLoopRegionButton_.setEnabled(false);
    addAndMakeVisible(exportLoopRegionButton_);

    // Export button
    exportButton_.setButtonText("Export");
    exportButton_.onClick = [this]() {
        if (onExport)
            onExport(getSettings());
        if (auto* dw = findParentComponentOfClass<juce::DialogWindow>())
            dw->exitModalState(0);
    };
    addAndMakeVisible(exportButton_);

    // Cancel button
    cancelButton_.setButtonText("Cancel");
    cancelButton_.onClick = [this]() {
        if (auto* dw = findParentComponentOfClass<juce::DialogWindow>())
            dw->exitModalState(0);
    };
    addAndMakeVisible(cancelButton_);

    setSize(400, 280);
}

ExportMidiDialog::~ExportMidiDialog() {
    setLookAndFeel(nullptr);
}

void ExportMidiDialog::paint(juce::Graphics& g) {
    g.fillAll(DarkTheme::getColour(DarkTheme::PANEL_BACKGROUND));
}

void ExportMidiDialog::resized() {
    auto bounds = getLocalBounds().reduced(20);

    // Format selection
    auto formatArea = bounds.removeFromTop(28);
    formatLabel_.setBounds(formatArea.removeFromLeft(120));
    formatArea.removeFromLeft(10);
    formatComboBox_.setBounds(formatArea);
    bounds.removeFromTop(20);

    // Export range
    rangeLabel_.setBounds(bounds.removeFromTop(24));
    bounds.removeFromTop(5);
    exportEntireSongButton_.setBounds(bounds.removeFromTop(24));
    bounds.removeFromTop(5);
    exportTimeSelectionButton_.setBounds(bounds.removeFromTop(24));
    bounds.removeFromTop(5);
    exportLoopRegionButton_.setBounds(bounds.removeFromTop(24));
    bounds.removeFromTop(20);

    // Buttons at bottom
    const int buttonHeight = 32;
    const int buttonWidth = 100;
    const int buttonSpacing = 10;
    auto buttonArea = bounds.removeFromBottom(buttonHeight);
    cancelButton_.setBounds(buttonArea.removeFromRight(buttonWidth));
    buttonArea.removeFromRight(buttonSpacing);
    exportButton_.setBounds(buttonArea.removeFromRight(buttonWidth));
}

ExportMidiDialog::Settings ExportMidiDialog::getSettings() const {
    Settings settings;
    settings.midiFormat = formatComboBox_.getSelectedId() == 1 ? 0 : 1;

    if (exportTimeSelectionButton_.getToggleState())
        settings.exportRange = ExportRange::TimeSelection;
    else if (exportLoopRegionButton_.getToggleState())
        settings.exportRange = ExportRange::LoopRegion;
    else
        settings.exportRange = ExportRange::EntireSong;

    return settings;
}

void ExportMidiDialog::setTimeSelectionAvailable(bool available) {
    exportTimeSelectionButton_.setEnabled(available);
    if (!available && exportTimeSelectionButton_.getToggleState())
        exportEntireSongButton_.setToggleState(true, juce::dontSendNotification);
}

void ExportMidiDialog::setLoopRegionAvailable(bool available) {
    exportLoopRegionButton_.setEnabled(available);
    if (!available && exportLoopRegionButton_.getToggleState())
        exportEntireSongButton_.setToggleState(true, juce::dontSendNotification);
}

void ExportMidiDialog::showDialog(juce::Component* parent,
                                  std::function<void(const Settings&)> exportCallback,
                                  bool hasTimeSelection, bool hasLoopRegion) {
    auto* dialog = new ExportMidiDialog();
    dialog->setTimeSelectionAvailable(hasTimeSelection);
    dialog->setLoopRegionAvailable(hasLoopRegion);
    dialog->onExport = exportCallback;

    juce::DialogWindow::LaunchOptions options;
    options.dialogTitle = "Export MIDI";
    options.dialogBackgroundColour = DarkTheme::getColour(DarkTheme::PANEL_BACKGROUND);
    options.content.setOwned(dialog);
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = false;

    options.launchAsync();
}

}  // namespace magda
