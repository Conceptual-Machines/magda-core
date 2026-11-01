// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include "modulation/FollowerEditorPanel.hpp"

#include <utility>

#include "ui/components/common/DeviceControls.hpp"
#include "ui/themes/FontManager.hpp"

namespace magda::daw::ui {

namespace {
constexpr int kRowH = 18;
constexpr int kGap = 4;

/** @brief A time short enough for a third of the row: ms, then seconds from one second. */
juce::String shortTime(const juce::String& tag, double ms) {
    return tag + " " +
           (ms < 1000.0 ? juce::String(juce::roundToInt(ms)) + " ms"
                        : juce::String(ms / 1000.0, 1) + " s");
}
}  // namespace

FollowerEditorPanel::FollowerEditorPanel() {
    setInterceptsMouseClicks(true, true);

    // Live output display (reuses the generic value-history scroller)
    addAndMakeVisible(followerDisplay_);

    // Audio source button (Self / external track) — opens the host picker.
    sourceButton_.setLookAndFeel(&DeviceControlLookAndFeel::getInstance());
    sourceButton_.onClick = [this]() {
        if (onSourceClicked)
            onSourceClicked();
    };
    addAndMakeVisible(sourceButton_);

    // Time sliders fold their label into the value text (e.g. "A 100 ms").
    auto setupTimeSlider = [this](TextSlider& s, const juce::String& tag, double def,
                                  const std::function<float&()>& field) {
        s.setRange(0.0, 5000.0, 1.0);
        s.setSkewForCentre(250.0);
        s.setValue(def, juce::dontSendNotification);
        device_controls::styleValue(s);
        s.setFont(FontManager::getInstance().getUIFontMedium(9.5f));  // three to a row
        s.setValueFormatter([tag](double v) { return shortTime(tag, v); });
        s.onValueChanged = [this, field](double value) {
            field() = static_cast<float>(value);
            fireFollowerChanged();
        };
        addAndMakeVisible(s);
    };
    setupTimeSlider(attackSlider_, "A", 100.0,
                    [this]() -> float& { return currentMod_.followerAttackMs; });
    setupTimeSlider(holdSlider_, "H", 0.0,
                    [this]() -> float& { return currentMod_.followerHoldMs; });
    setupTimeSlider(releaseSlider_, "R", 500.0,
                    [this]() -> float& { return currentMod_.followerReleaseMs; });

    gainSlider_.setRange(-20.0, 20.0, 0.1);
    gainSlider_.setValue(0.0, juce::dontSendNotification);
    device_controls::styleValue(gainSlider_);
    gainSlider_.setValueFormatter([](double v) { return "Gain " + juce::String(v, 1) + " dB"; });
    gainSlider_.onValueChanged = [this](double value) {
        currentMod_.followerGainDb = static_cast<float>(value);
        fireFollowerChanged();
    };
    addAndMakeVisible(gainSlider_);

    // Band-limit detection: a toggle + cutoff slider per band. Filtering the raw
    // source before peak detection lets the follower track just the bass or just
    // the highs of its source.
    auto setupBandToggle = [this](juce::TextButton& b, const std::function<bool&()>& field,
                                  TextSlider& freq) {
        b.setClickingTogglesState(true);
        b.setLookAndFeel(&DeviceControlLookAndFeel::getInstance());
        b.onClick = [this, &b, field, &freq]() {
            field() = b.getToggleState();
            freq.setEnabled(b.getToggleState());
            fireFollowerChanged();
        };
        addAndMakeVisible(b);
    };
    auto setupFreqSlider = [this](TextSlider& s, double def, const std::function<float&()>& field) {
        s.setRange(20.0, 20000.0, 1.0);
        s.setSkewForCentre(1000.0);
        s.setValue(def, juce::dontSendNotification);
        device_controls::styleValue(s);
        s.setFont(FontManager::getInstance().getUIFontMedium(9.5f));  // half a row
        s.setValueFormatter([](double v) {
            return v < 1000.0 ? juce::String(juce::roundToInt(v)) + " Hz"
                              : juce::String(v / 1000.0, v < 10000.0 ? 1 : 0) + " kHz";
        });
        s.onValueChanged = [this, field](double value) {
            field() = static_cast<float>(value);
            fireFollowerChanged();
        };
        addAndMakeVisible(s);
    };
    setupBandToggle(
        hpEnableButton_, [this]() -> bool& { return currentMod_.followerHpEnabled; },
        hpFreqSlider_);
    setupFreqSlider(hpFreqSlider_, 200.0,
                    [this]() -> float& { return currentMod_.followerHpFreq; });
    setupBandToggle(
        lpEnableButton_, [this]() -> bool& { return currentMod_.followerLpEnabled; },
        lpFreqSlider_);
    setupFreqSlider(lpFreqSlider_, 2000.0,
                    [this]() -> float& { return currentMod_.followerLpFreq; });
}

void FollowerEditorPanel::setModInfo(const magda::ModInfo& mod, const magda::ModInfo* liveMod,
                                     std::function<const magda::ModInfo*()> liveModGetter) {
    currentMod_ = mod;
    liveModPtr_ = liveMod;
    liveModGetter_ = std::move(liveModGetter);
    followerDisplay_.setModInfo(liveMod ? liveMod : &currentMod_, liveModGetter_);
    updateFromMod();
}

void FollowerEditorPanel::updateFromMod() {
    gainSlider_.setValue(currentMod_.followerGainDb, juce::dontSendNotification);
    attackSlider_.setValue(currentMod_.followerAttackMs, juce::dontSendNotification);
    holdSlider_.setValue(currentMod_.followerHoldMs, juce::dontSendNotification);
    releaseSlider_.setValue(currentMod_.followerReleaseMs, juce::dontSendNotification);
    hpEnableButton_.setToggleState(currentMod_.followerHpEnabled, juce::dontSendNotification);
    hpFreqSlider_.setValue(currentMod_.followerHpFreq, juce::dontSendNotification);
    hpFreqSlider_.setEnabled(currentMod_.followerHpEnabled);
    lpEnableButton_.setToggleState(currentMod_.followerLpEnabled, juce::dontSendNotification);
    lpFreqSlider_.setValue(currentMod_.followerLpFreq, juce::dontSendNotification);
    lpFreqSlider_.setEnabled(currentMod_.followerLpEnabled);
    repaint();
}

void FollowerEditorPanel::fireFollowerChanged() {
    if (selectedModIndex_ >= 0 && onFollowerChanged) {
        onFollowerChanged(currentMod_);
    }
    followerDisplay_.repaint();
    repaint();  // a band toggle dims its cutoff's field
}

void FollowerEditorPanel::paint(juce::Graphics& g) {
    // Value sliders draw no frame of their own; their fields go under them.
    for (auto* slider : {&gainSlider_, &attackSlider_, &holdSlider_, &releaseSlider_,
                         &hpFreqSlider_, &lpFreqSlider_})
        device_controls::paintField(g, slider->getBounds().toFloat(),
                                    slider->isEnabled() ? 1.0f : 0.4f);
}

void FollowerEditorPanel::resized() {
    auto area = getLocalBounds();
    const auto fromBottom = [&area] {
        const auto row = area.removeFromBottom(kRowH);
        area.removeFromBottom(kGap);
        return row;
    };

    sourceButton_.setBounds(fromBottom());

    // HP and LP side by side: the toggle, then its cutoff.
    {
        auto row = fromBottom();
        auto hp = row.removeFromLeft((row.getWidth() - kGap) / 2);
        row.removeFromLeft(kGap);
        constexpr int kToggleW = 26;
        hpEnableButton_.setBounds(hp.removeFromLeft(kToggleW));
        hp.removeFromLeft(2);
        hpFreqSlider_.setBounds(hp);
        lpEnableButton_.setBounds(row.removeFromLeft(kToggleW));
        row.removeFromLeft(2);
        lpFreqSlider_.setBounds(row);
    }

    {
        auto row = fromBottom();
        const int third = (row.getWidth() - 2 * kGap) / 3;
        attackSlider_.setBounds(row.removeFromLeft(third));
        row.removeFromLeft(kGap);
        holdSlider_.setBounds(row.removeFromLeft(third));
        row.removeFromLeft(kGap);
        releaseSlider_.setBounds(row);
    }

    gainSlider_.setBounds(fromBottom());

    // The display takes whatever height the rows leave.
    followerDisplay_.setBounds(area);
}

}  // namespace magda::daw::ui
