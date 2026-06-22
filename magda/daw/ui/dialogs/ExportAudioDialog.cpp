#include "ExportAudioDialog.hpp"

#include "../../core/Config.hpp"
#include "../../project/ProjectManager.hpp"
#include "../themes/DarkTheme.hpp"
#include "../themes/DialogLookAndFeel.hpp"
#include "../themes/FontManager.hpp"
#include "core/StringTable.hpp"
#include "core/TechnicalText.hpp"

namespace magda {

ExportAudioDialog::ExportAudioDialog() {
    setLookAndFeel(&daw::ui::DialogLookAndFeel::getInstance());

    // Format selection
    formatLabel_.setText(tr("export_audio.label.format"), juce::dontSendNotification);
    formatLabel_.setFont(FontManager::getInstance().getUIFontBold(14.0f));
    addAndMakeVisible(formatLabel_);

    // File-format designators — do not translate.
    formatComboBox_.addItem("WAV 16-bit", 1);
    formatComboBox_.addItem("WAV 24-bit", 2);
    formatComboBox_.addItem("WAV 32-bit Float", 3);
    formatComboBox_.addItem("FLAC", 4);
    // Initialise format from the project's render bit depth
    const auto& projectInfo = ProjectManager::getInstance().getCurrentProjectInfo();
    int bd = projectInfo.renderBitDepth;
    int formatId = 2;  // Default WAV 24-bit
    if (bd >= 32)
        formatId = 3;
    else if (bd >= 24)
        formatId = 2;
    else
        formatId = 1;
    formatComboBox_.setSelectedId(formatId, juce::dontSendNotification);
    formatComboBox_.onChange = [this]() { onFormatChanged(); };
    addAndMakeVisible(formatComboBox_);

    // Sample rate selection
    sampleRateLabel_.setText(tr("export_audio.label.sample_rate"), juce::dontSendNotification);
    sampleRateLabel_.setFont(FontManager::getInstance().getUIFontBold(14.0f));
    addAndMakeVisible(sampleRateLabel_);

    sampleRateComboBox_.addItem("44.1 kHz", 1);
    sampleRateComboBox_.addItem("48 kHz", 2);
    sampleRateComboBox_.addItem("96 kHz", 3);
    sampleRateComboBox_.addItem("192 kHz", 4);
    // Initialise sample rate from the project's sample rate
    double savedRate = projectInfo.sampleRate;
    int rateId = 1;  // Default 44.1kHz
    if (savedRate >= 192000.0)
        rateId = 4;
    else if (savedRate >= 96000.0)
        rateId = 3;
    else if (savedRate >= 48000.0)
        rateId = 2;
    sampleRateComboBox_.setSelectedId(rateId, juce::dontSendNotification);
    addAndMakeVisible(sampleRateComboBox_);

    // Bit depth (read-only, updates based on format)
    bitDepthLabel_.setText(tr("export_audio.label.bit_depth"), juce::dontSendNotification);
    bitDepthLabel_.setFont(FontManager::getInstance().getUIFontBold(14.0f));
    addAndMakeVisible(bitDepthLabel_);

    bitDepthValueLabel_.setFont(FontManager::getInstance().getUIFont(14.0f));
    addAndMakeVisible(bitDepthValueLabel_);
    updateBitDepthOptions();  // Set label based on restored format

    // Normalization option
    normalizeCheckbox_.setButtonText(tr("export_audio.toggle.normalize"));
    normalizeCheckbox_.setToggleState(false, juce::dontSendNotification);
    addAndMakeVisible(normalizeCheckbox_);

    // Real-time render option
    realTimeRenderCheckbox_.setButtonText(tr("export_audio.toggle.realtime_render"));
    realTimeRenderCheckbox_.setToggleState(false, juce::dontSendNotification);
    addAndMakeVisible(realTimeRenderCheckbox_);

    // Lead-in silence
    leadInSilenceLabel_.setText(tr("export_audio.label.lead_in_silence"),
                                juce::dontSendNotification);
    leadInSilenceLabel_.setFont(FontManager::getInstance().getUIFontBold(14.0f));
    addAndMakeVisible(leadInSilenceLabel_);

    leadInSilenceSlider_.setSliderStyle(juce::Slider::LinearBar);
    leadInSilenceSlider_.setRange(0.0, 2.0, 0.1);
    leadInSilenceSlider_.setValue(0.0, juce::dontSendNotification);
    leadInSilenceSlider_.setTextValueSuffix(" s");
    addAndMakeVisible(leadInSilenceSlider_);

    // Time range selection
    timeRangeLabel_.setText(tr("export_audio.label.export_range"), juce::dontSendNotification);
    timeRangeLabel_.setFont(FontManager::getInstance().getUIFontBold(14.0f));
    addAndMakeVisible(timeRangeLabel_);

    exportEntireSongButton_.setButtonText(tr("export_audio.option.entire_song"));
    exportEntireSongButton_.setRadioGroupId(1);
    exportEntireSongButton_.setToggleState(true, juce::dontSendNotification);
    addAndMakeVisible(exportEntireSongButton_);

    exportTimeSelectionButton_.setButtonText(tr("export_audio.option.time_selection"));
    exportTimeSelectionButton_.setRadioGroupId(1);
    exportTimeSelectionButton_.setEnabled(false);  // Disabled by default
    addAndMakeVisible(exportTimeSelectionButton_);

    exportLoopRegionButton_.setButtonText(tr("export_audio.option.loop_region"));
    exportLoopRegionButton_.setRadioGroupId(1);
    exportLoopRegionButton_.setEnabled(false);  // Disabled by default
    addAndMakeVisible(exportLoopRegionButton_);

    // Export button
    exportButton_.setButtonText(tr("export_audio.button.export"));
    exportButton_.onClick = [this]() {
        if (onExport) {
            auto settings = getSettings();
            // Persist render preferences
            auto& cfg = Config::getInstance();
            int bitDepth = 24;
            if (settings.format == "WAV16")
                bitDepth = 16;
            else if (settings.format == "WAV32")
                bitDepth = 32;
            cfg.setRenderBitDepth(bitDepth);
            cfg.setRenderSampleRate(settings.sampleRate);
            cfg.save();
            onExport(settings);
        }
        if (auto* dw = findParentComponentOfClass<juce::DialogWindow>()) {
            dw->exitModalState(0);
        }
    };
    addAndMakeVisible(exportButton_);

    // Cancel button
    cancelButton_.setButtonText(tr("dialogs.cancel"));
    cancelButton_.onClick = [this]() {
        if (auto* dw = findParentComponentOfClass<juce::DialogWindow>()) {
            dw->exitModalState(0);
        }
    };
    addAndMakeVisible(cancelButton_);

    // Set preferred size
    setSize(500, 450);
}

ExportAudioDialog::~ExportAudioDialog() {
    setLookAndFeel(nullptr);
}

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
    bounds.removeFromTop(5);

