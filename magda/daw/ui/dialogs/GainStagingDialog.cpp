#include "GainStagingDialog.hpp"

#include "../themes/DarkTheme.hpp"
#include "../themes/DialogLookAndFeel.hpp"
#include "../themes/FontManager.hpp"
#include "core/StringTable.hpp"

namespace magda {

GainStagingDialog::GainStagingDialog(float initialTargetDb, bool initialUseAi) {
    setLookAndFeel(&daw::ui::DialogLookAndFeel::getInstance());

    descriptionLabel_.setText(tr("gain_staging.description"), juce::dontSendNotification);
    descriptionLabel_.setFont(FontManager::getInstance().getUIFont(12.0f));
    descriptionLabel_.setColour(juce::Label::textColourId, DarkTheme::getSecondaryTextColour());
    descriptionLabel_.setJustificationType(juce::Justification::topLeft);
    addAndMakeVisible(descriptionLabel_);

    targetLabel_.setText(tr("gain_staging.label.target"), juce::dontSendNotification);
    targetLabel_.setFont(FontManager::getInstance().getUIFontBold(14.0f));
    addAndMakeVisible(targetLabel_);

    targetSlider_.setSliderStyle(juce::Slider::LinearHorizontal);
    targetSlider_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 70, 24);
    targetSlider_.setRange(-36.0, 0.0, 0.5);
    targetSlider_.setValue(initialTargetDb, juce::dontSendNotification);
    targetSlider_.setTextValueSuffix(" dB");
    addAndMakeVisible(targetSlider_);

    useAiButton_.setButtonText(tr("gain_staging.toggle.use_ai"));
    useAiButton_.setToggleState(initialUseAi, juce::dontSendNotification);
    addAndMakeVisible(useAiButton_);

    aiHintLabel_.setText(tr("gain_staging.hint.use_ai"), juce::dontSendNotification);
    aiHintLabel_.setFont(FontManager::getInstance().getUIFont(11.0f));
    aiHintLabel_.setColour(juce::Label::textColourId, DarkTheme::getSecondaryTextColour());
    aiHintLabel_.setJustificationType(juce::Justification::topLeft);
    addAndMakeVisible(aiHintLabel_);

    startButton_.setButtonText(tr("gain_staging.button.start"));
    startButton_.onClick = [this]() {
        if (onStart)
            onStart(getSettings());
        if (auto* dw = findParentComponentOfClass<juce::DialogWindow>())
            dw->exitModalState(0);
    };
    addAndMakeVisible(startButton_);

    cancelButton_.setButtonText(tr("dialogs.cancel"));
    cancelButton_.onClick = [this]() {
        if (auto* dw = findParentComponentOfClass<juce::DialogWindow>())
            dw->exitModalState(0);
    };
    addAndMakeVisible(cancelButton_);

    setSize(440, 250);
}

GainStagingDialog::~GainStagingDialog() {
    setLookAndFeel(nullptr);
}

void GainStagingDialog::paint(juce::Graphics& g) {
    g.fillAll(DarkTheme::getColour(DarkTheme::PANEL_BACKGROUND));
}

void GainStagingDialog::resized() {
    auto bounds = getLocalBounds().reduced(20);

    descriptionLabel_.setBounds(bounds.removeFromTop(44));
    bounds.removeFromTop(8);

    auto targetArea = bounds.removeFromTop(28);
    targetLabel_.setBounds(targetArea.removeFromLeft(90));
    targetArea.removeFromLeft(10);
    targetSlider_.setBounds(targetArea);
    bounds.removeFromTop(16);

    useAiButton_.setBounds(bounds.removeFromTop(24));
    bounds.removeFromTop(2);
    aiHintLabel_.setBounds(bounds.removeFromTop(32).withTrimmedLeft(24));

    const int buttonHeight = 32;
    const int buttonWidth = 100;
    const int buttonSpacing = 10;
    auto buttonArea = bounds.removeFromBottom(buttonHeight);
    cancelButton_.setBounds(buttonArea.removeFromRight(buttonWidth));
    buttonArea.removeFromRight(buttonSpacing);
    startButton_.setBounds(buttonArea.removeFromRight(buttonWidth));
}

GainStagingDialog::Settings GainStagingDialog::getSettings() const {
    Settings settings;
    settings.targetDb = static_cast<float>(targetSlider_.getValue());
    settings.useAi = useAiButton_.getToggleState();
    return settings;
}

void GainStagingDialog::showDialog(juce::Component* parent, float initialTargetDb,
                                   bool initialUseAi,
                                   std::function<void(const Settings&)> startCallback) {
    auto* dialog = new GainStagingDialog(initialTargetDb, initialUseAi);
    dialog->onStart = std::move(startCallback);

    juce::DialogWindow::LaunchOptions options;
    options.dialogTitle = tr("gain_staging.dialog.title");
    options.dialogBackgroundColour = DarkTheme::getColour(DarkTheme::PANEL_BACKGROUND);
    options.content.setOwned(dialog);
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = false;
    if (parent != nullptr)
        options.componentToCentreAround = parent;

    options.launchAsync();
}

}  // namespace magda
