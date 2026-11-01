// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include "modulation/ModulatorEditorPanel.hpp"

#include <algorithm>
#include <array>
#include <utility>

#include "BinaryData.h"
#include "core/AutomationInfo.hpp"
#include "core/AutomationManager.hpp"
#include "core/LinkModeManager.hpp"
#include "core/TrackManager.hpp"
#include "core/controllers/BindingRegistry.hpp"
#include "core/controllers/MidiLearnCoordinator.hpp"
#include "modulation/FollowerEditorPanel.hpp"
#include "ui/components/chain/modulation/SyncDivisionUi.hpp"
#include "ui/components/chain/params/ParamLinkResolver.hpp"
#include "ui/themes/ActiveTheme.hpp"
#include "ui/themes/FontManager.hpp"

namespace magda::daw::ui {

namespace {
/// The LFO shapes in the waveform choice's order.
constexpr std::array<magda::LFOWaveform, 5> kLfoShapes{
    magda::LFOWaveform::Sine, magda::LFOWaveform::Triangle, magda::LFOWaveform::Square,
    magda::LFOWaveform::Saw, magda::LFOWaveform::ReverseSaw};

constexpr int kRowH = 18;
constexpr int kGap = 4;
constexpr int kTabGap = 14;
}  // namespace

// ============================================================================
// ModMatrixContent
// ============================================================================

void ModMatrixContent::setLinks(const std::vector<LinkRow>& links) {
    links_ = links;
    setSize(getWidth(), juce::jmax(1, static_cast<int>(links_.size())) * ROW_HEIGHT);
    repaint();
}

bool ModMatrixContent::updateLinkState(const magda::ControlTarget& target, float amount,
                                       bool bipolar, bool enabled) {
    for (auto& link : links_) {
        if (link.target == target) {
            bool changed =
                (link.amount != amount || link.bipolar != bipolar || link.enabled != enabled);
            link.amount = amount;
            link.bipolar = bipolar;
            link.enabled = enabled;
            return changed;
        }
    }
    return false;
}

void ModMatrixContent::paint(juce::Graphics& g) {
    auto& fonts = FontManager::getInstance();
    const auto accent = ActiveTheme::getColour(ActiveTheme::ACCENT_ATTENTION);
    const auto dim = ActiveTheme::getColour(ActiveTheme::DEVICE_DIM);

    for (int i = 0; i < static_cast<int>(links_.size()); ++i) {
        const auto& link = links_[static_cast<size_t>(i)];
        const auto rowBounds = juce::Rectangle<int>(0, i * ROW_HEIGHT, getWidth(), ROW_HEIGHT);

        g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_LINE));
        g.fillRect(rowBounds.withTop(rowBounds.getBottom() - 1));

        auto remaining = rowBounds.reduced(2, 0);
        const auto rowTextColour = ActiveTheme::getColour(link.enabled ? ActiveTheme::DEVICE_TEXT
                                                                       : ActiveTheme::DEVICE_DIM2);

        // Delete, enable and bipolar zones from the right, as mouseDown reads them.
        g.setFont(device_controls::labelFont(9.0f));
        auto deleteBounds = remaining.removeFromRight(14);
        g.setColour(dim);
        g.drawText("x", deleteBounds, juce::Justification::centred);
        remaining.removeFromRight(2);

        auto enabledBounds = remaining.removeFromRight(18);
        g.setColour(link.enabled ? accent : dim);
        g.drawText(link.enabled ? "ON" : "OFF", enabledBounds, juce::Justification::centred, false);
        remaining.removeFromRight(2);

        auto bipolarBounds = remaining.removeFromRight(16);
        g.setColour(link.enabled && link.bipolar ? accent : dim);
        g.drawText(link.bipolar ? "BI" : "UN", bipolarBounds, juce::Justification::centred, false);
        remaining.removeFromRight(2);

        auto amountBounds = remaining.removeFromRight(32);
        g.setFont(fonts.getMonoFont(9.5f));
        g.setColour(ActiveTheme::getColour(link.enabled ? ActiveTheme::DEVICE_VALUE_TEXT
                                                        : ActiveTheme::DEVICE_DIM2));
        g.drawText(juce::String(static_cast<int>(link.amount * 100)) + "%", amountBounds,
                   juce::Justification::centredRight, false);
        remaining.removeFromRight(4);

