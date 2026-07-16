#include "../../../../themes/DarkTheme.hpp"
#include "../ClipInspector.hpp"

namespace magda::daw::ui {

void ClipInspector::onActivated() {
    magda::ClipManager::getInstance().addListener(this);
}

void ClipInspector::onDeactivated() {
    magda::ClipManager::getInstance().removeListener(this);
}

void ClipInspector::paint(juce::Graphics& g) {
    g.fillAll(DarkTheme::getBackgroundColour());

    // Segmented chip behind the view|type indicator icons in the name row.
    if (clipTypeIcon_ && clipTypeIcon_->isVisible() && !viewTypeChipBounds_.isEmpty()) {
        auto chip = viewTypeChipBounds_.toFloat();
        g.setColour(DarkTheme::getColour(DarkTheme::SURFACE));
        g.fillRoundedRectangle(chip, 4.0f);
        g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
        g.drawRoundedRectangle(chip.reduced(0.5f), 4.0f, 1.0f);
        if (clipViewIcon_ && clipViewIcon_->isVisible()) {
            g.drawLine(chip.getCentreX(), chip.getY() + 4.0f, chip.getCentreX(),
                       chip.getBottom() - 4.0f, 1.0f);
        }
    }
}

void ClipInspector::lookAndFeelChanged() {
    const auto primary = DarkTheme::getTextColour();
    const auto secondary = DarkTheme::getSecondaryTextColour();
    const auto surface = DarkTheme::getColour(DarkTheme::SURFACE);
    const auto border = DarkTheme::getColour(DarkTheme::BORDER);
    const auto accent = DarkTheme::getAccentColour();

    clipNameValue_.setColour(juce::Label::textColourId, primary);
    clipNameValue_.setColour(juce::Label::backgroundColourId, surface);

    if (clipTypeIcon_)
        clipTypeIcon_->setNormalColor(secondary);
    if (clipViewIcon_)
        clipViewIcon_->setNormalColor(secondary);
    if (clipGhostIcon_) {
        clipGhostIcon_->setNormalColor(secondary);
        clipGhostIcon_->setNormalBackgroundColor(surface);
        clipGhostIcon_->setBorderColor(border);
    }
    if (clipEnabledToggle_) {
        clipEnabledToggle_->setNormalBackgroundColor(surface);
        clipEnabledToggle_->setBorderColor(border);
    }

    clipCountLabel_.setColour(juce::Label::textColourId, primary);
    for (auto* label : {&clipFilePathLabel_,
                        &playbackColumnLabel_,
                        &loopColumnLabel_,
                        &clipStartLabel_,
                        &clipEndLabel_,
                        &clipLengthLabel_,
                        &clipLoopStartLabel_,
                        &clipLoopEndLabel_,
                        &clipLoopPhaseLabel_,
                        &audioPropsLabel_,
                        &clipBpmValue_,
                        &clipBpmUnitLabel_,
                        &clipBeatsUnitLabel_,
                        &clipKeyLabel_,
                        &pitchSectionLabel_,
                        &midiTransposeLabel_,
                        &beatDetectionSectionLabel_,
                        &transientSectionLabel_,
                        &transientSensitivityLabel_,
                        &grooveSectionLabel_,
                        &grooveStrengthLabel_,
                        &clipMixSectionLabel_,
                        &channelsSectionLabel_,
                        &launchModeLabel_,
                        &launchQuantizeLabel_,
                        &followActionLabel_,
                        &followActionDelayLabel_,
                        &followActionLoopCountLabel_})
        label->setColour(juce::Label::textColourId, secondary);
    clipBpmValue_.setColour(juce::Label::outlineColourId, border);

    for (auto* combo : {&stretchModeCombo_, &clipKeyRootCombo_, &autoPitchModeCombo_,
                        &launchModeCombo_, &launchQuantizeCombo_, &followActionCombo_}) {
        combo->setColour(juce::ComboBox::backgroundColourId, surface);
        combo->setColour(juce::ComboBox::textColourId, primary);
        combo->setColour(juce::ComboBox::outlineColourId, border);
    }

    const auto styleToggle = [surface, secondary, accent](juce::TextButton& button) {
        button.setColour(juce::TextButton::buttonColourId, surface);
        button.setColour(juce::TextButton::buttonOnColourId, accent.withAlpha(0.3f));
        button.setColour(juce::TextButton::textColourOffId, secondary);
        button.setColour(juce::TextButton::textColourOnId, accent);
    };
    for (auto* button :
         {&clipWarpToggle_, &clipAutoTempoToggle_, &autoPitchToggle_, &analogPitchToggle_,
          &reverseToggle_, &autoDetectBeatsToggle_, &leftChannelToggle_, &rightChannelToggle_})
        styleToggle(*button);

    audioPropsCollapseToggle_.setColour(juce::TextButton::buttonColourId,
                                        juce::Colours::transparentBlack);
    audioPropsCollapseToggle_.setColour(juce::TextButton::buttonOnColourId,
                                        juce::Colours::transparentBlack);
    audioPropsCollapseToggle_.setColour(juce::TextButton::textColourOffId, secondary);
    audioPropsCollapseToggle_.setColour(juce::TextButton::textColourOnId, secondary);

    for (auto* button : {&midiTransposeDownBtn_, &midiTransposeUpBtn_, &grooveTemplateButton_}) {
        button->setColour(juce::TextButton::buttonColourId, surface);
        button->setColour(juce::TextButton::textColourOffId, primary);
    }
    saveLibraryButton_.setColour(juce::TextButton::buttonColourId,
                                 DarkTheme::getColour(DarkTheme::BUTTON_NORMAL));
    saveLibraryButton_.setColour(juce::TextButton::textColourOffId, primary);

    repaint();
}

}  // namespace magda::daw::ui
