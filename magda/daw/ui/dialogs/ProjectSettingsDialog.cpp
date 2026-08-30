#include "ProjectSettingsDialog.hpp"

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
constexpr int kPagePad = 12;
constexpr int kRowGap = 12;
constexpr int kLabelW = 150;
constexpr int kControlW = 220;
constexpr int kTabBarDepth = 32;
constexpr int kButtonW = 90;
constexpr int kButtonGap = 8;
constexpr int kDialogW = 660;

// Metadata occupies two columns of short text fields. One column would make the
// Metadata tab twice as tall as the General one, and the dialog has to size to
// whichever tab is taller.
constexpr int kMetaLabelW = 118;
constexpr int kMetaColGutter = 20;
constexpr int kMetaRowGap = 8;
constexpr int kCommentH = 64;

// The first field takes the project name as its placeholder and the last gets
// the full-width multi-line box, which is right for a free-form comment and
// wrong for a one-line credit.
static_assert(kProjectMetadataFields.front().member == &ProjectMetadata::title,
              "ProjectSettingsDialog offers the project name as the first metadata field's "
              "placeholder; keep Title first in kProjectMetadataFields.");
static_assert(kProjectMetadataFields.back().member == &ProjectMetadata::comment,
              "ProjectSettingsDialog lays out the final metadata field as a multi-line "
              "comment box; keep Comment last in kProjectMetadataFields.");
constexpr size_t kMetaGridCount = kProjectMetadataFields.size() - 1;

const double kSampleRates[] = {44100.0, 48000.0, 88200.0, 96000.0, 192000.0};
const int kBitDepths[] = {16, 24, 32};  // 32 = 32-bit float

