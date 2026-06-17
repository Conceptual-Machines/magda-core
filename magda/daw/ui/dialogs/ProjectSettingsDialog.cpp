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
    // 4 setting rows + checkbox row + button row.
    setSize(380, kPad * 2 + kRowH * 6 + 12 * 5 + 8);
}

ProjectSettingsDialog::~ProjectSettingsDialog() {
    setLookAndFeel(nullptr);
}

void ProjectSettingsDialog::loadSettings() {
    const auto& info = ProjectManager::getInstance().getCurrentProjectInfo();
    lengthSlider_.setValue(info.timelineLengthBars, juce::dontSendNotification);
    sampleRateCombo_.setSelectedId(indexOfSampleRate(info.sampleRate), juce::dontSendNotification);
    renderBitCombo_.setSelectedId(indexOfBitDepth(info.renderBitDepth), juce::dontSendNotification);
    bounceBitCombo_.setSelectedId(indexOfBitDepth(info.bounceBitDepth), juce::dontSendNotification);
}

void ProjectSettingsDialog::applySettings() {
    auto& pm = ProjectManager::getInstance();
    auto& info = pm.getMutableProjectInfo();

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

void ProjectSettingsDialog::resized() {
    auto bounds = getLocalBounds().reduced(kPad);

    auto row = [&](juce::Label& label, juce::Component& control) {
        auto r = bounds.removeFromTop(kRowH);
        label.setBounds(r.removeFromLeft(kLabelW));
        control.setBounds(r);
        bounds.removeFromTop(12);
    };

    row(lengthLabel_, lengthSlider_);
    row(sampleRateLabel_, sampleRateCombo_);
    row(renderBitLabel_, renderBitCombo_);
    row(bounceBitLabel_, bounceBitCombo_);
    saveAsDefaultBtn_.setBounds(bounds.removeFromTop(kRowH));

    auto buttons = bounds.removeFromBottom(kRowH);
    cancelBtn_.setBounds(buttons.removeFromRight(90));
    buttons.removeFromRight(8);
    okBtn_.setBounds(buttons.removeFromRight(90));
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
