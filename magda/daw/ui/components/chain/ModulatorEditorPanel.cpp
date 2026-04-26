#include "ModulatorEditorPanel.hpp"

#include "BinaryData.h"
#include "core/AutomationInfo.hpp"
#include "core/AutomationManager.hpp"
#include "ui/themes/DarkTheme.hpp"

namespace {
// Stepped slider order — slow to fast. Multi-bar lengths first, then 1 Bar,
// then for each note (Half, Quarter, Eighth, Sixteenth, ThirtySecond):
// dotted → normal → triplet. Matches the labels used in valueFormatter.
constexpr magda::SyncDivision kSyncDivisionOrder[] = {
    magda::SyncDivision::SixteenBars,         // 16 Bars
    magda::SyncDivision::EightBars,           // 8 Bars
    magda::SyncDivision::FourBars,            // 4 Bars
    magda::SyncDivision::TwoBars,             // 2 Bars
    magda::SyncDivision::Whole,               // 1 Bar
    magda::SyncDivision::DottedHalf,          // 1/2.
    magda::SyncDivision::Half,                // 1/2
    magda::SyncDivision::TripletHalf,         // 1/2T
    magda::SyncDivision::DottedQuarter,       // 1/4.
    magda::SyncDivision::Quarter,             // 1/4
    magda::SyncDivision::TripletQuarter,      // 1/4T
    magda::SyncDivision::DottedEighth,        // 1/8.
    magda::SyncDivision::Eighth,              // 1/8
    magda::SyncDivision::TripletEighth,       // 1/8T
    magda::SyncDivision::DottedSixteenth,     // 1/16.
    magda::SyncDivision::Sixteenth,           // 1/16
    magda::SyncDivision::TripletSixteenth,    // 1/16T
    magda::SyncDivision::DottedThirtySecond,  // 1/32.
    magda::SyncDivision::ThirtySecond,        // 1/32
    magda::SyncDivision::TripletThirtySecond  // 1/32T
};

int syncDivisionToIndex(magda::SyncDivision d) {
    for (int i = 0; i < static_cast<int>(std::size(kSyncDivisionOrder)); ++i)
        if (kSyncDivisionOrder[i] == d)
            return i;
    // Quarter — find its position dynamically so this stays correct if the
    // order array is reshuffled.
    for (int i = 0; i < static_cast<int>(std::size(kSyncDivisionOrder)); ++i)
        if (kSyncDivisionOrder[i] == magda::SyncDivision::Quarter)
            return i;
    return 0;
}

magda::SyncDivision indexToSyncDivision(int idx) {
    if (idx < 0 || idx >= static_cast<int>(std::size(kSyncDivisionOrder)))
        return magda::SyncDivision::Quarter;
    return kSyncDivisionOrder[idx];
}
}  // namespace
#include "ui/themes/FontManager.hpp"
#include "ui/themes/SmallButtonLookAndFeel.hpp"
#include "ui/themes/SmallComboBoxLookAndFeel.hpp"

