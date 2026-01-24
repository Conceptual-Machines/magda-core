#include "LFOCurveEditorWindow.hpp"

#include "ui/themes/DarkTheme.hpp"
#include "ui/themes/FontManager.hpp"
#include "ui/themes/SmallButtonLookAndFeel.hpp"
#include "ui/themes/SmallComboBoxLookAndFeel.hpp"

namespace magda::daw::ui {

// ============================================================================
// LFOCurveEditorContent
// ============================================================================

LFOCurveEditorContent::LFOCurveEditorContent(magda::ModInfo* modInfo,
                                             std::function<void()> onWaveformChanged,
                                             std::function<void()> onDragPreview)
    : modInfo_(modInfo) {
    // Configure the curve editor
    curveEditor_.setModInfo(modInfo);
    curveEditor_.setCurveColour(DarkTheme::getColour(DarkTheme::ACCENT_ORANGE));
    curveEditor_.onWaveformChanged = std::move(onWaveformChanged);
    curveEditor_.onDragPreview = std::move(onDragPreview);
    addAndMakeVisible(curveEditor_);

    setupControls();
    updateControlsFromModInfo();
}

void LFOCurveEditorContent::setupControls() {
    // Sync toggle button
    syncToggle_.setButtonText("Free");
    syncToggle_.setColour(juce::TextButton::buttonColourId,
                          DarkTheme::getColour(DarkTheme::SURFACE));
    syncToggle_.setColour(juce::TextButton::buttonOnColourId,
                          DarkTheme::getColour(DarkTheme::ACCENT_ORANGE));
    syncToggle_.setColour(juce::TextButton::textColourOffId, DarkTheme::getSecondaryTextColour());
    syncToggle_.setColour(juce::TextButton::textColourOnId,
                          DarkTheme::getColour(DarkTheme::BACKGROUND));
    syncToggle_.setClickingTogglesState(true);
    syncToggle_.setLookAndFeel(&SmallButtonLookAndFeel::getInstance());
    syncToggle_.onClick = [this]() {
        bool synced = syncToggle_.getToggleState();
        syncToggle_.setButtonText(synced ? "Sync" : "Free");
        rateSlider_.setVisible(!synced);
        syncDivisionCombo_.setVisible(synced);
        if (modInfo_) {
            modInfo_->tempoSync = synced;
        }
        if (onTempoSyncChanged) {
            onTempoSyncChanged(synced);
        }
    };
    addAndMakeVisible(syncToggle_);

    // Rate slider (Hz)
    rateSlider_.setRange(0.01, 20.0, 0.01);
    rateSlider_.setValue(1.0, juce::dontSendNotification);
    rateSlider_.setFont(FontManager::getInstance().getUIFont(9.0f));
    rateSlider_.onValueChanged = [this](double value) {
        if (modInfo_) {
            modInfo_->rate = static_cast<float>(value);
        }
        if (onRateChanged) {
            onRateChanged(static_cast<float>(value));
        }
    };
    addAndMakeVisible(rateSlider_);

    // Sync division combo
    syncDivisionCombo_.addItem("1 Bar", static_cast<int>(magda::SyncDivision::Whole) + 100);
    syncDivisionCombo_.addItem("1/2", static_cast<int>(magda::SyncDivision::Half) + 100);
    syncDivisionCombo_.addItem("1/4", static_cast<int>(magda::SyncDivision::Quarter) + 100);
    syncDivisionCombo_.addItem("1/8", static_cast<int>(magda::SyncDivision::Eighth) + 100);
    syncDivisionCombo_.addItem("1/16", static_cast<int>(magda::SyncDivision::Sixteenth) + 100);
    syncDivisionCombo_.addItem("1/32", static_cast<int>(magda::SyncDivision::ThirtySecond) + 100);
    syncDivisionCombo_.setSelectedId(static_cast<int>(magda::SyncDivision::Quarter) + 100,
                                     juce::dontSendNotification);
    syncDivisionCombo_.setColour(juce::ComboBox::backgroundColourId,
                                 DarkTheme::getColour(DarkTheme::SURFACE));
    syncDivisionCombo_.setColour(juce::ComboBox::textColourId, DarkTheme::getTextColour());
    syncDivisionCombo_.setColour(juce::ComboBox::outlineColourId,
                                 DarkTheme::getColour(DarkTheme::BORDER));
    syncDivisionCombo_.setLookAndFeel(&SmallComboBoxLookAndFeel::getInstance());
    syncDivisionCombo_.onChange = [this]() {
        int id = syncDivisionCombo_.getSelectedId();
        if (id >= 100) {
            auto division = static_cast<magda::SyncDivision>(id - 100);
            if (modInfo_) {
                modInfo_->syncDivision = division;
            }
            if (onSyncDivisionChanged) {
                onSyncDivisionChanged(division);
            }
        }
    };
    addChildComponent(syncDivisionCombo_);

    // Loop/One-shot toggle
    loopOneShotToggle_.setButtonText("Loop");
    loopOneShotToggle_.setColour(juce::TextButton::buttonColourId,
                                 DarkTheme::getColour(DarkTheme::SURFACE));
    loopOneShotToggle_.setColour(juce::TextButton::buttonOnColourId,
                                 DarkTheme::getColour(DarkTheme::ACCENT_ORANGE));
    loopOneShotToggle_.setColour(juce::TextButton::textColourOffId,
                                 DarkTheme::getSecondaryTextColour());
    loopOneShotToggle_.setColour(juce::TextButton::textColourOnId,
                                 DarkTheme::getColour(DarkTheme::BACKGROUND));
    loopOneShotToggle_.setClickingTogglesState(true);
    loopOneShotToggle_.setLookAndFeel(&SmallButtonLookAndFeel::getInstance());
    loopOneShotToggle_.onClick = [this]() {
        bool oneShot = loopOneShotToggle_.getToggleState();
        loopOneShotToggle_.setButtonText(oneShot ? "1-Shot" : "Loop");
        if (modInfo_) {
            modInfo_->oneShot = oneShot;
        }
        if (onOneShotChanged) {
            onOneShotChanged(oneShot);
        }
    };
    addAndMakeVisible(loopOneShotToggle_);

    // MSEG toggle (loop region)
    msegToggle_.setButtonText("MSEG");
    msegToggle_.setColour(juce::TextButton::buttonColourId,
                          DarkTheme::getColour(DarkTheme::SURFACE));
    msegToggle_.setColour(juce::TextButton::buttonOnColourId,
                          DarkTheme::getColour(DarkTheme::ACCENT_ORANGE));
    msegToggle_.setColour(juce::TextButton::textColourOffId, DarkTheme::getSecondaryTextColour());
    msegToggle_.setColour(juce::TextButton::textColourOnId,
                          DarkTheme::getColour(DarkTheme::BACKGROUND));
    msegToggle_.setClickingTogglesState(true);
    msegToggle_.setLookAndFeel(&SmallButtonLookAndFeel::getInstance());
    msegToggle_.onClick = [this]() {
        bool useLoop = msegToggle_.getToggleState();
        curveEditor_.setShowLoopRegion(useLoop);
        if (modInfo_) {
            modInfo_->useLoopRegion = useLoop;
        }
        if (onLoopRegionChanged) {
            onLoopRegionChanged(useLoop);
        }
    };
    addAndMakeVisible(msegToggle_);

    // Preset selector
    presetCombo_.addItem("Triangle", static_cast<int>(magda::CurvePreset::Triangle) + 1);
    presetCombo_.addItem("Sine", static_cast<int>(magda::CurvePreset::Sine) + 1);
    presetCombo_.addItem("Ramp Up", static_cast<int>(magda::CurvePreset::RampUp) + 1);
    presetCombo_.addItem("Ramp Down", static_cast<int>(magda::CurvePreset::RampDown) + 1);
    presetCombo_.addItem("S-Curve", static_cast<int>(magda::CurvePreset::SCurve) + 1);
    presetCombo_.addItem("Exp", static_cast<int>(magda::CurvePreset::Exponential) + 1);
    presetCombo_.addItem("Log", static_cast<int>(magda::CurvePreset::Logarithmic) + 1);
    presetCombo_.setTextWhenNothingSelected("Preset");
    presetCombo_.setColour(juce::ComboBox::backgroundColourId,
                           DarkTheme::getColour(DarkTheme::SURFACE));
    presetCombo_.setColour(juce::ComboBox::textColourId, DarkTheme::getTextColour());
    presetCombo_.setColour(juce::ComboBox::outlineColourId,
                           DarkTheme::getColour(DarkTheme::BORDER));
    presetCombo_.setLookAndFeel(&SmallComboBoxLookAndFeel::getInstance());
    presetCombo_.onChange = [this]() {
        int id = presetCombo_.getSelectedId();
        if (id > 0) {
            auto preset = static_cast<magda::CurvePreset>(id - 1);
            curveEditor_.loadPreset(preset);
        }
    };
    addAndMakeVisible(presetCombo_);

    // Grid label
    gridLabel_.setText("Grid:", juce::dontSendNotification);
    gridLabel_.setFont(FontManager::getInstance().getUIFont(9.0f));
    gridLabel_.setColour(juce::Label::textColourId, DarkTheme::getSecondaryTextColour());
    addAndMakeVisible(gridLabel_);

    // Grid X divisions
    gridXCombo_.addItem("2", 2);
    gridXCombo_.addItem("4", 4);
    gridXCombo_.addItem("8", 8);
    gridXCombo_.addItem("16", 16);
    gridXCombo_.addItem("32", 32);
    gridXCombo_.setSelectedId(4, juce::dontSendNotification);
    gridXCombo_.setColour(juce::ComboBox::backgroundColourId,
                          DarkTheme::getColour(DarkTheme::SURFACE));
    gridXCombo_.setColour(juce::ComboBox::textColourId, DarkTheme::getTextColour());
    gridXCombo_.setColour(juce::ComboBox::outlineColourId, DarkTheme::getColour(DarkTheme::BORDER));
    gridXCombo_.setLookAndFeel(&SmallComboBoxLookAndFeel::getInstance());
    gridXCombo_.onChange = [this]() {
        curveEditor_.setGridDivisionsX(gridXCombo_.getSelectedId());
    };
    addAndMakeVisible(gridXCombo_);

    // Grid Y divisions
    gridYCombo_.addItem("2", 2);
    gridYCombo_.addItem("4", 4);
    gridYCombo_.addItem("8", 8);
    gridYCombo_.addItem("16", 16);
    gridYCombo_.setSelectedId(4, juce::dontSendNotification);
    gridYCombo_.setColour(juce::ComboBox::backgroundColourId,
                          DarkTheme::getColour(DarkTheme::SURFACE));
    gridYCombo_.setColour(juce::ComboBox::textColourId, DarkTheme::getTextColour());
    gridYCombo_.setColour(juce::ComboBox::outlineColourId, DarkTheme::getColour(DarkTheme::BORDER));
    gridYCombo_.setLookAndFeel(&SmallComboBoxLookAndFeel::getInstance());
    gridYCombo_.onChange = [this]() {
        curveEditor_.setGridDivisionsY(gridYCombo_.getSelectedId());
    };
    addAndMakeVisible(gridYCombo_);

    // Snap X toggle
    snapXToggle_.setButtonText("X");
    snapXToggle_.setColour(juce::TextButton::buttonColourId,
                           DarkTheme::getColour(DarkTheme::SURFACE));
    snapXToggle_.setColour(juce::TextButton::buttonOnColourId,
                           DarkTheme::getColour(DarkTheme::ACCENT_ORANGE));
    snapXToggle_.setColour(juce::TextButton::textColourOffId, DarkTheme::getSecondaryTextColour());
    snapXToggle_.setColour(juce::TextButton::textColourOnId,
                           DarkTheme::getColour(DarkTheme::BACKGROUND));
    snapXToggle_.setClickingTogglesState(true);
    snapXToggle_.setLookAndFeel(&SmallButtonLookAndFeel::getInstance());
    snapXToggle_.onClick = [this]() { curveEditor_.setSnapX(snapXToggle_.getToggleState()); };
    addAndMakeVisible(snapXToggle_);

    // Snap Y toggle
    snapYToggle_.setButtonText("Y");
    snapYToggle_.setColour(juce::TextButton::buttonColourId,
                           DarkTheme::getColour(DarkTheme::SURFACE));
    snapYToggle_.setColour(juce::TextButton::buttonOnColourId,
                           DarkTheme::getColour(DarkTheme::ACCENT_ORANGE));
    snapYToggle_.setColour(juce::TextButton::textColourOffId, DarkTheme::getSecondaryTextColour());
    snapYToggle_.setColour(juce::TextButton::textColourOnId,
                           DarkTheme::getColour(DarkTheme::BACKGROUND));
    snapYToggle_.setClickingTogglesState(true);
    snapYToggle_.setLookAndFeel(&SmallButtonLookAndFeel::getInstance());
    snapYToggle_.onClick = [this]() { curveEditor_.setSnapY(snapYToggle_.getToggleState()); };
    addAndMakeVisible(snapYToggle_);
}

void LFOCurveEditorContent::updateControlsFromModInfo() {
    if (!modInfo_)
        return;

    // Sync settings
    syncToggle_.setToggleState(modInfo_->tempoSync, juce::dontSendNotification);
    syncToggle_.setButtonText(modInfo_->tempoSync ? "Sync" : "Free");
    rateSlider_.setValue(modInfo_->rate, juce::dontSendNotification);
    rateSlider_.setVisible(!modInfo_->tempoSync);
    syncDivisionCombo_.setSelectedId(static_cast<int>(modInfo_->syncDivision) + 100,
                                     juce::dontSendNotification);
    syncDivisionCombo_.setVisible(modInfo_->tempoSync);

    // Loop/one-shot
    loopOneShotToggle_.setToggleState(modInfo_->oneShot, juce::dontSendNotification);
    loopOneShotToggle_.setButtonText(modInfo_->oneShot ? "1-Shot" : "Loop");

    // MSEG
    msegToggle_.setToggleState(modInfo_->useLoopRegion, juce::dontSendNotification);
    curveEditor_.setShowLoopRegion(modInfo_->useLoopRegion);
}

void LFOCurveEditorContent::paint(juce::Graphics& g) {
    // Footer background
    auto footerBounds = getLocalBounds().removeFromBottom(FOOTER_HEIGHT);
    g.setColour(DarkTheme::getColour(DarkTheme::SURFACE));
    g.fillRect(footerBounds);

    // Footer top border
    g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
    g.drawHorizontalLine(getHeight() - FOOTER_HEIGHT, 0.0f, static_cast<float>(getWidth()));
}

void LFOCurveEditorContent::resized() {
    auto bounds = getLocalBounds();

    // Footer at bottom
    auto footer = bounds.removeFromBottom(FOOTER_HEIGHT);
    footer.reduce(6, 4);

    constexpr int gap = 6;

    // Rate section: [Sync][Rate/Division]
    constexpr int syncWidth = 38;
    constexpr int rateWidth = 60;

    syncToggle_.setBounds(footer.removeFromLeft(syncWidth));
    footer.removeFromLeft(gap);
    auto rateBounds = footer.removeFromLeft(rateWidth);
    rateSlider_.setBounds(rateBounds);
    syncDivisionCombo_.setBounds(rateBounds);
    footer.removeFromLeft(gap * 2);

    // Mode section: [Loop/1-Shot][MSEG]
    constexpr int modeWidth = 46;
    loopOneShotToggle_.setBounds(footer.removeFromLeft(modeWidth));
    footer.removeFromLeft(gap);
    msegToggle_.setBounds(footer.removeFromLeft(modeWidth));
    footer.removeFromLeft(gap * 2);

    // Preset selector
    constexpr int presetWidth = 70;
    presetCombo_.setBounds(footer.removeFromLeft(presetWidth));
    footer.removeFromLeft(gap * 2);

    // Grid section: [Grid:][X combo][Y combo][Snap X][Snap Y]
    constexpr int labelWidth = 30;
    constexpr int comboWidth = 38;
    constexpr int snapWidth = 22;

    gridLabel_.setBounds(footer.removeFromLeft(labelWidth));
    gridXCombo_.setBounds(footer.removeFromLeft(comboWidth));
    footer.removeFromLeft(4);
    gridYCombo_.setBounds(footer.removeFromLeft(comboWidth));
    footer.removeFromLeft(gap);
    snapXToggle_.setBounds(footer.removeFromLeft(snapWidth));
    footer.removeFromLeft(4);
    snapYToggle_.setBounds(footer.removeFromLeft(snapWidth));

    // Curve editor takes remaining space (above footer)
    curveEditor_.setBounds(bounds.expanded(curveEditor_.getPadding()));
}

// ============================================================================
// LFOCurveEditorWindow
// ============================================================================

LFOCurveEditorWindow::LFOCurveEditorWindow(magda::ModInfo* modInfo,
                                           std::function<void()> onWaveformChanged,
                                           std::function<void()> onDragPreview)
    : DocumentWindow("LFO Curve Editor", DarkTheme::getColour(DarkTheme::BACKGROUND),
                     DocumentWindow::closeButton),
      content_(modInfo, std::move(onWaveformChanged), std::move(onDragPreview)) {
    // Wire up callbacks
    content_.onRateChanged = [this](float rate) {
        if (onRateChanged)
            onRateChanged(rate);
    };
    content_.onTempoSyncChanged = [this](bool synced) {
        if (onTempoSyncChanged)
            onTempoSyncChanged(synced);
    };
    content_.onSyncDivisionChanged = [this](magda::SyncDivision div) {
        if (onSyncDivisionChanged)
            onSyncDivisionChanged(div);
    };
    content_.onOneShotChanged = [this](bool oneShot) {
        if (onOneShotChanged)
            onOneShotChanged(oneShot);
    };
    content_.onLoopRegionChanged = [this](bool useLoop) {
        if (onLoopRegionChanged)
            onLoopRegionChanged(useLoop);
    };

    setContentNonOwned(&content_, true);

    // Window settings
    setSize(500, 300);
    setResizable(true, true);
    setResizeLimits(400, 200, 1000, 600);
    setUsingNativeTitleBar(false);
    setVisible(true);
    setAlwaysOnTop(true);

    centreWithSize(getWidth(), getHeight());
}

void LFOCurveEditorWindow::closeButtonPressed() {
    setVisible(false);
    if (onWindowClosed) {
        onWindowClosed();
    }
}

}  // namespace magda::daw::ui
