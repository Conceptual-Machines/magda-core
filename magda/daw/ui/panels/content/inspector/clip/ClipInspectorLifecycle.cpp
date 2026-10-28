// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../../../../themes/ActiveTheme.hpp"
#include "../ClipInspector.hpp"
#include "ClipInspectorStyle.hpp"

namespace magda::daw::ui {

void ClipInspector::onActivated() {
    magda::ClipManager::getInstance().addListener(this);
    magda::TrackManager::getInstance().addListener(this);
}

void ClipInspector::onDeactivated() {
    magda::ClipManager::getInstance().removeListener(this);
    magda::TrackManager::getInstance().removeListener(this);
}

void ClipInspector::trackDevicesChanged(magda::TrackId trackId) {
    for (auto id : selectedClipIds_)
        if (const auto* clip = magda::ClipManager::getInstance().getClip(id);
            clip != nullptr && clip->trackId == trackId) {
            updateFromSelectedClip();
            return;
        }
}

void ClipInspector::paint(juce::Graphics& g) {
    g.fillAll(ActiveTheme::getBackgroundColour());

    if (!clipNameValue_.isVisible())
        return;

    // Full bar fills the name block with the clip colour, as the track inspector does.
    if (nameFilled_) {
        g.setColour(nameFill_);
        g.fillRoundedRectangle(nameArea_.toFloat().expanded(6.0f, 3.0f), 4.0f);
    }

    // Hairline under the header, where the scrolling sections begin.
    g.setColour(ActiveTheme::getColour(ActiveTheme::SEPARATOR));
    g.fillRect(0, clipPropsViewport_.getY() - 1, getWidth(), 1);
}

void ClipInspector::lookAndFeelChanged() {
    applyHeaderStyle();

    for (auto* label :
         {&startColumnLabel_, &endColumnLabel_, &lengthColumnLabel_, &clipLoopPhaseLabel_})
        label->setColour(juce::Label::textColourId, ActiveTheme::getColour(ActiveTheme::TEXT_DIM));
    for (auto* label :
         {&clipRowLabel_, &loopRowLabel_, &volumeLabel_, &panLabel_, &gainLabel_,
          &transientSensitivityLabel_, &tempoLabel_, &sourceLengthLabel_, &speedLabel_,
          &pitchLabel_, &clipKeyLabel_, &midiTransposeLabel_, &launchQuantizeLabel_,
          &followActionLabel_, &followActionDelayLabel_, &followActionLoopCountLabel_})
        label->setColour(juce::Label::textColourId, ActiveTheme::getSecondaryTextColour());
    midiTransposeHint_.setColour(juce::Label::textColourId,
                                 ActiveTheme::getColour(ActiveTheme::TEXT_DIM));

    // The tempo field is a Label drawn as a field, with its unit dimmed inside the right edge.
    clipBpmValue_.setColour(juce::Label::textColourId, ActiveTheme::getTextColour());
    clipBpmValue_.setColour(juce::Label::backgroundColourId,
                            ActiveTheme::getColour(ActiveTheme::SURFACE));
    clipBpmValue_.setColour(juce::Label::outlineColourId,
                            ActiveTheme::getColour(ActiveTheme::BORDER));
    clipBpmUnitLabel_.setColour(juce::Label::textColourId,
                                ActiveTheme::getColour(ActiveTheme::TEXT_DIM));

    for (auto* combo :
         {&stretchModeCombo_, &clipKeyRootCombo_, &launchQuantizeCombo_, &followActionCombo_})
        clip_style::combo(*combo);
    for (auto* button : {&analogPitchToggle_, &reverseToggle_, &gainEnvelopeToggle_})
        clip_style::toggle(*button);
    for (auto* button : {&halveTempoButton_, &doubleTempoButton_})
        clip_style::action(*button);
    for (auto& button : midiTransposeButtons_)
        clip_style::action(button);

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
    nameFill_ = fullBar ? magda::deriveTrackSwatch(clip->colour) : juce::Colours::transparentBlack;
    clipNameValue_.setColour(juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    clipNameValue_.setColour(juce::Label::outlineColourId, juce::Colours::transparentBlack);
    clipNameValue_.setColour(juce::Label::textColourId,
                             fullBar ? juce::Colours::white : ActiveTheme::getTextColour());
    clipSubline_.setColour(juce::Label::textColourId,
                           fullBar ? juce::Colours::white.withAlpha(0.7f)
                                   : ActiveTheme::getColour(ActiveTheme::TEXT_DIM));
    repaint();
}

void ClipInspector::configChanged() {
    applyHeaderStyle();
}

}  // namespace magda::daw::ui