        g.setFont(fonts.getUIFont(10.0f));
        g.setColour(rowTextColour);
        g.drawText(link.paramName, remaining, juce::Justification::centredLeft, true);
    }

    if (links_.empty()) {
        g.setColour(dim);
        g.setFont(fonts.getUIFont(10.0f));
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

    // Enable toggle zone: next 18px + 2px padding
    if (x >= width - 36 && x < width - 18) {
        if (onToggleEnabled) {
            auto& link = links_[static_cast<size_t>(rowIndex)];
            onToggleEnabled(link.target, !link.enabled);
        }
        return;
    }

    // Bipolar toggle zone: next 16px + 2px padding
    if (x >= width - 54 && x < width - 38) {
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

    // Listen for link-mode toggles so the rate slider can act as a click
    // target when a macro/mod is in link mode (Bitwig-style).
    magda::LinkModeManager::getInstance().addListener(this);

    // Track MIDI Learn state for the rate so we can paint the orange
    // mapped-binding dot and the learn-mode pulse over the rate slider.
    magda::BindingRegistry::getInstance().addListener(this);
    magda::MidiLearnCoordinator::getInstance().addListener(this);

    startTimer(33);  // 30 FPS for waveform animation

    // The controls tab: every generator control lives here, at the panel's own coordinates.
    controlsPage_.setInterceptsMouseClicks(false, true);
    addAndMakeVisible(controlsPage_);

    // Renaming happens over the name tab; the tab paints the name otherwise.
    nameLabel_.setFont(FontManager::getInstance().getUIFont(11.0f));
    nameLabel_.setColour(juce::Label::textColourId,
                         ActiveTheme::getColour(ActiveTheme::DEVICE_TEXT));
    nameLabel_.setText("No Mod Selected", juce::dontSendNotification);
    nameLabel_.setEditable(false, false, false);
    nameLabel_.onTextChange = [this]() { onNameLabelEdited(); };
    nameLabel_.onEditorHide = [this]() {
        nameLabel_.setVisible(false);
        repaint();
    };
    addChildComponent(nameLabel_);

    waveformChoice_.setOptions({"Sine", "Triangle", "Square", "Saw Up", "Saw Down"});
    waveformChoice_.onChange = [this](int index) {
        if (index >= 0 && index < static_cast<int>(kLfoShapes.size()) && onWaveformChanged)
            onWaveformChanged(kLfoShapes[static_cast<size_t>(index)]);
    };
    controlsPage_.addAndMakeVisible(waveformChoice_);

    // Waveform display (for standard LFO shapes)
    controlsPage_.addAndMakeVisible(waveformDisplay_);

    // Curve editor (for curve mode - bezier editing with integrated phase indicator)
    curveEditor_.setName("miniLFO");
    curveEditor_.setVisible(false);
    curveEditor_.setCurveColour(ActiveTheme::ACCENT_ATTENTION);
    curveEditor_.setDeviceStyle(true);
    curveEditor_.setPadding(kGap);
    curveEditor_.onWaveformChanged = [this]() {
        DBG("[HardCorner] ModulatorEditorPanel embedded curve onWaveformChanged");
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
            curveEditorWindow_->getCurveEditor().syncFromModInfo();
        }
        // Notify parent for fluid MiniWaveformDisplay update
        if (onCurveChanged) {
            onCurveChanged();
        }
        repaint();
    };
    controlsPage_.addChildComponent(curveEditor_);

    // Button to open external curve editor window
    curveEditorButton_ = std::make_unique<magda::SvgButton>("Edit Curve", BinaryData::curve_svg,
                                                            BinaryData::curve_svgSize);
    curveEditorButton_->setNormalColor(ActiveTheme::DEVICE_DIM);
    curveEditorButton_->setHoverColor(ActiveTheme::DEVICE_TEXT);
    curveEditorButton_->setActiveColor(ActiveTheme::DEVICE_BG);
    curveEditorButton_->setActiveBackgroundColor(ActiveTheme::ACCENT_ATTENTION);
    curveEditorButton_->onClick = [this]() {
        if (!curveEditorWindow_) {
            auto* modInfo = liveModForEditing();
            curveEditorWindow_ = std::make_unique<LFOCurveEditorWindow>(
                modInfo,
                [this]() {
                    DBG("[HardCorner] ModulatorEditorPanel external curve onWaveformChanged");
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

            curveEditorWindow_->getCurveEditor().setUndoTarget(ownerDevicePath_, selectedModIndex_);

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
            curveEditorWindow_->onLoopRegionChanged = [this](bool /*useLoopRegion*/) {
                // useLoopRegion is already written to ModInfo by the MSEG
                // toggle; resync so CurveSnapshotHolder picks it up.
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
            curveEditorWindow_->setModInfo(liveModForEditing());
            curveEditorWindow_->getCurveEditor().setUndoTarget(ownerDevicePath_, selectedModIndex_);
            curveEditorWindow_->setVisible(true);
            curveEditorWindow_->toFront(true);
            curveEditorButton_->setActive(true);
        }
    };
    controlsPage_.addChildComponent(curveEditorButton_.get());

    // Curve preset selector (shown in curve mode below the name)
    curvePresetCombo_.addItem("Triangle", static_cast<int>(magda::CurvePreset::Triangle) + 1);
    curvePresetCombo_.addItem("Sine", static_cast<int>(magda::CurvePreset::Sine) + 1);
    curvePresetCombo_.addItem("Ramp Up", static_cast<int>(magda::CurvePreset::RampUp) + 1);
    curvePresetCombo_.addItem("Ramp Down", static_cast<int>(magda::CurvePreset::RampDown) + 1);
    curvePresetCombo_.addItem("S-Curve", static_cast<int>(magda::CurvePreset::SCurve) + 1);
    curvePresetCombo_.addItem("Exp", static_cast<int>(magda::CurvePreset::Exponential) + 1);
    curvePresetCombo_.addItem("Log", static_cast<int>(magda::CurvePreset::Logarithmic) + 1);
    curvePresetCombo_.setTextWhenNothingSelected("Preset");
    curvePresetCombo_.setJustificationType(juce::Justification::centredLeft);
    curvePresetCombo_.setLookAndFeel(&DeviceControlLookAndFeel::getInstance());
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
    controlsPage_.addChildComponent(curvePresetCombo_);

    // Save preset button (shown in curve mode next to preset combo)
    savePresetButton_ = std::make_unique<magda::SvgButton>("Save Preset", BinaryData::save_svg,
                                                           BinaryData::save_svgSize);
    savePresetButton_->setNormalColor(ActiveTheme::DEVICE_DIM);
    savePresetButton_->setHoverColor(ActiveTheme::DEVICE_TEXT);
    savePresetButton_->onClick = []() {
        // TODO: Show save preset dialog
    };
    controlsPage_.addChildComponent(savePresetButton_.get());

    // Sync toggle: lit while the rate follows the tempo.
    syncToggle_.setButtonText("Free");
    syncToggle_.setClickingTogglesState(true);
    syncToggle_.setLookAndFeel(&DeviceControlLookAndFeel::getInstance());
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
    controlsPage_.addAndMakeVisible(syncToggle_);

    // Sync division stepped slider — N musical divisions, indexed 0..N-1.
    // Stepped (interval=1) so each drag click steps to the next division.
    // valueFormatter renders the index as "1 Bar", "1/4", etc.
    constexpr int kNumDivisions = kNumSyncDivisions;
    syncDivisionSlider_.setRange(0.0, static_cast<double>(kNumDivisions - 1), 1.0);
    syncDivisionSlider_.setValue(
        static_cast<double>(syncDivisionToIndex(magda::SyncDivision::Quarter)),
        juce::dontSendNotification);
    device_controls::styleValue(syncDivisionSlider_);
    syncDivisionSlider_.setValueFormatter([](double v) {
        int idx = juce::jlimit(0, kNumDivisions - 1, static_cast<int>(std::round(v)));
        return syncDivisionLabelForIndex(idx);
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
    controlsPage_.addChildComponent(syncDivisionSlider_);  // Shown when sync is on

    // Rate slider
    rateSlider_.setRange(0.05, 20.0, 0.01);
    rateSlider_.setValue(1.0, juce::dontSendNotification);
    device_controls::styleValue(rateSlider_);
    rateSlider_.onValueChanged = [this](double value) {
        currentMod_.rate = static_cast<float>(value);
        if (onRateChanged) {
            onRateChanged(currentMod_.rate);
        }
    };
    rateSlider_.setRightClickEditsText(false);
    rateSlider_.onRightClicked = [this]() { showRateSliderContextMenu(); };
    controlsPage_.addAndMakeVisible(rateSlider_);
    updateRateAutomationTarget();

    // Trigger mode combo box
    triggerModeCombo_.addItem("Free", static_cast<int>(magda::LFOTriggerMode::Free) + 1);
    triggerModeCombo_.addItem("Transport", static_cast<int>(magda::LFOTriggerMode::Transport) + 1);
    triggerModeCombo_.addItem("MIDI", static_cast<int>(magda::LFOTriggerMode::MIDI) + 1);
    triggerModeCombo_.addItem("Audio", static_cast<int>(magda::LFOTriggerMode::Audio) + 1);
    triggerModeCombo_.setSelectedId(static_cast<int>(magda::LFOTriggerMode::Free) + 1,
                                    juce::dontSendNotification);
    triggerModeCombo_.setJustificationType(juce::Justification::centredLeft);
    triggerModeCombo_.setLookAndFeel(&DeviceControlLookAndFeel::getInstance());
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
    controlsPage_.addAndMakeVisible(triggerModeCombo_);

    // Audio attack slider (shown only when trigger mode = Audio)
    audioAttackSlider_.setRange(0.1, 500.0, 0.1);
    audioAttackSlider_.setValue(1.0, juce::dontSendNotification);
    device_controls::styleValue(audioAttackSlider_);
    audioAttackSlider_.setValueFormatter(
        [](double v) { return "Atk " + juce::String(v, 1) + " ms"; });
    audioAttackSlider_.onValueChanged = [this](double value) {
        currentMod_.audioAttackMs = static_cast<float>(value);
        if (onAudioAttackChanged) {
            onAudioAttackChanged(currentMod_.audioAttackMs);
        }
    };
    controlsPage_.addChildComponent(audioAttackSlider_);

    // Audio release slider (shown only when trigger mode = Audio)
    audioReleaseSlider_.setRange(1.0, 2000.0, 1.0);
    audioReleaseSlider_.setValue(100.0, juce::dontSendNotification);
    device_controls::styleValue(audioReleaseSlider_);
    audioReleaseSlider_.setValueFormatter(
        [](double v) { return "Rel " + juce::String(juce::roundToInt(v)) + " ms"; });
    audioReleaseSlider_.onValueChanged = [this](double value) {
        currentMod_.audioReleaseMs = static_cast<float>(value);
        if (onAudioReleaseChanged) {
            onAudioReleaseChanged(currentMod_.audioReleaseMs);
        }
    };
    controlsPage_.addChildComponent(audioReleaseSlider_);

    // ADSR envelope controls (shown only when type == Envelope)
    controlsPage_.addChildComponent(envelopeDisplay_);

    // Time sliders fold their label into the value text (e.g. "A 10 ms") so
    // they don't need separate label components or hand-painted captions.
    auto setupEnvTimeSlider = [this](TextSlider& s, const juce::String& tag, double def,
                                     const std::function<float&()>& field) {
        s.setRange(0.0, 30000.0, 1.0);
        s.setSkewForCentre(500.0);
        s.setValue(def, juce::dontSendNotification);
        device_controls::styleValue(s);
        s.setValueFormatter(
            [tag](double v) { return tag + " " + juce::String(juce::roundToInt(v)) + " ms"; });
        s.onValueChanged = [this, field](double value) {
            field() = static_cast<float>(value);
            fireEnvelopeChanged();
        };
        controlsPage_.addChildComponent(s);
    };
    setupEnvTimeSlider(envAttackSlider_, "A", 10.0,
                       [this]() -> float& { return currentMod_.envAttackMs; });
    setupEnvTimeSlider(envDecaySlider_, "D", 200.0,
                       [this]() -> float& { return currentMod_.envDecayMs; });
    setupEnvTimeSlider(envReleaseSlider_, "R", 300.0,
                       [this]() -> float& { return currentMod_.envReleaseMs; });

    envSustainSlider_.setRange(0.0, 1.0, 0.01);
    envSustainSlider_.setValue(0.7, juce::dontSendNotification);
    device_controls::styleValue(envSustainSlider_);
    envSustainSlider_.setValueFormatter([](double v) { return "S " + juce::String(v, 2); });
    envSustainSlider_.onValueChanged = [this](double value) {
        currentMod_.envSustain = static_cast<float>(value);
        fireEnvelopeChanged();
    };
    controlsPage_.addChildComponent(envSustainSlider_);

    auto setupCurveSlider = [this](TextSlider& s, const juce::String& tag,
                                   const std::function<float&()>& field) {
        s.setRange(-0.5, 0.5, 0.01);
        s.setValue(0.0, juce::dontSendNotification);
        device_controls::styleValue(s);
        s.setFont(FontManager::getInstance().getUIFontMedium(9.5f));  // three to a row
        s.setValueFormatter([tag](double v) { return tag + " " + juce::String(v, 2); });
        s.onValueChanged = [this, field](double value) {
            field() = static_cast<float>(value);
            fireEnvelopeChanged();
        };
        controlsPage_.addChildComponent(s);
    };
    setupCurveSlider(envAttackCurveSlider_, "A crv",
                     [this]() -> float& { return currentMod_.envAttackCurve; });
    setupCurveSlider(envDecayCurveSlider_, "D crv",
                     [this]() -> float& { return currentMod_.envDecayCurve; });
    setupCurveSlider(envReleaseCurveSlider_, "R crv",
                     [this]() -> float& { return currentMod_.envReleaseCurve; });

    // Random modulator controls (shown only when type == Random)
    controlsPage_.addChildComponent(randomDisplay_);

    randomTypeChoice_.setOptions({"Random", "Noise"});  // index = randomType
    randomTypeChoice_.setSelectedIndex(0);
    randomTypeChoice_.onChange = [this](int index) {
        currentMod_.randomType = index;
        fireRandomChanged();
    };
    controlsPage_.addChildComponent(randomTypeChoice_);

    // Random 0..1 sliders fold their label into the value text (e.g. "Shp 0.50").
    auto setupRandomSlider = [this](TextSlider& s, const juce::String& tag,
                                    const std::function<float&()>& field) {
        s.setRange(0.0, 1.0, 0.01);
        device_controls::styleValue(s);
        s.setFont(FontManager::getInstance().getUIFontMedium(9.5f));  // three to a row
        s.setValueFormatter([tag](double v) { return tag + " " + juce::String(v, 2); });
        s.onValueChanged = [this, field](double value) {
            field() = static_cast<float>(value);
            fireRandomChanged();
        };
        controlsPage_.addChildComponent(s);
    };
    setupRandomSlider(randomShapeSlider_, "Shp",
                      [this]() -> float& { return currentMod_.randomShape; });
    setupRandomSlider(randomSmoothSlider_, "Smo",
                      [this]() -> float& { return currentMod_.randomSmooth; });
    setupRandomSlider(randomStepDepthSlider_, "Stp",
                      [this]() -> float& { return currentMod_.randomStepDepth; });

    // Advanced settings button
    advancedButton_ = std::make_unique<magda::SvgButton>("Advanced", BinaryData::settings_nobg_svg,
                                                         BinaryData::settings_nobg_svgSize);
    advancedButton_->setNormalColor(ActiveTheme::DEVICE_DIM);
    advancedButton_->setHoverColor(ActiveTheme::DEVICE_TEXT);
    advancedButton_->onClick = [this]() {
        if (onAdvancedClicked)
            onAdvancedClicked();
    };
    controlsPage_.addAndMakeVisible(advancedButton_.get());

    // The Links tab: the link list scrolls when it outgrows the panel.
    modMatrixViewport_.setViewedComponent(&modMatrixContent_, false);
    modMatrixViewport_.setScrollBarsShown(true, false);
    modMatrixViewport_.setScrollBarThickness(6);
    addChildComponent(modMatrixViewport_);

    modMatrixContent_.onDeleteLink = [this](magda::ControlTarget target) {
        if (selectedModIndex_ >= 0 && onModLinkDeleted) {
            onModLinkDeleted(selectedModIndex_, std::move(target));
        }
    };
    modMatrixContent_.onToggleBipolar = [this](magda::ControlTarget target, bool bipolar) {
        if (selectedModIndex_ >= 0 && onModLinkBipolarChanged) {
            onModLinkBipolarChanged(selectedModIndex_, std::move(target), bipolar);
        }
    };
    modMatrixContent_.onToggleEnabled = [this](magda::ControlTarget target, bool enabled) {
        if (selectedModIndex_ >= 0 && onModLinkEnabledChanged) {
            onModLinkEnabledChanged(selectedModIndex_, std::move(target), enabled);
        }
    };
    modMatrixContent_.onAmountChanged = [this](magda::ControlTarget target, float amount) {
        if (selectedModIndex_ >= 0 && onModLinkAmountChanged) {
            onModLinkAmountChanged(selectedModIndex_, std::move(target), amount);
        }
    };

    // Embedded envelope-follower editor (its own class — no LFO/trigger/MIDI).
    // Shown as the controls tab in follower mode; its links are in our Links tab.
    followerEditorPanel_ = std::make_unique<FollowerEditorPanel>();
    followerEditorPanel_->onFollowerChanged = [this](const magda::ModInfo& mod) {
        if (onFollowerChanged)
            onFollowerChanged(mod);
    };
    followerEditorPanel_->onSourceClicked = [this]() {
        // Reuse the host's sidechain-source picker (sets the device sidechain).
        if (onAdvancedClicked)
            onAdvancedClicked();
    };
    addChildComponent(*followerEditorPanel_);
    lookAndFeelChanged();
}

ModulatorEditorPanel::~ModulatorEditorPanel() {
    magda::LinkModeManager::getInstance().removeListener(this);
    magda::BindingRegistry::getInstance().removeListener(this);
    magda::MidiLearnCoordinator::getInstance().removeListener(this);
    stopTimer();
}

void ModulatorEditorPanel::setModInfo(const magda::ModInfo& mod, const magda::ModInfo* liveMod,
                                      std::function<const magda::ModInfo*()> liveModGetter) {
    currentMod_ = mod;
    liveModPtr_ = liveMod;
    liveModGetter_ = std::move(liveModGetter);
    // Use live mod pointer if available (for animation), otherwise use local copy
    // Pass the getter too so the waveform display can refetch on each paint
    // — the raw `liveMod` pointer dangles after a mod-vector reallocation
    // (notifyDeviceModifiersChanged → resync → new engine state can shift things
    // and any code that mutates track->mods invalidates pointers into it).
    waveformDisplay_.setModInfo(liveMod ? liveMod : &currentMod_, liveModGetter_);
    updateFromMod();
    updateRateAutomationTarget();
    refreshRateMidiBindingState();
}

void ModulatorEditorPanel::setOwnerPath(magda::TrackId trackId,
                                        const magda::ChainNodePath& devicePath) {
    ownerTrackId_ = trackId;
    ownerDevicePath_ = devicePath;
    curveEditor_.setUndoTarget(ownerDevicePath_, selectedModIndex_);
    if (curveEditorWindow_) {
        curveEditorWindow_->getCurveEditor().setUndoTarget(ownerDevicePath_, selectedModIndex_);
    }
    updateRateAutomationTarget();
}

void ModulatorEditorPanel::updateRateAutomationTarget() {
    if (ownerTrackId_ == magda::INVALID_TRACK_ID || ownerDevicePath_.isPostFx() ||
        currentMod_.id == magda::INVALID_MOD_ID) {
        rateSlider_.clearAutomationTarget();
        syncDivisionSlider_.clearAutomationTarget();
        return;
    }
    // Both sliders point at the same unified Rate lane (modParamIndex 0); the
    // lane's scale and the slider visible to the user both follow tempoSync.
    magda::AutomationTarget target;
    target.kind = magda::ControlTarget::Kind::ModParam;
    target.devicePath.trackId = ownerTrackId_;
    target.devicePath = ownerDevicePath_;
    target.modId = currentMod_.id;
    target.modParamIndex = 0;
    rateSlider_.setAutomationTarget(target);
    syncDivisionSlider_.setAutomationTarget(target);
}

void ModulatorEditorPanel::showRateSliderContextMenu() {
    if (ownerTrackId_ == magda::INVALID_TRACK_ID || currentMod_.id == magda::INVALID_MOD_ID)
        return;

    magda::AutomationTarget target;
    target.kind = magda::ControlTarget::Kind::ModParam;
    target.devicePath.trackId = ownerTrackId_;
    target.devicePath = ownerDevicePath_;
    target.modId = currentMod_.id;
    target.modParamIndex = 0;

    juce::PopupMenu menu;
    constexpr int kShowLaneId = 1;
    constexpr int kUnlinkBaseId = 100;
    if (!ownerDevicePath_.isPostFx())
        menu.addItem(kShowLaneId, "Show Automation Lane");

    // Walk every macro / mod scope that could host an incoming link to this
    // mod's rate, collect the ones that actually do, and add an "Unlink"
    // item for each. Unlinks are stored as concrete (scopePath, kind, index,
    // displayName) tuples so the async callback can dispatch without
    // re-walking the data.
    enum class SrcKind { Macro, Mod };
    struct UnlinkEntry {
        SrcKind kind;
        magda::ChainNodePath parentPath;
        int index;
    };
    std::vector<UnlinkEntry> unlinks;

    const auto& tm = magda::TrackManager::getInstance();
    const auto* track = tm.getTrack(ownerTrackId_);
    if (track) {
        auto modId = currentMod_.id;
        auto considerMacros = [&](const std::vector<magda::MacroInfo>& macros,
                                  const magda::ChainNodePath& parent) {
            for (size_t i = 0; i < macros.size(); ++i) {
                for (const auto& l : macros[i].links) {
                    if (l.target.kind == magda::ControlTarget::Kind::ModParam &&
                        l.target.modId == modId && l.target.modParamIndex == 0) {
                        UnlinkEntry e{SrcKind::Macro, parent, static_cast<int>(i)};
                        unlinks.push_back(e);
                        juce::String name =
                            magda::getMacroDisplayName(static_cast<int>(i), macros[i].name);
                        menu.addItem(kUnlinkBaseId + static_cast<int>(unlinks.size() - 1),
                                     "Unlink " + name);
                        break;  // one link per (macro → this rate) at most
                    }
                }
            }
        };
        auto considerMods = [&](const std::vector<magda::ModInfo>& mods,
                                const magda::ChainNodePath& parent) {
            for (size_t i = 0; i < mods.size(); ++i) {
                if (mods[i].id == modId)
                    continue;  // Skip self
                for (const auto& l : mods[i].links) {
                    if (l.target.kind == magda::ControlTarget::Kind::ModParam &&
                        l.target.modId == modId && l.target.modParamIndex == 0) {
                        UnlinkEntry e{SrcKind::Mod, parent, static_cast<int>(i)};
                        unlinks.push_back(e);
                        juce::String name = magda::getModDisplayName(mods[i]);
                        menu.addItem(kUnlinkBaseId + static_cast<int>(unlinks.size() - 1),
                                     "Unlink " + name);
                        break;
                    }
                }
            }
        };

        // Track-level scope is always in scope of any mod on the track.
        considerMacros(track->macros, magda::ChainNodePath::trackLevel(ownerTrackId_));
        considerMods(track->mods, magda::ChainNodePath::trackLevel(ownerTrackId_));
        // Same scope as the editor's owner — rack or device.
        if (ownerDevicePath_.isValid()) {
            auto resolved = tm.resolvePath(ownerDevicePath_);
            if (resolved.valid && resolved.rack) {
                considerMacros(resolved.rack->macros, ownerDevicePath_);
                considerMods(resolved.rack->mods, ownerDevicePath_);
            } else if (resolved.valid && resolved.device) {
                considerMacros(resolved.device->macros, ownerDevicePath_);
                considerMods(resolved.device->mods, ownerDevicePath_);
            }
        }
    }

    // MIDI Learn / Clear — same shape as ParamLinkMenu, scoped to this mod's
    // rate (ownerDevicePath_, modId, modParamIndex=0). The path is the scope
    // hosting the modifier (track / rack / device), matching the AutomationTarget
    // built above.
    constexpr int kLearnId = 200;
    constexpr int kClearMidiId = 201;
    {
        auto& reg = magda::BindingRegistry::getInstance();
        auto& learn = magda::MidiLearnCoordinator::getInstance();
        const bool isLearning = learn.isLearning(
            magda::ControlTarget::modParam(ownerDevicePath_, currentMod_.id, /*modParamIndex=*/0));
        const int mappingCount = static_cast<int>(
            reg.findFor(magda::ControlTarget::modParam(ownerDevicePath_, currentMod_.id,
                                                       /*modParamIndex=*/0))
                .size());

        menu.addSeparator();
        menu.addItem(kLearnId, isLearning ? "Cancel MIDI Learn" : "Learn MIDI");
        menu.addItem(kClearMidiId,
                     "Clear MIDI Mapping" +
                         (mappingCount > 0 ? " (" + juce::String(mappingCount) + ")" : ""),
                     mappingCount > 0);
    }

    auto safeThis = juce::Component::SafePointer<ModulatorEditorPanel>(this);
    auto rateModId = currentMod_.id;
    auto learnPath = ownerDevicePath_;
    auto learnDisplayName = magda::getModParameterDisplayName(currentMod_, 0);
    menu.showMenuAsync(juce::PopupMenu::Options(), [safeThis, target, unlinks, rateModId, learnPath,
                                                    learnDisplayName](int result) {
        if (safeThis == nullptr || result == 0)
            return;
        if (result == kShowLaneId) {
            if (target.devicePath.isPostFx())
                return;

            auto& mgr = magda::AutomationManager::getInstance();
            auto laneId = mgr.getOrCreateLane(target, magda::AutomationLaneType::Absolute);
            mgr.setLaneVisible(laneId, true);
            return;
        }
        if (result == kLearnId) {
            auto& learn = magda::MidiLearnCoordinator::getInstance();
            const auto target =
                magda::ControlTarget::modParam(learnPath, rateModId, /*modParamIndex=*/0);
            if (learn.isLearning(target)) {
                learn.cancelLearn();
            } else {
                learn.beginLearn(target, learnDisplayName);
            }
            return;
        }
        if (result == kClearMidiId) {
            magda::MidiLearnCoordinator::getInstance().clearMappings(
                magda::ControlTarget::modParam(learnPath, rateModId, /*modParamIndex=*/0));
            return;
        }
        int unlinkIdx = result - kUnlinkBaseId;
        if (unlinkIdx < 0 || unlinkIdx >= static_cast<int>(unlinks.size()))
            return;
        const auto& e = unlinks[static_cast<size_t>(unlinkIdx)];
        auto& tmgr = magda::TrackManager::getInstance();
        if (e.kind == SrcKind::Macro) {
            magda::ControlTarget t;
            t.kind = magda::ControlTarget::Kind::ModParam;
            t.devicePath = learnPath;
            t.modId = rateModId;
            t.modParamIndex = 0;
            if (e.parentPath.isTrackLevel) {
                tmgr.removeMacroLink(ChainNodePath::trackLevel(e.parentPath.trackId), e.index, t);
            } else if (e.parentPath.getType() == magda::ChainNodeType::Rack) {
                tmgr.removeMacroLink(e.parentPath, e.index, t);
            } else {
                tmgr.removeMacroLink(e.parentPath, e.index, t);
            }
        } else {
            magda::ControlTarget t;
            t.kind = magda::ControlTarget::Kind::ModParam;
            t.devicePath = learnPath;
            t.modId = rateModId;
            t.modParamIndex = 0;
            if (e.parentPath.isTrackLevel) {
                tmgr.removeModLink(ChainNodePath::trackLevel(e.parentPath.trackId), e.index, t);
            } else if (e.parentPath.getType() == magda::ChainNodeType::Rack) {
                tmgr.removeModLink(e.parentPath, e.index, t);
            } else {
                tmgr.removeModLink(e.parentPath, e.index, t);
            }
        }
    });
}

void ModulatorEditorPanel::setSelectedModIndex(int index) {
    selectedModIndex_ = index;
    if (followerEditorPanel_)
        followerEditorPanel_->setSelectedModIndex(selectedModIndex_);
    curveEditor_.setUndoTarget(ownerDevicePath_, selectedModIndex_);
    if (curveEditorWindow_) {
        curveEditorWindow_->getCurveEditor().setUndoTarget(ownerDevicePath_, selectedModIndex_);
    }
    if (index < 0) {
        nameLabel_.setText("No Mod Selected", juce::dontSendNotification);
        nameLabel_.setEditable(false, false, false);
        waveformChoice_.setEnabled(false);
        syncToggle_.setEnabled(false);
        syncDivisionSlider_.setEnabled(false);
        rateSlider_.setEnabled(false);
        triggerModeCombo_.setEnabled(false);
        audioAttackSlider_.setEnabled(false);
        audioReleaseSlider_.setEnabled(false);
        advancedButton_->setEnabled(false);
    } else {
        waveformChoice_.setEnabled(true);
        syncToggle_.setEnabled(true);
        syncDivisionSlider_.setEnabled(true);
        rateSlider_.setEnabled(true);
        triggerModeCombo_.setEnabled(true);
        audioAttackSlider_.setEnabled(true);
        audioReleaseSlider_.setEnabled(true);
        nameLabel_.setEditable(false, true, false);
        // advancedButton_ enabled state is set in updateFromMod() based on trigger mode
    }
}

void ModulatorEditorPanel::setGeneratorControlsVisible(bool v) {
    waveformChoice_.setVisible(v);
    waveformDisplay_.setVisible(v);
    curveEditor_.setVisible(v);
    curveEditorButton_->setVisible(v);
    curvePresetCombo_.setVisible(v);
    savePresetButton_->setVisible(v);
    syncToggle_.setVisible(v);
    rateSlider_.setVisible(v);
    syncDivisionSlider_.setVisible(v);
    triggerModeCombo_.setVisible(v);
    advancedButton_->setVisible(v);
    audioAttackSlider_.setVisible(v);
    audioReleaseSlider_.setVisible(v);
    envelopeDisplay_.setVisible(v);
    envAttackSlider_.setVisible(v);
    envDecaySlider_.setVisible(v);
    envSustainSlider_.setVisible(v);
    envReleaseSlider_.setVisible(v);
    envAttackCurveSlider_.setVisible(v);
    envDecayCurveSlider_.setVisible(v);
    envReleaseCurveSlider_.setVisible(v);
    randomDisplay_.setVisible(v);
    randomTypeChoice_.setVisible(v);
    randomShapeSlider_.setVisible(v);
    randomSmoothSlider_.setVisible(v);
    randomStepDepthSlider_.setVisible(v);
}

void ModulatorEditorPanel::updateFromMod() {
    // The envelope follower has its own editor; hand off to it and hide our
    // own (generator) controls entirely.
    isFollowerMode_ = (currentMod_.type == magda::ModType::Follower);
    nameLabel_.setText(currentMod_.name, juce::dontSendNotification);
    if (isFollowerMode_) {
        setGeneratorControlsVisible(false);
        followerEditorPanel_->setSelectedModIndex(selectedModIndex_);
        followerEditorPanel_->setModInfo(currentMod_, liveModPtr_, liveModGetter_);
        updateModMatrix();
        showTab();
        resized();
        return;
    }
    // Re-show the generator controls; the per-mode logic below hides the ones
    // that don't apply.
    setGeneratorControlsVisible(true);

    isEnvelopeMode_ = (currentMod_.type == magda::ModType::Envelope);
    isRandomMode_ = (currentMod_.type == magda::ModType::Random);

    // Check if this is a Custom (Curve) waveform (LFO only)
    isCurveMode_ =
        (!isEnvelopeMode_ && !isRandomMode_ && currentMod_.waveform == magda::LFOWaveform::Custom);

    // ---- ADSR envelope controls ----
    envelopeDisplay_.setVisible(isEnvelopeMode_);
    envAttackSlider_.setVisible(isEnvelopeMode_);
    envDecaySlider_.setVisible(isEnvelopeMode_);
    envSustainSlider_.setVisible(isEnvelopeMode_);
    envReleaseSlider_.setVisible(isEnvelopeMode_);
    envAttackCurveSlider_.setVisible(isEnvelopeMode_);
    envDecayCurveSlider_.setVisible(isEnvelopeMode_);
    envReleaseCurveSlider_.setVisible(isEnvelopeMode_);
    if (isEnvelopeMode_) {
        envAttackSlider_.setValue(currentMod_.envAttackMs, juce::dontSendNotification);
        envDecaySlider_.setValue(currentMod_.envDecayMs, juce::dontSendNotification);
        envSustainSlider_.setValue(currentMod_.envSustain, juce::dontSendNotification);
        envReleaseSlider_.setValue(currentMod_.envReleaseMs, juce::dontSendNotification);
        envAttackCurveSlider_.setValue(currentMod_.envAttackCurve, juce::dontSendNotification);
        envDecayCurveSlider_.setValue(currentMod_.envDecayCurve, juce::dontSendNotification);
        envReleaseCurveSlider_.setValue(currentMod_.envReleaseCurve, juce::dontSendNotification);
        envelopeDisplay_.setModInfo(liveModPtr_ ? liveModPtr_ : &currentMod_, liveModGetter_);
    }

    // ---- Random controls ----
    randomDisplay_.setVisible(isRandomMode_);
    randomTypeChoice_.setVisible(isRandomMode_);
    randomShapeSlider_.setVisible(isRandomMode_);
    randomSmoothSlider_.setVisible(isRandomMode_);
    randomStepDepthSlider_.setVisible(isRandomMode_);
    if (isRandomMode_) {
        randomTypeChoice_.setSelectedIndex(currentMod_.randomType);
        randomShapeSlider_.setValue(currentMod_.randomShape, juce::dontSendNotification);
        randomSmoothSlider_.setValue(currentMod_.randomSmooth, juce::dontSendNotification);
        randomStepDepthSlider_.setValue(currentMod_.randomStepDepth, juce::dontSendNotification);
        randomDisplay_.setModInfo(liveModPtr_ ? liveModPtr_ : &currentMod_, liveModGetter_);
    }

    // ---- LFO controls (hidden in envelope/random mode) ----
    // Show/hide appropriate controls based on curve mode
    waveformChoice_.setVisible(!isCurveMode_ && !isEnvelopeMode_ && !isRandomMode_);

    // In curve mode, show the curve editor, edit button, preset selector, and save button
    curveEditor_.setVisible(isCurveMode_);
    curveEditorButton_->setVisible(isCurveMode_);
    curvePresetCombo_.setVisible(isCurveMode_);
    savePresetButton_->setVisible(isCurveMode_);
    waveformDisplay_.setVisible(!isCurveMode_ && !isEnvelopeMode_ && !isRandomMode_);
    // Tempo-synced ADSR stage times are engine-supported but not yet exposed;
    // the envelope UI is ms-based for now, so hide the LFO sync toggle/rate.
    syncToggle_.setVisible(!isEnvelopeMode_);

    if (isCurveMode_) {
        // Pass ModInfo to curve editor for loading/saving curve points
        auto* modInfo = liveModForEditing();
        curveEditor_.setModInfo(modInfo);
        curveEditor_.setUndoTarget(ownerDevicePath_, selectedModIndex_);
        // Mirror the loop region in the inline preview so it's visible without
        // opening the external editor.
        curveEditor_.setShowLoopRegion(currentMod_.useLoopRegion);
        if (curveEditorWindow_ && curveEditorWindow_->isVisible()) {
            curveEditorWindow_->setModInfo(modInfo);
            curveEditorWindow_->getCurveEditor().setUndoTarget(ownerDevicePath_, selectedModIndex_);
        }
    } else {
        // LFO mode - show waveform shape
        const auto* const shape =
            std::find(kLfoShapes.begin(), kLfoShapes.end(), currentMod_.waveform);
        waveformChoice_.setSelectedIndex(
            shape != kLfoShapes.end() ? static_cast<int>(shape - kLfoShapes.begin()) : -1);
    }

    // Tempo sync controls
    syncToggle_.setToggleState(currentMod_.tempoSync, juce::dontSendNotification);
    syncToggle_.setButtonText(currentMod_.tempoSync ? "Sync" : "Free");
    syncDivisionSlider_.setValue(static_cast<double>(syncDivisionToIndex(currentMod_.syncDivision)),
                                 juce::dontSendNotification);
    rateSlider_.setValue(currentMod_.rate, juce::dontSendNotification);

    // Show/hide rate vs division based on sync state (LFO only)
    rateSlider_.setVisible(!isEnvelopeMode_ && !currentMod_.tempoSync);
    syncDivisionSlider_.setVisible(!isEnvelopeMode_ && currentMod_.tempoSync);

    // Trigger mode
    triggerModeCombo_.setSelectedId(static_cast<int>(currentMod_.triggerMode) + 1,
                                    juce::dontSendNotification);

    // Advanced (config) button only enabled for MIDI/Audio sidechain modes
    bool hasSidechainConfig = (currentMod_.triggerMode == magda::LFOTriggerMode::MIDI ||
                               currentMod_.triggerMode == magda::LFOTriggerMode::Audio);
    advancedButton_->setEnabled(hasSidechainConfig);

    // Audio envelope sliders (only visible when trigger mode = Audio, LFO only —
    // they smooth the follower input, not relevant to the ADSR/Random generators)
    bool isAudioTrigger = (!isEnvelopeMode_ && !isRandomMode_ &&
                           currentMod_.triggerMode == magda::LFOTriggerMode::Audio);
    audioAttackSlider_.setVisible(isAudioTrigger);
    audioReleaseSlider_.setVisible(isAudioTrigger);
    if (isAudioTrigger) {
        audioAttackSlider_.setValue(currentMod_.audioAttackMs, juce::dontSendNotification);
        audioReleaseSlider_.setValue(currentMod_.audioReleaseMs, juce::dontSendNotification);
    }

    // Update mod matrix
    updateModMatrix();
    showTab();

    // Update layout since curve/LFO mode affects component positions
    resized();
}

magda::ModInfo* ModulatorEditorPanel::liveModForEditing() {
    const auto* live = liveModGetter_ ? liveModGetter_() : liveModPtr_;
    return const_cast<magda::ModInfo*>(live != nullptr ? live : &currentMod_);
}

void ModulatorEditorPanel::fireEnvelopeChanged() {
    if (selectedModIndex_ >= 0 && onEnvelopeChanged)
        onEnvelopeChanged(currentMod_);
    // Reflect the edit in the local display immediately.
    envelopeDisplay_.repaint();
}

void ModulatorEditorPanel::fireRandomChanged() {
    if (selectedModIndex_ >= 0 && onRandomChanged)
        onRandomChanged(currentMod_);
    // Reflect the edit in the local display immediately.
    randomDisplay_.repaint();
}

void ModulatorEditorPanel::onNameLabelEdited() {
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

void ModulatorEditorPanel::paintOverChildren(juce::Graphics& g) {
    // Everything below marks the rate slider, so nothing while the Links tab hides it.
    if (!(currentMod_.tempoSync ? syncDivisionSlider_ : rateSlider_).isShowing())
        return;

    // Mapped-binding dot + learn-mode pulse on the visible rate slider, same
    // visual language as LinkableTextSlider uses for plugin params.
    {
        auto& visSlider = currentMod_.tempoSync ? syncDivisionSlider_ : rateSlider_;
        auto sb = visSlider.getBounds();
        if (!sb.isEmpty() && hasRateMidiBinding_ && !isRateInMidiLearnMode_) {
            constexpr float dotSize = 5.0f;
            constexpr float margin = 3.0f;
            auto r = sb.toFloat();
            juce::Rectangle<float> dot(r.getRight() - margin - dotSize, r.getY() + margin, dotSize,
                                       dotSize);
            g.setColour(ActiveTheme::getColour(ActiveTheme::MIDI_LEARN).withAlpha(0.85f));
            g.fillEllipse(dot);
        }
        if (!sb.isEmpty() && isRateInMidiLearnMode_) {
            float phase = std::fmod(
                static_cast<float>(juce::Time::getMillisecondCounterHiRes() * 0.003), 1.0f);
            float alpha = 0.7f + 0.3f * std::sin(phase * juce::MathConstants<float>::twoPi);
            g.setColour(ActiveTheme::getColour(ActiveTheme::MIDI_LEARN).withAlpha(alpha));
            g.drawRoundedRectangle(sb.toFloat().reduced(0.5f), 2.0f, 1.5f);
        }
    }

    // Link-mode highlight on the visible rate slider — translucent fill in the
    // active source's accent (purple = macro, orange = mod), matching the
    // convention ParamSlotComponent uses on every other parameter slot.
    if (linkModeActiveAndInScope_) {
        auto& visSlider = currentMod_.tempoSync ? syncDivisionSlider_ : rateSlider_;
        auto sb = visSlider.getBounds();
        if (!sb.isEmpty()) {
            auto& lmm = magda::LinkModeManager::getInstance();
            bool macro = (lmm.getLinkModeType() == magda::LinkModeType::Macro);
            auto colour = (macro ? ActiveTheme::getColour(ActiveTheme::ACCENT_MODULATION)
                                 : ActiveTheme::getColour(ActiveTheme::ACCENT_ATTENTION))
                              .withAlpha(0.18f);
            g.setColour(colour);
            g.fillRoundedRectangle(sb.toFloat(), 2.0f);
        }
    }

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
            if (l.target.kind != magda::ControlTarget::Kind::ModParam ||
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
            if (!l.enabled || l.target.kind != magda::ControlTarget::Kind::ModParam ||
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

    // Current normalized value of the slider — bar starts here and extends
    // by the modulation total, signed. Match the slider's actual visible
    // mapping: sync mode is linear 0..N divisions, Hz mode is logarithmic
    // (0.05..20 Hz, same scale used by TextSlider). Using linear math for
    // the Hz case left the bar misaligned with the thumb and pushed it past
    // the slider's right edge as the LFO oscillated.
    float currentNorm = 0.0f;
    if (currentMod_.tempoSync) {
        constexpr float kSyncMin = 0.0f;
        const auto kSyncMax = static_cast<float>(std::size(kSyncDivisionOrder) - 1);
        if (kSyncMax > kSyncMin)
            currentNorm = juce::jlimit(0.0f, 1.0f,
                                       (static_cast<float>(visibleSlider.getValue()) - kSyncMin) /
                                           (kSyncMax - kSyncMin));
    } else {
        constexpr float kHzMin = 0.05f;
        constexpr float kHzMax = 20.0f;
        const float v = juce::jlimit(kHzMin, kHzMax, static_cast<float>(visibleSlider.getValue()));
        currentNorm = std::log(v / kHzMin) / std::log(kHzMax / kHzMin);
    }

    const int maxWidth = sb.getWidth();
    const int leftX = sb.getX();
    const int rightX = sb.getRight();
    const int barHeight = 5;  // Matches ParamModulationPainter "movement bar"

    // Bar is drawn from the slider thumb position, extending by `total`
    // (signed) — clamped to the slider rect so a high-amount or fast LFO
    // can't paint past the slider's edges.
    auto drawBar = [&](float total, int y, juce::Colour colour) {
        if (total == 0.0f)
            return;
        const int startX = leftX + static_cast<int>(maxWidth * currentNorm);
        const int endX = startX + static_cast<int>(maxWidth * total);
        const int x0 = juce::jlimit(leftX, rightX, juce::jmin(startX, endX));
        const int x1 = juce::jlimit(leftX, rightX, juce::jmax(startX, endX));
        const int width = x1 - x0;
        if (width <= 0)
            return;
        g.setColour(colour);
        g.fillRoundedRectangle(static_cast<float>(x0), static_cast<float>(y),
                               static_cast<float>(width), static_cast<float>(barHeight), 1.0f);
    };

    // Macro bar at top (purple), mod bar at bottom (orange) — same convention
    // as ParamModulationPainter on every other parameter slider in MAGDA.
    drawBar(macroTotal, sb.getY() + 2,
            ActiveTheme::getColour(ActiveTheme::ACCENT_MODULATION).withAlpha(0.6f));
    drawBar(modTotal, sb.getBottom() - barHeight - 2,
            ActiveTheme::getColour(ActiveTheme::ACCENT_ATTENTION).withAlpha(0.6f));
}

void ModulatorEditorPanel::lookAndFeelChanged() {
    // Re-apply cached theme colours after a live theme switch.
    nameLabel_.setColour(juce::Label::textColourId,
                         ActiveTheme::getColour(ActiveTheme::DEVICE_TEXT));
    auto& scrollBar = modMatrixViewport_.getVerticalScrollBar();
    scrollBar.setColour(juce::ScrollBar::thumbColourId,
                        ActiveTheme::getColour(ActiveTheme::DEVICE_LINE2));
    scrollBar.setColour(juce::ScrollBar::trackColourId, juce::Colours::transparentBlack);
    repaint();
}

juce::String ModulatorEditorPanel::tabText(int index) const {
    if (index == 0)
        return nameLabel_.getText().toUpperCase();
    const auto count = std::count_if(currentMod_.links.begin(), currentMod_.links.end(),
                                     [](const auto& link) { return link.isValid(); });
    return count > 0 ? "LINKS " + juce::String(count) : juce::String("LINKS");
}

juce::Rectangle<int> ModulatorEditorPanel::tabBounds(int index) const {
    auto row = getLocalBounds().removeFromTop(TAB_HEIGHT).reduced(10, 0);
    const auto font = device_controls::labelFont();
    const int linksW = juce::GlyphArrangement::getStringWidthInt(font, tabText(1)) + 1;
    const int nameW = juce::jmin(juce::GlyphArrangement::getStringWidthInt(font, tabText(0)) + 1,
                                 row.getWidth() - linksW - kTabGap);
    const auto name = row.removeFromLeft(juce::jmax(0, nameW));
    if (index == 0)
        return name;
    row.removeFromLeft(kTabGap);
    return row.removeFromLeft(linksW);
}

void ModulatorEditorPanel::showLinks(bool links) {
    if (linksShown_ == links)
        return;
    linksShown_ = links;
    showTab();
    resized();
    repaint();
}

void ModulatorEditorPanel::showTab() {
    controlsPage_.setVisible(!linksShown_ && !isFollowerMode_);
    followerEditorPanel_->setVisible(!linksShown_ && isFollowerMode_);
    modMatrixViewport_.setVisible(linksShown_);
}

void ModulatorEditorPanel::paint(juce::Graphics& g) {
    g.fillAll(ActiveTheme::getColour(ActiveTheme::DEVICE_BG));
    g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_LINE));
    g.fillRect(getLocalBounds().removeFromRight(1));

    // The name and Links tabs: the shown one lit and underlined, as the faceplates' tabs.
    g.setFont(device_controls::labelFont());
    for (int i = 0; i < 2; ++i) {
        const auto tab = tabBounds(i);
        const bool selected = (i == 1) == linksShown_;
        if (i == 1 || !nameLabel_.isVisible()) {
            g.setColour(ActiveTheme::getColour(selected ? ActiveTheme::DEVICE_TITLE
                                                        : ActiveTheme::DEVICE_DIM));
            g.drawText(tabText(i), tab, juce::Justification::centredLeft, true);
        }
        if (selected) {
            g.setColour(ActiveTheme::getColour(ActiveTheme::ACCENT_ATTENTION));
            g.fillRect(tab.withTop(tab.getBottom() - 2));
        }
    }

    if (linksShown_ || isFollowerMode_)
        return;

    // Value sliders draw no frame of their own; their fields go under them.
    for (auto* slider :
         {&rateSlider_, &syncDivisionSlider_, &audioAttackSlider_, &audioReleaseSlider_,
          &envAttackSlider_, &envDecaySlider_, &envSustainSlider_, &envReleaseSlider_,
          &envAttackCurveSlider_, &envDecayCurveSlider_, &envReleaseCurveSlider_,
          &randomShapeSlider_, &randomSmoothSlider_, &randomStepDepthSlider_}) {
        if (slider->isVisible())
            device_controls::paintField(g, slider->getBounds().toFloat(),
                                        slider->isEnabled() ? 1.0f : 0.4f);
    }
    if (curveEditor_.isVisible())
        device_controls::paintGraph(
            g, curveEditor_.getBounds().reduced(curveEditor_.getPadding()).toFloat());

    if (!triggerCaption_.isEmpty()) {
        g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_DIM));
        g.setFont(device_controls::labelFont(9.0f));
        g.drawText("TRIG", triggerCaption_, juce::Justification::centredLeft, false);
    }
}

void ModulatorEditorPanel::resized() {
    controlsPage_.setBounds(getLocalBounds());
    const auto content = getLocalBounds()
                             .withTrimmedTop(TAB_HEIGHT + 4)
                             .withTrimmedRight(1)
                             .withTrimmedBottom(6)
                             .reduced(6, 0);

    if (linksShown_) {
        const int rowsH = static_cast<int>(currentMod_.links.size()) * ModMatrixContent::ROW_HEIGHT;
        const bool scrolls = rowsH > content.getHeight();
        modMatrixViewport_.setBounds(content);
        modMatrixContent_.setSize(
            content.getWidth() - (scrolls ? modMatrixViewport_.getScrollBarThickness() + 2 : 0),
            juce::jmax(content.getHeight(), rowsH));
    }

    if (followerEditorPanel_)
        followerEditorPanel_->setBounds(content);

    triggerCaption_ = {};
    if (!linksShown_ && !isFollowerMode_)
        layoutControls(content);
}

void ModulatorEditorPanel::layoutControls(juce::Rectangle<int> area) {
    const auto fromTop = [&area] {
        const auto row = area.removeFromTop(kRowH);
        area.removeFromTop(kGap);
        return row;
    };
    const auto fromBottom = [&area] {
        const auto row = area.removeFromBottom(kRowH);
        area.removeFromBottom(kGap);
        return row;
    };
    const auto columns = [](juce::Rectangle<int> row,
                            std::initializer_list<juce::Component*> parts) {
        const int n = static_cast<int>(parts.size());
        const int width = (row.getWidth() - (n - 1) * kGap) / n;
        int i = 0;
        for (auto* part : parts) {
            part->setBounds(++i == n ? row : row.removeFromLeft(width));
            row.removeFromLeft(kGap);
        }
    };

    // Every generator ends on its trigger: caption, mode, sidechain settings.
    {
        auto row = fromBottom();
        triggerCaption_ = row.removeFromLeft(28);
        advancedButton_->setBounds(row.removeFromRight(kRowH));
        row.removeFromRight(kGap);
        triggerModeCombo_.setBounds(row);
    }

    // The display takes whatever height the rows leave.
    if (isEnvelopeMode_) {
        columns(fromBottom(),
                {&envAttackCurveSlider_, &envDecayCurveSlider_, &envReleaseCurveSlider_});
        columns(fromBottom(), {&envSustainSlider_, &envReleaseSlider_});
        columns(fromBottom(), {&envAttackSlider_, &envDecaySlider_});
        envelopeDisplay_.setBounds(area);
        return;
    }

    {
        auto row = fromBottom();
        syncToggle_.setBounds(row.removeFromLeft(38));
        row.removeFromLeft(kGap);
        rateSlider_.setBounds(row);
        syncDivisionSlider_.setBounds(row);
    }

    if (isRandomMode_) {
        columns(fromBottom(), {&randomShapeSlider_, &randomSmoothSlider_, &randomStepDepthSlider_});
        randomTypeChoice_.setBounds(fromTop());
        randomDisplay_.setBounds(area);
        return;
    }

    if (audioAttackSlider_.isVisible())
        columns(fromBottom(), {&audioAttackSlider_, &audioReleaseSlider_});

    if (isCurveMode_) {
        auto row = fromTop();
        curveEditorButton_->setBounds(row.removeFromRight(kRowH));
        row.removeFromRight(kGap);
        savePresetButton_->setBounds(row.removeFromRight(kRowH));
        row.removeFromRight(kGap);
        curvePresetCombo_.setBounds(row);
    } else {
        waveformChoice_.setBounds(fromTop());
    }
    waveformDisplay_.setBounds(area);
    // Dots on the curve's edge reach into the padding, over the gaps around the graph.
    curveEditor_.setBounds(area.expanded(curveEditor_.getPadding()));
}

bool ModulatorEditorPanel::keyPressed(const juce::KeyPress& key) {
    return key == juce::KeyPress::returnKey;
}

void ModulatorEditorPanel::mouseDown(const juce::MouseEvent& e) {
    for (int i = 0; i < 2; ++i) {
        if (tabBounds(i).contains(e.getPosition())) {
            showLinks(i == 1);
            return;
        }
    }

    // While link-mode is active and in scope, the rate sliders below have
    // their click-intercept disabled — so a click that lands on the slider
    // bubbles up here. Treat that click as "begin a link-mode drag", same
    // gesture ParamSlotComponent uses for device parameters: vertical drag
    // sets the link amount, mouseUp finalizes without leaving link mode.
    if (linkModeActiveAndInScope_ && e.mods.isLeftButtonDown()) {
        auto& visibleSlider = currentMod_.tempoSync ? syncDivisionSlider_ : rateSlider_;
        if (visibleSlider.isShowing() && visibleSlider.getBounds().contains(e.getPosition())) {
            // Look up existing link amount on the source side (if any) so we
            // edit in place instead of resetting to a default.
            auto& lmm = magda::LinkModeManager::getInstance();
            auto& tm = magda::TrackManager::getInstance();
            float initialAmount = 0.3f;

            // Walk source path → macros/mods array, look up the existing
            // link amount so this drag edits in place instead of resetting
            // to default. Track-level uses Track*; rack/device-level uses
            // resolvePath.
            auto findMacrosAt =
                [&](const magda::ChainNodePath& p) -> const std::vector<magda::MacroInfo>* {
                if (p.isTrackLevel) {
                    if (const auto* t = tm.getTrack(p.trackId))
                        return &t->macros;
                    return nullptr;
                }
                auto r = tm.resolvePath(p);
                if (!r.valid)
                    return nullptr;
                if (r.rack)
                    return &r.rack->macros;
                if (r.device)
                    return &r.device->macros;
                return nullptr;
            };
            auto findModsAt =
                [&](const magda::ChainNodePath& p) -> const std::vector<magda::ModInfo>* {
                if (p.isTrackLevel) {
                    if (const auto* t = tm.getTrack(p.trackId))
                        return &t->mods;
                    return nullptr;
                }
                auto r = tm.resolvePath(p);
                if (!r.valid)
                    return nullptr;
                if (r.rack)
                    return &r.rack->mods;
                if (r.device)
                    return &r.device->mods;
                return nullptr;
            };

            if (lmm.getLinkModeType() == magda::LinkModeType::Macro) {
                const auto& sel = lmm.getMacroInLinkMode();
                if (const auto* macros = findMacrosAt(sel.parentPath)) {
                    if (sel.macroIndex >= 0 && sel.macroIndex < static_cast<int>(macros->size())) {
                        magda::ControlTarget t;
                        t.kind = magda::ControlTarget::Kind::ModParam;
                        t.devicePath = ownerDevicePath_;
                        t.modId = currentMod_.id;
                        t.modParamIndex = 0;
                        if (const auto* link =
                                (*macros)[static_cast<size_t>(sel.macroIndex)].getLink(t))
                            initialAmount = link->amount;
                    }
                }
            } else if (lmm.getLinkModeType() == magda::LinkModeType::Mod) {
                const auto& sel = lmm.getModInLinkMode();
                if (const auto* mods = findModsAt(sel.parentPath)) {
                    if (sel.modIndex >= 0 && sel.modIndex < static_cast<int>(mods->size())) {
                        magda::ControlTarget t;
                        t.kind = magda::ControlTarget::Kind::ModParam;
                        t.devicePath = ownerDevicePath_;
                        t.modId = currentMod_.id;
                        t.modParamIndex = 0;
                        if (const auto* link =
                                (*mods)[static_cast<size_t>(sel.modIndex)].getLink(t))
                            initialAmount = link->amount;
                    }
                }
            }

            isLinkModeDrag_ = true;
            linkModeDragStartAmount_ = initialAmount;
            linkModeDragCurrentAmount_ = initialAmount;
            linkModeDragStartY_ = e.getMouseDownY();

            // Set up the floating amount label FIRST. Writing the link below
            // can fire notifyTrackDevicesChanged → chain rebuild → destruction
            // of this panel. Doing label work first means we never touch a
            // freed `this` after the link write.
            bool macroSrc = (lmm.getLinkModeType() == magda::LinkModeType::Macro);
            auto bg = (macroSrc ? ActiveTheme::getColour(ActiveTheme::ACCENT_MODULATION)
                                : ActiveTheme::getColour(ActiveTheme::ACCENT_ATTENTION))
                          .withAlpha(0.95f);
            int percent = static_cast<int>(std::round(initialAmount * 100.0f));
            linkModeAmountLabel_.setText(juce::String(percent) + "%", juce::dontSendNotification);
            linkModeAmountLabel_.setColour(juce::Label::backgroundColourId, bg);
            linkModeAmountLabel_.setColour(juce::Label::textColourId,
                                           ActiveTheme::getColour(ActiveTheme::BACKGROUND));
            linkModeAmountLabel_.setJustificationType(juce::Justification::centred);
            linkModeAmountLabel_.setFont(FontManager::getInstance().getUIFontBold(9.0f));
            if (!linkModeAmountLabel_.isOnDesktop()) {
                linkModeAmountLabel_.addToDesktop(juce::ComponentPeer::windowIsTemporary |
                                                  juce::ComponentPeer::windowIgnoresMouseClicks);
            }
            auto screenBounds = visibleSlider.getScreenBounds();
            linkModeAmountLabel_.setBounds(screenBounds.getX(), screenBounds.getY() - 22,
                                           screenBounds.getWidth(), 20);
            linkModeAmountLabel_.setVisible(true);
            linkModeAmountLabel_.toFront(true);
            repaint();

            // Write the link LAST and guard against synchronous destruction.
            // Creating a brand-new link can trigger notifyTrackDevicesChanged
            // which rebuilds the device chain UI and may delete this panel.
            juce::Component::SafePointer<ModulatorEditorPanel> safeThis(this);
            writeLinkAmountFromActiveSource(initialAmount);
            (void)safeThis;  // No more `this` access after this line.
            return;
        }
    }
    // Otherwise consume to prevent propagation to parent.
}

void ModulatorEditorPanel::mouseDrag(const juce::MouseEvent& e) {
    if (!isLinkModeDrag_)
        return;
    int deltaY = linkModeDragStartY_ - e.getPosition().y;
    constexpr float sensitivity = 0.005f;
    float newAmount = juce::jlimit(-1.0f, 1.0f, linkModeDragStartAmount_ + (deltaY * sensitivity));
    if (newAmount == linkModeDragCurrentAmount_)
        return;
    linkModeDragCurrentAmount_ = newAmount;
    int percent = static_cast<int>(std::round(newAmount * 100.0f));
    linkModeAmountLabel_.setText(juce::String(percent) + "%", juce::dontSendNotification);
    writeLinkAmountFromActiveSource(newAmount);
    repaint();
}

void ModulatorEditorPanel::mouseUp(const juce::MouseEvent& /*e*/) {
    if (isLinkModeDrag_) {
        isLinkModeDrag_ = false;
        linkModeAmountLabel_.setVisible(false);
        if (linkModeAmountLabel_.isOnDesktop())
            linkModeAmountLabel_.removeFromDesktop();
        repaint();
    }
    // Stay in link mode — only the user (link button or ESC) exits.
}

void ModulatorEditorPanel::mouseDoubleClick(const juce::MouseEvent& e) {
    if (selectedModIndex_ < 0 || !tabBounds(0).contains(e.getPosition()))
        return;
    nameLabel_.setBounds(getLocalBounds().removeFromTop(TAB_HEIGHT).reduced(6, 4));
    nameLabel_.setVisible(true);
    nameLabel_.showEditor();
}

// ============================================================================
// LinkModeManagerListener
// ============================================================================

void ModulatorEditorPanel::macroLinkModeChanged(bool active,
                                                const magda::MacroSelection& selection) {
    // Owner-scope path: device path if set, otherwise the track-level path —
    // global mod editor instances don't carry a device path.
    auto ownerScope = ownerDevicePath_.isValid() ? ownerDevicePath_
                                                 : magda::ChainNodePath::trackLevel(ownerTrackId_);
    bool inScope = active && ownerScope.isValid() &&
                   magda::daw::ui::isInScopeOf(ownerScope, selection.parentPath);
    linkModeActiveAndInScope_ = inScope;
    applyLinkModeToRateSliders();
    repaint();
}

void ModulatorEditorPanel::modLinkModeChanged(bool active, const magda::ModSelection& selection) {
    auto ownerScope = ownerDevicePath_.isValid() ? ownerDevicePath_
                                                 : magda::ChainNodePath::trackLevel(ownerTrackId_);
    // A mod can't drive its own rate — skip this case so we don't create a
    // self-loop link from clicking the rate slider while the mod itself is
    // in link mode.
    bool isSelf = (selection.parentPath == ownerDevicePath_) &&
                  (selection.modIndex >= 0 && selectedModIndex_ == selection.modIndex);
    bool inScope = active && !isSelf && ownerScope.isValid() &&
                   magda::daw::ui::isInScopeOf(ownerScope, selection.parentPath);
    linkModeActiveAndInScope_ = inScope;
    applyLinkModeToRateSliders();
    repaint();
}

void ModulatorEditorPanel::applyLinkModeToRateSliders() {
    // When link mode is active, drop the slider's click intercept so the
    // panel's mouseDown receives the click (and routes it to link
    // creation). Otherwise restore normal slider behavior.
    bool intercept = !linkModeActiveAndInScope_;
    rateSlider_.setInterceptsMouseClicks(intercept, intercept);
    syncDivisionSlider_.setInterceptsMouseClicks(intercept, intercept);
    auto cursor = linkModeActiveAndInScope_ ? juce::MouseCursor::PointingHandCursor
                                            : juce::MouseCursor::NormalCursor;
    rateSlider_.setMouseCursor(cursor);
    syncDivisionSlider_.setMouseCursor(cursor);
}

void ModulatorEditorPanel::writeLinkAmountFromActiveSource(float amount) {
    if (currentMod_.id == magda::INVALID_MOD_ID)
        return;

    auto& lmm = magda::LinkModeManager::getInstance();
    auto& tm = magda::TrackManager::getInstance();

    if (lmm.getLinkModeType() == magda::LinkModeType::Macro) {
        const auto& sel = lmm.getMacroInLinkMode();
        if (!sel.isValid())
            return;
        magda::ControlTarget target;
        target.kind = magda::ControlTarget::Kind::ModParam;
        target.devicePath = ownerDevicePath_;
        target.modId = currentMod_.id;
        target.modParamIndex = 0;  // Rate

        // setXxxMacroLinkAmount creates the link if missing, otherwise
        // updates its amount — same single-call semantics across scopes.
        if (sel.parentPath.isTrackLevel) {
            tm.setMacroLinkAmount(ChainNodePath::trackLevel(sel.parentPath.trackId), sel.macroIndex,
                                  target, amount);
        } else if (sel.parentPath.getType() == magda::ChainNodeType::Rack) {
            tm.setMacroLinkAmount(sel.parentPath, sel.macroIndex, target, amount);
        } else {
            tm.setMacroLinkAmount(sel.parentPath, sel.macroIndex, target, amount);
        }
    } else if (lmm.getLinkModeType() == magda::LinkModeType::Mod) {
        const auto& sel = lmm.getModInLinkMode();
        if (!sel.isValid())
            return;
        magda::ControlTarget target;
        target.kind = magda::ControlTarget::Kind::ModParam;
        target.devicePath = ownerDevicePath_;
        target.modId = currentMod_.id;
        target.modParamIndex = 0;  // Rate

        if (sel.parentPath.isTrackLevel) {
            tm.setModLinkAmount(ChainNodePath::trackLevel(sel.parentPath.trackId), sel.modIndex,
                                target, amount);
        } else if (sel.parentPath.getType() == magda::ChainNodeType::Rack) {
            tm.setModLinkAmount(sel.parentPath, sel.modIndex, target, amount);
        } else {
            tm.setModLinkAmount(sel.parentPath, sel.modIndex, target, amount);
        }
    }
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
        row.enabled = link.enabled;

        if (link.target.kind == magda::ControlTarget::Kind::ModParam) {
            row.paramName = magda::getDisplayNameForTarget(link.target);
        } else if (paramNameResolver_) {
            row.paramName = paramNameResolver_(link.target.deviceId(), link.target.paramIndex);
        } else {
            row.paramName = "Unresolved parameter";
        }

        rows.push_back(row);
    }

    modMatrixContent_.setLinks(rows);
    resized();
}

void ModulatorEditorPanel::timerCallback() {
    // The curve editors write into the mod, so they follow it when its vector reallocates.
    if (isCurveMode_) {
        auto* live = liveModForEditing();
        if (curveEditor_.getModInfo() != live) {
            curveEditor_.setModInfo(live);
            if (curveEditorWindow_)
                curveEditorWindow_->setModInfo(live);
        }
    }

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
                if (modMatrixContent_.updateLinkState(liveLink.target, liveLink.amount,
                                                      liveLink.bipolar, liveLink.enabled))
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

void ModulatorEditorPanel::refreshRateMidiBindingState() {
    bool newState = false;
    if (currentMod_.id != magda::INVALID_MOD_ID) {
        newState = magda::BindingRegistry::getInstance().hasActiveBindingFor(
            magda::ControlTarget::modParam(ownerDevicePath_, currentMod_.id, /*modParamIndex=*/0));
    }
    if (newState != hasRateMidiBinding_) {
        hasRateMidiBinding_ = newState;
        repaint();
    }
}

void ModulatorEditorPanel::bindingRegistryChanged(magda::BindingScope) {
    refreshRateMidiBindingState();
}

void ModulatorEditorPanel::midiLearnStateChanged(const magda::ChainNodePath& path, int paramIndex,
                                                 magda::ControlTarget::Kind owner, bool learning) {
    if (owner != magda::ControlTarget::Kind::ModParam)
        return;
    bool isMe =
        (path == ownerDevicePath_ && paramIndex == 0 && currentMod_.id != magda::INVALID_MOD_ID);
    isRateInMidiLearnMode_ = learning && isMe;
    repaint();
}

void ModulatorEditorPanel::midiLearnCompleted(const magda::ChainNodePath& path, int paramIndex,
                                              magda::ControlTarget::Kind owner,
                                              const magda::Binding&) {
    if (owner != magda::ControlTarget::Kind::ModParam)
        return;
    if (path == ownerDevicePath_ && paramIndex == 0)
        refreshRateMidiBindingState();
}

void ModulatorEditorPanel::midiLearnCleared(const magda::ChainNodePath& path, int paramIndex,
                                            magda::ControlTarget::Kind owner, int) {
    if (owner != magda::ControlTarget::Kind::ModParam)
        return;
    if (path == ownerDevicePath_ && paramIndex == 0)
        refreshRateMidiBindingState();
}

}  // namespace magda::daw::ui
