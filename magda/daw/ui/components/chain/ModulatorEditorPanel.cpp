#include "ModulatorEditorPanel.hpp"

#include <stdexcept>

#include "BinaryData.h"
#include "core/AutomationInfo.hpp"
#include "core/AutomationManager.hpp"
#include "core/LinkModeManager.hpp"
#include "core/TrackManager.hpp"
#include "core/controllers/BindingRegistry.hpp"
#include "core/controllers/MidiLearnCoordinator.hpp"
#include "ui/components/chain/ParamLinkResolver.hpp"
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

// Build the lane label for a mod's Rate parameter, matching the format used by
// the "Add New Lane" submenu in TrackHeadersPanel: device/rack-scope mods get
// "<owner>: <mod> Rate"; track-scope mods get just "<mod> Rate" (no track
// prefix, since the lane already lives under the track header).
juce::String buildQualifiedModRateName(const magda::ChainNodePath& path,
                                       const juce::String& modName) {
    juce::String suffix = modName + " Rate";
    auto& tm = magda::TrackManager::getInstance();
    switch (path.getType()) {
        case magda::ChainNodeType::Track:
            return suffix;
        case magda::ChainNodeType::Rack:
            if (auto* rack = tm.getRackByPath(path))
                return rack->name + ": " + suffix;
            break;
        case magda::ChainNodeType::TopLevelDevice:
        case magda::ChainNodeType::Device:
            if (auto* dev = tm.getDeviceInChainByPath(path))
                return dev->name + ": " + suffix;
            break;
        case magda::ChainNodeType::None:
        case magda::ChainNodeType::Chain:
            throw std::runtime_error(
                "buildQualifiedModRateName: mod path has no mod-bearing scope");
    }
    throw std::runtime_error("buildQualifiedModRateName: failed to resolve owner for mod path");
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

    // Listen for link-mode toggles so the rate slider can act as a click
    // target when a macro/mod is in link mode (Bitwig-style).
    magda::LinkModeManager::getInstance().addListener(this);

    // Track MIDI Learn state for the rate so we can paint the orange
    // mapped-binding dot and the learn-mode pulse over the rate slider.
    magda::BindingRegistry::getInstance().addListener(this);
    magda::MidiLearnCoordinator::getInstance().addListener(this);

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
    // (notifyDeviceModifiersChanged → resync → new TE state can shift things
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
    target.paramName = buildQualifiedModRateName(ownerDevicePath_, currentMod_.name);
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
    target.paramName = buildQualifiedModRateName(ownerDevicePath_, currentMod_.name);

    juce::PopupMenu menu;
    constexpr int kShowLaneId = 1;
    constexpr int kUnlinkBaseId = 100;
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
                    if (l.target.kind == magda::MacroTarget::Kind::ModParam &&
                        l.target.modId == modId && l.target.modParamIndex == 0) {
                        UnlinkEntry e{SrcKind::Macro, parent, static_cast<int>(i)};
                        unlinks.push_back(e);
                        juce::String name = macros[i].name.isNotEmpty()
                                                ? macros[i].name
                                                : "Macro " + juce::String(i + 1);
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
                    if (l.target.kind == magda::ModTarget::Kind::ModParam &&
                        l.target.modId == modId && l.target.modParamIndex == 0) {
                        UnlinkEntry e{SrcKind::Mod, parent, static_cast<int>(i)};
                        unlinks.push_back(e);
                        juce::String name =
                            mods[i].name.isNotEmpty() ? mods[i].name : "Mod " + juce::String(i + 1);
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
        const bool isLearning =
            learn.isLearningModParam(ownerDevicePath_, currentMod_.id, /*modParamIndex=*/0);
        const int mappingCount = static_cast<int>(
            reg.findForModParam(ownerDevicePath_, currentMod_.id, /*modParamIndex=*/0).size());

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
    auto learnDisplayName = currentMod_.name + " Rate";
    menu.showMenuAsync(juce::PopupMenu::Options(), [safeThis, target, unlinks, rateModId, learnPath,
                                                    learnDisplayName](int result) {
        if (safeThis == nullptr || result == 0)
            return;
        if (result == kShowLaneId) {
            auto& mgr = magda::AutomationManager::getInstance();
            auto laneId = mgr.getOrCreateLane(target, magda::AutomationLaneType::Absolute);
            mgr.setLaneVisible(laneId, true);
            return;
        }
        if (result == kLearnId) {
            auto& learn = magda::MidiLearnCoordinator::getInstance();
            if (learn.isLearningModParam(learnPath, rateModId, /*modParamIndex=*/0)) {
                learn.cancelLearn();
            } else {
                learn.beginLearnModParam(learnPath, rateModId, /*modParamIndex=*/0,
                                         learnDisplayName);
            }
            return;
        }
        if (result == kClearMidiId) {
            magda::MidiLearnCoordinator::getInstance().clearModParamMappings(learnPath, rateModId,
                                                                             /*modParamIndex=*/0);
            return;
        }
        int unlinkIdx = result - kUnlinkBaseId;
        if (unlinkIdx < 0 || unlinkIdx >= static_cast<int>(unlinks.size()))
            return;
        const auto& e = unlinks[static_cast<size_t>(unlinkIdx)];
        auto& tmgr = magda::TrackManager::getInstance();
        if (e.kind == SrcKind::Macro) {
            magda::MacroTarget t;
            t.kind = magda::MacroTarget::Kind::ModParam;
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
            magda::ModTarget t;
            t.kind = magda::ModTarget::Kind::ModParam;
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
            g.setColour(juce::Colour(0xFFFF6B35).withAlpha(0.85f));
            g.fillEllipse(dot);
        }
        if (!sb.isEmpty() && isRateInMidiLearnMode_) {
            float phase = std::fmod(
                static_cast<float>(juce::Time::getMillisecondCounterHiRes() * 0.003), 1.0f);
            float alpha = 0.7f + 0.3f * std::sin(phase * juce::MathConstants<float>::twoPi);
            g.setColour(juce::Colour(0xFFFF6B35).withAlpha(alpha));
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
            auto colour = (macro ? DarkTheme::getColour(DarkTheme::ACCENT_PURPLE)
                                 : DarkTheme::getColour(DarkTheme::ACCENT_ORANGE))
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

    // Current normalized value of the slider — bar starts here and extends
    // by the modulation total, signed. Match the slider's actual visible
    // mapping: sync mode is linear 0..N divisions, Hz mode is logarithmic
    // (0.05..20 Hz, same scale used by TextSlider). Using linear math for
    // the Hz case left the bar misaligned with the thumb and pushed it past
    // the slider's right edge as the LFO oscillated.
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

void ModulatorEditorPanel::mouseDown(const juce::MouseEvent& e) {
    // While link-mode is active and in scope, the rate sliders below have
    // their click-intercept disabled — so a click that lands on the slider
    // bubbles up here. Treat that click as "begin a link-mode drag", same
    // gesture ParamSlotComponent uses for device parameters: vertical drag
    // sets the link amount, mouseUp finalizes without leaving link mode.
    if (linkModeActiveAndInScope_ && e.mods.isLeftButtonDown()) {
        auto& visibleSlider = currentMod_.tempoSync ? syncDivisionSlider_ : rateSlider_;
        if (visibleSlider.getBounds().contains(e.getPosition())) {
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
                if (auto* macros = findMacrosAt(sel.parentPath)) {
                    if (sel.macroIndex >= 0 && sel.macroIndex < static_cast<int>(macros->size())) {
                        magda::MacroTarget t;
                        t.kind = magda::MacroTarget::Kind::ModParam;
                        t.modId = currentMod_.id;
                        t.modParamIndex = 0;
                        if (auto* link = (*macros)[static_cast<size_t>(sel.macroIndex)].getLink(t))
                            initialAmount = link->amount;
                    }
                }
            } else if (lmm.getLinkModeType() == magda::LinkModeType::Mod) {
                const auto& sel = lmm.getModInLinkMode();
                if (auto* mods = findModsAt(sel.parentPath)) {
                    if (sel.modIndex >= 0 && sel.modIndex < static_cast<int>(mods->size())) {
                        magda::ModTarget t;
                        t.kind = magda::ModTarget::Kind::ModParam;
                        t.modId = currentMod_.id;
                        t.modParamIndex = 0;
                        if (auto* link = (*mods)[static_cast<size_t>(sel.modIndex)].getLink(t))
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
            auto bg = (macroSrc ? DarkTheme::getColour(DarkTheme::ACCENT_PURPLE)
                                : DarkTheme::getColour(DarkTheme::ACCENT_ORANGE))
                          .withAlpha(0.95f);
            int percent = static_cast<int>(std::round(initialAmount * 100.0f));
            linkModeAmountLabel_.setText(juce::String(percent) + "%", juce::dontSendNotification);
            linkModeAmountLabel_.setColour(juce::Label::backgroundColourId, bg);
            linkModeAmountLabel_.setColour(juce::Label::textColourId,
                                           DarkTheme::getColour(DarkTheme::BACKGROUND));
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
        magda::MacroTarget target;
        target.kind = magda::MacroTarget::Kind::ModParam;
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
        magda::ModTarget target;
        target.kind = magda::ModTarget::Kind::ModParam;
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

void ModulatorEditorPanel::refreshRateMidiBindingState() {
    bool newState = false;
    if (currentMod_.id != magda::INVALID_MOD_ID) {
        newState = magda::BindingRegistry::getInstance().hasActiveBindingForModParam(
            ownerDevicePath_, currentMod_.id, /*modParamIndex=*/0);
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
                                                 magda::StaticTarget::Owner owner, bool learning) {
    if (owner != magda::StaticTarget::Owner::ModParam)
        return;
    bool isMe =
        (path == ownerDevicePath_ && paramIndex == 0 && currentMod_.id != magda::INVALID_MOD_ID);
    isRateInMidiLearnMode_ = learning && isMe;
    repaint();
}

void ModulatorEditorPanel::midiLearnCompleted(const magda::ChainNodePath& path, int paramIndex,
                                              magda::StaticTarget::Owner owner,
                                              const magda::Binding&) {
    if (owner != magda::StaticTarget::Owner::ModParam)
        return;
    if (path == ownerDevicePath_ && paramIndex == 0)
        refreshRateMidiBindingState();
}

void ModulatorEditorPanel::midiLearnCleared(const magda::ChainNodePath& path, int paramIndex,
                                            magda::StaticTarget::Owner owner, int) {
    if (owner != magda::StaticTarget::Owner::ModParam)
        return;
    if (path == ownerDevicePath_ && paramIndex == 0)
        refreshRateMidiBindingState();
}

}  // namespace magda::daw::ui
