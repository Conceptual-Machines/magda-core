#include "modulation/FollowerEditorPanel.hpp"

#include "audio/modifiers/ADSRDebugLog.hpp"
#include "core/AutomationInfo.hpp"
#include "ui/themes/DarkTheme.hpp"
#include "ui/themes/FontManager.hpp"
#include "ui/themes/SmallButtonLookAndFeel.hpp"

namespace magda::daw::ui {

FollowerEditorPanel::FollowerEditorPanel() {
    setInterceptsMouseClicks(true, true);

    // Name label at top (editable)
    nameLabel_.setFont(FontManager::getInstance().getUIFontBold(10.0f));
    nameLabel_.setColour(juce::Label::textColourId, DarkTheme::getTextColour());
    nameLabel_.setJustificationType(juce::Justification::centred);
    nameLabel_.setText("No Mod Selected", juce::dontSendNotification);
    nameLabel_.setEditable(false, true, false);
    nameLabel_.onTextChange = [this]() { onNameLabelEdited(); };
    addAndMakeVisible(nameLabel_);

    // Live output display (reuses the generic value-history scroller)
    addAndMakeVisible(followerDisplay_);

    // Audio source button (Self / external track) — opens the host picker.
    // Use the shared small-button look (theme font, tighter corners) to match
    // the rest of the modulation toolbar.
    sourceButton_.setLookAndFeel(&SmallButtonLookAndFeel::getInstance());
    sourceButton_.setColour(juce::TextButton::buttonColourId,
                            DarkTheme::getColour(DarkTheme::SURFACE));
    sourceButton_.setColour(juce::TextButton::textColourOffId, DarkTheme::getSecondaryTextColour());
    sourceButton_.onClick = [this]() {
        if (onSourceClicked)
            onSourceClicked();
    };
    addAndMakeVisible(sourceButton_);

    // Time sliders fold their label into the value text (e.g. "A 100 ms").
    auto setupTimeSlider = [this](TextSlider& s, const juce::String& tag, double def,
                                  std::function<float&()> field) {
        s.setRange(0.0, 5000.0, 1.0);
        s.setSkewForCentre(250.0);
        s.setValue(def, juce::dontSendNotification);
        s.setFont(FontManager::getInstance().getUIFont(9.0f));
        s.setShowFillIndicator(false);
        s.setValueFormatter(
            [tag](double v) { return tag + " " + juce::String(juce::roundToInt(v)) + " ms"; });
        s.onValueChanged = [this, field](double value) {
            field() = static_cast<float>(value);
            MAGDA_ADSR_AUDIO_LOG("follower-ui time-change modIndex="
                                 << selectedModIndex_ << " gainDb=" << currentMod_.followerGainDb
                                 << " attack=" << currentMod_.followerAttackMs
                                 << " hold=" << currentMod_.followerHoldMs
                                 << " release=" << currentMod_.followerReleaseMs);
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
    gainSlider_.setFont(FontManager::getInstance().getUIFont(9.0f));
    gainSlider_.setShowFillIndicator(false);
    gainSlider_.setValueFormatter([](double v) { return "Gain " + juce::String(v, 1) + " dB"; });
    gainSlider_.onValueChanged = [this](double value) {
        currentMod_.followerGainDb = static_cast<float>(value);
        MAGDA_ADSR_AUDIO_LOG("follower-ui gain-change modIndex=" << selectedModIndex_ << " gainDb="
                                                                 << currentMod_.followerGainDb);
        fireFollowerChanged();
    };
    addAndMakeVisible(gainSlider_);

    // Band-limit detection: a toggle + cutoff slider per band. Filtering the raw
    // source before peak detection lets the follower track just the bass or just
    // the highs of its source.
    auto setupBandToggle = [this](juce::TextButton& b, std::function<bool&()> field,
                                  TextSlider& freq) {
        b.setClickingTogglesState(true);
        b.setLookAndFeel(&SmallButtonLookAndFeel::getInstance());
        b.setColour(juce::TextButton::buttonColourId, DarkTheme::getColour(DarkTheme::SURFACE));
        b.setColour(juce::TextButton::buttonOnColourId,
                    DarkTheme::getColour(DarkTheme::ACCENT_GREEN));
        b.setColour(juce::TextButton::textColourOffId, DarkTheme::getSecondaryTextColour());
        b.setColour(juce::TextButton::textColourOnId, DarkTheme::getTextColour());
        b.onClick = [this, &b, field, &freq]() {
            field() = b.getToggleState();
            freq.setEnabled(b.getToggleState());
            MAGDA_ADSR_AUDIO_LOG("follower-ui band-toggle modIndex="
                                 << selectedModIndex_
                                 << " hpOn=" << static_cast<int>(currentMod_.followerHpEnabled)
                                 << " hpHz=" << currentMod_.followerHpFreq
                                 << " lpOn=" << static_cast<int>(currentMod_.followerLpEnabled)
                                 << " lpHz=" << currentMod_.followerLpFreq);
            fireFollowerChanged();
        };
        addAndMakeVisible(b);
    };
    auto setupFreqSlider = [this](TextSlider& s, double def, std::function<float&()> field) {
        s.setRange(20.0, 20000.0, 1.0);
        s.setSkewForCentre(1000.0);
        s.setValue(def, juce::dontSendNotification);
        s.setFont(FontManager::getInstance().getUIFont(9.0f));
        s.setShowFillIndicator(false);
        s.setValueFormatter([](double v) { return juce::String(juce::roundToInt(v)) + " Hz"; });
        s.onValueChanged = [this, field](double value) {
            field() = static_cast<float>(value);
            MAGDA_ADSR_AUDIO_LOG("follower-ui freq-change modIndex="
                                 << selectedModIndex_
                                 << " hpOn=" << static_cast<int>(currentMod_.followerHpEnabled)
                                 << " hpHz=" << currentMod_.followerHpFreq
                                 << " lpOn=" << static_cast<int>(currentMod_.followerLpEnabled)
                                 << " lpHz=" << currentMod_.followerLpFreq);
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

    // Mod matrix (links)
    modMatrixViewport_.setViewedComponent(&modMatrixContent_, false);
    modMatrixViewport_.setScrollBarsShown(true, false);
    addAndMakeVisible(modMatrixViewport_);

    modMatrixContent_.onDeleteLink = [this](magda::ControlTarget target) {
        if (selectedModIndex_ >= 0 && onModLinkDeleted)
            onModLinkDeleted(selectedModIndex_, target);
    };
    modMatrixContent_.onToggleBipolar = [this](magda::ControlTarget target, bool bipolar) {
        if (selectedModIndex_ >= 0 && onModLinkBipolarChanged)
            onModLinkBipolarChanged(selectedModIndex_, target, bipolar);
    };
    modMatrixContent_.onToggleEnabled = [this](magda::ControlTarget target, bool enabled) {
        if (selectedModIndex_ >= 0 && onModLinkEnabledChanged)
            onModLinkEnabledChanged(selectedModIndex_, target, enabled);
    };
    modMatrixContent_.onAmountChanged = [this](magda::ControlTarget target, float amount) {
        if (selectedModIndex_ >= 0 && onModLinkAmountChanged)
            onModLinkAmountChanged(selectedModIndex_, target, amount);
    };
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
    nameLabel_.setText(currentMod_.name, juce::dontSendNotification);
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
    updateModMatrix();
    resized();
}

void FollowerEditorPanel::updateModMatrix() {
    std::vector<ModMatrixContent::LinkRow> rows;
    for (const auto& link : currentMod_.links) {
        if (!link.isValid())
            continue;

        ModMatrixContent::LinkRow row;
        row.target = link.target;
        row.amount = link.amount;
        row.bipolar = link.bipolar;
        row.enabled = link.enabled;

        if (link.target.kind == magda::ControlTarget::Kind::ModParam)
            row.paramName = magda::getDisplayNameForTarget(link.target);
        else if (paramNameResolver_)
            row.paramName = paramNameResolver_(link.target.deviceId(), link.target.paramIndex);
        else
            row.paramName = "Unresolved parameter";

        rows.push_back(row);
    }
    modMatrixContent_.setLinks(rows);
}

void FollowerEditorPanel::onNameLabelEdited() {
    if (selectedModIndex_ < 0)
        return;

    auto newName = nameLabel_.getText().trim();
    if (newName.isEmpty()) {
        newName = magda::ModInfo::getDefaultName(selectedModIndex_, currentMod_.type);
        nameLabel_.setText(newName, juce::dontSendNotification);
    }
    if (newName != currentMod_.name) {
        currentMod_.name = newName;
        if (onNameChanged)
            onNameChanged(newName);
    }
}

void FollowerEditorPanel::fireFollowerChanged() {
    if (selectedModIndex_ >= 0 && onFollowerChanged) {
        MAGDA_ADSR_AUDIO_LOG("follower-ui fire-change modIndex="
                             << selectedModIndex_ << " gainDb=" << currentMod_.followerGainDb
                             << " hpOn=" << static_cast<int>(currentMod_.followerHpEnabled)
                             << " hpHz=" << currentMod_.followerHpFreq
                             << " lpOn=" << static_cast<int>(currentMod_.followerLpEnabled)
                             << " lpHz=" << currentMod_.followerLpFreq);
        onFollowerChanged(currentMod_);
    } else {
        MAGDA_ADSR_AUDIO_LOG("follower-ui drop-change modIndex="
                             << selectedModIndex_
                             << " hasCallback=" << static_cast<int>(onFollowerChanged != nullptr));
    }
    followerDisplay_.repaint();
}

void FollowerEditorPanel::paint(juce::Graphics& g) {
    g.setColour(DarkTheme::getColour(DarkTheme::BACKGROUND).brighter(0.03f));
    g.fillRect(getLocalBounds());
    g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
    g.drawRect(getLocalBounds());

    // Caption flow mirrors resized().
    auto bounds = getLocalBounds().reduced(6);
    g.setColour(DarkTheme::getSecondaryTextColour());
    g.setFont(FontManager::getInstance().getUIFont(8.0f));
    bounds.removeFromTop(18 + 6);  // name + gap
    bounds.removeFromTop(46 + 6);  // display + gap
    bounds.removeFromTop(18 + 4);  // gain row + gap
    bounds.removeFromTop(18 + 4);  // attack/hold row + gap
    bounds.removeFromTop(18 + 4);  // release row + gap
    bounds.removeFromTop(18 + 4);  // HP row + gap
    bounds.removeFromTop(18 + 6);  // LP row + gap
    bounds.removeFromTop(18 + 8);  // source button + gap
    g.drawText("Links", bounds.removeFromTop(12), juce::Justification::centredLeft);
}

void FollowerEditorPanel::resized() {
    constexpr int kGap = 4;
    auto bounds = getLocalBounds().reduced(6);

    nameLabel_.setBounds(bounds.removeFromTop(18));
    bounds.removeFromTop(6);

    followerDisplay_.setBounds(bounds.removeFromTop(46));
    bounds.removeFromTop(6);

    gainSlider_.setBounds(bounds.removeFromTop(18));
    bounds.removeFromTop(kGap);

    // Attack | Hold two-column row.
    {
        auto row = bounds.removeFromTop(18);
        const int half = (row.getWidth() - kGap) / 2;
        attackSlider_.setBounds(row.removeFromLeft(half));
        row.removeFromLeft(kGap);
        holdSlider_.setBounds(row);
    }
    bounds.removeFromTop(kGap);

    releaseSlider_.setBounds(bounds.removeFromTop(18));
    bounds.removeFromTop(kGap);

    // HP / LP band-limit rows: toggle on the left, cutoff slider filling the rest.
    constexpr int kToggleW = 30;
    {
        auto row = bounds.removeFromTop(18);
        hpEnableButton_.setBounds(row.removeFromLeft(kToggleW));
        row.removeFromLeft(kGap);
        hpFreqSlider_.setBounds(row);
    }
    bounds.removeFromTop(kGap);
    {
        auto row = bounds.removeFromTop(18);
        lpEnableButton_.setBounds(row.removeFromLeft(kToggleW));
        row.removeFromLeft(kGap);
        lpFreqSlider_.setBounds(row);
    }
    bounds.removeFromTop(6);

    sourceButton_.setBounds(bounds.removeFromTop(18));
    bounds.removeFromTop(8);

    bounds.removeFromTop(12);  // "Links" label (painted)
    if (bounds.getHeight() > 0) {
        modMatrixViewport_.setBounds(bounds);
        modMatrixContent_.setSize(
            bounds.getWidth() - (modMatrixViewport_.isVerticalScrollBarShown() ? 8 : 0),
            juce::jmax(bounds.getHeight(),
                       static_cast<int>(currentMod_.links.size()) * ModMatrixContent::ROW_HEIGHT));
    }
}

}  // namespace magda::daw::ui
