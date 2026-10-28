// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ExportAudioDialog.hpp"

#include <iterator>

#include "../../core/Config.hpp"
#include "../../project/ProjectManager.hpp"
#include "../themes/ActiveTheme.hpp"
#include "../themes/DialogLookAndFeel.hpp"
#include "../themes/FontManager.hpp"
#include "ExportFormats.hpp"
#include "core/StringTable.hpp"

namespace magda {

namespace {

struct ExportFormatChoice {
    const char* name;   // What Settings::format and the config hold
    const char* label;  // What the combo shows
};

constexpr ExportFormatChoice kFormats[] = {
    {"WAV", "WAV"}, {"FLAC", "FLAC"}, {"OPUS", "Opus"}, {"MP3", "MP3"}};

}  // namespace

ExportAudioDialog::ExportAudioDialog() {
    setLookAndFeel(&daw::ui::DialogLookAndFeel::getInstance());

    // Format selection
    formatLabel_.setText(tr("export_audio.label.format"), juce::dontSendNotification);
    formatLabel_.setFont(FontManager::getInstance().getUIFontBold(14.0f));
    addAndMakeVisible(formatLabel_);

    // File-format designators — do not translate. Item ids index kFormats.
    for (auto index = 0; index < static_cast<int>(std::size(kFormats)); ++index)
        formatComboBox_.addItem(kFormats[index].label, index + 1);
    // The last container exported to; the depth comes from the project.
    const auto& projectInfo = ProjectManager::getInstance().getCurrentProjectInfo();
    const auto lastFormat = normaliseExportFormat(Config::getInstance().getExportFormat());
    for (auto index = 0; index < static_cast<int>(std::size(kFormats)); ++index)
        if (lastFormat == kFormats[index].name)
            formatComboBox_.setSelectedId(index + 1, juce::dontSendNotification);
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

    bitDepthLabel_.setText(tr("export_audio.label.bit_depth"), juce::dontSendNotification);
    bitDepthLabel_.setFont(FontManager::getInstance().getUIFontBold(14.0f));
    addAndMakeVisible(bitDepthLabel_);

    bitDepthComboBox_.onChange = [this]() { updateFormatRows(); };
    addAndMakeVisible(bitDepthComboBox_);

    ditherLabel_.setText(tr("export_audio.label.dither"), juce::dontSendNotification);
    ditherLabel_.setFont(FontManager::getInstance().getUIFontBold(14.0f));
    addAndMakeVisible(ditherLabel_);

    ditherComboBox_.addItem(tr("export_audio.dither.off"), 1);
    ditherComboBox_.addItem("TPDF", 2);  // Technical designator, not translated.
    ditherComboBox_.addItem(tr("export_audio.dither.shaped"), 3);
    ditherComboBox_.setSelectedId(2, juce::dontSendNotification);
    addAndMakeVisible(ditherComboBox_);

    bitrateLabel_.setText(tr("export_audio.label.bitrate"), juce::dontSendNotification);
    bitrateLabel_.setFont(FontManager::getInstance().getUIFontBold(14.0f));
    addChildComponent(bitrateLabel_);

    addChildComponent(bitrateComboBox_);

    populateBitDepthOptions();
    bitDepthComboBox_.setSelectedId(projectInfo.renderBitDepth, juce::dontSendNotification);
    if (bitDepthComboBox_.getSelectedId() == 0 && bitDepthComboBox_.getNumItems() > 0)
        bitDepthComboBox_.setSelectedId(24, juce::dontSendNotification);
    populateBitrateOptions();
    updateFormatRows();

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
            cfg.setExportFormat(settings.format.toStdString());
            if (isMp3Selected())
                cfg.setExportMp3QualityId(bitrateComboBox_.getSelectedId());
            else if (isLossyExportFormat(settings.format))
                cfg.setExportBitrateKbps(settings.bitrateKbps);
            else
                cfg.setRenderBitDepth(settings.bitDepth);
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
    setSize(500, 488);
}

ExportAudioDialog::~ExportAudioDialog() {
    setLookAndFeel(nullptr);
}

void ExportAudioDialog::paint(juce::Graphics& g) {
    g.fillAll(ActiveTheme::getColour(ActiveTheme::PANEL_BACKGROUND));
}

void ExportAudioDialog::lookAndFeelChanged() {
    refreshHostWindowBackground(*this);
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

    // A lossy format's bitrate row takes the place of bit depth and dither, so
    // the rows below keep their positions.
    if (isLossySelected()) {
        auto bitrateArea = bounds.removeFromTop(28);
        bitrateLabel_.setBounds(bitrateArea.removeFromLeft(120));
        bitrateArea.removeFromLeft(10);
        bitrateComboBox_.setBounds(bitrateArea);
        bounds.removeFromTop(10 + 28 + 15);
    } else {
        auto bitDepthArea = bounds.removeFromTop(28);
        bitDepthLabel_.setBounds(bitDepthArea.removeFromLeft(120));
        bitDepthArea.removeFromLeft(10);
        bitDepthComboBox_.setBounds(bitDepthArea);
        bounds.removeFromTop(10);

        auto ditherArea = bounds.removeFromTop(28);
        ditherLabel_.setBounds(ditherArea.removeFromLeft(120));
        ditherArea.removeFromLeft(10);
        ditherComboBox_.setBounds(ditherArea);
        bounds.removeFromTop(15);
    }

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

    settings.format = selectedFormat();
    if (!isLossySelected())
        settings.bitDepth = bitDepthComboBox_.getSelectedId();

    if (isMp3Selected()) {
        const auto quality = mp3QualityForItemId(bitrateComboBox_.getSelectedId());
        settings.bitrateKbps = quality.bitrateKbps;
        settings.vbrQuality = quality.vbrQuality;
    } else if (isLossySelected()) {
        settings.bitrateKbps = bitrateComboBox_.getSelectedId();
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

    if (settings.bitDepth >= 32 || isLossySelected()) {
        settings.dither = OfflineRenderDither::None;
    } else {
        switch (ditherComboBox_.getSelectedId()) {
            case 1:
                settings.dither = OfflineRenderDither::None;
                break;
            case 3:
                settings.dither = OfflineRenderDither::Shaped;
                break;
            default:
                settings.dither = OfflineRenderDither::Tpdf;
                break;
        }
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
    populateBitDepthOptions();
    populateBitrateOptions();
    updateFormatRows();
}

void ExportAudioDialog::populateBitDepthOptions() {
    // FLAC has no float, so a 32-bit WAV becomes a 24-bit FLAC.
    const auto current = bitDepthComboBox_.getSelectedId();
    bitDepthComboBox_.clear(juce::dontSendNotification);
    for (const auto depth : exportBitDepthsFor(selectedFormat()))
        bitDepthComboBox_.addItem(depth >= 32 ? tr("export_audio.bit_depth.32_float")
                                              : tr("export_audio.bit_depth." + juce::String(depth)),
                                  depth);
    bitDepthComboBox_.setSelectedId(current, juce::dontSendNotification);
    if (bitDepthComboBox_.getSelectedId() == 0 && bitDepthComboBox_.getNumItems() > 0)
        bitDepthComboBox_.setSelectedId(24, juce::dontSendNotification);
}

void ExportAudioDialog::populateBitrateOptions() {
    // Item ids are the bitrates themselves, and for an MP3 VBR level
    // kMp3VbrItemIdBase plus the level. Labels are technical, not translated.
    const auto& config = Config::getInstance();
    bitrateComboBox_.clear(juce::dontSendNotification);

    if (isMp3Selected()) {
        for (const auto level : kMp3VbrLevels)
            bitrateComboBox_.addItem("VBR V" + juce::String(level), kMp3VbrItemIdBase + level);
        for (const auto kbps : kMp3CbrBitratesKbps)
            bitrateComboBox_.addItem("CBR " + juce::String(kbps) + " kbps", kbps);
        bitrateComboBox_.setSelectedId(config.getExportMp3QualityId(), juce::dontSendNotification);
        if (bitrateComboBox_.getSelectedId() == 0)
            bitrateComboBox_.setSelectedId(kDefaultMp3ItemId, juce::dontSendNotification);
        return;
    }

    for (const auto kbps : kExportBitratesKbps)
        bitrateComboBox_.addItem(juce::String(kbps) + " kbps", kbps);
    bitrateComboBox_.setSelectedId(config.getExportBitrateKbps(), juce::dontSendNotification);
    if (bitrateComboBox_.getSelectedId() == 0)
        bitrateComboBox_.setSelectedId(kDefaultExportBitrateKbps, juce::dontSendNotification);
}

void ExportAudioDialog::updateFormatRows() {
    // A float file has no grid to dither onto.
    ditherComboBox_.setEnabled(bitDepthComboBox_.getSelectedId() < 32);

    const auto lossy = isLossySelected();
    for (auto* row : std::initializer_list<juce::Component*>{&bitDepthLabel_, &bitDepthComboBox_,
                                                             &ditherLabel_, &ditherComboBox_})
        row->setVisible(!lossy);
    bitrateLabel_.setVisible(lossy);
    bitrateComboBox_.setVisible(lossy);

    // MP3 stops at 48 kHz; the engine would resample a higher render down anyway.
    const auto mp3 = isMp3Selected();
    sampleRateComboBox_.setItemEnabled(3, !mp3);
    sampleRateComboBox_.setItemEnabled(4, !mp3);
    if (mp3 && sampleRateComboBox_.getSelectedId() > 2)
        sampleRateComboBox_.setSelectedId(2, juce::dontSendNotification);
    resized();
}

juce::String ExportAudioDialog::selectedFormat() const {
    const auto index = formatComboBox_.getSelectedId() - 1;
    return index >= 0 && index < static_cast<int>(std::size(kFormats)) ? kFormats[index].name
                                                                       : "WAV";
}

bool ExportAudioDialog::isLossySelected() const {
    return isLossyExportFormat(selectedFormat());
}

bool ExportAudioDialog::isMp3Selected() const {
    return selectedFormat() == "MP3";
}

void ExportAudioDialog::showDialog(juce::Component* parent,
                                   std::function<void(const Settings&)> exportCallback,
                                   bool hasTimeSelection, bool hasLoopRegion) {
    auto* dialog = new ExportAudioDialog();
    dialog->setTimeSelectionAvailable(hasTimeSelection);
    dialog->setLoopRegionAvailable(hasLoopRegion);
    dialog->onExport = std::move(exportCallback);

    juce::DialogWindow::LaunchOptions options;
    options.dialogTitle = tr("dialogs.export_audio");
    options.dialogBackgroundColour = ActiveTheme::getColour(ActiveTheme::PANEL_BACKGROUND);
    options.content.setOwned(dialog);
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = false;

    options.launchAsync();
}

}  // namespace magda
