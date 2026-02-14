#include <cmath>

#include "../../../../state/TimelineController.hpp"
#include "../../../../themes/DarkTheme.hpp"
#include "../../../../themes/FontManager.hpp"
#include "../../../../themes/InspectorComboBoxLookAndFeel.hpp"
#include "../../../../themes/SmallButtonLookAndFeel.hpp"
#include "../../../../utils/TimelineUtils.hpp"
#include "../ClipInspector.hpp"
#include "BinaryData.h"
#include "core/ClipOperations.hpp"

namespace magda::daw::ui {

// ========================================================================
// Clip properties section
// ========================================================================

void ClipInspector::initClipPropertiesSection() {
    // Clip name (used as header - no "Name" label needed)
    clipNameLabel_.setVisible(false);  // Not used anymore

    clipNameValue_.setFont(FontManager::getInstance().getUIFont(14.0f));  // Larger for header
    clipNameValue_.setColour(juce::Label::textColourId, DarkTheme::getTextColour());
    clipNameValue_.setColour(juce::Label::backgroundColourId,
                             DarkTheme::getColour(DarkTheme::SURFACE));
    clipNameValue_.setEditable(true);
    clipNameValue_.onTextChange = [this]() {
        if (selectedClipId_ != magda::INVALID_CLIP_ID) {
            magda::ClipManager::getInstance().setClipName(selectedClipId_,
                                                          clipNameValue_.getText());
        }
    };
    addChildComponent(clipNameValue_);

    // Clip file path (read-only, inside viewport)
    clipFilePathLabel_.setFont(FontManager::getInstance().getUIFont(10.0f));
    clipFilePathLabel_.setColour(juce::Label::textColourId, DarkTheme::getSecondaryTextColour());
    clipFilePathLabel_.setJustificationType(juce::Justification::centredLeft);
    clipPropsContainer_.addChildComponent(clipFilePathLabel_);

    // Clip type icon (sinewave for audio, midi for MIDI)
    clipTypeIcon_ = std::make_unique<magda::SvgButton>("Type", BinaryData::sinewave_svg,
                                                       BinaryData::sinewave_svgSize);
    clipTypeIcon_->setOriginalColor(juce::Colour(0xFFB3B3B3));
    clipTypeIcon_->setNormalColor(DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
    clipTypeIcon_->setInterceptsMouseClicks(false, false);
    clipTypeIcon_->setTooltip("Audio clip");
    addChildComponent(*clipTypeIcon_);

    // Detected BPM (shown at bottom with WARP button)
    clipBpmValue_.setFont(FontManager::getInstance().getUIFont(11.0f));
    clipBpmValue_.setColour(juce::Label::textColourId, DarkTheme::getSecondaryTextColour());
    clipBpmValue_.setColour(juce::Label::outlineColourId, DarkTheme::getColour(DarkTheme::BORDER));
    clipBpmValue_.setJustificationType(juce::Justification::centred);
    clipPropsContainer_.addChildComponent(clipBpmValue_);

    // Length in beats (shown next to BPM when auto-tempo is enabled)
    clipBeatsLengthValue_ = std::make_unique<DraggableValueLabel>(DraggableValueLabel::Format::Raw);
    clipBeatsLengthValue_->setRange(0.25, 128.0, 4.0);  // Min 0.25 beats, max 128 beats
    clipBeatsLengthValue_->setSuffix(" beats");
    clipBeatsLengthValue_->setDecimalPlaces(2);
    clipBeatsLengthValue_->setSnapToInteger(true);
    clipBeatsLengthValue_->setDrawBackground(false);
    clipBeatsLengthValue_->setDrawBorder(true);
    clipBeatsLengthValue_->setShowFillIndicator(false);
    clipBeatsLengthValue_->onValueChange = [this]() {
        if (selectedClipId_ != magda::INVALID_CLIP_ID) {
            auto* clip = magda::ClipManager::getInstance().getClip(selectedClipId_);
            if (clip && clip->autoTempo) {
                double newBeats = clipBeatsLengthValue_->getValue();
                double bpm =
                    timelineController_ ? timelineController_->getState().tempo.bpm : 120.0;
                // Stretch: keep source audio constant, change how many beats it fills
                magda::ClipManager::getInstance().setLengthBeats(selectedClipId_, newBeats, bpm);
            }
        }
    };
    clipPropsContainer_.addChildComponent(*clipBeatsLengthValue_);

    // Position icon (static, non-interactive)
    clipPositionIcon_ = std::make_unique<magda::SvgButton>("Position", BinaryData::position_svg,
                                                           BinaryData::position_svgSize);
    clipPositionIcon_->setOriginalColor(juce::Colour(0xFFB3B3B3));
    clipPositionIcon_->setNormalColor(DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
    clipPositionIcon_->setInterceptsMouseClicks(false, false);
    clipPropsContainer_.addChildComponent(*clipPositionIcon_);

    // Row labels for position grid
    playbackColumnLabel_.setText("position", juce::dontSendNotification);
    playbackColumnLabel_.setFont(FontManager::getInstance().getUIFont(11.0f));
    playbackColumnLabel_.setColour(juce::Label::textColourId, DarkTheme::getSecondaryTextColour());
    clipPropsContainer_.addChildComponent(playbackColumnLabel_);

    loopColumnLabel_.setText("loop", juce::dontSendNotification);
    loopColumnLabel_.setFont(FontManager::getInstance().getUIFont(11.0f));
    loopColumnLabel_.setColour(juce::Label::textColourId, DarkTheme::getSecondaryTextColour());
    clipPropsContainer_.addChildComponent(loopColumnLabel_);

    // Clip start
    clipStartLabel_.setText("start", juce::dontSendNotification);
    clipStartLabel_.setFont(FontManager::getInstance().getUIFont(11.0f));
    clipStartLabel_.setColour(juce::Label::textColourId, DarkTheme::getSecondaryTextColour());
    clipPropsContainer_.addChildComponent(clipStartLabel_);

    clipStartValue_ = std::make_unique<magda::BarsBeatsTicksLabel>();
    clipStartValue_->setRange(0.0, 10000.0, 0.0);
    clipStartValue_->setDoubleClickResetsValue(false);
    clipStartValue_->onValueChange = [this]() {
        if (selectedClipId_ == magda::INVALID_CLIP_ID)
            return;
        const auto* clip = magda::ClipManager::getInstance().getClip(selectedClipId_);
        if (!clip || clip->view == magda::ClipView::Session)
            return;

        double bpm = 120.0;
        if (timelineController_) {
            bpm = timelineController_->getState().tempo.bpm;
        }
        double newStartSeconds =
            magda::TimelineUtils::beatsToSeconds(clipStartValue_->getValue(), bpm);
        magda::ClipManager::getInstance().moveClip(selectedClipId_, newStartSeconds, bpm);
    };
    clipPropsContainer_.addChildComponent(*clipStartValue_);

    // Clip end
    clipEndLabel_.setText("end", juce::dontSendNotification);
    clipEndLabel_.setFont(FontManager::getInstance().getUIFont(11.0f));
    clipEndLabel_.setColour(juce::Label::textColourId, DarkTheme::getSecondaryTextColour());
    clipPropsContainer_.addChildComponent(clipEndLabel_);

    clipEndValue_ = std::make_unique<magda::BarsBeatsTicksLabel>();
    clipEndValue_->setRange(0.0, 10000.0, 4.0);
    clipEndValue_->setDoubleClickResetsValue(false);
    clipEndValue_->onValueChange = [this]() {
        if (selectedClipId_ == magda::INVALID_CLIP_ID)
            return;
        const auto* clip = magda::ClipManager::getInstance().getClip(selectedClipId_);
        if (!clip)
            return;

        if (clip->view == magda::ClipView::Session) {
            // Session clips: End field controls clip length in beats
            double bpm = 120.0;
            if (timelineController_) {
                bpm = timelineController_->getState().tempo.bpm;
            }
            double newClipEndBeats = clipEndValue_->getValue();

            // Resize the clip
            double newLengthSeconds = magda::TimelineUtils::beatsToSeconds(newClipEndBeats, bpm);
            magda::ClipManager::getInstance().resizeClip(selectedClipId_, newLengthSeconds, false,
                                                         bpm);

            // Clamp offset and loop length so they stay within clip bounds
            double newClipEndSeconds = magda::TimelineUtils::beatsToSeconds(newClipEndBeats, bpm);
            double offsetSeconds = clip->offset;

            // If offset is past new clip end, pull it back
            if (offsetSeconds >= clip->loopStart + newClipEndSeconds) {
                double srcLen = clip->loopLength > 0.0 ? clip->loopLength
                                                       : newClipEndSeconds * clip->speedRatio;
                offsetSeconds = std::max(clip->loopStart, clip->loopStart + srcLen -
                                                              newClipEndSeconds * clip->speedRatio);
                if (offsetSeconds < clip->loopStart)
                    offsetSeconds = clip->loopStart;
                magda::ClipManager::getInstance().setOffset(selectedClipId_, offsetSeconds);
            }

            // If source region exceeds clip end, shrink it
            double sourceLengthSeconds =
                clip->loopLength > 0.0 ? clip->loopLength : newClipEndSeconds * clip->speedRatio;
            double sourceEndSeconds = clip->loopStart + sourceLengthSeconds;
            if (sourceEndSeconds > clip->loopStart + newClipEndSeconds) {
                double clampedLoopLength = std::max(magda::ClipOperations::MIN_SOURCE_LENGTH,
                                                    newClipEndSeconds * clip->speedRatio);
                magda::ClipManager::getInstance().setLoopLength(selectedClipId_, clampedLoopLength);
            }
        } else {
            // Arrangement clips: resize based on new end position
            double bpm = 120.0;
            if (timelineController_) {
                bpm = timelineController_->getState().tempo.bpm;
            }
            double endBeats = clipEndValue_->getValue();
            double startBeats = magda::TimelineUtils::secondsToBeats(clip->startTime, bpm);
            double newLengthBeats = endBeats - startBeats;
            if (newLengthBeats < 0.0)
                newLengthBeats = 0.0;
            double newLengthSeconds = magda::TimelineUtils::beatsToSeconds(newLengthBeats, bpm);
            magda::ClipManager::getInstance().resizeClip(selectedClipId_, newLengthSeconds, false,
                                                         bpm);
        }
    };
    clipPropsContainer_.addChildComponent(*clipEndValue_);

    // Content offset (shown in position row, 3rd column)
    clipOffsetLabel_.setText("offset", juce::dontSendNotification);
    clipOffsetLabel_.setFont(FontManager::getInstance().getUIFont(11.0f));
    clipOffsetLabel_.setColour(juce::Label::textColourId, DarkTheme::getSecondaryTextColour());
    clipPropsContainer_.addChildComponent(clipOffsetLabel_);

    clipContentOffsetValue_ = std::make_unique<magda::BarsBeatsTicksLabel>();
    clipContentOffsetValue_->setRange(0.0, 10000.0, 0.0);
    clipContentOffsetValue_->setDoubleClickResetsValue(true);  // Double-click resets to 0
    clipContentOffsetValue_->onValueChange = [this]() {
        if (selectedClipId_ == magda::INVALID_CLIP_ID)
            return;
        const auto* clip = magda::ClipManager::getInstance().getClip(selectedClipId_);
        if (!clip)
            return;

        if (clip->type == magda::ClipType::MIDI) {
            double newOffsetBeats = clipContentOffsetValue_->getValue();
            magda::ClipManager::getInstance().setClipMidiOffset(selectedClipId_, newOffsetBeats);
        } else if (clip->type == magda::ClipType::Audio) {
            double bpm = 120.0;
            if (timelineController_) {
                bpm = timelineController_->getState().tempo.bpm;
            }
            double newOffsetBeats = clipContentOffsetValue_->getValue();
            double newOffsetSeconds = magda::TimelineUtils::beatsToSeconds(newOffsetBeats, bpm);
            magda::ClipManager::getInstance().setOffset(selectedClipId_, newOffsetSeconds);
        }
    };
    clipPropsContainer_.addChildComponent(*clipContentOffsetValue_);

    // Loop toggle (infinito icon)
    clipLoopToggle_ = std::make_unique<magda::SvgButton>("Loop", BinaryData::infinito_svg,
                                                         BinaryData::infinito_svgSize);
    clipLoopToggle_->setOriginalColor(juce::Colour(0xFFB3B3B3));
    clipLoopToggle_->setNormalColor(DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
    clipLoopToggle_->setHoverColor(DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
    clipLoopToggle_->setActiveColor(DarkTheme::getColour(DarkTheme::ACCENT_BLUE));
    clipLoopToggle_->setClickingTogglesState(false);
    clipLoopToggle_->onClick = [this]() {
        if (selectedClipId_ != magda::INVALID_CLIP_ID) {
            auto* clip = magda::ClipManager::getInstance().getClip(selectedClipId_);
            if (!clip)
                return;

            // Beat mode requires loop — don't allow disabling
            if (clip->autoTempo && clipLoopToggle_->isActive())
                return;

            bool newState = !clipLoopToggle_->isActive();
            clipLoopToggle_->setActive(newState);
            double bpm = 120.0;
            if (timelineController_) {
                bpm = timelineController_->getState().tempo.bpm;
            }
            magda::ClipManager::getInstance().setClipLoopEnabled(selectedClipId_, newState, bpm);
        }
    };
    clipPropsContainer_.addChildComponent(*clipLoopToggle_);

    // Warp toggle (pin icon)
    clipWarpToggle_.setButtonText("WARP");
    clipWarpToggle_.setLookAndFeel(&SmallButtonLookAndFeel::getInstance());
    clipWarpToggle_.setColour(juce::TextButton::buttonColourId,
                              DarkTheme::getColour(DarkTheme::SURFACE));
    clipWarpToggle_.setColour(juce::TextButton::buttonOnColourId,
                              DarkTheme::getAccentColour().withAlpha(0.3f));
    clipWarpToggle_.setColour(juce::TextButton::textColourOffId,
                              DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
    clipWarpToggle_.setColour(juce::TextButton::textColourOnId, DarkTheme::getAccentColour());
    clipWarpToggle_.setClickingTogglesState(false);
    clipWarpToggle_.onClick = [this]() {
        if (selectedClipId_ != magda::INVALID_CLIP_ID) {
            bool newState = !clipWarpToggle_.getToggleState();
            clipWarpToggle_.setToggleState(newState, juce::dontSendNotification);
            magda::ClipManager::getInstance().setClipWarpEnabled(selectedClipId_, newState);
        }
    };
    clipPropsContainer_.addChildComponent(clipWarpToggle_);

    // Auto-tempo (beat mode) toggle
    clipAutoTempoToggle_.setButtonText("BEAT");
    clipAutoTempoToggle_.setLookAndFeel(&SmallButtonLookAndFeel::getInstance());
    clipAutoTempoToggle_.setColour(juce::TextButton::buttonColourId,
                                   DarkTheme::getColour(DarkTheme::SURFACE));
    clipAutoTempoToggle_.setColour(juce::TextButton::buttonOnColourId,
                                   DarkTheme::getAccentColour().withAlpha(0.3f));
    clipAutoTempoToggle_.setColour(juce::TextButton::textColourOffId,
                                   DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
    clipAutoTempoToggle_.setColour(juce::TextButton::textColourOnId, DarkTheme::getAccentColour());
    clipAutoTempoToggle_.setClickingTogglesState(false);
    clipAutoTempoToggle_.setTooltip(
        "Lock clip to musical time (bars/beats) instead of absolute time.\n"
        "Clip length changes with tempo to maintain fixed beat length.");

    // Helper lambda: apply auto-tempo state change and sync
    auto applyAutoTempo = [this](bool enable) {
        auto* clip = magda::ClipManager::getInstance().getClip(selectedClipId_);
        if (!clip)
            return;

        double bpm = 120.0;
        if (timelineController_) {
            bpm = timelineController_->getState().tempo.bpm;
        }

        magda::ClipOperations::setAutoTempo(*clip, enable, bpm);
        magda::ClipManager::getInstance().resizeClip(selectedClipId_, clip->length, false, bpm);
        updateFromSelectedClip();
    };

    clipAutoTempoToggle_.onClick = [this, applyAutoTempo]() {
        if (selectedClipId_ == magda::INVALID_CLIP_ID)
            return;
        auto* clip = magda::ClipManager::getInstance().getClip(selectedClipId_);
        if (!clip)
            return;

        bool newState = !clip->autoTempo;

        if (newState && std::abs(clip->speedRatio - 1.0) > 0.001) {
            // Show async warning — avoid re-entrancy from synchronous modal loop
            auto clipId = selectedClipId_;
            juce::NativeMessageBox::showAsync(
                juce::MessageBoxOptions()
                    .withIconType(juce::MessageBoxIconType::WarningIcon)
                    .withTitle("Reset Time Stretch")
                    .withMessage("Auto-tempo mode requires speed ratio 1.0.\nCurrent stretch (" +
                                 juce::String(clip->speedRatio, 2) +
                                 "x) will be reset.\n\nContinue?")
                    .withButton("OK")
                    .withButton("Cancel"),
                [this, clipId, applyAutoTempo](int result) {
                    if (result == 1 && selectedClipId_ == clipId) {
                        applyAutoTempo(true);
                    }
                });
            return;
        }

        applyAutoTempo(newState);
    };
    clipPropsContainer_.addChildComponent(clipAutoTempoToggle_);

    clipStretchValue_ = std::make_unique<DraggableValueLabel>(DraggableValueLabel::Format::Raw);
    clipStretchValue_->setRange(0.25, 4.0, 1.0);
    clipStretchValue_->setSuffix("x");
    clipStretchValue_->setDrawBackground(false);
    clipStretchValue_->setDrawBorder(true);
    clipStretchValue_->setShowFillIndicator(false);
    clipStretchValue_->onValueChange = [this]() {
        if (selectedClipId_ != magda::INVALID_CLIP_ID)
            magda::ClipManager::getInstance().setSpeedRatio(selectedClipId_,
                                                            clipStretchValue_->getValue());
    };
    clipPropsContainer_.addChildComponent(*clipStretchValue_);

    // Stretch mode selector (algorithm)
    stretchModeCombo_.setColour(juce::ComboBox::backgroundColourId,
                                DarkTheme::getColour(DarkTheme::SURFACE));
    stretchModeCombo_.setColour(juce::ComboBox::textColourId, DarkTheme::getTextColour());
    stretchModeCombo_.setColour(juce::ComboBox::outlineColourId,
                                DarkTheme::getColour(DarkTheme::BORDER));
    // Mode values match TimeStretcher::Mode enum (combo ID = mode + 1)
    stretchModeCombo_.addItem("Off", 1);            // disabled = 0
    stretchModeCombo_.addItem("SoundTouch", 4);     // soundtouchNormal = 3
    stretchModeCombo_.addItem("SoundTouch HQ", 5);  // soundtouchBetter = 4
    stretchModeCombo_.setSelectedId(1, juce::dontSendNotification);
    stretchModeCombo_.onChange = [this]() {
        if (selectedClipId_ != magda::INVALID_CLIP_ID) {
            // ComboBox ID is mode+1, so subtract 1 to get actual mode
            int mode = stretchModeCombo_.getSelectedId() - 1;
            magda::ClipManager::getInstance().setTimeStretchMode(selectedClipId_, mode);
        }
    };
    clipPropsContainer_.addChildComponent(stretchModeCombo_);

    // Apply themed LookAndFeel to all inspector combo boxes
    stretchModeCombo_.setLookAndFeel(&InspectorComboBoxLookAndFeel::getInstance());
    autoPitchModeCombo_.setLookAndFeel(&InspectorComboBoxLookAndFeel::getInstance());
    launchModeCombo_.setLookAndFeel(&InspectorComboBoxLookAndFeel::getInstance());
    launchQuantizeCombo_.setLookAndFeel(&InspectorComboBoxLookAndFeel::getInstance());

    // Loop start
    clipLoopStartLabel_.setText("start", juce::dontSendNotification);
    clipLoopStartLabel_.setFont(FontManager::getInstance().getUIFont(11.0f));
    clipLoopStartLabel_.setColour(juce::Label::textColourId, DarkTheme::getSecondaryTextColour());
    clipPropsContainer_.addChildComponent(clipLoopStartLabel_);

    clipLoopStartValue_ = std::make_unique<magda::BarsBeatsTicksLabel>();
    clipLoopStartValue_->setRange(0.0, 10000.0, 0.0);
    clipLoopStartValue_->setDoubleClickResetsValue(true);
    clipLoopStartValue_->onValueChange = [this]() {
        if (selectedClipId_ == magda::INVALID_CLIP_ID)
            return;
        const auto* clip = magda::ClipManager::getInstance().getClip(selectedClipId_);
        if (!clip)
            return;

        double bpm = 120.0;
        if (timelineController_) {
            bpm = timelineController_->getState().tempo.bpm;
        }
        // Preserve current phase when moving loop start
        double currentPhase = clip->offset - clip->loopStart;
        double newLoopStartBeats = clipLoopStartValue_->getValue();
        double newLoopStartSeconds = magda::TimelineUtils::beatsToSeconds(newLoopStartBeats, bpm);
        newLoopStartSeconds = std::max(0.0, newLoopStartSeconds);
        double newOffset = newLoopStartSeconds + currentPhase;
        magda::ClipManager::getInstance().setLoopStart(selectedClipId_, newLoopStartSeconds, bpm);
        magda::ClipManager::getInstance().setOffset(selectedClipId_, newOffset);
    };
    clipPropsContainer_.addChildComponent(*clipLoopStartValue_);

    // Loop end (derived: loopStart + loopLength)
    clipLoopEndLabel_.setText("end", juce::dontSendNotification);
    clipLoopEndLabel_.setFont(FontManager::getInstance().getUIFont(11.0f));
    clipLoopEndLabel_.setColour(juce::Label::textColourId, DarkTheme::getSecondaryTextColour());
    clipPropsContainer_.addChildComponent(clipLoopEndLabel_);

    clipLoopEndValue_ = std::make_unique<magda::BarsBeatsTicksLabel>();
    clipLoopEndValue_->setRange(0.25, 10000.0, 4.0);
    clipLoopEndValue_->setDoubleClickResetsValue(false);
    clipLoopEndValue_->onValueChange = [this]() {
        if (selectedClipId_ == magda::INVALID_CLIP_ID)
            return;
        const auto* clip = magda::ClipManager::getInstance().getClip(selectedClipId_);
        if (!clip)
            return;

        double bpm = 120.0;
        if (timelineController_) {
            bpm = timelineController_->getState().tempo.bpm;
        }

        // Compute new loop length from loop end - loop start
        double newLoopEndBeats = clipLoopEndValue_->getValue();
        double loopStartBeats = magda::TimelineUtils::secondsToBeats(clip->loopStart, bpm);
        double newLoopLengthBeats = newLoopEndBeats - loopStartBeats;
        if (newLoopLengthBeats < 0.25)
            newLoopLengthBeats = 0.25;

        double newLoopLengthSeconds;
        if (clip->autoTempo && clip->sourceBPM > 0.0) {
            newLoopLengthSeconds = (newLoopLengthBeats * 60.0) / clip->sourceBPM;
        } else {
            double timelineSeconds = magda::TimelineUtils::beatsToSeconds(newLoopLengthBeats, bpm);
            newLoopLengthSeconds = timelineSeconds * clip->speedRatio;
        }

        if (clip->view == magda::ClipView::Session) {
            double clipEndSeconds = clip->length;
            double currentSourceEnd = clip->loopStart + clip->loopLength;
            bool sourceEndMatchedClipEnd = std::abs(currentSourceEnd - clipEndSeconds) < 0.001;
            double newSourceEnd = clip->loopStart + newLoopLengthSeconds;

            if (sourceEndMatchedClipEnd && newSourceEnd > clipEndSeconds) {
                magda::ClipManager::getInstance().resizeClip(selectedClipId_, newSourceEnd, false,
                                                             bpm);
            } else {
                if (newSourceEnd > clipEndSeconds) {
                    newLoopLengthSeconds = clipEndSeconds - clip->loopStart;
                }
            }
        }

        magda::ClipManager::getInstance().setLoopLength(selectedClipId_, newLoopLengthSeconds, bpm);
    };
    clipPropsContainer_.addChildComponent(*clipLoopEndValue_);

    // Loop phase (offset into loop region)
    clipLoopPhaseLabel_.setText("phase", juce::dontSendNotification);
    clipLoopPhaseLabel_.setFont(FontManager::getInstance().getUIFont(11.0f));
    clipLoopPhaseLabel_.setColour(juce::Label::textColourId, DarkTheme::getSecondaryTextColour());
    clipPropsContainer_.addChildComponent(clipLoopPhaseLabel_);

    clipLoopPhaseValue_ = std::make_unique<magda::BarsBeatsTicksLabel>();
    clipLoopPhaseValue_->setRange(0.0, 10000.0, 0.0);
    clipLoopPhaseValue_->setBarsBeatsIsPosition(false);
    clipLoopPhaseValue_->setDoubleClickResetsValue(true);
    clipLoopPhaseValue_->onValueChange = [this]() {
        if (selectedClipId_ == magda::INVALID_CLIP_ID)
            return;
        const auto* clip = magda::ClipManager::getInstance().getClip(selectedClipId_);
        if (!clip)
            return;

        double bpm = 120.0;
        if (timelineController_) {
            bpm = timelineController_->getState().tempo.bpm;
        }
        double newPhaseBeats = clipLoopPhaseValue_->getValue();
        double newPhaseSeconds = magda::TimelineUtils::beatsToSeconds(newPhaseBeats, bpm);
        newPhaseSeconds = std::max(0.0, newPhaseSeconds);
        double newOffset = clip->loopStart + newPhaseSeconds;
        magda::ClipManager::getInstance().setOffset(selectedClipId_, newOffset);
    };
    clipPropsContainer_.addChildComponent(*clipLoopPhaseValue_);
}

// ========================================================================
// Session clip launch properties
// ========================================================================

void ClipInspector::initSessionLaunchSection() {
    launchModeLabel_.setText("Launch Mode", juce::dontSendNotification);
    launchModeLabel_.setFont(FontManager::getInstance().getUIFont(11.0f));
    launchModeLabel_.setColour(juce::Label::textColourId, DarkTheme::getSecondaryTextColour());
    clipPropsContainer_.addChildComponent(launchModeLabel_);

    launchModeCombo_.addItem("Trigger", 1);
    launchModeCombo_.addItem("Toggle", 2);
    launchModeCombo_.setColour(juce::ComboBox::backgroundColourId,
                               DarkTheme::getColour(DarkTheme::SURFACE));
    launchModeCombo_.setColour(juce::ComboBox::textColourId, DarkTheme::getTextColour());
    launchModeCombo_.setColour(juce::ComboBox::outlineColourId,
                               DarkTheme::getColour(DarkTheme::SEPARATOR));
    launchModeCombo_.onChange = [this]() {
        if (selectedClipId_ != magda::INVALID_CLIP_ID) {
            auto mode = static_cast<magda::LaunchMode>(launchModeCombo_.getSelectedId() - 1);
            magda::ClipManager::getInstance().setClipLaunchMode(selectedClipId_, mode);
        }
    };
    clipPropsContainer_.addChildComponent(launchModeCombo_);

    launchQuantizeLabel_.setText("Launch Quantize", juce::dontSendNotification);
    launchQuantizeLabel_.setFont(FontManager::getInstance().getUIFont(11.0f));
    launchQuantizeLabel_.setColour(juce::Label::textColourId, DarkTheme::getSecondaryTextColour());
    clipPropsContainer_.addChildComponent(launchQuantizeLabel_);

    launchQuantizeCombo_.addItem("None", 1);
    launchQuantizeCombo_.addItem("8 Bars", 2);
    launchQuantizeCombo_.addItem("4 Bars", 3);
    launchQuantizeCombo_.addItem("2 Bars", 4);
    launchQuantizeCombo_.addItem("1 Bar", 5);
    launchQuantizeCombo_.addItem("1/2", 6);
    launchQuantizeCombo_.addItem("1/4", 7);
    launchQuantizeCombo_.addItem("1/8", 8);
    launchQuantizeCombo_.addItem("1/16", 9);
    launchQuantizeCombo_.setColour(juce::ComboBox::backgroundColourId,
                                   DarkTheme::getColour(DarkTheme::SURFACE));
    launchQuantizeCombo_.setColour(juce::ComboBox::textColourId, DarkTheme::getTextColour());
    launchQuantizeCombo_.setColour(juce::ComboBox::outlineColourId,
                                   DarkTheme::getColour(DarkTheme::SEPARATOR));
    launchQuantizeCombo_.onChange = [this]() {
        if (selectedClipId_ != magda::INVALID_CLIP_ID) {
            auto quantize =
                static_cast<magda::LaunchQuantize>(launchQuantizeCombo_.getSelectedId() - 1);
            magda::ClipManager::getInstance().setClipLaunchQuantize(selectedClipId_, quantize);
        }
    };
    clipPropsContainer_.addChildComponent(launchQuantizeCombo_);
}

// ========================================================================
// Clip properties viewport (scrollable container)
// ========================================================================

void ClipInspector::initViewport() {
    clipPropsViewport_.setViewedComponent(&clipPropsContainer_, false);
    clipPropsViewport_.setScrollBarsShown(true, false);
    addChildComponent(clipPropsViewport_);
}

// ========================================================================
// Pitch section
// ========================================================================

void ClipInspector::initPitchSection() {
    pitchSectionLabel_.setText("Pitch", juce::dontSendNotification);
    pitchSectionLabel_.setFont(FontManager::getInstance().getUIFont(11.0f));
    pitchSectionLabel_.setColour(juce::Label::textColourId, DarkTheme::getSecondaryTextColour());
    clipPropsContainer_.addChildComponent(pitchSectionLabel_);

    autoPitchToggle_.setButtonText("AUTO-PITCH");
    autoPitchToggle_.setColour(juce::TextButton::buttonColourId,
                               DarkTheme::getColour(DarkTheme::SURFACE));
    autoPitchToggle_.setColour(juce::TextButton::buttonOnColourId,
                               DarkTheme::getAccentColour().withAlpha(0.3f));
    autoPitchToggle_.setColour(juce::TextButton::textColourOffId,
                               DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
    autoPitchToggle_.setColour(juce::TextButton::textColourOnId, DarkTheme::getAccentColour());
    autoPitchToggle_.onClick = [this]() {
        if (selectedClipId_ != magda::INVALID_CLIP_ID) {
            auto* clip = magda::ClipManager::getInstance().getClip(selectedClipId_);
            if (clip) {
                magda::ClipManager::getInstance().setAutoPitch(selectedClipId_, !clip->autoPitch);
            }
        }
    };
    clipPropsContainer_.addChildComponent(autoPitchToggle_);

    autoPitchModeCombo_.setColour(juce::ComboBox::backgroundColourId,
                                  DarkTheme::getColour(DarkTheme::SURFACE));
    autoPitchModeCombo_.setColour(juce::ComboBox::textColourId, DarkTheme::getTextColour());
    autoPitchModeCombo_.setColour(juce::ComboBox::outlineColourId,
                                  DarkTheme::getColour(DarkTheme::BORDER));
    autoPitchModeCombo_.addItem("Pitch Track", 1);
    autoPitchModeCombo_.addItem("Chord Mono", 2);
    autoPitchModeCombo_.addItem("Chord Poly", 3);
    autoPitchModeCombo_.setSelectedId(1, juce::dontSendNotification);
    autoPitchModeCombo_.onChange = [this]() {
        if (selectedClipId_ != magda::INVALID_CLIP_ID) {
            int mode = autoPitchModeCombo_.getSelectedId() - 1;
            magda::ClipManager::getInstance().setAutoPitchMode(selectedClipId_, mode);
        }
    };
    clipPropsContainer_.addChildComponent(autoPitchModeCombo_);

    pitchChangeValue_ = std::make_unique<DraggableValueLabel>(DraggableValueLabel::Format::Raw);
    pitchChangeValue_->setRange(-48.0, 48.0, 0.0);
    pitchChangeValue_->setSuffix(" st");
    pitchChangeValue_->setDecimalPlaces(1);
    pitchChangeValue_->onValueChange = [this]() {
        if (selectedClipId_ != magda::INVALID_CLIP_ID)
            magda::ClipManager::getInstance().setPitchChange(
                selectedClipId_, static_cast<float>(pitchChangeValue_->getValue()));
    };
    clipPropsContainer_.addChildComponent(*pitchChangeValue_);

    transposeValue_ = std::make_unique<DraggableValueLabel>(DraggableValueLabel::Format::Integer);
    transposeValue_->setRange(-24.0, 24.0, 0.0);
    transposeValue_->setSuffix(" st");
    transposeValue_->onValueChange = [this]() {
        if (selectedClipId_ != magda::INVALID_CLIP_ID)
            magda::ClipManager::getInstance().setTranspose(
                selectedClipId_, static_cast<int>(transposeValue_->getValue()));
    };
    clipPropsContainer_.addChildComponent(*transposeValue_);
}

// ========================================================================
// Per-Clip Mix section
// ========================================================================

void ClipInspector::initMixSection() {
    clipMixSectionLabel_.setText("Mix", juce::dontSendNotification);
    clipMixSectionLabel_.setFont(FontManager::getInstance().getUIFont(11.0f));
    clipMixSectionLabel_.setColour(juce::Label::textColourId, DarkTheme::getSecondaryTextColour());
    clipPropsContainer_.addChildComponent(clipMixSectionLabel_);

    clipGainValue_ = std::make_unique<DraggableValueLabel>(DraggableValueLabel::Format::Decibels);
    clipGainValue_->setRange(-60.0, 24.0, 0.0);
    clipGainValue_->onValueChange = [this]() {
        if (selectedClipId_ != magda::INVALID_CLIP_ID)
            magda::ClipManager::getInstance().setClipGainDB(
                selectedClipId_, static_cast<float>(clipGainValue_->getValue()));
    };
    clipPropsContainer_.addChildComponent(*clipGainValue_);

    clipPanValue_ = std::make_unique<DraggableValueLabel>(DraggableValueLabel::Format::Pan);
    clipPanValue_->setRange(-1.0, 1.0, 0.0);
    clipPanValue_->onValueChange = [this]() {
        if (selectedClipId_ != magda::INVALID_CLIP_ID)
            magda::ClipManager::getInstance().setClipPan(
                selectedClipId_, static_cast<float>(clipPanValue_->getValue()));
    };
    clipPropsContainer_.addChildComponent(*clipPanValue_);
}

// ========================================================================
// Playback / Beat Detection section
// ========================================================================

void ClipInspector::initPlaybackSection() {
    beatDetectionSectionLabel_.setText("Playback", juce::dontSendNotification);
    beatDetectionSectionLabel_.setFont(FontManager::getInstance().getUIFont(11.0f));
    beatDetectionSectionLabel_.setColour(juce::Label::textColourId,
                                         DarkTheme::getSecondaryTextColour());
    clipPropsContainer_.addChildComponent(beatDetectionSectionLabel_);

    reverseToggle_.setButtonText("REVERSE");
    reverseToggle_.setLookAndFeel(&SmallButtonLookAndFeel::getInstance());
    reverseToggle_.setColour(juce::TextButton::buttonColourId,
                             DarkTheme::getColour(DarkTheme::SURFACE));
    reverseToggle_.setColour(juce::TextButton::buttonOnColourId,
                             DarkTheme::getAccentColour().withAlpha(0.3f));
    reverseToggle_.setColour(juce::TextButton::textColourOffId,
                             DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
    reverseToggle_.setColour(juce::TextButton::textColourOnId, DarkTheme::getAccentColour());
    reverseToggle_.onClick = [this]() {
        if (selectedClipId_ != magda::INVALID_CLIP_ID) {
            auto* clip = magda::ClipManager::getInstance().getClip(selectedClipId_);
            if (clip)
                magda::ClipManager::getInstance().setIsReversed(selectedClipId_, !clip->isReversed);
        }
    };
    clipPropsContainer_.addChildComponent(reverseToggle_);

    autoDetectBeatsToggle_.setButtonText("AUTO-DETECT");
    autoDetectBeatsToggle_.setColour(juce::TextButton::buttonColourId,
                                     DarkTheme::getColour(DarkTheme::SURFACE));
    autoDetectBeatsToggle_.setColour(juce::TextButton::buttonOnColourId,
                                     DarkTheme::getAccentColour().withAlpha(0.3f));
    autoDetectBeatsToggle_.setColour(juce::TextButton::textColourOffId,
                                     DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
    autoDetectBeatsToggle_.setColour(juce::TextButton::textColourOnId,
                                     DarkTheme::getAccentColour());
    autoDetectBeatsToggle_.onClick = [this]() {
        if (selectedClipId_ != magda::INVALID_CLIP_ID) {
            auto* clip = magda::ClipManager::getInstance().getClip(selectedClipId_);
            if (clip)
                magda::ClipManager::getInstance().setAutoDetectBeats(selectedClipId_,
                                                                     !clip->autoDetectBeats);
        }
    };
    clipPropsContainer_.addChildComponent(autoDetectBeatsToggle_);

    beatSensitivityValue_ =
        std::make_unique<DraggableValueLabel>(DraggableValueLabel::Format::Percentage);
    beatSensitivityValue_->setRange(0.0, 1.0, 0.5);
    beatSensitivityValue_->onValueChange = [this]() {
        if (selectedClipId_ != magda::INVALID_CLIP_ID)
            magda::ClipManager::getInstance().setBeatSensitivity(
                selectedClipId_, static_cast<float>(beatSensitivityValue_->getValue()));
    };
    clipPropsContainer_.addChildComponent(*beatSensitivityValue_);
}

// ========================================================================
// Fades section
// ========================================================================

void ClipInspector::initFadesSection() {
    fadesSectionLabel_.setText("Fades", juce::dontSendNotification);
    fadesSectionLabel_.setFont(FontManager::getInstance().getUIFont(11.0f));
    fadesSectionLabel_.setColour(juce::Label::textColourId, DarkTheme::getSecondaryTextColour());
    clipPropsContainer_.addChildComponent(fadesSectionLabel_);

    fadeInValue_ = std::make_unique<DraggableValueLabel>(DraggableValueLabel::Format::Raw);
    fadeInValue_->setRange(0.0, 30.0, 0.0);
    fadeInValue_->setSuffix(" s");
    fadeInValue_->setDecimalPlaces(3);
    fadeInValue_->onValueChange = [this]() {
        if (selectedClipId_ != magda::INVALID_CLIP_ID)
            magda::ClipManager::getInstance().setFadeIn(selectedClipId_, fadeInValue_->getValue());
    };
    clipPropsContainer_.addChildComponent(*fadeInValue_);

    fadeOutValue_ = std::make_unique<DraggableValueLabel>(DraggableValueLabel::Format::Raw);
    fadeOutValue_->setRange(0.0, 30.0, 0.0);
    fadeOutValue_->setSuffix(" s");
    fadeOutValue_->setDecimalPlaces(3);
    fadeOutValue_->onValueChange = [this]() {
        if (selectedClipId_ != magda::INVALID_CLIP_ID)
            magda::ClipManager::getInstance().setFadeOut(selectedClipId_,
                                                         fadeOutValue_->getValue());
    };
    clipPropsContainer_.addChildComponent(*fadeOutValue_);

    // Fade type icon buttons: matches AudioFadeCurve::Type (1=linear, 2=convex, 3=concave,
    // 4=sCurve)
    struct FadeTypeIcon {
        const char* name;
        const char* data;
        size_t size;
        const char* tooltip;
    };
    FadeTypeIcon fadeTypeIcons[] = {
        {"Linear", BinaryData::fade_linear_svg, BinaryData::fade_linear_svgSize, "Linear"},
        {"Convex", BinaryData::fade_convex_svg, BinaryData::fade_convex_svgSize, "Convex"},
        {"Concave", BinaryData::fade_concave_svg, BinaryData::fade_concave_svgSize, "Concave"},
        {"SCurve", BinaryData::fade_scurve_svg, BinaryData::fade_scurve_svgSize, "S-Curve"},
    };

    auto setupFadeTypeButton = [this](std::unique_ptr<magda::SvgButton>& btn,
                                      const FadeTypeIcon& icon) {
        btn = std::make_unique<magda::SvgButton>(icon.name, icon.data, icon.size);
        btn->setOriginalColor(juce::Colour(0xFFE3E3E3));
        btn->setNormalColor(DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
        btn->setHoverColor(DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
        btn->setActiveColor(DarkTheme::getColour(DarkTheme::ACCENT_BLUE));
        btn->setTooltip(icon.tooltip);
        btn->setClickingTogglesState(false);
        clipPropsContainer_.addChildComponent(*btn);
    };

    for (int i = 0; i < 4; ++i) {
        setupFadeTypeButton(fadeInTypeButtons_[i], fadeTypeIcons[i]);
        int fadeType =
            i + 1;  // AudioFadeCurve::Type is 1-based (1=linear,2=convex,3=concave,4=sCurve)
        fadeInTypeButtons_[i]->onClick = [this, i, fadeType]() {
            if (selectedClipId_ != magda::INVALID_CLIP_ID) {
                magda::ClipManager::getInstance().setFadeInType(selectedClipId_, fadeType);
                for (int j = 0; j < 4; ++j)
                    fadeInTypeButtons_[j]->setActive(j == i);
            }
        };

        setupFadeTypeButton(fadeOutTypeButtons_[i], fadeTypeIcons[i]);
        fadeOutTypeButtons_[i]->onClick = [this, i, fadeType]() {
            if (selectedClipId_ != magda::INVALID_CLIP_ID) {
                magda::ClipManager::getInstance().setFadeOutType(selectedClipId_, fadeType);
                for (int j = 0; j < 4; ++j)
                    fadeOutTypeButtons_[j]->setActive(j == i);
            }
        };
    }

    // Fade behaviour icon buttons: 0=gainFade, 1=speedRamp
    struct FadeBehaviourIcon {
        const char* name;
        const char* data;
        size_t size;
        const char* tooltip;
    };
    FadeBehaviourIcon fadeBehaviourIcons[] = {
        {"GainFade", BinaryData::fade_gain_svg, BinaryData::fade_gain_svgSize, "Gain Fade"},
        {"SpeedRamp", BinaryData::fade_speedramp_svg, BinaryData::fade_speedramp_svgSize,
         "Speed Ramp"},
    };

    auto setupFadeBehaviourButton = [this](std::unique_ptr<magda::SvgButton>& btn,
                                           const FadeBehaviourIcon& icon) {
        btn = std::make_unique<magda::SvgButton>(icon.name, icon.data, icon.size);
        btn->setOriginalColor(juce::Colour(0xFFE3E3E3));
        btn->setNormalColor(DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
        btn->setHoverColor(DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
        btn->setActiveColor(DarkTheme::getColour(DarkTheme::ACCENT_BLUE));
        btn->setTooltip(icon.tooltip);
        btn->setClickingTogglesState(false);
        clipPropsContainer_.addChildComponent(*btn);
    };

    for (int i = 0; i < 2; ++i) {
        setupFadeBehaviourButton(fadeInBehaviourButtons_[i], fadeBehaviourIcons[i]);
        fadeInBehaviourButtons_[i]->onClick = [this, i]() {
            if (selectedClipId_ != magda::INVALID_CLIP_ID) {
                magda::ClipManager::getInstance().setFadeInBehaviour(selectedClipId_, i);
                for (int j = 0; j < 2; ++j)
                    fadeInBehaviourButtons_[j]->setActive(j == i);
            }
        };

        setupFadeBehaviourButton(fadeOutBehaviourButtons_[i], fadeBehaviourIcons[i]);
        fadeOutBehaviourButtons_[i]->onClick = [this, i]() {
            if (selectedClipId_ != magda::INVALID_CLIP_ID) {
                magda::ClipManager::getInstance().setFadeOutBehaviour(selectedClipId_, i);
                for (int j = 0; j < 2; ++j)
                    fadeOutBehaviourButtons_[j]->setActive(j == i);
            }
        };
    }

    autoCrossfadeToggle_.setButtonText("AUTO-XFADE");
    autoCrossfadeToggle_.setLookAndFeel(&SmallButtonLookAndFeel::getInstance());
    autoCrossfadeToggle_.setColour(juce::TextButton::buttonColourId,
                                   DarkTheme::getColour(DarkTheme::SURFACE));
    autoCrossfadeToggle_.setColour(juce::TextButton::buttonOnColourId,
                                   DarkTheme::getAccentColour().withAlpha(0.3f));
    autoCrossfadeToggle_.setColour(juce::TextButton::textColourOffId,
                                   DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
    autoCrossfadeToggle_.setColour(juce::TextButton::textColourOnId, DarkTheme::getAccentColour());
    autoCrossfadeToggle_.onClick = [this]() {
        if (selectedClipId_ != magda::INVALID_CLIP_ID) {
            auto* clip = magda::ClipManager::getInstance().getClip(selectedClipId_);
            if (clip)
                magda::ClipManager::getInstance().setAutoCrossfade(selectedClipId_,
                                                                   !clip->autoCrossfade);
        }
    };
    clipPropsContainer_.addChildComponent(autoCrossfadeToggle_);

    // Fades collapse toggle (triangle button)
    fadesCollapseToggle_.setButtonText(juce::String::charToString(0x25BC));  // ▼ expanded
    fadesCollapseToggle_.setColour(juce::TextButton::buttonColourId,
                                   juce::Colours::transparentBlack);
    fadesCollapseToggle_.setColour(juce::TextButton::buttonOnColourId,
                                   juce::Colours::transparentBlack);
    fadesCollapseToggle_.setColour(juce::TextButton::textColourOffId,
                                   DarkTheme::getSecondaryTextColour());
    fadesCollapseToggle_.setColour(juce::TextButton::textColourOnId,
                                   DarkTheme::getSecondaryTextColour());
    fadesCollapseToggle_.setConnectedEdges(
        juce::Button::ConnectedOnLeft | juce::Button::ConnectedOnRight |
        juce::Button::ConnectedOnTop | juce::Button::ConnectedOnBottom);
    fadesCollapseToggle_.onClick = [this]() {
        fadesCollapsed_ = !fadesCollapsed_;
        fadesCollapseToggle_.setButtonText(
            juce::String::charToString(fadesCollapsed_ ? 0x25B6 : 0x25BC));  // ▶ or ▼
        resized();
    };
    clipPropsContainer_.addChildComponent(fadesCollapseToggle_);
}

// ========================================================================
// Channels section
// ========================================================================

void ClipInspector::initChannelsSection() {
    channelsSectionLabel_.setText("Channels", juce::dontSendNotification);
    channelsSectionLabel_.setFont(FontManager::getInstance().getUIFont(11.0f));
    channelsSectionLabel_.setColour(juce::Label::textColourId, DarkTheme::getSecondaryTextColour());
    clipPropsContainer_.addChildComponent(channelsSectionLabel_);

    leftChannelToggle_.setButtonText("L");
    leftChannelToggle_.setLookAndFeel(&SmallButtonLookAndFeel::getInstance());
    leftChannelToggle_.setColour(juce::TextButton::buttonColourId,
                                 DarkTheme::getColour(DarkTheme::SURFACE));
    leftChannelToggle_.setColour(juce::TextButton::buttonOnColourId,
                                 DarkTheme::getAccentColour().withAlpha(0.3f));
    leftChannelToggle_.setColour(juce::TextButton::textColourOffId,
                                 DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
    leftChannelToggle_.setColour(juce::TextButton::textColourOnId, DarkTheme::getAccentColour());
    leftChannelToggle_.onClick = [this]() {
        if (selectedClipId_ != magda::INVALID_CLIP_ID) {
            auto* clip = magda::ClipManager::getInstance().getClip(selectedClipId_);
            if (clip)
                magda::ClipManager::getInstance().setLeftChannelActive(selectedClipId_,
                                                                       !clip->leftChannelActive);
        }
    };
    clipPropsContainer_.addChildComponent(leftChannelToggle_);

    rightChannelToggle_.setButtonText("R");
    rightChannelToggle_.setLookAndFeel(&SmallButtonLookAndFeel::getInstance());
    rightChannelToggle_.setColour(juce::TextButton::buttonColourId,
                                  DarkTheme::getColour(DarkTheme::SURFACE));
    rightChannelToggle_.setColour(juce::TextButton::buttonOnColourId,
                                  DarkTheme::getAccentColour().withAlpha(0.3f));
    rightChannelToggle_.setColour(juce::TextButton::textColourOffId,
                                  DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
    rightChannelToggle_.setColour(juce::TextButton::textColourOnId, DarkTheme::getAccentColour());
    rightChannelToggle_.onClick = [this]() {
        if (selectedClipId_ != magda::INVALID_CLIP_ID) {
            auto* clip = magda::ClipManager::getInstance().getClip(selectedClipId_);
            if (clip)
                magda::ClipManager::getInstance().setRightChannelActive(selectedClipId_,
                                                                        !clip->rightChannelActive);
        }
    };
    clipPropsContainer_.addChildComponent(rightChannelToggle_);
}

}  // namespace magda::daw::ui