void setupFieldLabel(juce::Component& owner, juce::Label& label, const juce::String& text) {
    label.setText(text, juce::dontSendNotification);
    label.setFont(FontManager::getInstance().getUIFont(14.0f));
    label.setColour(juce::Label::textColourId, DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
    owner.addAndMakeVisible(label);
}

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

// ============================================================================
// Metadata tab
// ============================================================================

ProjectSettingsDialog::MetadataPage::MetadataPage() {
    for (size_t i = 0; i < kProjectMetadataFields.size(); ++i) {
        auto& row = rows_[i];
        const juce::String key(kProjectMetadataFields[i].key);
        setupFieldLabel(*this, row.label, tr("project_settings.metadata." + key));

        if (i == kProjectMetadataFields.size() - 1) {
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
}

int ProjectSettingsDialog::MetadataPage::preferredHeight() {
    const int gridH = static_cast<int>(kMetaGridCount / 2) * (kRowH + kMetaRowGap);
    return (kPagePad * 2) + gridH + kCommentH;
}

void ProjectSettingsDialog::MetadataPage::load(const ProjectInfo& info) {
    for (size_t i = 0; i < kProjectMetadataFields.size(); ++i)
        rows_[i].editor.setText(info.metadata.*kProjectMetadataFields[i].member,
                                juce::dontSendNotification);

    // A project that already has a name should not make anyone retype it as a
    // title. Leaving the box empty is what exports the name as <Title>, so the
    // placeholder is the value the field actually stands for, not a prompt -
    // and because it stays empty, renaming the project keeps carrying through
    // instead of freezing whatever the project was called the first time
    // somebody opened this dialog.
    rows_.front().editor.setTextToShowWhenEmpty(info.name,
                                                DarkTheme::getColour(DarkTheme::TEXT_DIM));
}

void ProjectSettingsDialog::MetadataPage::apply(ProjectInfo& info) const {
    // Trimmed, because trailing whitespace in a credit is invisible here and
    // still ends up in metadata.xml.
    for (size_t i = 0; i < kProjectMetadataFields.size(); ++i)
        info.metadata.*kProjectMetadataFields[i].member = rows_[i].editor.getText().trim();
}

void ProjectSettingsDialog::MetadataPage::resized() {
    auto bounds = getLocalBounds().reduced(0, kPagePad);

    auto field = [](Row& row, juce::Rectangle<int> area) {
        row.label.setBounds(area.removeFromLeft(kMetaLabelW));
        row.editor.setBounds(area);
    };

    for (size_t i = 0; i < kMetaGridCount; i += 2) {
        auto r = bounds.removeFromTop(kRowH);
        const int columnW = (r.getWidth() - kMetaColGutter) / 2;
        field(rows_[i], r.removeFromLeft(columnW));
        r.removeFromLeft(kMetaColGutter);
        field(rows_[i + 1], r);
        bounds.removeFromTop(kMetaRowGap);
    }

    field(rows_.back(), bounds.removeFromTop(kCommentH));
}

// ============================================================================
// General tab
// ============================================================================

ProjectSettingsDialog::GeneralPage::GeneralPage() {
    setupFieldLabel(*this, lengthLabel_, tr("project_settings.total_length"));
    setupFieldLabel(*this, sampleRateLabel_, tr("project_settings.sample_rate"));
    setupFieldLabel(*this, renderBitLabel_, tr("project_settings.render_bit_depth"));
    setupFieldLabel(*this, bounceBitLabel_, tr("project_settings.bounce_bit_depth"));

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
}

int ProjectSettingsDialog::GeneralPage::preferredHeight() {
    return (kPagePad * 2) + (4 * (kRowH + kRowGap));
}

void ProjectSettingsDialog::GeneralPage::load(const ProjectInfo& info) {
    lengthSlider_.setValue(info.timelineLengthBars, juce::dontSendNotification);
    sampleRateCombo_.setSelectedId(indexOfSampleRate(info.sampleRate), juce::dontSendNotification);
    renderBitCombo_.setSelectedId(indexOfBitDepth(info.renderBitDepth), juce::dontSendNotification);
    bounceBitCombo_.setSelectedId(indexOfBitDepth(info.bounceBitDepth), juce::dontSendNotification);
}

void ProjectSettingsDialog::GeneralPage::apply(ProjectInfo& info) const {
    info.timelineLengthBars = juce::jmax(1, static_cast<int>(lengthSlider_.getValue()));
    info.sampleRate = kSampleRates[juce::jmax(0, sampleRateCombo_.getSelectedId() - 1)];
    info.renderBitDepth = kBitDepths[juce::jmax(0, renderBitCombo_.getSelectedId() - 1)];
    info.bounceBitDepth = kBitDepths[juce::jmax(0, bounceBitCombo_.getSelectedId() - 1)];
}

void ProjectSettingsDialog::GeneralPage::resized() {
    auto bounds = getLocalBounds().reduced(0, kPagePad);

    auto row = [&](juce::Label& label, juce::Component& control) {
        auto r = bounds.removeFromTop(kRowH);
        label.setBounds(r.removeFromLeft(kLabelW));
        // The dialog is as wide as the Metadata tab's two columns need, which is
        // far wider than a bit-depth combo wants to be.
        control.setBounds(r.removeFromLeft(juce::jmin(r.getWidth(), kControlW)));
        bounds.removeFromTop(kRowGap);
    };

    row(lengthLabel_, lengthSlider_);
    row(sampleRateLabel_, sampleRateCombo_);
    row(renderBitLabel_, renderBitCombo_);
    row(bounceBitLabel_, bounceBitCombo_);
}

// ============================================================================
// Dialog
// ============================================================================

ProjectSettingsDialog::ProjectSettingsDialog() {
    setLookAndFeel(&daw::ui::DialogLookAndFeel::getInstance());

    const auto tabBg = DarkTheme::getColour(DarkTheme::PANEL_BACKGROUND);
    tabs_.addTab(tr("project_settings.section.general"), tabBg, &generalPage_, false);
    tabs_.addTab(tr("project_settings.section.metadata"), tabBg, &metadataPage_, false);
    tabs_.setTabBarDepth(kTabBarDepth);
    tabs_.setOutline(0);
    addAndMakeVisible(tabs_);

    // Apply without closing, so a length or a credit can be tried against the
    // arrangement behind the dialog rather than committed blind. There is no
    // OK: it would only be Apply that also closes, and the dialog is cheap to
    // dismiss once you can already see what you changed.
    applyBtn_.setButtonText(tr("dialogs.apply"));
    applyBtn_.onClick = [this]() { applySettings(); };

    cancelBtn_.setButtonText(tr("dialogs.cancel"));
    cancelBtn_.onClick = [this]() {
        if (auto* dw = findParentComponentOfClass<juce::DialogWindow>())
            dw->closeButtonPressed();
    };
    addAndMakeVisible(applyBtn_);
    addAndMakeVisible(cancelBtn_);

    loadSettings();

    // The tabs cannot resize as you switch between them, so the content area is
    // as tall as the taller page needs.
    const int pageH = juce::jmax(MetadataPage::preferredHeight(), GeneralPage::preferredHeight());
    setSize(kDialogW, (kPad * 2) + kTabBarDepth + pageH + kRowGap + kRowH);
}

ProjectSettingsDialog::~ProjectSettingsDialog() {
    setLookAndFeel(nullptr);
}

void ProjectSettingsDialog::loadSettings() {
    const auto& info = ProjectManager::getInstance().getCurrentProjectInfo();
    metadataPage_.load(info);
    generalPage_.load(info);
}

void ProjectSettingsDialog::applySettings() {
    auto& pm = ProjectManager::getInstance();
    auto& info = pm.getMutableProjectInfo();

    metadataPage_.apply(info);
    generalPage_.apply(info);

    pm.markDirty();

    // Apply the new length to the live timeline immediately.
    if (auto* tc = TimelineController::getCurrent()) {
        const int beatsPerBar = juce::jmax(1, tc->getState().tempo.timeSignatureNumerator);
        tc->dispatch(SetTimelineLengthBeatsEvent{static_cast<double>(info.timelineLengthBars) *
                                                 beatsPerBar});
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

    auto buttons = bounds.removeFromBottom(kRowH);
    cancelBtn_.setBounds(buttons.removeFromRight(kButtonW));
    buttons.removeFromRight(kButtonGap);
    applyBtn_.setBounds(buttons.removeFromRight(kButtonW));
    bounds.removeFromBottom(kRowGap);

    tabs_.setBounds(bounds);
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