    // Real-time render checkbox
    realTimeRenderCheckbox_.setBounds(bounds.removeFromTop(24));
    bounds.removeFromTop(10);

    // Lead-in silence
    auto leadInArea = bounds.removeFromTop(28);
    leadInSilenceLabel_.setBounds(leadInArea.removeFromLeft(120));
    leadInArea.removeFromLeft(10);
    leadInSilenceSlider_.setBounds(leadInArea.removeFromLeft(80));
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
    settings.realTimeRender = realTimeRenderCheckbox_.getToggleState();

    settings.leadInSilence = leadInSilenceSlider_.getValue();

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
            bitDepthText = tr("export_audio.bit_depth.16");
            break;
        case 2:  // WAV 24-bit
            bitDepthText = tr("export_audio.bit_depth.24");
            break;
        case 3:  // WAV 32-bit Float
            bitDepthText = tr("export_audio.bit_depth.32_float");
            break;
        case 4:  // FLAC — "24-bit (FLAC)"; the format suffix is a fixed technical
                 // token, so compose from the translatable 24-bit label rather
                 // than a separate (untranslatable) string.
            bitDepthText = tr("export_audio.bit_depth.24") + " (" +
                           magda::technicalText(magda::TechnicalTextToken::Flac) + ")";
            break;
        default:
            bitDepthText = tr("export_audio.bit_depth.24");
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
    options.dialogTitle = tr("dialogs.export_audio");
    options.dialogBackgroundColour = DarkTheme::getColour(DarkTheme::PANEL_BACKGROUND);
    options.content.setOwned(dialog);
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = false;

    options.launchAsync();
}

}  // namespace magda
