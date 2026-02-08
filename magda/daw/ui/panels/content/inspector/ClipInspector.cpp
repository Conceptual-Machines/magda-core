#include "ClipInspector.hpp"

#include <cmath>

#include "../../../../audio/AudioThumbnailManager.hpp"
#include "../../../state/TimelineController.hpp"
#include "../../themes/DarkTheme.hpp"
#include "../../themes/FontManager.hpp"
#include "../../themes/InspectorComboBoxLookAndFeel.hpp"
#include "../../themes/SmallButtonLookAndFeel.hpp"
#include "../../utils/TimelineUtils.hpp"
#include "BinaryData.h"
#include "core/ClipManager.hpp"
#include "core/ClipOperations.hpp"

namespace magda::daw::ui {

ClipInspector::ClipInspector() {
    // ========================================================================
    // Clip properties section
    // ========================================================================

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

    // Clip length
    clipLengthLabel_.setText("length", juce::dontSendNotification);
    clipLengthLabel_.setFont(FontManager::getInstance().getUIFont(11.0f));
    clipLengthLabel_.setColour(juce::Label::textColourId, DarkTheme::getSecondaryTextColour());
    clipPropsContainer_.addChildComponent(clipLengthLabel_);

    clipLengthValue_ = std::make_unique<magda::BarsBeatsTicksLabel>();
    clipLengthValue_->setRange(0.0, 10000.0, 4.0);
    clipLengthValue_->setDoubleClickResetsValue(false);
    clipLengthValue_->setBarsBeatsIsPosition(false);
    clipLengthValue_->onValueChange = [this]() {
        if (selectedClipId_ == magda::INVALID_CLIP_ID)
            return;
        const auto* clip = magda::ClipManager::getInstance().getClip(selectedClipId_);
        if (!clip)
            return;

        double bpm = 120.0;
        if (timelineController_) {
            bpm = timelineController_->getState().tempo.bpm;
        }
        double newLengthBeats = clipLengthValue_->getValue();
        if (newLengthBeats < 0.0)
            newLengthBeats = 0.0;
        double newLengthSeconds = magda::TimelineUtils::beatsToSeconds(newLengthBeats, bpm);
        magda::ClipManager::getInstance().resizeClip(selectedClipId_, newLengthSeconds, false, bpm);
    };
    clipPropsContainer_.addChildComponent(*clipLengthValue_);

    // Content offset icon (MIDI only - for non-destructive trim)
    clipContentOffsetIcon_ = std::make_unique<magda::SvgButton>("Offset", BinaryData::Offset_svg,
                                                                BinaryData::Offset_svgSize);
    clipContentOffsetIcon_->setOriginalColor(juce::Colour(0xFFB3B3B3));
    clipContentOffsetIcon_->setNormalColor(DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
    clipContentOffsetIcon_->setInterceptsMouseClicks(false, false);
    clipContentOffsetIcon_->setTooltip("Content offset");
    clipPropsContainer_.addChildComponent(*clipContentOffsetIcon_);

    clipOffsetRowLabel_.setText("offset", juce::dontSendNotification);
    clipOffsetRowLabel_.setFont(FontManager::getInstance().getUIFont(11.0f));
    clipOffsetRowLabel_.setColour(juce::Label::textColourId, DarkTheme::getSecondaryTextColour());
    clipPropsContainer_.addChildComponent(clipOffsetRowLabel_);

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

    // Loop length
    clipLoopLengthLabel_.setText("length", juce::dontSendNotification);
    clipLoopLengthLabel_.setFont(FontManager::getInstance().getUIFont(11.0f));
    clipLoopLengthLabel_.setColour(juce::Label::textColourId, DarkTheme::getSecondaryTextColour());
    clipPropsContainer_.addChildComponent(clipLoopLengthLabel_);

    clipLoopLengthValue_ = std::make_unique<magda::BarsBeatsTicksLabel>();
    clipLoopLengthValue_->setRange(0.25, 10000.0, 4.0);
    clipLoopLengthValue_->setDoubleClickResetsValue(false);
    clipLoopLengthValue_->setBarsBeatsIsPosition(false);
    clipLoopLengthValue_->onValueChange = [this]() {
        if (selectedClipId_ == magda::INVALID_CLIP_ID)
            return;
        const auto* clip = magda::ClipManager::getInstance().getClip(selectedClipId_);
        if (!clip)
            return;

        double newLoopLengthBeats = clipLoopLengthValue_->getValue();
        double bpm = 120.0;
        if (timelineController_) {
            bpm = timelineController_->getState().tempo.bpm;
        }

        double newLoopLengthSeconds;
        if (clip->autoTempo && clip->sourceBPM > 0.0) {
            // AutoTempo: beats are in source BPM domain, convert directly
            newLoopLengthSeconds = (newLoopLengthBeats * 60.0) / clip->sourceBPM;
        } else {
            // Manual: convert beats to timeline seconds, then to source seconds
            double timelineSeconds = magda::TimelineUtils::beatsToSeconds(newLoopLengthBeats, bpm);
            newLoopLengthSeconds = timelineSeconds * clip->speedRatio;
        }

        if (clip->view == magda::ClipView::Session) {
            double clipEndSeconds = clip->length;
            double currentSourceEnd = clip->loopStart + clip->loopLength;

            // Check if source end was aligned with clip end before the change
            bool sourceEndMatchedClipEnd = std::abs(currentSourceEnd - clipEndSeconds) < 0.001;

            double newSourceEnd = clip->loopStart + newLoopLengthSeconds;

            if (sourceEndMatchedClipEnd && newSourceEnd > clipEndSeconds) {
                // Source end was aligned with clip end and is growing — extend clip to follow
                magda::ClipManager::getInstance().resizeClip(selectedClipId_, newSourceEnd, false,
                                                             bpm);
            } else {
                // Clamp source region so it doesn't exceed clip end
                if (newSourceEnd > clipEndSeconds) {
                    newLoopLengthSeconds = clipEndSeconds - clip->loopStart;
                }
            }
        }

        // Set loopLength (in seconds) — pass BPM to update beat values in autoTempo mode
        magda::ClipManager::getInstance().setLoopLength(selectedClipId_, newLoopLengthSeconds, bpm);
    };
    clipPropsContainer_.addChildComponent(*clipLoopLengthValue_);

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

    // ========================================================================
    // Session clip launch properties
    // ========================================================================

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

    // ========================================================================
    // Clip properties viewport (scrollable container)
    // ========================================================================

    clipPropsViewport_.setViewedComponent(&clipPropsContainer_, false);
    clipPropsViewport_.setScrollBarsShown(true, false);
    addChildComponent(clipPropsViewport_);

    // ========================================================================
    // Pitch section
    // ========================================================================

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

    // ========================================================================
    // Per-Clip Mix section
    // ========================================================================

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

    // ========================================================================
    // Playback / Beat Detection section
    // ========================================================================

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

    // ========================================================================
    // Fades section
    // ========================================================================

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

    // ========================================================================
    // Channels section
    // ========================================================================

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

ClipInspector::~ClipInspector() {
    magda::ClipManager::getInstance().removeListener(this);
}

void ClipInspector::onActivated() {
    magda::ClipManager::getInstance().addListener(this);
}

void ClipInspector::onDeactivated() {
    magda::ClipManager::getInstance().removeListener(this);
}

void ClipInspector::paint(juce::Graphics& g) {
    g.fillAll(DarkTheme::getBackgroundColour());
}

void ClipInspector::resized() {
    auto bounds = getLocalBounds().reduced(10);

    // Clip name as header with type icon (outside viewport)
    {
        const int iconSize = 18;
        const int gap = 6;
        auto headerRow = bounds.removeFromTop(24);
        clipTypeIcon_->setBounds(
            headerRow.removeFromLeft(iconSize).withSizeKeepingCentre(iconSize, iconSize));
        headerRow.removeFromLeft(gap);
        clipNameValue_.setBounds(headerRow);
    }
    bounds.removeFromTop(8);

    // Viewport takes remaining space for scrollable clip properties
    clipPropsViewport_.setBounds(bounds);

    // Layout all clip properties inside the container
    const int containerWidth = bounds.getWidth() - 12;  // Account for scrollbar
    auto cb = juce::Rectangle<int>(0, 0, containerWidth, 0);
    auto addRow = [&](int height) -> juce::Rectangle<int> {
        auto row = juce::Rectangle<int>(0, cb.getHeight(), containerWidth, height);
        cb.setHeight(cb.getHeight() + height);
        return row;
    };
    auto addSpace = [&](int height) { cb.setHeight(cb.getHeight() + height); };

    // Clear separator positions for this layout pass
    clipPropsContainer_.separatorYPositions.clear();
    auto addSeparator = [&]() {
        addSpace(4);
        clipPropsContainer_.separatorYPositions.push_back(cb.getHeight());
        addSpace(5);
    };

    const int iconSize = 22;
    const int gap = 3;
    const int labelHeight = 14;
    const int valueHeight = 22;
    int fieldWidth = (containerWidth - iconSize - gap * 3) / 3;

    // Position grid Row 1: position icon — start, end, length (always visible)
    {
        auto labelRow = addRow(labelHeight);
        labelRow.removeFromLeft(iconSize + gap);
        clipStartLabel_.setBounds(labelRow.removeFromLeft(fieldWidth));
        labelRow.removeFromLeft(gap);
        clipEndLabel_.setBounds(labelRow.removeFromLeft(fieldWidth));
        labelRow.removeFromLeft(gap);
        clipLengthLabel_.setBounds(labelRow.removeFromLeft(fieldWidth));

        auto valueRow = addRow(valueHeight);
        clipPositionIcon_->setBounds(valueRow.removeFromLeft(iconSize));
        valueRow.removeFromLeft(gap);
        clipStartValue_->setBounds(valueRow.removeFromLeft(fieldWidth));
        valueRow.removeFromLeft(gap);
        clipEndValue_->setBounds(valueRow.removeFromLeft(fieldWidth));
        valueRow.removeFromLeft(gap);
        clipLengthValue_->setBounds(valueRow.removeFromLeft(fieldWidth));
    }

    addSeparator();

    // File path label (full width)
    clipFilePathLabel_.setBounds(addRow(16));

    addSeparator();

    // Source data Row 2: loop toggle + conditional content
    if (clipLoopToggle_->isVisible()) {
        const auto* clip = magda::ClipManager::getInstance().getClip(selectedClipId_);
        bool loopOn = clip && (clip->loopEnabled || clip->view == magda::ClipView::Session);

        if (loopOn) {
            // Loop ON: loop toggle — start, length, phase
            auto labelRow = addRow(labelHeight);
            labelRow.removeFromLeft(iconSize + gap);
            clipLoopStartLabel_.setBounds(labelRow.removeFromLeft(fieldWidth));
            labelRow.removeFromLeft(gap);
            clipLoopLengthLabel_.setBounds(labelRow.removeFromLeft(fieldWidth));
            labelRow.removeFromLeft(gap);
            clipLoopPhaseLabel_.setBounds(labelRow.removeFromLeft(fieldWidth));

            auto valueRow = addRow(valueHeight);
            clipLoopToggle_->setBounds(
                valueRow.removeFromLeft(iconSize).withSizeKeepingCentre(iconSize, iconSize));
            valueRow.removeFromLeft(gap);
            clipLoopStartValue_->setBounds(valueRow.removeFromLeft(fieldWidth));
            valueRow.removeFromLeft(gap);
            clipLoopLengthValue_->setBounds(valueRow.removeFromLeft(fieldWidth));
            valueRow.removeFromLeft(gap);
            clipLoopPhaseValue_->setBounds(valueRow.removeFromLeft(fieldWidth));
        } else {
            // Loop OFF: "offset" label above, matching loop-ON label row
            auto labelRow = addRow(labelHeight);
            labelRow.removeFromLeft(iconSize + gap);
            clipOffsetRowLabel_.setBounds(labelRow.removeFromLeft(fieldWidth));

            auto valueRow = addRow(valueHeight);
            clipLoopToggle_->setBounds(
                valueRow.removeFromLeft(iconSize).withSizeKeepingCentre(iconSize, iconSize));
            valueRow.removeFromLeft(gap);
            clipContentOffsetValue_->setBounds(valueRow.removeFromLeft(fieldWidth));
        }
    }
    addSeparator();

    // 2-column grid: warp toggles | combo  /  BPM | speed/beats
    {
        const int colGap = 8;
        int halfWidth = (containerWidth - colGap) / 2;

        // Row 1: [WARP] [BEAT] centered | [stretch combo]
        if (clipWarpToggle_.isVisible() || clipAutoTempoToggle_.isVisible()) {
            auto row1 = addRow(24);
            auto left = row1.removeFromLeft(halfWidth);
            row1.removeFromLeft(colGap);
            auto right = row1;

            const int btnWidth = 46;
            const int btnGap = 4;
            int numBtns =
                (clipWarpToggle_.isVisible() ? 1 : 0) + (clipAutoTempoToggle_.isVisible() ? 1 : 0);
            int totalBtnsWidth = numBtns * btnWidth + (numBtns > 1 ? btnGap : 0);
            int btnOffset = (left.getWidth() - totalBtnsWidth) / 2;
            left.removeFromLeft(btnOffset);

            if (clipWarpToggle_.isVisible()) {
                clipWarpToggle_.setBounds(left.removeFromLeft(btnWidth).reduced(0, 1));
                left.removeFromLeft(btnGap);
            }
            if (clipAutoTempoToggle_.isVisible()) {
                clipAutoTempoToggle_.setBounds(left.removeFromLeft(btnWidth).reduced(0, 1));
            }
            if (stretchModeCombo_.isVisible()) {
                stretchModeCombo_.setBounds(right.reduced(0, 1));
            }
        }

        // Row 2: [BPM] centered | [speed OR beats]
        if (clipBpmValue_.isVisible() || (clipStretchValue_ && clipStretchValue_->isVisible()) ||
            clipBeatsLengthValue_->isVisible()) {
            addSpace(4);
            auto row2 = addRow(22);
            auto left = row2.removeFromLeft(halfWidth);
            row2.removeFromLeft(colGap);
            auto right = row2;

            if (clipBpmValue_.isVisible()) {
                int bpmWidth = 96;  // matches WARP(46) + gap(4) + BEAT(46)
                int bpmOffset = (left.getWidth() - bpmWidth) / 2;
                clipBpmValue_.setBounds(left.withX(left.getX() + bpmOffset).withWidth(bpmWidth));
            }
            if (clipStretchValue_ && clipStretchValue_->isVisible()) {
                clipStretchValue_->setBounds(right.reduced(0, 1));
            }
            if (clipBeatsLengthValue_->isVisible()) {
                clipBeatsLengthValue_->setBounds(right.reduced(0, 1));
            }
        }
    }

    // Separator: after position/warp rows, before Pitch
    if (pitchSectionLabel_.isVisible())
        addSeparator();

    // Pitch section (audio clips only)
    if (pitchSectionLabel_.isVisible()) {
        pitchSectionLabel_.setBounds(addRow(16));
        if (autoPitchToggle_.isVisible()) {
            addSpace(4);
            auto row = addRow(22);
            int halfWidth = (containerWidth - 8) / 2;
            autoPitchToggle_.setBounds(row.removeFromLeft(halfWidth).reduced(0, 1));
            row.removeFromLeft(8);
            autoPitchModeCombo_.setBounds(row.removeFromLeft(halfWidth).reduced(0, 1));
        }
        addSpace(4);
        {
            auto row = addRow(22);
            int halfWidth = (containerWidth - 8) / 2;
            pitchChangeValue_->setBounds(row.removeFromLeft(halfWidth));
            row.removeFromLeft(8);
            transposeValue_->setBounds(row.removeFromLeft(halfWidth));
        }
    }

    // Separator: between Pitch and Mix
    if (clipMixSectionLabel_.isVisible())
        addSeparator();

    // Mix section (audio clips only) — 2-column: volume/pan, reverse/LR
    if (clipMixSectionLabel_.isVisible()) {
        clipMixSectionLabel_.setBounds(addRow(16));
        addSpace(4);
        const int colGap = 8;
        int halfWidth = (containerWidth - colGap) / 2;

        // Row 1: [volume] | [pan]
        {
            auto row = addRow(22);
            clipGainValue_->setBounds(row.removeFromLeft(halfWidth));
            row.removeFromLeft(colGap);
            clipPanValue_->setBounds(row.removeFromLeft(halfWidth));
        }
        addSpace(4);
        // Row 2: [REVERSE full width]
        {
            auto row = addRow(22);
            reverseToggle_.setBounds(row.reduced(0, 1));
        }
    }

    // Separator: between Mix and Fades
    if (fadesSectionLabel_.isVisible())
        addSeparator();

    // Fades section (arrangement clips only, collapsible)
    if (fadesSectionLabel_.isVisible()) {
        {
            auto headerRow = addRow(16);
            fadesCollapseToggle_.setBounds(headerRow.removeFromLeft(16));
            fadesSectionLabel_.setBounds(headerRow);
        }
        if (!fadesCollapsed_) {
            addSpace(4);
            const int colGap = 8;
            int halfWidth = (containerWidth - colGap) / 2;

            // Row 1: [fade in] | [fade out]
            {
                auto row = addRow(22);
                fadeInValue_->setBounds(row.removeFromLeft(halfWidth));
                row.removeFromLeft(colGap);
                fadeOutValue_->setBounds(row.removeFromLeft(halfWidth));
            }
            addSpace(4);

            // Row 2: fade type buttons (4 icons each side)
            {
                auto row = addRow(24);
                auto left = row.removeFromLeft(halfWidth);
                row.removeFromLeft(colGap);
                auto right = row;

                const int btnSize = 24;
                const int btnGap = 2;
                for (int i = 0; i < 4; ++i) {
                    if (fadeInTypeButtons_[i]) {
                        fadeInTypeButtons_[i]->setBounds(left.removeFromLeft(btnSize));
                        if (i < 3)
                            left.removeFromLeft(btnGap);
                    }
                    if (fadeOutTypeButtons_[i]) {
                        fadeOutTypeButtons_[i]->setBounds(right.removeFromLeft(btnSize));
                        if (i < 3)
                            right.removeFromLeft(btnGap);
                    }
                }
            }
            addSpace(4);

            // Row 3: fade behavior buttons (2 icons each side)
            {
                auto row = addRow(24);
                auto left = row.removeFromLeft(halfWidth);
                row.removeFromLeft(colGap);
                auto right = row;

                const int btnSize = 24;
                const int btnGap = 2;
                for (int i = 0; i < 2; ++i) {
                    if (fadeInBehaviourButtons_[i]) {
                        fadeInBehaviourButtons_[i]->setBounds(left.removeFromLeft(btnSize));
                        if (i < 1)
                            left.removeFromLeft(btnGap);
                    }
                    if (fadeOutBehaviourButtons_[i]) {
                        fadeOutBehaviourButtons_[i]->setBounds(right.removeFromLeft(btnSize));
                        if (i < 1)
                            right.removeFromLeft(btnGap);
                    }
                }
            }
            addSpace(4);

            // Row 4: auto-crossfade toggle
            {
                auto row = addRow(22);
                autoCrossfadeToggle_.setBounds(row.reduced(0, 1));
            }
        }
    }

    // Separator: between Fades and Channels
    if (channelsSectionLabel_.isVisible())
        addSeparator();

    // Channels section (hidden for now, controls moved to Mix section)
    if (channelsSectionLabel_.isVisible()) {
        channelsSectionLabel_.setBounds(addRow(16));
        addSpace(4);
        const int btnWidth = 46;
        const int btnGap = 8;
        auto row = addRow(22);
        leftChannelToggle_.setBounds(row.removeFromLeft(btnWidth).reduced(0, 1));
        row.removeFromLeft(btnGap);
        rightChannelToggle_.setBounds(row.removeFromLeft(btnWidth).reduced(0, 1));
    }

    // Separator: after last visible section, before launch controls
    if (launchQuantizeLabel_.isVisible())
        addSeparator();

    // Session clip launch properties
    if (launchModeLabel_.isVisible()) {
        launchModeLabel_.setBounds(addRow(16));
        addSpace(4);
        launchModeCombo_.setBounds(addRow(22).reduced(0, 1));
    }
    if (launchQuantizeLabel_.isVisible()) {
        launchQuantizeLabel_.setBounds(addRow(16));
        addSpace(4);
        launchQuantizeCombo_.setBounds(addRow(22).reduced(0, 1));
    }

    // Set container bounds to accommodate all content
    clipPropsContainer_.setBounds(cb);
}

void ClipInspector::setSelectedClip(magda::ClipId clipId) {
    selectedClipId_ = clipId;
    updateFromSelectedClip();
}

void ClipInspector::clipsChanged() {
    updateFromSelectedClip();
}

void ClipInspector::clipPropertyChanged(magda::ClipId clipId) {
    if (clipId == selectedClipId_) {
        updateFromSelectedClip();
    }
}

void ClipInspector::clipSelectionChanged(magda::ClipId clipId) {
    setSelectedClip(clipId);
}

void ClipInspector::updateFromSelectedClip() {
    if (selectedClipId_ == magda::INVALID_CLIP_ID) {
        showClipControls(false);
        return;
    }

    // Sanitize stale audio clip values (e.g. offset past file end from old model)
    auto* mutableClip = magda::ClipManager::getInstance().getClip(selectedClipId_);
    if (mutableClip && mutableClip->type == magda::ClipType::Audio &&
        !mutableClip->audioFilePath.isEmpty()) {
        auto* thumbnail =
            magda::AudioThumbnailManager::getInstance().getThumbnail(mutableClip->audioFilePath);
        if (thumbnail) {
            double fileDur = thumbnail->getTotalLength();
            if (fileDur > 0.0) {
                bool fixed = false;
                if (mutableClip->offset > fileDur) {
                    mutableClip->offset = juce::jmin(mutableClip->offset, fileDur);
                    fixed = true;
                }
                if (mutableClip->loopStart > fileDur) {
                    mutableClip->loopStart = 0.0;
                    fixed = true;
                }
                double avail = fileDur - mutableClip->loopStart;
                if (mutableClip->loopLength > avail) {
                    mutableClip->loopLength = avail;
                    fixed = true;
                }
                if (mutableClip->offset > fileDur) {
                    mutableClip->offset = juce::jmin(mutableClip->offset, fileDur);
                    fixed = true;
                }
                if (fixed) {
                    magda::ClipManager::getInstance().forceNotifyClipPropertyChanged(
                        selectedClipId_);
                    return;  // Will be called again with fixed values
                }
            }
        }
    }

    const auto* clip = magda::ClipManager::getInstance().getClip(selectedClipId_);
    if (clip) {
        clipNameValue_.setText(clip->name, juce::dontSendNotification);

        // Update file path label (show filename only, full path in tooltip)
        if (clip->type == magda::ClipType::Audio && clip->audioFilePath.isNotEmpty()) {
            juce::File audioFile(clip->audioFilePath);
            clipFilePathLabel_.setText(audioFile.getFileName(), juce::dontSendNotification);
            clipFilePathLabel_.setTooltip(clip->audioFilePath);
        } else if (clip->type == magda::ClipType::MIDI) {
            clipFilePathLabel_.setText("(MIDI)", juce::dontSendNotification);
            clipFilePathLabel_.setTooltip("");
        } else {
            clipFilePathLabel_.setText("", juce::dontSendNotification);
            clipFilePathLabel_.setTooltip("");
        }

        // Update type icon based on clip type
        bool isAudioClip = (clip->type == magda::ClipType::Audio);
        if (isAudioClip) {
            clipTypeIcon_->updateSvgData(BinaryData::sinewave_svg, BinaryData::sinewave_svgSize);
            clipTypeIcon_->setTooltip("Audio clip");
        } else {
            clipTypeIcon_->updateSvgData(BinaryData::midi_svg, BinaryData::midi_svgSize);
            clipTypeIcon_->setTooltip("MIDI clip");
        }

        // Show BPM for audio clips (at bottom with WARP)
        if (isAudioClip) {
            double detectedBPM =
                magda::AudioThumbnailManager::getInstance().detectBPM(clip->audioFilePath);
            clipBpmValue_.setVisible(true);
            if (detectedBPM > 0.0) {
                clipBpmValue_.setText(juce::String(detectedBPM, 1) + " BPM",
                                      juce::dontSendNotification);
            } else {
                clipBpmValue_.setText(juce::String::fromUTF8("\xe2\x80\x94"),  // em dash
                                      juce::dontSendNotification);
            }
        } else {
            clipBpmValue_.setVisible(false);
        }

        // Show length in beats for audio clips with auto-tempo enabled (read-only display)
        if (isAudioClip && clip->autoTempo) {
            clipBeatsLengthValue_->setVisible(true);
            clipBeatsLengthValue_->setEnabled(true);
            clipBeatsLengthValue_->setAlpha(1.0f);
            clipBeatsLengthValue_->setValue(clip->loopLengthBeats, juce::dontSendNotification);
        } else {
            clipBeatsLengthValue_->setVisible(false);
        }

        // Get tempo from TimelineController, fallback to 120 BPM if not available
        double bpm = 120.0;
        int beatsPerBar = 4;
        if (timelineController_) {
            const auto& state = timelineController_->getState();
            bpm = state.tempo.bpm;
            beatsPerBar = state.tempo.timeSignatureNumerator;
        }

        bool isSessionClip = (clip->view == magda::ClipView::Session);

        // Update beatsPerBar on all draggable labels
        clipStartValue_->setBeatsPerBar(beatsPerBar);
        clipEndValue_->setBeatsPerBar(beatsPerBar);
        clipContentOffsetValue_->setBeatsPerBar(beatsPerBar);
        clipLoopLengthValue_->setBeatsPerBar(beatsPerBar);

        if (isSessionClip) {
            // Session clips: start is always 0, greyed out and non-interactive
            clipStartValue_->setValue(0.0, juce::dontSendNotification);
            clipStartValue_->setEnabled(false);
            clipStartValue_->setAlpha(0.4f);
            clipEndValue_->setValue(magda::TimelineUtils::secondsToBeats(clip->length, bpm),
                                    juce::dontSendNotification);
        } else {
            // Arrangement clips: start and end as positions in beats
            clipStartValue_->setEnabled(true);
            clipStartValue_->setAlpha(1.0f);

            // Use centralized methods (single source of truth)
            clipStartValue_->setValue(clip->getStartBeats(bpm), juce::dontSendNotification);
            clipEndValue_->setValue(clip->getEndBeats(bpm), juce::dontSendNotification);
        }

        // Clip length (always visible)
        clipLengthValue_->setBeatsPerBar(beatsPerBar);
        clipLengthValue_->setValue(magda::TimelineUtils::secondsToBeats(clip->length, bpm),
                                   juce::dontSendNotification);

        // Position icon visible, content offset icon hidden (replaced by grid column)
        clipPositionIcon_->setVisible(true);
        clipContentOffsetIcon_->setVisible(false);

        clipLoopToggle_->setActive(clip->loopEnabled);
        // Beat mode forces loop on — disable the toggle so user can't turn it off
        clipLoopToggle_->setEnabled(!clip->autoTempo);

        // Conditional Row 2 based on loop state
        bool loopOn = isSessionClip || clip->loopEnabled;

        if (loopOn) {
            // Loop ON: show loop start/length/phase, hide offset
            clipOffsetRowLabel_.setVisible(false);
            clipContentOffsetValue_->setVisible(false);

            clipLoopStartLabel_.setVisible(true);
            clipLoopStartValue_->setVisible(true);
            clipLoopStartValue_->setBeatsPerBar(beatsPerBar);
            double loopStartBeats = magda::TimelineUtils::secondsToBeats(clip->loopStart, bpm);
            clipLoopStartValue_->setValue(loopStartBeats, juce::dontSendNotification);
            clipLoopStartValue_->setEnabled(true);
            clipLoopStartValue_->setAlpha(1.0f);
            clipLoopStartLabel_.setAlpha(1.0f);

            // Display loop length in beats
            double loopLengthDisplayBeats;
            if (clip->autoTempo && clip->loopLengthBeats > 0.0) {
                loopLengthDisplayBeats = clip->loopLengthBeats;
            } else {
                double sourceLength =
                    clip->loopLength > 0.0 ? clip->loopLength : clip->length * clip->speedRatio;
                loopLengthDisplayBeats = magda::TimelineUtils::secondsToBeats(sourceLength, bpm);
            }
            clipLoopLengthLabel_.setVisible(true);
            clipLoopLengthValue_->setVisible(true);
            clipLoopLengthValue_->setValue(loopLengthDisplayBeats, juce::dontSendNotification);
            clipLoopLengthValue_->setEnabled(true);
            clipLoopLengthValue_->setAlpha(1.0f);
            clipLoopLengthLabel_.setAlpha(1.0f);

            clipLoopPhaseLabel_.setVisible(true);
            clipLoopPhaseValue_->setVisible(true);
            clipLoopPhaseValue_->setBeatsPerBar(beatsPerBar);
            double phaseSeconds = clip->offset - clip->loopStart;
            double phaseBeats = magda::TimelineUtils::secondsToBeats(phaseSeconds, bpm);
            clipLoopPhaseValue_->setValue(phaseBeats, juce::dontSendNotification);
            clipLoopPhaseValue_->setEnabled(true);
            clipLoopPhaseValue_->setAlpha(1.0f);
            clipLoopPhaseLabel_.setAlpha(1.0f);
        } else {
            // Loop OFF: show offset, hide loop start/length/phase
            clipOffsetRowLabel_.setVisible(true);
            clipContentOffsetValue_->setVisible(true);

            if (clip->type == magda::ClipType::MIDI) {
                clipContentOffsetValue_->setValue(clip->midiOffset, juce::dontSendNotification);
            } else if (clip->type == magda::ClipType::Audio) {
                double offsetBeats = magda::TimelineUtils::secondsToBeats(clip->offset, bpm);
                clipContentOffsetValue_->setValue(offsetBeats, juce::dontSendNotification);
            }
            clipContentOffsetValue_->setEnabled(true);
            clipContentOffsetValue_->setAlpha(1.0f);

            clipLoopStartLabel_.setVisible(false);
            clipLoopStartValue_->setVisible(false);
            clipLoopLengthLabel_.setVisible(false);
            clipLoopLengthValue_->setVisible(false);
            clipLoopPhaseLabel_.setVisible(false);
            clipLoopPhaseValue_->setVisible(false);
        }

        // Warp toggle (always visible for audio clips)
        clipWarpToggle_.setVisible(isAudioClip);
        if (isAudioClip) {
            clipWarpToggle_.setToggleState(clip->warpEnabled, juce::dontSendNotification);
        }

        // Auto-tempo toggle (always visible for audio clips)
        clipAutoTempoToggle_.setVisible(isAudioClip);
        if (isAudioClip) {
            clipAutoTempoToggle_.setToggleState(clip->autoTempo, juce::dontSendNotification);
            // Disable stretch control when auto-tempo is enabled (speedRatio must be 1.0)
            if (clip->autoTempo && clipStretchValue_) {
                clipStretchValue_->setEnabled(false);
                clipStretchValue_->setAlpha(0.4f);
            }
        }

        clipStretchValue_->setVisible(isAudioClip && !clip->autoTempo);
        stretchModeCombo_.setVisible(isAudioClip);
        if (isAudioClip) {
            clipStretchValue_->setValue(clip->speedRatio, juce::dontSendNotification);
            // Show effective stretch mode (auto-upgraded when autoTempo/warp is active)
            int effectiveMode = clip->timeStretchMode;
            if (effectiveMode == 0 && (clip->autoTempo || clip->warpEnabled ||
                                       std::abs(clip->speedRatio - 1.0) > 0.001)) {
                effectiveMode = 4;  // soundtouchBetter (defaultMode)
            }
            stretchModeCombo_.setSelectedId(effectiveMode + 1, juce::dontSendNotification);

            // Enable/disable stretch controls based on auto-tempo mode
            if (!clip->autoTempo && clipStretchValue_) {
                clipStretchValue_->setEnabled(true);
                clipStretchValue_->setAlpha(1.0f);
            }
        }

        loopColumnLabel_.setAlpha(loopOn ? 1.0f : 0.4f);

        // Session clip launch properties
        launchModeLabel_.setVisible(false);
        launchModeCombo_.setVisible(false);
        launchQuantizeLabel_.setVisible(isSessionClip);
        launchQuantizeCombo_.setVisible(isSessionClip);

        if (isSessionClip) {
            launchQuantizeCombo_.setSelectedId(static_cast<int>(clip->launchQuantize) + 1,
                                               juce::dontSendNotification);
        }

        // ====================================================================
        // New audio clip property sections
        // ====================================================================

        // Pitch section (audio clips only)
        pitchSectionLabel_.setVisible(isAudioClip);
        autoPitchToggle_.setVisible(false);     // hidden for now
        autoPitchModeCombo_.setVisible(false);  // hidden for now
        pitchChangeValue_->setVisible(isAudioClip);
        transposeValue_->setVisible(isAudioClip);
        if (isAudioClip) {
            autoPitchToggle_.setToggleState(clip->autoPitch, juce::dontSendNotification);
            autoPitchModeCombo_.setSelectedId(clip->autoPitchMode + 1, juce::dontSendNotification);
            pitchChangeValue_->setValue(clip->pitchChange, juce::dontSendNotification);
            transposeValue_->setValue(clip->transpose, juce::dontSendNotification);

            // autoPitchMode only meaningful when autoPitch is on
            autoPitchModeCombo_.setEnabled(clip->autoPitch);
            autoPitchModeCombo_.setAlpha(clip->autoPitch ? 1.0f : 0.4f);

            // transpose disabled when autoPitch is on
            transposeValue_->setEnabled(!clip->autoPitch);
            transposeValue_->setAlpha(clip->autoPitch ? 0.4f : 1.0f);
        }

        // Mix section (audio clips only) — includes Gain/Pan + Reverse/L/R
        clipMixSectionLabel_.setVisible(isAudioClip);
        clipGainValue_->setVisible(isAudioClip);
        clipPanValue_->setVisible(isAudioClip);
        reverseToggle_.setVisible(isAudioClip);
        leftChannelToggle_.setVisible(false);
        rightChannelToggle_.setVisible(false);
        if (isAudioClip) {
            clipGainValue_->setValue(clip->gainDB, juce::dontSendNotification);
            clipPanValue_->setValue(clip->pan, juce::dontSendNotification);
            reverseToggle_.setToggleState(clip->isReversed, juce::dontSendNotification);
        }

        // Playback / Beat Detection section — hidden (all controls moved or unused)
        beatDetectionSectionLabel_.setVisible(false);
        autoDetectBeatsToggle_.setVisible(false);
        beatSensitivityValue_->setVisible(false);

        // Fades section (arrangement audio clips only, hidden for session, collapsible)
        bool showFades = isAudioClip && !isSessionClip;
        bool showFadeControls = showFades && !fadesCollapsed_;
        fadesSectionLabel_.setVisible(showFades);
        fadesCollapseToggle_.setVisible(showFades);
        fadeInValue_->setVisible(showFadeControls);
        fadeOutValue_->setVisible(showFadeControls);
        for (int i = 0; i < 4; ++i) {
            fadeInTypeButtons_[i]->setVisible(showFadeControls);
            fadeOutTypeButtons_[i]->setVisible(showFadeControls);
        }
        for (int i = 0; i < 2; ++i) {
            fadeInBehaviourButtons_[i]->setVisible(showFadeControls);
            fadeOutBehaviourButtons_[i]->setVisible(showFadeControls);
        }
        autoCrossfadeToggle_.setVisible(showFadeControls);
        if (showFades) {
            fadeInValue_->setValue(clip->fadeIn, juce::dontSendNotification);
            fadeOutValue_->setValue(clip->fadeOut, juce::dontSendNotification);
            for (int i = 0; i < 4; ++i) {
                fadeInTypeButtons_[i]->setActive(i == clip->fadeInType - 1);
                fadeOutTypeButtons_[i]->setActive(i == clip->fadeOutType - 1);
            }
            for (int i = 0; i < 2; ++i) {
                fadeInBehaviourButtons_[i]->setActive(i == clip->fadeInBehaviour);
                fadeOutBehaviourButtons_[i]->setActive(i == clip->fadeOutBehaviour);
            }
            autoCrossfadeToggle_.setToggleState(clip->autoCrossfade, juce::dontSendNotification);
        }

        // Channels section label hidden (controls moved to Mix section)
        channelsSectionLabel_.setVisible(false);

        showClipControls(true);
    } else {
        showClipControls(false);
    }

    resized();
    repaint();
}

void ClipInspector::showClipControls(bool show) {
    clipNameValue_.setVisible(show);
    clipFilePathLabel_.setVisible(show);
    clipTypeIcon_->setVisible(show);
    clipPropsViewport_.setVisible(show);

    if (!show) {
        // Hide everything managed by viewport container
        clipBpmValue_.setVisible(false);
        clipBeatsLengthValue_->setVisible(false);
        clipPositionIcon_->setVisible(false);
        clipOffsetRowLabel_.setVisible(false);
        clipStartLabel_.setVisible(false);
        clipStartValue_->setVisible(false);
        clipEndLabel_.setVisible(false);
        clipEndValue_->setVisible(false);
        clipLengthLabel_.setVisible(false);
        clipLengthValue_->setVisible(false);
        clipContentOffsetValue_->setVisible(false);
        clipLoopToggle_->setVisible(false);
        clipLoopStartLabel_.setVisible(false);
        clipLoopStartValue_->setVisible(false);
        clipLoopLengthLabel_.setVisible(false);
        clipLoopLengthValue_->setVisible(false);
        clipLoopPhaseLabel_.setVisible(false);
        clipLoopPhaseValue_->setVisible(false);
        clipWarpToggle_.setVisible(false);
        clipAutoTempoToggle_.setVisible(false);
        if (clipStretchValue_)
            clipStretchValue_->setVisible(false);
        stretchModeCombo_.setVisible(false);
        launchModeLabel_.setVisible(false);
        launchModeCombo_.setVisible(false);
        launchQuantizeLabel_.setVisible(false);
        launchQuantizeCombo_.setVisible(false);

        // New sections
        pitchSectionLabel_.setVisible(false);
        autoPitchToggle_.setVisible(false);
        autoPitchModeCombo_.setVisible(false);
        pitchChangeValue_->setVisible(false);
        transposeValue_->setVisible(false);
        clipMixSectionLabel_.setVisible(false);
        clipGainValue_->setVisible(false);
        clipPanValue_->setVisible(false);
        beatDetectionSectionLabel_.setVisible(false);
        reverseToggle_.setVisible(false);
        autoDetectBeatsToggle_.setVisible(false);
        beatSensitivityValue_->setVisible(false);
        fadesSectionLabel_.setVisible(false);
        fadeInValue_->setVisible(false);
        fadeOutValue_->setVisible(false);
        for (auto& btn : fadeInTypeButtons_)
            if (btn)
                btn->setVisible(false);
        for (auto& btn : fadeOutTypeButtons_)
            if (btn)
                btn->setVisible(false);
        for (auto& btn : fadeInBehaviourButtons_)
            if (btn)
                btn->setVisible(false);
        for (auto& btn : fadeOutBehaviourButtons_)
            if (btn)
                btn->setVisible(false);
        autoCrossfadeToggle_.setVisible(false);
        fadesCollapseToggle_.setVisible(false);
        channelsSectionLabel_.setVisible(false);
        leftChannelToggle_.setVisible(false);
        rightChannelToggle_.setVisible(false);
    } else {
        // Show always-visible clip controls (viewport is shown, conditional
        // Row 2 visibility is managed by updateFromSelectedClip)
        clipPositionIcon_->setVisible(true);
        clipStartLabel_.setVisible(true);
        clipStartValue_->setVisible(true);
        clipEndLabel_.setVisible(true);
        clipEndValue_->setVisible(true);
        clipLengthLabel_.setVisible(true);
        clipLengthValue_->setVisible(true);
        clipLoopToggle_->setVisible(true);
    }

    // Unused labels/icons always hidden
    playbackColumnLabel_.setVisible(false);
    loopColumnLabel_.setVisible(false);
    clipContentOffsetIcon_->setVisible(false);
}

void ClipInspector::ClipPropsContainer::paint(juce::Graphics& g) {
    // Draw separator lines between sections
    g.setColour(DarkTheme::getColour(DarkTheme::SEPARATOR));
    for (int y : separatorYPositions) {
        g.drawHorizontalLine(y, 0.0f, static_cast<float>(getWidth()));
    }
}

}  // namespace magda::daw::ui
