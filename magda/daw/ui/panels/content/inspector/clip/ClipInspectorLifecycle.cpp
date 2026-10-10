#include "../../../../themes/ActiveTheme.hpp"
#include "../ClipInspector.hpp"

namespace magda::daw::ui {

void ClipInspector::onActivated() {
    magda::ClipManager::getInstance().addListener(this);
}

void ClipInspector::onDeactivated() {
    magda::ClipManager::getInstance().removeListener(this);
}

void ClipInspector::paint(juce::Graphics& g) {
    g.fillAll(ActiveTheme::getBackgroundColour());

    // The name field, rounded like the chips beside it, as in the track inspector.
    if (clipNameValue_.isVisible()) {
        const auto field = clipNameValue_.getBounds().toFloat();
        const float radius = juce::jlimit(2.0f, 8.0f, field.getHeight() * 0.15f);
        g.setColour(nameFill_);
        g.fillRoundedRectangle(field, radius);
        g.setColour(nameOutline_);
        g.drawRoundedRectangle(field.reduced(0.5f), radius, 1.0f);
    }
}

void ClipInspector::lookAndFeelChanged() {
    const auto primary = ActiveTheme::getTextColour();
    const auto secondary = ActiveTheme::getSecondaryTextColour();
    const auto surface = ActiveTheme::getColour(ActiveTheme::SURFACE);
    const auto border = ActiveTheme::getColour(ActiveTheme::BORDER);
    const auto accent = ActiveTheme::getAccentColour();

    applyHeaderStyle();

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
                                 ActiveTheme::getColour(ActiveTheme::BUTTON_NORMAL));
    saveLibraryButton_.setColour(juce::TextButton::textColourOffId, primary);

    repaint();
}

void ClipInspector::applyHeaderStyle() {
    const auto* clip = selectedClipIds_.size() == 1
                           ? magda::ClipManager::getInstance().getClip(primaryClipId())
                           : nullptr;
    const bool fullBar = magda::Config::getInstance().getTrackColourStyle() == "full" &&
                         clip != nullptr && clip->colour != juce::Colour(0xFF444444);
    if (nameFilled_ != fullBar) {
        nameFilled_ = fullBar;
        resized();
    }
    nameFill_ = fullBar ? magda::deriveTrackSwatch(clip->colour)
                        : ActiveTheme::getColour(ActiveTheme::SURFACE);
    nameOutline_ = fullBar ? juce::Colours::transparentBlack : ActiveTheme::getBorderColour();
    clipNameValue_.setColour(juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    clipNameValue_.setColour(juce::Label::outlineColourId, juce::Colours::transparentBlack);
    clipNameValue_.setColour(juce::Label::textColourId,
                             fullBar ? juce::Colours::white : ActiveTheme::getTextColour());
    // The watermark icons take the name text's colour, so they read on a filled field too.
    for (auto* icon : {clipTypeIcon_.get(), clipViewIcon_.get()})
        icon->setNormalColor(fullBar ? juce::Colours::white
                                     : ActiveTheme::getColour(ActiveTheme::TEXT_SECONDARY));
    repaint();
}

void ClipInspector::configChanged() {
    applyHeaderStyle();
}

}  // namespace magda::daw::ui