namespace magda::daw::ui {

// ============================================================================
// ModMatrixContent
// ============================================================================

void ModMatrixContent::setLinks(const std::vector<LinkRow>& links) {
    links_ = links;
    setSize(getWidth(), juce::jmax(1, static_cast<int>(links_.size())) * ROW_HEIGHT);
    repaint();
}

bool ModMatrixContent::updateLinkAmount(magda::ModTarget target, float amount, bool bipolar) {
    for (auto& link : links_) {
        if (link.target == target) {
            bool changed = (link.amount != amount || link.bipolar != bipolar);
            link.amount = amount;
            link.bipolar = bipolar;
            return changed;
        }
    }
    return false;
}

void ModMatrixContent::paint(juce::Graphics& g) {
    auto font = FontManager::getInstance().getUIFont(8.0f);
    g.setFont(font);

    for (int i = 0; i < static_cast<int>(links_.size()); ++i) {
        const auto& link = links_[static_cast<size_t>(i)];
        int y = i * ROW_HEIGHT;
        auto rowBounds = juce::Rectangle<int>(0, y, getWidth(), ROW_HEIGHT);

        // Alternating row background
        if (i % 2 == 0) {
            g.setColour(DarkTheme::getColour(DarkTheme::SURFACE).withAlpha(0.3f));
            g.fillRect(rowBounds);
        }

        auto remaining = rowBounds.reduced(2, 0);

        // Delete button (X) on right - 14px
        auto deleteBounds = remaining.removeFromRight(14);
        g.setColour(DarkTheme::getSecondaryTextColour());
        g.drawText("x", deleteBounds, juce::Justification::centred);
        remaining.removeFromRight(2);

        // Bipolar toggle - 16px
        auto bipolarBounds = remaining.removeFromRight(16);
        g.setColour(link.bipolar ? DarkTheme::getColour(DarkTheme::ACCENT_ORANGE)
                                 : DarkTheme::getSecondaryTextColour());
        g.drawText(link.bipolar ? "Bi" : "Un", bipolarBounds, juce::Justification::centred);
        remaining.removeFromRight(2);

        // Amount - 28px
        auto amountBounds = remaining.removeFromRight(28);
        int percent = static_cast<int>(link.amount * 100);
        g.setColour(DarkTheme::getTextColour());
        g.drawText(juce::String(percent) + "%", amountBounds, juce::Justification::centredRight);
        remaining.removeFromRight(2);

        // Param name takes remaining space
        g.setColour(DarkTheme::getTextColour());
        g.drawText(link.paramName, remaining, juce::Justification::centredLeft, true);
    }

    if (links_.empty()) {
        g.setColour(DarkTheme::getSecondaryTextColour());
        g.drawText("No links", getLocalBounds(), juce::Justification::centred);
    }
}

void ModMatrixContent::mouseDown(const juce::MouseEvent& e) {
    int rowIndex = e.getPosition().y / ROW_HEIGHT;
    if (rowIndex < 0 || rowIndex >= static_cast<int>(links_.size()))
        return;

    int x = e.getPosition().x;
    int width = getWidth();

    // Delete button zone: rightmost 14px + 2px padding
    if (x >= width - 16) {
        if (onDeleteLink)
            onDeleteLink(links_[static_cast<size_t>(rowIndex)].target);
        return;
    }

    // Bipolar toggle zone: next 16px + 2px padding
    if (x >= width - 36 && x < width - 18) {
        if (onToggleBipolar) {
            auto& link = links_[static_cast<size_t>(rowIndex)];
            onToggleBipolar(link.target, !link.bipolar);
        }
        return;
    }

    // Amount drag — anywhere else in the row
    draggingRow_ = rowIndex;
    dragStartAmount_ = links_[static_cast<size_t>(rowIndex)].amount;
    dragStartX_ = e.getPosition().x;
}

void ModMatrixContent::mouseDrag(const juce::MouseEvent& e) {
    if (draggingRow_ < 0 || draggingRow_ >= static_cast<int>(links_.size()))
        return;

    float delta = static_cast<float>(e.getPosition().x - dragStartX_) / 100.0f;
    float newAmount = juce::jlimit(-1.0f, 1.0f, dragStartAmount_ + delta);
    links_[static_cast<size_t>(draggingRow_)].amount = newAmount;
    repaint();

    if (onAmountChanged)
        onAmountChanged(links_[static_cast<size_t>(draggingRow_)].target, newAmount);
}

void ModMatrixContent::mouseUp(const juce::MouseEvent&) {
    draggingRow_ = -1;
}

// ============================================================================
// ModulatorEditorPanel
// ============================================================================

ModulatorEditorPanel::ModulatorEditorPanel() {
    // Intercept mouse clicks to prevent propagation to parent
    setInterceptsMouseClicks(true, true);

    startTimer(33);  // 30 FPS for waveform animation

    // Name label at top
    nameLabel_.setFont(FontManager::getInstance().getUIFontBold(10.0f));
    nameLabel_.setColour(juce::Label::textColourId, DarkTheme::getTextColour());
    nameLabel_.setJustificationType(juce::Justification::centred);
    nameLabel_.setText("No Mod Selected", juce::dontSendNotification);
    addAndMakeVisible(nameLabel_);

    // Waveform selector (for LFO shapes - hidden when Custom/Curve)
    waveformCombo_.addItem("Sine", static_cast<int>(magda::LFOWaveform::Sine) + 1);
    waveformCombo_.addItem("Triangle", static_cast<int>(magda::LFOWaveform::Triangle) + 1);
    waveformCombo_.addItem("Square", static_cast<int>(magda::LFOWaveform::Square) + 1);
    waveformCombo_.addItem("Saw", static_cast<int>(magda::LFOWaveform::Saw) + 1);
    waveformCombo_.addItem("Reverse Saw", static_cast<int>(magda::LFOWaveform::ReverseSaw) + 1);
    waveformCombo_.setSelectedId(1, juce::dontSendNotification);
    waveformCombo_.setColour(juce::ComboBox::backgroundColourId,
                             DarkTheme::getColour(DarkTheme::SURFACE));
    waveformCombo_.setColour(juce::ComboBox::textColourId, DarkTheme::getTextColour());
    waveformCombo_.setColour(juce::ComboBox::outlineColourId,
                             DarkTheme::getColour(DarkTheme::BORDER));
    waveformCombo_.setJustificationType(juce::Justification::centredLeft);
    waveformCombo_.setLookAndFeel(&SmallComboBoxLookAndFeel::getInstance());
    waveformCombo_.onChange = [this]() {
        int id = waveformCombo_.getSelectedId();
        if (id > 0 && onWaveformChanged) {
            onWaveformChanged(static_cast<magda::LFOWaveform>(id - 1));
        }
    };
    addAndMakeVisible(waveformCombo_);

    // Waveform display (for standard LFO shapes)
    addAndMakeVisible(waveformDisplay_);

    // Curve editor (for curve mode - bezier editing with integrated phase indicator)
    curveEditor_.setVisible(false);
    curveEditor_.setCurveColour(DarkTheme::getColour(DarkTheme::ACCENT_ORANGE));
    curveEditor_.onWaveformChanged = [this]() {
        // Curve points are stored directly in ModInfo by LFOCurveEditor
        // Sync external editor window if open
        if (curveEditorWindow_ && curveEditorWindow_->isVisible()) {
            curveEditorWindow_->getCurveEditor().setModInfo(curveEditor_.getModInfo());
        }
        // Notify parent (NodeComponent) to update MiniWaveformDisplay
        if (onCurveChanged) {
            onCurveChanged();
        }
        repaint();
    };
    curveEditor_.onDragPreview = [this]() {
        // Sync external editor during drag for fluid preview
        if (curveEditorWindow_ && curveEditorWindow_->isVisible()) {
            curveEditorWindow_->getCurveEditor().repaint();
        }
        // Notify parent for fluid MiniWaveformDisplay update
        if (onCurveChanged) {
            onCurveChanged();
        }
        repaint();
    };
    addChildComponent(curveEditor_);

    // Button to open external curve editor window
    curveEditorButton_ = std::make_unique<magda::SvgButton>("Edit Curve", BinaryData::curve_svg,
                                                            BinaryData::curve_svgSize);
    curveEditorButton_->setNormalColor(DarkTheme::getSecondaryTextColour());
    curveEditorButton_->setHoverColor(DarkTheme::getTextColour());
    curveEditorButton_->setActiveColor(DarkTheme::getColour(DarkTheme::BACKGROUND));
    curveEditorButton_->setActiveBackgroundColor(DarkTheme::getColour(DarkTheme::ACCENT_ORANGE));
    curveEditorButton_->onClick = [this]() {
        if (!curveEditorWindow_) {
            auto* modInfo = const_cast<magda::ModInfo*>(liveModPtr_ ? liveModPtr_ : &currentMod_);
            curveEditorWindow_ = std::make_unique<LFOCurveEditorWindow>(
                modInfo,
                [this]() {
                    // Sync embedded editor when external editor changes
                    curveEditor_.setModInfo(curveEditor_.getModInfo());
                    if (onCurveChanged) {
                        onCurveChanged();
                    }
                    repaint();
                },
                [this]() {
                    // Sync embedded editor from ModInfo during external window drag
                    curveEditor_.syncFromModInfo();
                    // Notify parent for fluid MiniWaveformDisplay update during drag
                    if (onCurveChanged) {
                        onCurveChanged();
                    }
                    repaint();
                });

            // Wire up rate/sync callbacks from external editor
            curveEditorWindow_->onRateChanged = [this](float rate) {
                currentMod_.rate = rate;
                rateSlider_.setValue(rate, juce::dontSendNotification);
                if (onRateChanged) {
                    onRateChanged(rate);
                }
            };
            curveEditorWindow_->onTempoSyncChanged =
                [safeThis = juce::Component::SafePointer(this)](bool synced) {
                    if (!safeThis)
                        return;
                    safeThis->currentMod_.tempoSync = synced;
                    safeThis->syncToggle_.setToggleState(synced, juce::dontSendNotification);
                    safeThis->syncToggle_.setButtonText(synced ? "Sync" : "Free");
                    safeThis->rateSlider_.setVisible(!synced);
                    safeThis->syncDivisionSlider_.setVisible(synced);
                    if (safeThis->onTempoSyncChanged) {
                        safeThis->onTempoSyncChanged(synced);
                    }
                    if (safeThis)
                        safeThis->resized();
                };
            curveEditorWindow_->onSyncDivisionChanged = [this](magda::SyncDivision div) {
                currentMod_.syncDivision = div;
                syncDivisionSlider_.setValue(static_cast<double>(syncDivisionToIndex(div)),
                                             juce::dontSendNotification);
                if (onSyncDivisionChanged) {
                    onSyncDivisionChanged(div);
                }
            };
            curveEditorWindow_->onOneShotChanged = [this](bool /*oneShot*/) {
                // oneShot is already written to ModInfo by the toggle.
                // Trigger curve resync so CurveSnapshotHolder picks it up.
                if (onCurveChanged)
                    onCurveChanged();
            };
            curveEditorWindow_->onWindowClosed = [this]() { curveEditorButton_->setActive(false); };

            curveEditorButton_->setActive(true);
        } else if (curveEditorWindow_->isVisible()) {
            curveEditorWindow_->setVisible(false);
            curveEditorButton_->setActive(false);
        } else {
            // Re-sync curve data from ModInfo before showing
            curveEditorWindow_->getCurveEditor().setModInfo(
                const_cast<magda::ModInfo*>(liveModPtr_ ? liveModPtr_ : &currentMod_));
            curveEditorWindow_->setVisible(true);
            curveEditorWindow_->toFront(true);
            curveEditorButton_->setActive(true);
        }
    };
    addChildComponent(curveEditorButton_.get());

    // Curve preset selector (shown in curve mode below the name)
    curvePresetCombo_.addItem("Triangle", static_cast<int>(magda::CurvePreset::Triangle) + 1);
    curvePresetCombo_.addItem("Sine", static_cast<int>(magda::CurvePreset::Sine) + 1);
    curvePresetCombo_.addItem("Ramp Up", static_cast<int>(magda::CurvePreset::RampUp) + 1);
    curvePresetCombo_.addItem("Ramp Down", static_cast<int>(magda::CurvePreset::RampDown) + 1);
    curvePresetCombo_.addItem("S-Curve", static_cast<int>(magda::CurvePreset::SCurve) + 1);
    curvePresetCombo_.addItem("Exp", static_cast<int>(magda::CurvePreset::Exponential) + 1);
    curvePresetCombo_.addItem("Log", static_cast<int>(magda::CurvePreset::Logarithmic) + 1);
    curvePresetCombo_.setTextWhenNothingSelected("Preset");
    curvePresetCombo_.setColour(juce::ComboBox::backgroundColourId,
                                DarkTheme::getColour(DarkTheme::SURFACE));
    curvePresetCombo_.setColour(juce::ComboBox::textColourId, DarkTheme::getTextColour());
    curvePresetCombo_.setColour(juce::ComboBox::outlineColourId,
                                DarkTheme::getColour(DarkTheme::BORDER));
    curvePresetCombo_.setLookAndFeel(&SmallComboBoxLookAndFeel::getInstance());
    curvePresetCombo_.onChange = [this]() {
        int id = curvePresetCombo_.getSelectedId();
        if (id > 0) {
            auto preset = static_cast<magda::CurvePreset>(id - 1);
            curveEditor_.loadPreset(preset);
            // Sync external editor if open
            if (curveEditorWindow_ && curveEditorWindow_->isVisible()) {
                curveEditorWindow_->getCurveEditor().setModInfo(curveEditor_.getModInfo());
            }
            if (onCurveChanged) {
                onCurveChanged();
            }
        }
    };
    addChildComponent(curvePresetCombo_);

    // Save preset button (shown in curve mode next to preset combo)
    savePresetButton_ = std::make_unique<magda::SvgButton>("Save Preset", BinaryData::save_svg,
                                                           BinaryData::save_svgSize);
    savePresetButton_->setNormalColor(DarkTheme::getSecondaryTextColour());
    savePresetButton_->setHoverColor(DarkTheme::getTextColour());
    savePresetButton_->onClick = []() {
        // TODO: Show save preset dialog
    };
    addChildComponent(savePresetButton_.get());

    // Sync toggle button (small square button style)
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
    syncToggle_.onClick = [safeThis = juce::Component::SafePointer(this)]() {
        if (!safeThis)
            return;
        bool synced = safeThis->syncToggle_.getToggleState();
        safeThis->currentMod_.tempoSync = synced;
        // Update button text
        safeThis->syncToggle_.setButtonText(synced ? "Sync" : "Free");
        // Show/hide appropriate control
        safeThis->rateSlider_.setVisible(!synced);
        safeThis->syncDivisionSlider_.setVisible(synced);
        if (safeThis->onTempoSyncChanged) {
            safeThis->onTempoSyncChanged(synced);
        }
        if (safeThis)
            safeThis->resized();  // Re-layout
    };
    addAndMakeVisible(syncToggle_);

    // Sync division stepped slider — N musical divisions, indexed 0..N-1.
    // Stepped (interval=1) so each drag click steps to the next division.
    // valueFormatter renders the index as "1 Bar", "1/4", etc.
    constexpr int kNumDivisions = static_cast<int>(std::size(kSyncDivisionOrder));
    syncDivisionSlider_.setRange(0.0, static_cast<double>(kNumDivisions - 1), 1.0);
    syncDivisionSlider_.setValue(
        static_cast<double>(syncDivisionToIndex(magda::SyncDivision::Quarter)),
        juce::dontSendNotification);
    syncDivisionSlider_.setFont(FontManager::getInstance().getUIFont(9.0f));
    syncDivisionSlider_.setValueFormatter([](double v) {
        int idx = juce::jlimit(0, kNumDivisions - 1, static_cast<int>(std::round(v)));
        // Order matches kSyncDivisionOrder above (slow → fast, grouped per note).
        static const char* const kLabels[] = {"16 Bars", "8 Bars", "4 Bars", "2 Bars", "1 Bar",
                                              "1/2.",    "1/2",    "1/2T",   "1/4.",   "1/4",
                                              "1/4T",    "1/8.",   "1/8",    "1/8T",   "1/16.",
                                              "1/16",    "1/16T",  "1/32.",  "1/32",   "1/32T"};
        return juce::String(kLabels[idx]);
    });
    syncDivisionSlider_.onValueChanged = [this](double value) {
        int idx = juce::jlimit(0, kNumDivisions - 1, static_cast<int>(std::round(value)));
        auto division = indexToSyncDivision(idx);
        currentMod_.syncDivision = division;
        if (onSyncDivisionChanged)
            onSyncDivisionChanged(division);
    };
    syncDivisionSlider_.setRightClickEditsText(false);
    syncDivisionSlider_.onRightClicked = [this]() { showRateSliderContextMenu(); };
    addChildComponent(syncDivisionSlider_);  // Hidden by default (shown when sync enabled)

    // Rate slider
    rateSlider_.setRange(0.05, 20.0, 0.01);
    rateSlider_.setValue(1.0, juce::dontSendNotification);
    rateSlider_.setFont(FontManager::getInstance().getUIFont(9.0f));
    rateSlider_.onValueChanged = [this](double value) {
        currentMod_.rate = static_cast<float>(value);
        if (onRateChanged) {
            onRateChanged(currentMod_.rate);
        }
    };
    rateSlider_.setRightClickEditsText(false);
    rateSlider_.onRightClicked = [this]() { showRateSliderContextMenu(); };
    addAndMakeVisible(rateSlider_);
    updateRateAutomationTarget();

    // Trigger mode combo box
    triggerModeCombo_.addItem("Free", static_cast<int>(magda::LFOTriggerMode::Free) + 1);
    triggerModeCombo_.addItem("Transport", static_cast<int>(magda::LFOTriggerMode::Transport) + 1);
    triggerModeCombo_.addItem("MIDI", static_cast<int>(magda::LFOTriggerMode::MIDI) + 1);
    triggerModeCombo_.addItem("Audio", static_cast<int>(magda::LFOTriggerMode::Audio) + 1);
    triggerModeCombo_.setSelectedId(static_cast<int>(magda::LFOTriggerMode::Free) + 1,
                                    juce::dontSendNotification);
    triggerModeCombo_.setColour(juce::ComboBox::backgroundColourId,
                                DarkTheme::getColour(DarkTheme::SURFACE));
    triggerModeCombo_.setColour(juce::ComboBox::textColourId, DarkTheme::getTextColour());
    triggerModeCombo_.setColour(juce::ComboBox::outlineColourId,
                                DarkTheme::getColour(DarkTheme::BORDER));
    triggerModeCombo_.setJustificationType(juce::Justification::centredLeft);
    triggerModeCombo_.setLookAndFeel(&SmallComboBoxLookAndFeel::getInstance());
    triggerModeCombo_.onChange = [this]() {
        int id = triggerModeCombo_.getSelectedId();
        if (id > 0) {
            auto mode = static_cast<magda::LFOTriggerMode>(id - 1);
            currentMod_.triggerMode = mode;
            // Enable config button only for MIDI/Audio sidechain modes
            bool hasSidechainConfig =
                (mode == magda::LFOTriggerMode::MIDI || mode == magda::LFOTriggerMode::Audio);
            advancedButton_->setEnabled(hasSidechainConfig);
            // Show/hide audio envelope sliders based on trigger mode
            bool isAudioTrigger = (mode == magda::LFOTriggerMode::Audio);
            audioAttackSlider_.setVisible(isAudioTrigger);
            audioReleaseSlider_.setVisible(isAudioTrigger);
            resized();
            if (onTriggerModeChanged) {
                onTriggerModeChanged(mode);
            }
        }
    };
    addAndMakeVisible(triggerModeCombo_);

    // Audio attack slider (shown only when trigger mode = Audio)
    audioAttackSlider_.setRange(0.1, 500.0, 0.1);
    audioAttackSlider_.setValue(1.0, juce::dontSendNotification);
    audioAttackSlider_.setFont(FontManager::getInstance().getUIFont(9.0f));
    audioAttackSlider_.onValueChanged = [this](double value) {
        currentMod_.audioAttackMs = static_cast<float>(value);
        if (onAudioAttackChanged) {
            onAudioAttackChanged(currentMod_.audioAttackMs);
        }
    };
    addChildComponent(audioAttackSlider_);

    // Audio release slider (shown only when trigger mode = Audio)
    audioReleaseSlider_.setRange(1.0, 2000.0, 1.0);
    audioReleaseSlider_.setValue(100.0, juce::dontSendNotification);
    audioReleaseSlider_.setFont(FontManager::getInstance().getUIFont(9.0f));
    audioReleaseSlider_.onValueChanged = [this](double value) {
        currentMod_.audioReleaseMs = static_cast<float>(value);
        if (onAudioReleaseChanged) {
            onAudioReleaseChanged(currentMod_.audioReleaseMs);
        }
    };
    addChildComponent(audioReleaseSlider_);

    // Advanced settings button
    advancedButton_ = std::make_unique<magda::SvgButton>("Advanced", BinaryData::settings_nobg_svg,
                                                         BinaryData::settings_nobg_svgSize);
    advancedButton_->setNormalColor(DarkTheme::getSecondaryTextColour());
    advancedButton_->setHoverColor(DarkTheme::getTextColour());
    advancedButton_->onClick = [this]() {
        if (onAdvancedClicked)
            onAdvancedClicked();
    };
    addAndMakeVisible(advancedButton_.get());

    // Mod matrix viewport
    modMatrixViewport_.setViewedComponent(&modMatrixContent_, false);
    modMatrixViewport_.setScrollBarsShown(true, false);
    addAndMakeVisible(modMatrixViewport_);

    modMatrixContent_.onDeleteLink = [this](magda::ModTarget target) {
        if (selectedModIndex_ >= 0 && onModLinkDeleted) {
            onModLinkDeleted(selectedModIndex_, target);
        }
    };
    modMatrixContent_.onToggleBipolar = [this](magda::ModTarget target, bool bipolar) {
        if (selectedModIndex_ >= 0 && onModLinkBipolarChanged) {
            onModLinkBipolarChanged(selectedModIndex_, target, bipolar);
        }
    };
    modMatrixContent_.onAmountChanged = [this](magda::ModTarget target, float amount) {
        if (selectedModIndex_ >= 0 && onModLinkAmountChanged) {
            onModLinkAmountChanged(selectedModIndex_, target, amount);
        }
    };
}

ModulatorEditorPanel::~ModulatorEditorPanel() {
    stopTimer();
}

void ModulatorEditorPanel::setModInfo(const magda::ModInfo& mod, const magda::ModInfo* liveMod,
                                      std::function<const magda::ModInfo*()> liveModGetter) {
    currentMod_ = mod;
    liveModPtr_ = liveMod;
    liveModGetter_ = std::move(liveModGetter);
    // Use live mod pointer if available (for animation), otherwise use local copy
    waveformDisplay_.setModInfo(liveMod ? liveMod : &currentMod_);
    updateFromMod();
    updateRateAutomationTarget();
}

void ModulatorEditorPanel::setOwnerPath(magda::TrackId trackId,
                                        const magda::ChainNodePath& devicePath) {
    ownerTrackId_ = trackId;
    ownerDevicePath_ = devicePath;
    updateRateAutomationTarget();
}

void ModulatorEditorPanel::updateRateAutomationTarget() {
    if (ownerTrackId_ == magda::INVALID_TRACK_ID || currentMod_.id == magda::INVALID_MOD_ID) {
        rateSlider_.clearAutomationTarget();
        syncDivisionSlider_.clearAutomationTarget();
        return;
    }
    // Both sliders point at the same unified Rate lane (modParamIndex 0); the
    // lane's scale and the slider visible to the user both follow tempoSync.
    magda::AutomationTarget target;
    target.type = magda::AutomationTargetType::ModParameter;
    target.trackId = ownerTrackId_;
    target.devicePath = ownerDevicePath_;
    target.modId = currentMod_.id;
    target.modParamIndex = 0;
    target.paramName = currentMod_.name + " Rate";
    rateSlider_.setAutomationTarget(target);
    syncDivisionSlider_.setAutomationTarget(target);
}

void ModulatorEditorPanel::showRateSliderContextMenu() {
    if (ownerTrackId_ == magda::INVALID_TRACK_ID || currentMod_.id == magda::INVALID_MOD_ID)
        return;

    magda::AutomationTarget target;
    target.type = magda::AutomationTargetType::ModParameter;
    target.trackId = ownerTrackId_;
    target.devicePath = ownerDevicePath_;
    target.modId = currentMod_.id;
    target.modParamIndex = 0;
    target.paramName = currentMod_.name + " Rate";

    juce::PopupMenu menu;
    menu.addItem(1, "Show Automation Lane");

    auto safeThis = juce::Component::SafePointer<ModulatorEditorPanel>(this);
    menu.showMenuAsync(juce::PopupMenu::Options(), [safeThis, target](int result) {
        if (safeThis == nullptr || result != 1)
            return;
        auto& mgr = magda::AutomationManager::getInstance();
        auto laneId = mgr.getOrCreateLane(target, magda::AutomationLaneType::Absolute);
        mgr.setLaneVisible(laneId, true);
    });
}

void ModulatorEditorPanel::setSelectedModIndex(int index) {
    selectedModIndex_ = index;
    if (index < 0) {
        nameLabel_.setText("No Mod Selected", juce::dontSendNotification);
        waveformCombo_.setEnabled(false);
        syncToggle_.setEnabled(false);
        syncDivisionSlider_.setEnabled(false);
        rateSlider_.setEnabled(false);
        triggerModeCombo_.setEnabled(false);
        audioAttackSlider_.setEnabled(false);
        audioReleaseSlider_.setEnabled(false);
        advancedButton_->setEnabled(false);
    } else {
        waveformCombo_.setEnabled(true);
        syncToggle_.setEnabled(true);
        syncDivisionSlider_.setEnabled(true);
        rateSlider_.setEnabled(true);
        triggerModeCombo_.setEnabled(true);
        audioAttackSlider_.setEnabled(true);
        audioReleaseSlider_.setEnabled(true);
        // advancedButton_ enabled state is set in updateFromMod() based on trigger mode
    }
}

void ModulatorEditorPanel::updateFromMod() {
    nameLabel_.setText(currentMod_.name, juce::dontSendNotification);

    // Check if this is a Custom (Curve) waveform
    isCurveMode_ = (currentMod_.waveform == magda::LFOWaveform::Custom);

    // Show/hide appropriate controls based on curve mode
    waveformCombo_.setVisible(!isCurveMode_);

    // In curve mode, show the curve editor, edit button, preset selector, and save button
    curveEditor_.setVisible(isCurveMode_);
    curveEditorButton_->setVisible(isCurveMode_);
    curvePresetCombo_.setVisible(isCurveMode_);
    savePresetButton_->setVisible(isCurveMode_);
    waveformDisplay_.setVisible(!isCurveMode_);

    if (isCurveMode_) {
        // Pass ModInfo to curve editor for loading/saving curve points
        auto* modInfo = const_cast<magda::ModInfo*>(liveModPtr_ ? liveModPtr_ : &currentMod_);
        curveEditor_.setModInfo(modInfo);
    } else {
        // LFO mode - show waveform shape
        waveformCombo_.setSelectedId(static_cast<int>(currentMod_.waveform) + 1,
                                     juce::dontSendNotification);
    }

    // Tempo sync controls
    syncToggle_.setToggleState(currentMod_.tempoSync, juce::dontSendNotification);
    syncToggle_.setButtonText(currentMod_.tempoSync ? "Sync" : "Free");
    syncDivisionSlider_.setValue(static_cast<double>(syncDivisionToIndex(currentMod_.syncDivision)),
                                 juce::dontSendNotification);
    rateSlider_.setValue(currentMod_.rate, juce::dontSendNotification);

    // Show/hide rate vs division based on sync state
    rateSlider_.setVisible(!currentMod_.tempoSync);
    syncDivisionSlider_.setVisible(currentMod_.tempoSync);

    // Trigger mode
    triggerModeCombo_.setSelectedId(static_cast<int>(currentMod_.triggerMode) + 1,
                                    juce::dontSendNotification);

    // Advanced (config) button only enabled for MIDI/Audio sidechain modes
    bool hasSidechainConfig = (currentMod_.triggerMode == magda::LFOTriggerMode::MIDI ||
                               currentMod_.triggerMode == magda::LFOTriggerMode::Audio);
    advancedButton_->setEnabled(hasSidechainConfig);

    // Audio envelope sliders (only visible when trigger mode = Audio)
    bool isAudioTrigger = (currentMod_.triggerMode == magda::LFOTriggerMode::Audio);
    audioAttackSlider_.setVisible(isAudioTrigger);
    audioReleaseSlider_.setVisible(isAudioTrigger);
    if (isAudioTrigger) {
        audioAttackSlider_.setValue(currentMod_.audioAttackMs, juce::dontSendNotification);
        audioReleaseSlider_.setValue(currentMod_.audioReleaseMs, juce::dontSendNotification);
    }

    // Update mod matrix
    updateModMatrix();

    // Update layout since curve/LFO mode affects component positions
    resized();
}

void ModulatorEditorPanel::paintOverChildren(juce::Graphics& g) {
    // Modulation overlay on the visible rate slider — same purple-top /
    // orange-bottom bar convention used by ParamModulationPainter on regular
    // device-param sliders. Painted in paintOverChildren so the slider's own
    // paint doesn't cover it.
    if (currentMod_.id == magda::INVALID_MOD_ID || ownerTrackId_ == magda::INVALID_TRACK_ID)
        return;

    // Locate the same-scope macros / mods (track, rack, or device-level).
    const auto& tm = magda::TrackManager::getInstance();
    const auto* track = tm.getTrack(ownerTrackId_);
    if (!track)
        return;
    const std::vector<magda::MacroInfo>* macros = nullptr;
    const std::vector<magda::ModInfo>* mods = nullptr;
    if (ownerDevicePath_.isValid()) {
        auto resolved = tm.resolvePath(ownerDevicePath_);
        if (resolved.valid && resolved.rack) {
            macros = &resolved.rack->macros;
            mods = &resolved.rack->mods;
        } else if (resolved.valid && resolved.device) {
            macros = &resolved.device->macros;
            mods = &resolved.device->mods;
        }
    }
    if (!macros)
        macros = &track->macros;
    if (!mods)
        mods = &track->mods;

    // Sum macro and mod contributions to rate (modParamIndex 0). Mirrors
    // ParamLinkResolver::computeTotalMacroModulation / computeTotalModModulation
    // shape: per link, offset = bipolar ? value*2-1 : value; total += offset * amount.
    float macroTotal = 0.0f;
    for (const auto& m : *macros) {
        for (const auto& l : m.links) {
            if (l.target.kind != magda::MacroTarget::Kind::ModParam ||
                l.target.modId != currentMod_.id || l.target.modParamIndex != 0)
                continue;
            float offset = l.bipolar ? (m.value * 2.0f - 1.0f) : m.value;
            macroTotal += offset * l.amount;
        }
    }
    float modTotal = 0.0f;
    for (const auto& m : *mods) {
        if (m.id == currentMod_.id)
            continue;
        for (const auto& l : m.links) {
            if (l.target.kind != magda::ModTarget::Kind::ModParam ||
                l.target.modId != currentMod_.id || l.target.modParamIndex != 0)
                continue;
            float offset = l.bipolar ? (m.value * 2.0f - 1.0f) : m.value;
            modTotal += offset * l.amount;
        }
    }

    if (macroTotal == 0.0f && modTotal == 0.0f)
        return;

    auto& visibleSlider = currentMod_.tempoSync ? syncDivisionSlider_ : rateSlider_;
    auto sb = visibleSlider.getBounds();
    if (sb.isEmpty())
        return;

    // Current normalized value of the slider — bar starts here and extends by
    // the modulation total, signed.
    // The rate slider uses different ranges in Hz vs sync mode (Hz: 0.05..20,
    // sync: 0..N divisions). TextSlider exposes getValue but not min/max, so
    // hard-code per visible mode — both ranges are panel-owned constants.
    float currentNorm = 0.0f;
    if (currentMod_.tempoSync) {
        constexpr float kSyncMin = 0.0f;
        const float kSyncMax = static_cast<float>(std::size(kSyncDivisionOrder) - 1);
        if (kSyncMax > kSyncMin)
            currentNorm = juce::jlimit(0.0f, 1.0f,
                                       (static_cast<float>(visibleSlider.getValue()) - kSyncMin) /
                                           (kSyncMax - kSyncMin));
    } else {
        constexpr float kHzMin = 0.05f;
        constexpr float kHzMax = 20.0f;
        currentNorm = juce::jlimit(0.0f, 1.0f,
                                   (static_cast<float>(visibleSlider.getValue()) - kHzMin) /
                                       (kHzMax - kHzMin));
    }

    const int maxWidth = sb.getWidth();
    const int leftX = sb.getX();
    const int barHeight = 5;  // Matches ParamModulationPainter "movement bar"

    auto drawBar = [&](float total, int y, juce::Colour colour) {
        if (total == 0.0f)
            return;
        int startX = leftX + static_cast<int>(maxWidth * currentNorm);
        int barWidth = static_cast<int>(maxWidth * total);
        g.setColour(colour);
        if (barWidth > 0)
            g.fillRoundedRectangle(static_cast<float>(startX), static_cast<float>(y),
                                   static_cast<float>(juce::jmax(1, barWidth)),
                                   static_cast<float>(barHeight), 1.0f);
        else
            g.fillRoundedRectangle(static_cast<float>(startX + barWidth), static_cast<float>(y),
                                   static_cast<float>(juce::jmax(1, -barWidth)),
                                   static_cast<float>(barHeight), 1.0f);
    };

    // Macro bar at top (purple), mod bar at bottom (orange) — same convention
    // as ParamModulationPainter on every other parameter slider in MAGDA.
    drawBar(macroTotal, sb.getY() + 2,
            DarkTheme::getColour(DarkTheme::ACCENT_PURPLE).withAlpha(0.6f));
    drawBar(modTotal, sb.getBottom() - barHeight - 2,
            DarkTheme::getColour(DarkTheme::ACCENT_ORANGE).withAlpha(0.6f));
}

void ModulatorEditorPanel::paint(juce::Graphics& g) {
    // Background
    g.setColour(DarkTheme::getColour(DarkTheme::BACKGROUND).brighter(0.03f));
    g.fillRect(getLocalBounds());

    // Border
    g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
    g.drawRect(getLocalBounds());

    // Section headers
    auto bounds = getLocalBounds().reduced(6);
    bounds.removeFromTop(18 + 6);  // Skip name label + gap

    // Skip the area below name - different for curve vs LFO mode
    g.setColour(DarkTheme::getSecondaryTextColour());
    g.setFont(FontManager::getInstance().getUIFont(8.0f));
    if (isCurveMode_) {
        bounds.removeFromTop(18 + 4);  // Skip preset combo + gap
    } else {
        // "Waveform" label (only shown for LFO mode)
        g.drawText("Waveform", bounds.removeFromTop(10), juce::Justification::centredLeft);
        bounds.removeFromTop(18 + 4);  // Skip waveform selector + gap
    }
    int displayHeight = isCurveMode_ ? 70 : 46;
    bounds.removeFromTop(displayHeight + 6);  // Skip waveform/curve display + gap
    bounds.removeFromTop(18 + 8);             // Skip rate row + gap

    // "Trigger" label
    g.drawText("Trigger", bounds.removeFromTop(12), juce::Justification::centredLeft);

    // Skip trigger row (no dot painted here — waveform display handles the indicator)
    bounds.removeFromTop(18);

    // "Links" label for mod matrix
    {
        auto linkLabelBounds = getLocalBounds().reduced(6);
        // Skip to same position as resized calculates
        linkLabelBounds.removeFromTop(18 + 6);  // name + gap
        if (isCurveMode_) {
            linkLabelBounds.removeFromTop(18 + 4);  // preset combo + gap
        } else {
            linkLabelBounds.removeFromTop(10 + 18 + 4);  // label + waveform + gap
        }
        int dh = isCurveMode_ ? 70 : 46;
        linkLabelBounds.removeFromTop(dh + 6);   // display + gap
        linkLabelBounds.removeFromTop(18 + 8);   // rate row + gap
        linkLabelBounds.removeFromTop(12 + 18);  // trigger label + trigger row

        if (audioAttackSlider_.isVisible()) {
            linkLabelBounds.removeFromTop(6 + 10 + 18 + 4 + 10 + 18);  // audio sliders
        }

        linkLabelBounds.removeFromTop(8);  // gap before Links label
        g.setColour(DarkTheme::getSecondaryTextColour());
        g.setFont(FontManager::getInstance().getUIFont(8.0f));
        g.drawText("Links", linkLabelBounds.removeFromTop(12), juce::Justification::centredLeft);
    }

    // Audio envelope labels (when trigger mode = Audio)
    if (audioAttackSlider_.isVisible()) {
        auto labelBounds = getLocalBounds().reduced(6);
        // Skip to position after trigger row
        labelBounds.removeFromTop(18 + 6);  // name + gap
        if (isCurveMode_) {
            labelBounds.removeFromTop(18 + 4);  // preset combo + gap
        } else {
            labelBounds.removeFromTop(10 + 18 + 4);  // label + waveform + gap
        }
        int displayHeight = isCurveMode_ ? 70 : 46;
        labelBounds.removeFromTop(displayHeight + 6);  // display + gap
        labelBounds.removeFromTop(18 + 8);             // rate row + gap
        labelBounds.removeFromTop(12 + 18);            // trigger label + trigger row
        labelBounds.removeFromTop(6);                  // gap

        g.setColour(DarkTheme::getSecondaryTextColour());
        g.setFont(FontManager::getInstance().getUIFont(8.0f));
        g.drawText("Attack (ms)", labelBounds.removeFromTop(10), juce::Justification::centredLeft);
        labelBounds.removeFromTop(18 + 4);  // slider + gap
        g.drawText("Release (ms)", labelBounds.removeFromTop(10), juce::Justification::centredLeft);
    }
}

void ModulatorEditorPanel::resized() {
    auto bounds = getLocalBounds().reduced(6);

    // Name label at top with curve edit button on right (in curve mode)
    auto headerRow = bounds.removeFromTop(18);
    if (isCurveMode_) {
        int editButtonWidth = 18;
        curveEditorButton_->setBounds(headerRow.removeFromRight(editButtonWidth));
        headerRow.removeFromRight(4);  // Gap
    }
    nameLabel_.setBounds(headerRow);
    bounds.removeFromTop(6);

    if (isCurveMode_) {
        // Curve mode: show preset selector + save button below name
        auto presetRow = bounds.removeFromTop(18);
        int saveButtonWidth = 18;
        savePresetButton_->setBounds(presetRow.removeFromRight(saveButtonWidth));
        presetRow.removeFromRight(4);  // Gap
        curvePresetCombo_.setBounds(presetRow);
        bounds.removeFromTop(4);
    } else {
        // LFO mode: show waveform label + selector
        bounds.removeFromTop(10);  // "Waveform" label
        waveformCombo_.setBounds(bounds.removeFromTop(18));
        bounds.removeFromTop(4);
    }

    // Waveform display or curve editor (same area)
    // Give more height to curve editor since it needs space for editing
    int displayHeight = isCurveMode_ ? 70 : 46;
    auto waveformArea = bounds.removeFromTop(displayHeight);
    waveformDisplay_.setBounds(waveformArea);
    // Expand curve editor bounds by its padding so the curve content fills the visual area
    // while dots can extend into the padding without clipping
    curveEditor_.setBounds(waveformArea.expanded(curveEditor_.getPadding()));
    bounds.removeFromTop(6);

    // Rate row: [Sync button] [Rate slider/division combo]
    auto rateRow = bounds.removeFromTop(18);

    // Sync toggle (small square button)
    int syncToggleWidth = 32;
    syncToggle_.setBounds(rateRow.removeFromLeft(syncToggleWidth));
    rateRow.removeFromLeft(4);  // Small gap

    // Rate slider or division combo takes remaining space (same position, shown alternately)
    rateSlider_.setBounds(rateRow);
    syncDivisionSlider_.setBounds(rateRow);
    bounds.removeFromTop(8);

    // Trigger row: [dropdown] [advanced button]
    bounds.removeFromTop(12);  // "Trigger" label
    auto triggerRow = bounds.removeFromTop(18);

    // Advanced button on the right
    int advButtonWidth = 20;
    advancedButton_->setBounds(triggerRow.removeFromRight(advButtonWidth));
    triggerRow.removeFromRight(4);  // Gap before advanced

    // Trigger combo takes remaining space
    triggerModeCombo_.setBounds(triggerRow);

    // Audio attack/release sliders (below trigger row, only when Audio mode)
    if (audioAttackSlider_.isVisible()) {
        bounds.removeFromTop(6);

        // "Attack" label + slider
        bounds.removeFromTop(10);  // Label space
        audioAttackSlider_.setBounds(bounds.removeFromTop(18));
        bounds.removeFromTop(4);

        // "Release" label + slider
        bounds.removeFromTop(10);  // Label space
        audioReleaseSlider_.setBounds(bounds.removeFromTop(18));
    }

    // Mod matrix section: takes all remaining space
    bounds.removeFromTop(8);
    bounds.removeFromTop(12);  // "Links" label
    if (bounds.getHeight() > 0) {
        modMatrixViewport_.setBounds(bounds);
        modMatrixContent_.setSize(
            bounds.getWidth() - (modMatrixViewport_.isVerticalScrollBarShown() ? 8 : 0),
            juce::jmax(bounds.getHeight(),
                       static_cast<int>(currentMod_.links.size()) * ModMatrixContent::ROW_HEIGHT));
    }
}

void ModulatorEditorPanel::mouseDown(const juce::MouseEvent& /*e*/) {
    // Consume mouse events to prevent propagation to parent
}

void ModulatorEditorPanel::mouseUp(const juce::MouseEvent& /*e*/) {
    // Consume mouse events to prevent propagation to parent
}

void ModulatorEditorPanel::updateModMatrix() {
    std::vector<ModMatrixContent::LinkRow> rows;

    for (const auto& link : currentMod_.links) {
        if (!link.isValid())
            continue;

        ModMatrixContent::LinkRow row;
        row.target = link.target;
        row.amount = link.amount;
        row.bipolar = link.bipolar;

        if (paramNameResolver_) {
            row.paramName = paramNameResolver_(link.target.deviceId, link.target.paramIndex);
        } else {
            row.paramName = "P" + juce::String(link.target.paramIndex);
        }

        rows.push_back(row);
    }

    modMatrixContent_.setLinks(rows);
}

void ModulatorEditorPanel::timerCallback() {
    // Sync mod matrix amounts from live data (handles slider→matrix updates)
    // Use the getter to fetch a fresh pointer — the raw liveModPtr_ can dangle
    // when the mod vector reallocates (mods added/removed).
    const auto* liveMod = liveModGetter_ ? liveModGetter_() : liveModPtr_;
    if (liveMod && !modMatrixContent_.isDragging()) {
        // If links were added or removed, rebuild the full matrix
        if (liveMod->links.size() != currentMod_.links.size()) {
            currentMod_.links = liveMod->links;
            updateModMatrix();
        } else {
            bool changed = false;
            for (const auto& liveLink : liveMod->links) {
                if (!liveLink.isValid())
                    continue;
                if (modMatrixContent_.updateLinkAmount(liveLink.target, liveLink.amount,
                                                       liveLink.bipolar))
                    changed = true;
            }
            if (changed)
                modMatrixContent_.repaint();
        }
    }

    // Pull external rate writes (automation playback / curve drag preview)
    // back into the slider position. Skip while the user is actively driving
    // the slider with the mouse — the in-flight round trip TrackManager
    // → liveMod can briefly trail the local edit.
    if (liveMod && !rateSlider_.isBeingDragged() &&
        std::abs(liveMod->rate - currentMod_.rate) > 1e-4f) {
        currentMod_.rate = liveMod->rate;
        rateSlider_.setValue(liveMod->rate, juce::dontSendNotification);
    }

    // Same dance for sync division — an automation lane targeting paramIndex
    // 1 will write back via setRack/Device/TrackModSyncDivision, so the
    // stepped slider follows whichever division the curve picks.
    if (liveMod && !syncDivisionSlider_.isBeingDragged() &&
        liveMod->syncDivision != currentMod_.syncDivision) {
        currentMod_.syncDivision = liveMod->syncDivision;
        syncDivisionSlider_.setValue(
            static_cast<double>(syncDivisionToIndex(liveMod->syncDivision)),
            juce::dontSendNotification);
    }

    repaint();
}

}  // namespace magda::daw::ui
