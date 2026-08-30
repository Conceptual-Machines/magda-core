#include "ProjectSettingsDialog.hpp"

#include "../../core/Config.hpp"
#include "../../core/StringTable.hpp"
#include "../../project/ProjectManager.hpp"
#include "../state/TimelineController.hpp"
#include "../themes/DarkTheme.hpp"
#include "../themes/DialogLookAndFeel.hpp"
#include "../themes/FontManager.hpp"

namespace magda {

namespace {
constexpr int kRowH = 28;
constexpr int kPad = 16;
constexpr int kLabelW = 150;
constexpr int kHeaderH = 20;

// Metadata occupies two columns of short text fields, because thirteen stacked
// rows would push the dialog past the height of a laptop screen on its own.
constexpr int kMetaLabelW = 118;
constexpr int kMetaColGutter = 20;
constexpr int kMetaRowGap = 8;
constexpr int kCommentH = 64;
constexpr int kDialogW = 660;

// The last field gets the full-width multi-line box, which is right for a
// free-form comment and wrong for a one-line credit.
static_assert(kProjectMetadataFields.back().member == &ProjectMetadata::comment,
              "ProjectSettingsDialog lays out the final metadata field as a multi-line "
              "comment box; keep Comment last in kProjectMetadataFields.");
constexpr size_t kMetaGridCount = kProjectMetadataFields.size() - 1;

const double kSampleRates[] = {44100.0, 48000.0, 88200.0, 96000.0, 192000.0};
const int kBitDepths[] = {16, 24, 32};  // 32 = 32-bit float

void fillSampleRateCombo(juce::ComboBox& c) {
    for (int i = 0; i < static_cast<int>(std::size(kSampleRates)); ++i)
        c.addItem(juce::String(static_cast<int>(kSampleRates[i])) + " Hz", i + 1);
}

void fillBitDepthCombo(juce::ComboBox& c) {
    c.addItem("16-bit", 1);
    c.addItem("24-bit", 2);
    c.addItem("32-bit float", 3);
}

int indexOfSampleRate(double rate) {
    for (int i = 0; i < static_cast<int>(std::size(kSampleRates)); ++i)
        if (std::abs(kSampleRates[i] - rate) < 0.5)
            return i + 1;
    return 2;  // default 48000
}

int indexOfBitDepth(int depth) {
    for (int i = 0; i < static_cast<int>(std::size(kBitDepths)); ++i)
        if (kBitDepths[i] == depth)
            return i + 1;
    return 2;  // default 24
}
}  // namespace

ProjectSettingsDialog::ProjectSettingsDialog() {
    setLookAndFeel(&daw::ui::DialogLookAndFeel::getInstance());

    auto setupLabel = [this](juce::Label& l, const juce::String& text) {
        l.setText(text, juce::dontSendNotification);
        l.setFont(FontManager::getInstance().getUIFont(14.0f));
        l.setColour(juce::Label::textColourId, DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
        addAndMakeVisible(l);
    };

    auto setupHeader = [this](juce::Label& l, const juce::String& text) {
        l.setText(text, juce::dontSendNotification);
        l.setFont(FontManager::getInstance().getUIFontBold(14.0f));
        l.setColour(juce::Label::textColourId, DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
        addAndMakeVisible(l);
    };

    setupHeader(metadataHeader_, tr("project_settings.section.metadata"));
    setupHeader(generalHeader_, tr("project_settings.section.general"));

    for (size_t i = 0; i < kProjectMetadataFields.size(); ++i) {
        auto& row = metadataRows_[i];
        const juce::String key(kProjectMetadataFields[i].key);
        setupLabel(row.label, tr("project_settings.metadata." + key));

        const bool isComment = i == kProjectMetadataFields.size() - 1;
        if (isComment) {
            row.label.setJustificationType(juce::Justification::topLeft);
            row.editor.setMultiLine(true, true);
            row.editor.setReturnKeyStartsNewLine(true);
        }

        row.editor.setFont(FontManager::getInstance().getUIFont(13.0f));
        row.editor.setColour(juce::TextEditor::backgroundColourId,
                             DarkTheme::getColour(DarkTheme::SURFACE));
        row.editor.setColour(juce::TextEditor::textColourId,
                             DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
        row.editor.setColour(juce::TextEditor::outlineColourId,
                             DarkTheme::getColour(DarkTheme::BORDER));
        addAndMakeVisible(row.editor);
    }

    setupLabel(lengthLabel_, tr("project_settings.total_length"));
    setupLabel(sampleRateLabel_, tr("project_settings.sample_rate"));
    setupLabel(renderBitLabel_, tr("project_settings.render_bit_depth"));
    setupLabel(bounceBitLabel_, tr("project_settings.bounce_bit_depth"));

    lengthSlider_.setSliderStyle(juce::Slider::IncDecButtons);
    // Total length is constrained to multiples of 16 bars.
    lengthSlider_.setRange(16.0, 4096.0, 16.0);
    lengthSlider_.setTextBoxStyle(juce::Slider::TextBoxLeft, false, 80, kRowH);
    lengthSlider_.setTextValueSuffix(" " + tr("project_settings.bars"));
    addAndMakeVisible(lengthSlider_);

    fillSampleRateCombo(sampleRateCombo_);
    fillBitDepthCombo(renderBitCombo_);
    fillBitDepthCombo(bounceBitCombo_);
    addAndMakeVisible(sampleRateCombo_);
    addAndMakeVisible(renderBitCombo_);
    addAndMakeVisible(bounceBitCombo_);

    saveAsDefaultBtn_.setButtonText(tr("project_settings.save_as_default"));
    saveAsDefaultBtn_.setClickingTogglesState(true);
    addAndMakeVisible(saveAsDefaultBtn_);

    okBtn_.onClick = [this]() {
        applySettings();
        if (auto* dw = findParentComponentOfClass<juce::DialogWindow>())
            dw->closeButtonPressed();
    };
    cancelBtn_.onClick = [this]() {
        if (auto* dw = findParentComponentOfClass<juce::DialogWindow>())
            dw->closeButtonPressed();
    };
    addAndMakeVisible(okBtn_);
    addAndMakeVisible(cancelBtn_);

    loadSettings();

    // Metadata: a header, six two-up rows, the comment box. Then the general
    // block: a header, four rows, the save-as-default checkbox. Then buttons.
    const int metadataH =
        kHeaderH + 6 + static_cast<int>(kMetaGridCount / 2) * (kRowH + kMetaRowGap) + kCommentH;
    const int generalH = kHeaderH + 6 + kRowH * 5 + 12 * 4;
    setSize(kDialogW, kPad * 2 + metadataH + 14 + generalH + 12 + kRowH);
}

ProjectSettingsDialog::~ProjectSettingsDialog() {
    setLookAndFeel(nullptr);
}

void ProjectSettingsDialog::loadSettings() {
    const auto& info = ProjectManager::getInstance().getCurrentProjectInfo();

    for (size_t i = 0; i < kProjectMetadataFields.size(); ++i)
        metadataRows_[i].editor.setText(info.metadata.*kProjectMetadataFields[i].member,
                                        juce::dontSendNotification);

    lengthSlider_.setValue(info.timelineLengthBars, juce::dontSendNotification);
    sampleRateCombo_.setSelectedId(indexOfSampleRate(info.sampleRate), juce::dontSendNotification);
    renderBitCombo_.setSelectedId(indexOfBitDepth(info.renderBitDepth), juce::dontSendNotification);
    bounceBitCombo_.setSelectedId(indexOfBitDepth(info.bounceBitDepth), juce::dontSendNotification);
}

void ProjectSettingsDialog::applySettings() {
    auto& pm = ProjectManager::getInstance();
    auto& info = pm.getMutableProjectInfo();

    // Trimmed, because trailing whitespace in a credit is invisible here and
    // still ends up in metadata.xml.
    for (size_t i = 0; i < kProjectMetadataFields.size(); ++i)
        info.metadata.*kProjectMetadataFields[i].member = metadataRows_[i].editor.getText().trim();

    const int bars = juce::jmax(1, static_cast<int>(lengthSlider_.getValue()));
    info.timelineLengthBars = bars;
    info.sampleRate = kSampleRates[juce::jmax(0, sampleRateCombo_.getSelectedId() - 1)];
    info.renderBitDepth = kBitDepths[juce::jmax(0, renderBitCombo_.getSelectedId() - 1)];
    info.bounceBitDepth = kBitDepths[juce::jmax(0, bounceBitCombo_.getSelectedId() - 1)];

    pm.markDirty();

    // Optionally persist these as the defaults for new projects.
    if (saveAsDefaultBtn_.getToggleState()) {
        auto& config = Config::getInstance();
        config.setDefaultTimelineLengthBars(info.timelineLengthBars);
        config.setRenderSampleRate(info.sampleRate);
        config.setRenderBitDepth(info.renderBitDepth);
        config.setBounceBitDepth(info.bounceBitDepth);
        config.save();
    }

    // Apply the new length to the live timeline immediately.
    if (auto* tc = TimelineController::getCurrent()) {
        const int beatsPerBar = juce::jmax(1, tc->getState().tempo.timeSignatureNumerator);
        tc->dispatch(SetTimelineLengthBeatsEvent{static_cast<double>(bars) * beatsPerBar});
    }
}

void ProjectSettingsDialog::paint(juce::Graphics& g) {
    g.fillAll(DarkTheme::getColour(DarkTheme::PANEL_BACKGROUND));
}

void ProjectSettingsDialog::lookAndFeelChanged() {
    refreshHostWindowBackground(*this);
}

void ProjectSettingsDialog::resized() {
    auto bounds = getLocalBounds().reduced(kPad);

    // Buttons first, off the bottom, so the blocks above can take what is left
    // without having to know how tall the footer is.
    auto buttons = bounds.removeFromBottom(kRowH);
    cancelBtn_.setBounds(buttons.removeFromRight(90));
    buttons.removeFromRight(8);
    okBtn_.setBounds(buttons.removeFromRight(90));
    bounds.removeFromBottom(12);

    auto metaField = [](MetadataRow& row, juce::Rectangle<int> area) {
        row.label.setBounds(area.removeFromLeft(kMetaLabelW));
        row.editor.setBounds(area);
    };

    metadataHeader_.setBounds(bounds.removeFromTop(kHeaderH));
    bounds.removeFromTop(6);

    for (size_t i = 0; i < kMetaGridCount; i += 2) {
        auto r = bounds.removeFromTop(kRowH);
        const int columnW = (r.getWidth() - kMetaColGutter) / 2;
        metaField(metadataRows_[i], r.removeFromLeft(columnW));
        r.removeFromLeft(kMetaColGutter);
        metaField(metadataRows_[i + 1], r);
        bounds.removeFromTop(kMetaRowGap);
    }

    metaField(metadataRows_.back(), bounds.removeFromTop(kCommentH));
    bounds.removeFromTop(14);

    generalHeader_.setBounds(bounds.removeFromTop(kHeaderH));
    bounds.removeFromTop(6);

    auto row = [&](juce::Label& label, juce::Component& control) {
        auto r = bounds.removeFromTop(kRowH);
        label.setBounds(r.removeFromLeft(kLabelW));
        // The dialog is wide enough for two metadata columns, which is far wider
        // than a bit-depth combo wants to be.
        control.setBounds(r.removeFromLeft(juce::jmin(r.getWidth(), 220)));
        bounds.removeFromTop(12);
    };

    row(lengthLabel_, lengthSlider_);
    row(sampleRateLabel_, sampleRateCombo_);
    row(renderBitLabel_, renderBitCombo_);
    row(bounceBitLabel_, bounceBitCombo_);
    saveAsDefaultBtn_.setBounds(bounds.removeFromTop(kRowH));
}

void ProjectSettingsDialog::showDialog(juce::Component* parent) {
    (void)parent;
    auto* dialog = new ProjectSettingsDialog();

    juce::DialogWindow::LaunchOptions options;
    options.dialogTitle = tr("menu.file.project_settings");
    options.dialogBackgroundColour = DarkTheme::getColour(DarkTheme::PANEL_BACKGROUND);
    options.content.setOwned(dialog);
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = false;

    options.launchAsync();
}

}  // namespace magda
