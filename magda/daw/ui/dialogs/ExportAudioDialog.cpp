#include "ExportAudioDialog.hpp"

#include "../../core/Config.hpp"
#include "../themes/DarkTheme.hpp"

namespace magda {

ExportAudioDialog::ExportAudioDialog() {
    // Format selection
    formatLabel_.setText("Format:", juce::dontSendNotification);
    formatLabel_.setFont(juce::Font(14.0f, juce::Font::bold));
    addAndMakeVisible(formatLabel_);

    formatComboBox_.addItem("WAV 16-bit", 1);
    formatComboBox_.addItem("WAV 24-bit", 2);
    formatComboBox_.addItem("WAV 32-bit Float", 3);
    formatComboBox_.addItem("FLAC", 4);
    formatComboBox_.setSelectedId(2, juce::dontSendNotification);  // Default to WAV 24-bit
    formatComboBox_.onChange = [this]() { onFormatChanged(); };
    addAndMakeVisible(formatComboBox_);

    // Sample rate selection
    sampleRateLabel_.setText("Sample Rate:", juce::dontSendNotification);
    sampleRateLabel_.setFont(juce::Font(14.0f, juce::Font::bold));
    addAndMakeVisible(sampleRateLabel_);

    sampleRateComboBox_.addItem("44.1 kHz", 1);
    sampleRateComboBox_.addItem("48 kHz", 2);
    sampleRateComboBox_.addItem("96 kHz", 3);
    sampleRateComboBox_.addItem("192 kHz", 4);
    sampleRateComboBox_.setSelectedId(2, juce::dontSendNotification);  // Default to 48kHz
    addAndMakeVisible(sampleRateComboBox_);

    // Bit depth (read-only, updates based on format)
    bitDepthLabel_.setText("Bit Depth:", juce::dontSendNotification);
    bitDepthLabel_.setFont(juce::Font(14.0f, juce::Font::bold));
    addAndMakeVisible(bitDepthLabel_);

    bitDepthValueLabel_.setText("24-bit", juce::dontSendNotification);
    bitDepthValueLabel_.setFont(juce::Font(14.0f));
    addAndMakeVisible(bitDepthValueLabel_);

    // Normalization option
    normalizeCheckbox_.setButtonText("Normalize to 0 dB (peak)");
    normalizeCheckbox_.setToggleState(false, juce::dontSendNotification);
    addAndMakeVisible(normalizeCheckbox_);

    // Time range selection
    timeRangeLabel_.setText("Export Range:", juce::dontSendNotification);
    timeRangeLabel_.setFont(juce::Font(14.0f, juce::Font::bold));
    addAndMakeVisible(timeRangeLabel_);

    exportEntireSongButton_.setButtonText("Entire Song");
    exportEntireSongButton_.setRadioGroupId(1);
    exportEntireSongButton_.setToggleState(true, juce::dontSendNotification);
    addAndMakeVisible(exportEntireSongButton_);

    exportTimeSelectionButton_.setButtonText("Time Selection");
    exportTimeSelectionButton_.setRadioGroupId(1);
    exportTimeSelectionButton_.setEnabled(false);  // Disabled by default
    addAndMakeVisible(exportTimeSelectionButton_);

    exportLoopRegionButton_.setButtonText("Loop Region");
    exportLoopRegionButton_.setRadioGroupId(1);
    exportLoopRegionButton_.setEnabled(false);  // Disabled by default
    addAndMakeVisible(exportLoopRegionButton_);

    // Export button
    exportButton_.setButtonText("Export");
    exportButton_.onClick = [this]() {
        if (onExport) {
            onExport(getSettings());
        }
        if (auto* dw = findParentComponentOfClass<juce::DialogWindow>()) {
            dw->exitModalState(0);
        }
    };
    addAndMakeVisible(exportButton_);

    // Cancel button
    cancelButton_.setButtonText("Cancel");
    cancelButton_.onClick = [this]() {
        if (auto* dw = findParentComponentOfClass<juce::DialogWindow>()) {
            dw->exitModalState(0);
        }
    };
    addAndMakeVisible(cancelButton_);

    // Set preferred size
    setSize(500, 380);
}

ExportAudioDialog::~ExportAudioDialog() = default;

void ExportAudioDialog::paint(juce::Graphics& g) {
    g.fillAll(DarkTheme::getColour(DarkTheme::PANEL_BACKGROUND));
}

void ExportAudioDialog::resized() {
    auto bounds = getLocalBounds().reduced(20);

    // Format selection
    auto formatArea = bounds.removeFromTop(28);
    formatLabel_.setBounds(formatArea.removeFromLeft(120));
    formatArea.removeFromLeft(10);
    formatComboBox_.setBounds(formatArea);
    bounds.removeFromTop(10);

    // Sample rate selection
    auto sampleRateArea = bounds.removeFromTop(28);
    sampleRateLabel_.setBounds(sampleRateArea.removeFromLeft(120));
    sampleRateArea.removeFromLeft(10);
    sampleRateComboBox_.setBounds(sampleRateArea);
    bounds.removeFromTop(10);

    // Bit depth display
    auto bitDepthArea = bounds.removeFromTop(28);
    bitDepthLabel_.setBounds(bitDepthArea.removeFromLeft(120));
    bitDepthArea.removeFromLeft(10);
    bitDepthValueLabel_.setBounds(bitDepthArea);
    bounds.removeFromTop(15);

    // Normalization checkbox
    normalizeCheckbox_.setBounds(bounds.removeFromTop(24));
    bounds.removeFromTop(20);

    // Time range label
    timeRangeLabel_.setBounds(bounds.removeFromTop(24));
    bounds.removeFromTop(5);

    // Time range radio buttons
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

ExportAudioDialog::Settings ExportAudioDialog::getSettings() const {
    Settings settings;

    // Get format
    int formatId = formatComboBox_.getSelectedId();
    switch (formatId) {
        case 1:
            settings.format = "WAV16";
            break;
        case 2:
            settings.format = "WAV24";
            break;
        case 3:
            settings.format = "WAV32";
            break;
        case 4:
            settings.format = "FLAC";
            break;
        default:
            settings.format = "WAV24";
            break;
    }

    // Get sample rate
    int sampleRateId = sampleRateComboBox_.getSelectedId();
    switch (sampleRateId) {
        case 1:
            settings.sampleRate = 44100.0;
            break;
        case 2:
            settings.sampleRate = 48000.0;
            break;
        case 3:
            settings.sampleRate = 96000.0;
            break;
        case 4:
            settings.sampleRate = 192000.0;
            break;
        default:
            settings.sampleRate = 48000.0;
            break;
    }

    settings.normalize = normalizeCheckbox_.getToggleState();

    // Determine export range
    if (exportTimeSelectionButton_.getToggleState()) {
        settings.exportRange = ExportRange::TimeSelection;
    } else if (exportLoopRegionButton_.getToggleState()) {
        settings.exportRange = ExportRange::LoopRegion;
    } else {
        settings.exportRange = ExportRange::EntireSong;
    }

    return settings;
}

void ExportAudioDialog::setTimeSelectionAvailable(bool available) {
    exportTimeSelectionButton_.setEnabled(available);
    if (!available && exportTimeSelectionButton_.getToggleState()) {
        exportEntireSongButton_.setToggleState(true, juce::dontSendNotification);
    }
}

void ExportAudioDialog::setLoopRegionAvailable(bool available) {
    exportLoopRegionButton_.setEnabled(available);
    if (!available && exportLoopRegionButton_.getToggleState()) {
        exportEntireSongButton_.setToggleState(true, juce::dontSendNotification);
    }
}

void ExportAudioDialog::onFormatChanged() {
    updateBitDepthOptions();
}

void ExportAudioDialog::updateBitDepthOptions() {
    int formatId = formatComboBox_.getSelectedId();
    juce::String bitDepthText;

    switch (formatId) {
        case 1:  // WAV 16-bit
            bitDepthText = "16-bit";
            break;
        case 2:  // WAV 24-bit
            bitDepthText = "24-bit";
            break;
        case 3:  // WAV 32-bit Float
            bitDepthText = "32-bit Float";
            break;
        case 4:  // FLAC
            bitDepthText = "24-bit (FLAC)";
            break;
        default:
            bitDepthText = "24-bit";
            break;
    }

    bitDepthValueLabel_.setText(bitDepthText, juce::dontSendNotification);
}

void ExportAudioDialog::showDialog(juce::Component* parent,
                                   std::function<void(const Settings&)> exportCallback,
                                   bool hasTimeSelection, bool hasLoopRegion) {
    auto* dialog = new ExportAudioDialog();
    dialog->setTimeSelectionAvailable(hasTimeSelection);
    dialog->setLoopRegionAvailable(hasLoopRegion);
    dialog->onExport = exportCallback;

    juce::DialogWindow::LaunchOptions options;
    options.dialogTitle = "Export Audio";
    options.dialogBackgroundColour = DarkTheme::getColour(DarkTheme::PANEL_BACKGROUND);
    options.content.setOwned(dialog);
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = false;

    options.launchAsync();
}

}  // namespace magda
