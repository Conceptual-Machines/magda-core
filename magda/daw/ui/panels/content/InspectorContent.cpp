#include "InspectorContent.hpp"

#include <BinaryData.h>

#include <cmath>

#include "../../../audio/MidiBridge.hpp"
#include "../../../engine/AudioEngine.hpp"
#include "../../state/TimelineController.hpp"
#include "../../themes/DarkTheme.hpp"
#include "../../themes/FontManager.hpp"
#include "../../themes/InspectorComboBoxLookAndFeel.hpp"
#include "../../utils/TimelineUtils.hpp"
#include "audio/AudioThumbnailManager.hpp"
#include "core/MidiNoteCommands.hpp"

namespace magda::daw::ui {

InspectorContent::InspectorContent() {
    setName("Inspector");

    // Setup title
    titleLabel_.setText("Inspector", juce::dontSendNotification);
    titleLabel_.setFont(FontManager::getInstance().getUIFont(14.0f));
    titleLabel_.setColour(juce::Label::textColourId, DarkTheme::getTextColour());
    addAndMakeVisible(titleLabel_);

    // No selection label
    noSelectionLabel_.setText("No selection", juce::dontSendNotification);
    noSelectionLabel_.setFont(FontManager::getInstance().getUIFont(12.0f));
    noSelectionLabel_.setColour(juce::Label::textColourId, DarkTheme::getSecondaryTextColour());
    noSelectionLabel_.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(noSelectionLabel_);

    // ========================================================================
    // Track properties section
    // ========================================================================

    // Track name
    trackNameLabel_.setText("Name", juce::dontSendNotification);
    trackNameLabel_.setFont(FontManager::getInstance().getUIFont(11.0f));
    trackNameLabel_.setColour(juce::Label::textColourId, DarkTheme::getSecondaryTextColour());
    addChildComponent(trackNameLabel_);

    trackNameValue_.setFont(FontManager::getInstance().getUIFont(12.0f));
    trackNameValue_.setColour(juce::Label::textColourId, DarkTheme::getTextColour());
    trackNameValue_.setColour(juce::Label::backgroundColourId,
                              DarkTheme::getColour(DarkTheme::SURFACE));
    trackNameValue_.setEditable(true);
    trackNameValue_.onTextChange = [this]() {
        if (selectedTrackId_ != magda::INVALID_TRACK_ID) {
            magda::TrackManager::getInstance().setTrackName(selectedTrackId_,
                                                            trackNameValue_.getText());
        }
    };
    addChildComponent(trackNameValue_);

    // Mute button (TCP style)
    muteButton_.setButtonText("M");
    muteButton_.setConnectedEdges(juce::Button::ConnectedOnLeft | juce::Button::ConnectedOnRight |
                                  juce::Button::ConnectedOnTop | juce::Button::ConnectedOnBottom);
    muteButton_.setColour(juce::TextButton::buttonColourId,
                          DarkTheme::getColour(DarkTheme::SURFACE));
    muteButton_.setColour(juce::TextButton::buttonOnColourId,
                          DarkTheme::getColour(DarkTheme::STATUS_WARNING));
    muteButton_.setColour(juce::TextButton::textColourOffId,
                          DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
    muteButton_.setColour(juce::TextButton::textColourOnId,
                          DarkTheme::getColour(DarkTheme::BACKGROUND));
    muteButton_.setClickingTogglesState(true);
    muteButton_.onClick = [this]() {
        if (selectedTrackId_ != magda::INVALID_TRACK_ID) {
            magda::TrackManager::getInstance().setTrackMuted(selectedTrackId_,
                                                             muteButton_.getToggleState());
        }
    };
    addChildComponent(muteButton_);

    // Solo button (TCP style)
    soloButton_.setButtonText("S");
    soloButton_.setConnectedEdges(juce::Button::ConnectedOnLeft | juce::Button::ConnectedOnRight |
                                  juce::Button::ConnectedOnTop | juce::Button::ConnectedOnBottom);
    soloButton_.setColour(juce::TextButton::buttonColourId,
                          DarkTheme::getColour(DarkTheme::SURFACE));
    soloButton_.setColour(juce::TextButton::buttonOnColourId,
                          DarkTheme::getColour(DarkTheme::ACCENT_ORANGE));
    soloButton_.setColour(juce::TextButton::textColourOffId,
                          DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
    soloButton_.setColour(juce::TextButton::textColourOnId,
                          DarkTheme::getColour(DarkTheme::BACKGROUND));
    soloButton_.setClickingTogglesState(true);
    soloButton_.onClick = [this]() {
        if (selectedTrackId_ != magda::INVALID_TRACK_ID) {
            magda::TrackManager::getInstance().setTrackSoloed(selectedTrackId_,
                                                              soloButton_.getToggleState());
        }
    };
    addChildComponent(soloButton_);

    // Record button (TCP style)
    recordButton_.setButtonText("R");
    recordButton_.setConnectedEdges(juce::Button::ConnectedOnLeft | juce::Button::ConnectedOnRight |
                                    juce::Button::ConnectedOnTop | juce::Button::ConnectedOnBottom);
    recordButton_.setColour(juce::TextButton::buttonColourId,
                            DarkTheme::getColour(DarkTheme::SURFACE));
    recordButton_.setColour(juce::TextButton::buttonOnColourId,
                            DarkTheme::getColour(DarkTheme::STATUS_ERROR));  // Red when armed
    recordButton_.setColour(juce::TextButton::textColourOffId,
                            DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
    recordButton_.setColour(juce::TextButton::textColourOnId,
                            DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
    recordButton_.setClickingTogglesState(true);
    addChildComponent(recordButton_);

    // Gain label (TCP style - draggable dB display)
    gainLabel_ =
        std::make_unique<magda::DraggableValueLabel>(magda::DraggableValueLabel::Format::Decibels);
    gainLabel_->setRange(-60.0, 6.0, 0.0);  // -60 to +6 dB, default 0 dB
    gainLabel_->onValueChange = [this]() {
        if (selectedTrackId_ != magda::INVALID_TRACK_ID) {
            // Convert dB to linear gain
            double db = gainLabel_->getValue();
            float gain = (db <= -60.0f) ? 0.0f : std::pow(10.0f, static_cast<float>(db) / 20.0f);
            magda::TrackManager::getInstance().setTrackVolume(selectedTrackId_, gain);
        }
    };
    addChildComponent(*gainLabel_);

    // Pan label (TCP style - draggable L/C/R display)
    panLabel_ =
        std::make_unique<magda::DraggableValueLabel>(magda::DraggableValueLabel::Format::Pan);
    panLabel_->setRange(-1.0, 1.0, 0.0);  // -1 (L) to +1 (R), default center
    panLabel_->onValueChange = [this]() {
        if (selectedTrackId_ != magda::INVALID_TRACK_ID) {
            magda::TrackManager::getInstance().setTrackPan(
                selectedTrackId_, static_cast<float>(panLabel_->getValue()));
        }
    };
    addChildComponent(*panLabel_);

    // ========================================================================
    // Routing section (MIDI/Audio In/Out)
    // ========================================================================

    routingSectionLabel_.setText("Routing", juce::dontSendNotification);
    routingSectionLabel_.setFont(FontManager::getInstance().getUIFont(11.0f));
    routingSectionLabel_.setColour(juce::Label::textColourId, DarkTheme::getSecondaryTextColour());
    addChildComponent(routingSectionLabel_);

    // Audio input selector
    audioInSelector_ =
        std::make_unique<magda::RoutingSelector>(magda::RoutingSelector::Type::AudioIn);
    // Options will be populated from audio device manager in setAudioEngine()
    addChildComponent(*audioInSelector_);

    // Audio output selector
    audioOutSelector_ =
        std::make_unique<magda::RoutingSelector>(magda::RoutingSelector::Type::AudioOut);
    // Options will be populated from audio device manager in setAudioEngine()
    addChildComponent(*audioOutSelector_);

    // MIDI input selector
    midiInSelector_ =
        std::make_unique<magda::RoutingSelector>(magda::RoutingSelector::Type::MidiIn);
    // Options will be populated from MidiBridge in setAudioEngine()
    addChildComponent(*midiInSelector_);

    // MIDI output selector
    midiOutSelector_ =
        std::make_unique<magda::RoutingSelector>(magda::RoutingSelector::Type::MidiOut);
    // Options will be populated from MidiBridge in setAudioEngine()
    addChildComponent(*midiOutSelector_);

    // ========================================================================
    // Send/Receive section
    // ========================================================================

    sendReceiveSectionLabel_.setText("Sends / Receives", juce::dontSendNotification);
    sendReceiveSectionLabel_.setFont(FontManager::getInstance().getUIFont(11.0f));
    sendReceiveSectionLabel_.setColour(juce::Label::textColourId,
                                       DarkTheme::getSecondaryTextColour());
    addChildComponent(sendReceiveSectionLabel_);

    sendsLabel_.setText("No sends", juce::dontSendNotification);
    sendsLabel_.setFont(FontManager::getInstance().getUIFont(10.0f));
    sendsLabel_.setColour(juce::Label::textColourId, DarkTheme::getSecondaryTextColour());
    addChildComponent(sendsLabel_);

    receivesLabel_.setText("No receives", juce::dontSendNotification);
    receivesLabel_.setFont(FontManager::getInstance().getUIFont(10.0f));
    receivesLabel_.setColour(juce::Label::textColourId, DarkTheme::getSecondaryTextColour());
    addChildComponent(receivesLabel_);

    // ========================================================================
    // Clips section
    // ========================================================================

    clipsSectionLabel_.setText("Clips", juce::dontSendNotification);
    clipsSectionLabel_.setFont(FontManager::getInstance().getUIFont(11.0f));
    clipsSectionLabel_.setColour(juce::Label::textColourId, DarkTheme::getSecondaryTextColour());
    addChildComponent(clipsSectionLabel_);

    clipCountLabel_.setText("0 clips", juce::dontSendNotification);
    clipCountLabel_.setFont(FontManager::getInstance().getUIFont(12.0f));
    clipCountLabel_.setColour(juce::Label::textColourId, DarkTheme::getTextColour());
    addChildComponent(clipCountLabel_);

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
    clipBpmValue_.setJustificationType(juce::Justification::centredLeft);
    clipPropsContainer_.addChildComponent(clipBpmValue_);

    // Length in beats (shown next to BPM when auto-tempo is enabled)
    clipBeatsLengthValue_ = std::make_unique<DraggableValueLabel>(DraggableValueLabel::Format::Raw);
    clipBeatsLengthValue_->setRange(0.25, 128.0, 4.0);  // Min 0.25 beats, max 128 beats
    clipBeatsLengthValue_->setSuffix(" beats");
    clipBeatsLengthValue_->setDecimalPlaces(2);
    clipBeatsLengthValue_->setSnapToInteger(true);
    clipBeatsLengthValue_->setDrawBackground(false);
    clipBeatsLengthValue_->setDrawBorder(false);
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
    clipWarpToggle_ =
        std::make_unique<magda::SvgButton>("Warp", BinaryData::pin_svg, BinaryData::pin_svgSize);
    clipWarpToggle_->setOriginalColor(juce::Colour(0xFFB3B3B3));
    clipWarpToggle_->setNormalColor(DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
    clipWarpToggle_->setHoverColor(DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
    clipWarpToggle_->setActiveColor(DarkTheme::getAccentColour());
    clipWarpToggle_->setClickingTogglesState(false);
    clipWarpToggle_->onClick = [this]() {
        if (selectedClipId_ != magda::INVALID_CLIP_ID) {
            bool newState = !clipWarpToggle_->isActive();
            clipWarpToggle_->setActive(newState);
            magda::ClipManager::getInstance().setClipWarpEnabled(selectedClipId_, newState);
        }
    };
    clipPropsContainer_.addChildComponent(*clipWarpToggle_);

    // Auto-tempo (musical mode) toggle (note icon)
    clipAutoTempoToggle_ = std::make_unique<magda::SvgButton>("Musical", BinaryData::note_svg,
                                                              BinaryData::note_svgSize);
    clipAutoTempoToggle_->setOriginalColor(juce::Colour(0xFFB3B3B3));
    clipAutoTempoToggle_->setNormalColor(DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
    clipAutoTempoToggle_->setHoverColor(DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
    clipAutoTempoToggle_->setActiveColor(DarkTheme::getAccentColour());
    clipAutoTempoToggle_->setClickingTogglesState(false);
    clipAutoTempoToggle_->setTooltip(
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

    clipAutoTempoToggle_->onClick = [this, applyAutoTempo]() {
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
    clipPropsContainer_.addChildComponent(*clipAutoTempoToggle_);

    clipStretchValue_ = std::make_unique<DraggableValueLabel>(DraggableValueLabel::Format::Raw);
    clipStretchValue_->setRange(0.25, 4.0, 1.0);
    clipStretchValue_->setSuffix("x");
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

    // ========================================================================
    // Note properties section
    // ========================================================================

    // Note count (shown when multiple notes selected)
    noteCountLabel_.setFont(FontManager::getInstance().getUIFont(12.0f));
    noteCountLabel_.setColour(juce::Label::textColourId, DarkTheme::getTextColour());
    addChildComponent(noteCountLabel_);

    // Note pitch
    notePitchLabel_.setText("Pitch", juce::dontSendNotification);
    notePitchLabel_.setFont(FontManager::getInstance().getUIFont(11.0f));
    notePitchLabel_.setColour(juce::Label::textColourId, DarkTheme::getSecondaryTextColour());
    addChildComponent(notePitchLabel_);

    notePitchValue_ =
        std::make_unique<magda::DraggableValueLabel>(magda::DraggableValueLabel::Format::MidiNote);
    notePitchValue_->setRange(0.0, 127.0, 60.0);  // MIDI note range
    notePitchValue_->onValueChange = [this]() {
        if (noteSelection_.isValid() && noteSelection_.isSingleNote()) {
            const auto* clip = magda::ClipManager::getInstance().getClip(noteSelection_.clipId);
            if (clip && noteSelection_.noteIndices[0] < clip->midiNotes.size()) {
                const auto& note = clip->midiNotes[noteSelection_.noteIndices[0]];
                int newPitch = static_cast<int>(notePitchValue_->getValue());
                auto cmd = std::make_unique<magda::MoveMidiNoteCommand>(
                    noteSelection_.clipId, noteSelection_.noteIndices[0], note.startBeat, newPitch);
                magda::UndoManager::getInstance().executeCommand(std::move(cmd));
            }
        }
    };
    addChildComponent(*notePitchValue_);

    // Note velocity
    noteVelocityLabel_.setText("Velocity", juce::dontSendNotification);
    noteVelocityLabel_.setFont(FontManager::getInstance().getUIFont(11.0f));
    noteVelocityLabel_.setColour(juce::Label::textColourId, DarkTheme::getSecondaryTextColour());
    addChildComponent(noteVelocityLabel_);

    noteVelocityValue_ =
        std::make_unique<magda::DraggableValueLabel>(magda::DraggableValueLabel::Format::Integer);
    noteVelocityValue_->setRange(1.0, 127.0, 100.0);
    noteVelocityValue_->onValueChange = [this]() {
        if (noteSelection_.isValid() && noteSelection_.isSingleNote()) {
            int newVelocity = static_cast<int>(noteVelocityValue_->getValue());
            auto cmd = std::make_unique<magda::SetMidiNoteVelocityCommand>(
                noteSelection_.clipId, noteSelection_.noteIndices[0], newVelocity);
            magda::UndoManager::getInstance().executeCommand(std::move(cmd));
        }
    };
    addChildComponent(*noteVelocityValue_);

    // Note start
    noteStartLabel_.setText("Start", juce::dontSendNotification);
    noteStartLabel_.setFont(FontManager::getInstance().getUIFont(11.0f));
    noteStartLabel_.setColour(juce::Label::textColourId, DarkTheme::getSecondaryTextColour());
    addChildComponent(noteStartLabel_);

    noteStartValue_.setFont(FontManager::getInstance().getUIFont(12.0f));
    noteStartValue_.setColour(juce::Label::textColourId, DarkTheme::getTextColour());
    addChildComponent(noteStartValue_);

    // Note length
    noteLengthLabel_.setText("Length", juce::dontSendNotification);
    noteLengthLabel_.setFont(FontManager::getInstance().getUIFont(11.0f));
    noteLengthLabel_.setColour(juce::Label::textColourId, DarkTheme::getSecondaryTextColour());
    addChildComponent(noteLengthLabel_);

    noteLengthValue_ =
        std::make_unique<magda::DraggableValueLabel>(magda::DraggableValueLabel::Format::Beats);
    noteLengthValue_->setRange(0.0625, 16.0, 1.0);  // 1/16 note to 16 beats
    noteLengthValue_->onValueChange = [this]() {
        if (noteSelection_.isValid() && noteSelection_.isSingleNote()) {
            double newLength = noteLengthValue_->getValue();
            auto cmd = std::make_unique<magda::ResizeMidiNoteCommand>(
                noteSelection_.clipId, noteSelection_.noteIndices[0], newLength);
            magda::UndoManager::getInstance().executeCommand(std::move(cmd));
        }
    };
    addChildComponent(*noteLengthValue_);

    // ========================================================================
    // Chain node properties section
    // ========================================================================

    chainNodeTypeLabel_.setFont(FontManager::getInstance().getUIFont(11.0f));
    chainNodeTypeLabel_.setColour(juce::Label::textColourId, DarkTheme::getSecondaryTextColour());
    addChildComponent(chainNodeTypeLabel_);

    chainNodeNameLabel_.setText("Name", juce::dontSendNotification);
    chainNodeNameLabel_.setFont(FontManager::getInstance().getUIFont(11.0f));
    chainNodeNameLabel_.setColour(juce::Label::textColourId, DarkTheme::getSecondaryTextColour());
    addChildComponent(chainNodeNameLabel_);

    chainNodeNameValue_.setFont(FontManager::getInstance().getUIFont(12.0f));
    chainNodeNameValue_.setColour(juce::Label::textColourId, DarkTheme::getTextColour());
    addChildComponent(chainNodeNameValue_);

    // ========================================================================
    // Device parameters section
    // ========================================================================

    deviceParamsLabel_.setText("Parameters", juce::dontSendNotification);
    deviceParamsLabel_.setFont(FontManager::getInstance().getUIFont(11.0f));
    deviceParamsLabel_.setColour(juce::Label::textColourId, DarkTheme::getSecondaryTextColour());
    addChildComponent(deviceParamsLabel_);

    deviceParamsViewport_.setViewedComponent(&deviceParamsContainer_, false);
    deviceParamsViewport_.setScrollBarsShown(true, false);
    addChildComponent(deviceParamsViewport_);

    // Register as listeners
    magda::TrackManager::getInstance().addListener(this);
    magda::ClipManager::getInstance().addListener(this);
    magda::SelectionManager::getInstance().addListener(this);

    // Check if there's already a selection
    currentSelectionType_ = magda::SelectionManager::getInstance().getSelectionType();
    selectedTrackId_ = magda::SelectionManager::getInstance().getSelectedTrack();
    selectedClipId_ = magda::SelectionManager::getInstance().getSelectedClip();
    updateSelectionDisplay();
}

InspectorContent::~InspectorContent() {
    stretchModeCombo_.setLookAndFeel(nullptr);
    autoPitchModeCombo_.setLookAndFeel(nullptr);
    launchModeCombo_.setLookAndFeel(nullptr);
    launchQuantizeCombo_.setLookAndFeel(nullptr);

    if (timelineController_)
        timelineController_->removeListener(this);
    magda::TrackManager::getInstance().removeListener(this);
    magda::ClipManager::getInstance().removeListener(this);
    magda::SelectionManager::getInstance().removeListener(this);
}

void InspectorContent::setTimelineController(magda::TimelineController* controller) {
    if (timelineController_)
        timelineController_->removeListener(this);
    timelineController_ = controller;
    if (timelineController_) {
        timelineController_->addListener(this);
        DBG("InspectorContent: registered as TimelineStateListener");
    }
    // Refresh display with new tempo info if a clip is selected
    if (currentSelectionType_ == magda::SelectionType::Clip) {
        updateFromSelectedClip();
    }
}

void InspectorContent::setAudioEngine(magda::AudioEngine* engine) {
    audioEngine_ = engine;
    // Populate routing selectors with real device options
    if (audioEngine_) {
        populateRoutingSelectors();
    }
    // Note: We now receive routing changes via trackPropertyChanged() from TrackManager
    // instead of listening to MidiBridge directly
}

void InspectorContent::ClipPropsContainer::paint(juce::Graphics& g) {
    g.setColour(DarkTheme::getColour(DarkTheme::SEPARATOR));
    for (int y : separatorYPositions)
        g.fillRect(4, y, getWidth() - 8, 1);
}

void InspectorContent::paint(juce::Graphics& g) {
    g.fillAll(DarkTheme::getPanelBackgroundColour());
}

void InspectorContent::resized() {
    auto bounds = getLocalBounds().reduced(10);

    // Show "Inspector" title for non-clip selections
    if (currentSelectionType_ != magda::SelectionType::Clip) {
        titleLabel_.setVisible(true);
        titleLabel_.setBounds(bounds.removeFromTop(24));
        bounds.removeFromTop(8);  // Spacing
    } else {
        // For clip selection, hide title - clip name is the header
        titleLabel_.setVisible(false);
    }

    if (currentSelectionType_ == magda::SelectionType::None) {
        // Center the no-selection label
        noSelectionLabel_.setBounds(bounds);
    } else if (currentSelectionType_ == magda::SelectionType::Track) {
        // Track properties layout (TCP style)
        trackNameLabel_.setBounds(bounds.removeFromTop(16));
        trackNameValue_.setBounds(bounds.removeFromTop(24));
        bounds.removeFromTop(12);

        // M S R buttons row
        auto buttonRow = bounds.removeFromTop(24);
        const int buttonSize = 24;
        const int buttonGap = 2;
        muteButton_.setBounds(buttonRow.removeFromLeft(buttonSize));
        buttonRow.removeFromLeft(buttonGap);
        soloButton_.setBounds(buttonRow.removeFromLeft(buttonSize));
        buttonRow.removeFromLeft(buttonGap);
        recordButton_.setBounds(buttonRow.removeFromLeft(buttonSize));
        bounds.removeFromTop(12);

        // Gain and Pan on same row (TCP style draggable labels)
        auto mixRow = bounds.removeFromTop(20);
        const int labelWidth = 50;
        const int labelGap = 8;
        gainLabel_->setBounds(mixRow.removeFromLeft(labelWidth));
        mixRow.removeFromLeft(labelGap);
        panLabel_->setBounds(mixRow.removeFromLeft(labelWidth));
        bounds.removeFromTop(16);

        // Routing section
        routingSectionLabel_.setBounds(bounds.removeFromTop(16));
        bounds.removeFromTop(4);

        const int selectorWidth = 55;
        const int selectorHeight = 18;
        const int selectorGap = 4;

        // Audio In/Out row
        auto audioRow = bounds.removeFromTop(selectorHeight);
        audioInSelector_->setBounds(audioRow.removeFromLeft(selectorWidth));
        audioRow.removeFromLeft(selectorGap);
        audioOutSelector_->setBounds(audioRow.removeFromLeft(selectorWidth));
        bounds.removeFromTop(4);

        // MIDI In/Out row
        auto midiRow = bounds.removeFromTop(selectorHeight);
        midiInSelector_->setBounds(midiRow.removeFromLeft(selectorWidth));
        midiRow.removeFromLeft(selectorGap);
        midiOutSelector_->setBounds(midiRow.removeFromLeft(selectorWidth));
        bounds.removeFromTop(16);

        // Send/Receive section
        sendReceiveSectionLabel_.setBounds(bounds.removeFromTop(16));
        bounds.removeFromTop(4);
        sendsLabel_.setBounds(bounds.removeFromTop(16));
        receivesLabel_.setBounds(bounds.removeFromTop(16));
        bounds.removeFromTop(16);

        // Clips section
        clipsSectionLabel_.setBounds(bounds.removeFromTop(16));
        bounds.removeFromTop(4);
        clipCountLabel_.setBounds(bounds.removeFromTop(20));
    } else if (currentSelectionType_ == magda::SelectionType::Clip) {
        // Clip properties layout - clip name as header with type icon (outside viewport)
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
                // Loop OFF: loop toggle — offset only (first column)
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
        addSpace(4);

        // Row 1: WARP icon | MUSICAL icon | stretch mode dropdown
        if (clipWarpToggle_->isVisible() || clipAutoTempoToggle_->isVisible()) {
            auto row1 = addRow(24);
            if (clipWarpToggle_->isVisible()) {
                clipWarpToggle_->setBounds(
                    row1.removeFromLeft(iconSize).withSizeKeepingCentre(iconSize, iconSize));
                row1.removeFromLeft(4);
            }
            if (clipAutoTempoToggle_->isVisible()) {
                clipAutoTempoToggle_->setBounds(
                    row1.removeFromLeft(iconSize).withSizeKeepingCentre(iconSize, iconSize));
                row1.removeFromLeft(8);
            }
            if (stretchModeCombo_.isVisible()) {
                stretchModeCombo_.setBounds(row1.removeFromLeft(100).reduced(0, 1));
            }
            addSpace(4);
        }

        // Row 2: BPM | speed OR beats
        if (clipBpmValue_.isVisible() || (clipStretchValue_ && clipStretchValue_->isVisible()) ||
            clipBeatsLengthValue_->isVisible()) {
            auto row2 = addRow(22);
            if (clipBpmValue_.isVisible()) {
                clipBpmValue_.setBounds(row2.removeFromLeft(70));
                row2.removeFromLeft(8);
            }
            if (clipStretchValue_ && clipStretchValue_->isVisible()) {
                clipStretchValue_->setBounds(row2.removeFromLeft(60).reduced(0, 1));
            }
            if (clipBeatsLengthValue_->isVisible()) {
                clipBeatsLengthValue_->setBounds(row2.removeFromLeft(80));
            }
        }

        // Separator: after position/warp rows, before Pitch
        if (pitchSectionLabel_.isVisible())
            addSeparator();

        // Pitch section (audio clips only)
        if (pitchSectionLabel_.isVisible()) {
            pitchSectionLabel_.setBounds(addRow(16));
            addSpace(4);
            {
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

        // Mix section (audio clips only) — includes Gain/Pan + Reverse/L/R
        if (clipMixSectionLabel_.isVisible()) {
            clipMixSectionLabel_.setBounds(addRow(16));
            addSpace(4);
            {
                auto row = addRow(22);
                int halfWidth = (containerWidth - 8) / 2;
                clipGainValue_->setBounds(row.removeFromLeft(halfWidth));
                row.removeFromLeft(8);
                clipPanValue_->setBounds(row.removeFromLeft(halfWidth));
            }
            addSpace(4);
            {
                auto row = addRow(22);
                reverseToggle_.setBounds(row.removeFromLeft(70).reduced(0, 1));
                row.removeFromLeft(8);
                leftChannelToggle_.setBounds(row.removeFromLeft(30).reduced(0, 1));
                row.removeFromLeft(4);
                rightChannelToggle_.setBounds(row.removeFromLeft(30).reduced(0, 1));
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
                {
                    auto row = addRow(22);
                    int halfWidth = (containerWidth - 8) / 2;
                    fadeInValue_->setBounds(row.removeFromLeft(halfWidth));
                    row.removeFromLeft(8);
                    fadeOutValue_->setBounds(row.removeFromLeft(halfWidth));
                }
                addSpace(4);
                {
                    auto row = addRow(22);
                    row.removeFromLeft(4);   // left padding
                    row.removeFromRight(4);  // right padding
                    int halfWidth = (row.getWidth() - 8) / 2;
                    // Fade-in type buttons (4 icons in left half)
                    auto leftHalf = row.removeFromLeft(halfWidth);
                    int btnSize = juce::jmin(20, (leftHalf.getWidth() - 6) / 4);
                    for (int i = 0; i < 4; ++i) {
                        fadeInTypeButtons_[i]->setBounds(
                            leftHalf.removeFromLeft(btnSize).reduced(1));
                        if (i < 3)
                            leftHalf.removeFromLeft(2);
                    }
                    row.removeFromLeft(8);
                    // Fade-out type buttons (4 icons in right half)
                    auto rightHalf = row.removeFromLeft(halfWidth);
                    for (int i = 0; i < 4; ++i) {
                        fadeOutTypeButtons_[i]->setBounds(
                            rightHalf.removeFromLeft(btnSize).reduced(1));
                        if (i < 3)
                            rightHalf.removeFromLeft(2);
                    }
                }
                addSpace(4);
                {
                    auto row = addRow(22);
                    row.removeFromLeft(4);   // left padding
                    row.removeFromRight(4);  // right padding
                    int halfWidth = (row.getWidth() - 8) / 2;
                    // Fade-in behaviour buttons (2 icons in left half)
                    auto leftHalf2 = row.removeFromLeft(halfWidth);
                    int behBtnSize = juce::jmin(20, (leftHalf2.getWidth() - 2) / 2);
                    for (int i = 0; i < 2; ++i) {
                        fadeInBehaviourButtons_[i]->setBounds(
                            leftHalf2.removeFromLeft(behBtnSize).reduced(1));
                        if (i < 1)
                            leftHalf2.removeFromLeft(2);
                    }
                    row.removeFromLeft(8);
                    // Fade-out behaviour buttons (2 icons in right half)
                    auto rightHalf2 = row.removeFromLeft(halfWidth);
                    for (int i = 0; i < 2; ++i) {
                        fadeOutBehaviourButtons_[i]->setBounds(
                            rightHalf2.removeFromLeft(behBtnSize).reduced(1));
                        if (i < 1)
                            rightHalf2.removeFromLeft(2);
                    }
                }
                addSpace(4);
                autoCrossfadeToggle_.setBounds(addRow(22).removeFromLeft(100).reduced(0, 1));
            }
        }

        // Separator: after Fades, before Launch (session clips)
        if (launchQuantizeLabel_.isVisible())
            addSeparator();

        // Session clip launch properties (only for session clips)
        if (launchQuantizeLabel_.isVisible()) {
            launchQuantizeLabel_.setBounds(addRow(16));
            launchQuantizeCombo_.setBounds(addRow(24));
            addSpace(8);
        }

        // Set container size to fit all laid-out content
        clipPropsContainer_.setSize(containerWidth, cb.getHeight());

    } else if (currentSelectionType_ == magda::SelectionType::Note) {
        // Note properties layout
        if (noteSelection_.getCount() > 1) {
            // Multiple notes selected - show count
            noteCountLabel_.setBounds(bounds.removeFromTop(24));
            bounds.removeFromTop(12);
        }

        // Pitch
        notePitchLabel_.setBounds(bounds.removeFromTop(16));
        notePitchValue_->setBounds(bounds.removeFromTop(24));
        bounds.removeFromTop(12);

        // Velocity
        noteVelocityLabel_.setBounds(bounds.removeFromTop(16));
        noteVelocityValue_->setBounds(bounds.removeFromTop(24));
        bounds.removeFromTop(12);

        // Start (read-only for now)
        noteStartLabel_.setBounds(bounds.removeFromTop(16));
        noteStartValue_.setBounds(bounds.removeFromTop(20));
        bounds.removeFromTop(12);

        // Length
        noteLengthLabel_.setBounds(bounds.removeFromTop(16));
        noteLengthValue_->setBounds(bounds.removeFromTop(24));
    } else if (currentSelectionType_ == magda::SelectionType::ChainNode) {
        // Chain node properties layout
        chainNodeTypeLabel_.setBounds(bounds.removeFromTop(20));
        bounds.removeFromTop(12);

        chainNodeNameLabel_.setBounds(bounds.removeFromTop(16));
        chainNodeNameValue_.setBounds(bounds.removeFromTop(24));
        bounds.removeFromTop(16);

        // Device parameters section (if visible)
        if (deviceParamsLabel_.isVisible()) {
            auto labelBounds = bounds.removeFromTop(16);
            deviceParamsLabel_.setBounds(labelBounds);
            DBG("InspectorContent::resized - deviceParamsLabel bounds: " << labelBounds.toString());

            bounds.removeFromTop(4);

            // Viewport takes remaining space
            deviceParamsViewport_.setBounds(bounds);
            DBG("InspectorContent::resized - deviceParamsViewport bounds: "
                << bounds.toString() << " container size: " << deviceParamsContainer_.getWidth()
                << "x" << deviceParamsContainer_.getHeight());
        }
    }
}

void InspectorContent::onActivated() {
    // Refresh from current selection
    currentSelectionType_ = magda::SelectionManager::getInstance().getSelectionType();
    selectedTrackId_ = magda::SelectionManager::getInstance().getSelectedTrack();
    selectedClipId_ = magda::SelectionManager::getInstance().getSelectedClip();
    updateSelectionDisplay();
}

void InspectorContent::onDeactivated() {
    // Nothing to do
}

// ============================================================================
// TrackManagerListener
// ============================================================================

void InspectorContent::tracksChanged() {
    // Track may have been deleted
    if (selectedTrackId_ != magda::INVALID_TRACK_ID) {
        const auto* track = magda::TrackManager::getInstance().getTrack(selectedTrackId_);
        if (!track) {
            selectedTrackId_ = magda::INVALID_TRACK_ID;
            updateSelectionDisplay();
        }
    }
}

void InspectorContent::trackPropertyChanged(int trackId) {
    if (static_cast<magda::TrackId>(trackId) == selectedTrackId_) {
        updateFromSelectedTrack();
    }
}

void InspectorContent::trackSelectionChanged(magda::TrackId trackId) {
    if (currentSelectionType_ == magda::SelectionType::Track) {
        selectedTrackId_ = trackId;
        updateFromSelectedTrack();
    }
}

void InspectorContent::deviceParameterChanged(magda::DeviceId deviceId, int paramIndex,
                                              float newValue) {
    // Check if this parameter belongs to the currently selected device
    if (!selectedChainNode_.isValid()) {
        return;
    }

    // Get the device ID from the current selection
    magda::DeviceId selectedDeviceId = magda::INVALID_DEVICE_ID;

    if (selectedChainNode_.getType() == magda::ChainNodeType::TopLevelDevice) {
        selectedDeviceId = selectedChainNode_.topLevelDeviceId;
    } else if (selectedChainNode_.getType() == magda::ChainNodeType::Device) {
        auto resolved = magda::TrackManager::getInstance().resolvePath(selectedChainNode_);
        if (resolved.valid && resolved.device) {
            selectedDeviceId = resolved.device->id;
        }
    }

    // If this is the selected device, update the UI
    if (selectedDeviceId == deviceId) {
        if (paramIndex >= 0 && paramIndex < static_cast<int>(deviceParamControls_.size())) {
            auto* control = deviceParamControls_[paramIndex].get();
            if (control) {
                // Update slider without triggering callback
                control->slider.setValue(newValue, juce::dontSendNotification);

                // Update value label
                const auto* device = magda::TrackManager::getInstance().getDevice(
                    selectedChainNode_.trackId, deviceId);
                if (device && paramIndex < static_cast<int>(device->parameters.size())) {
                    const auto& param = device->parameters[paramIndex];
                    juce::String valueText = juce::String(newValue, 2);
                    if (param.unit.isNotEmpty()) {
                        valueText += " " + param.unit;
                    }
                    control->valueLabel.setText(valueText, juce::dontSendNotification);
                }
            }
        }
    }
}

// ============================================================================
// ClipManagerListener
// ============================================================================

void InspectorContent::clipsChanged() {
    // Clip may have been deleted
    if (selectedClipId_ != magda::INVALID_CLIP_ID) {
        const auto* clip = magda::ClipManager::getInstance().getClip(selectedClipId_);
        if (!clip) {
            selectedClipId_ = magda::INVALID_CLIP_ID;
            updateSelectionDisplay();
        }
    }
}

void InspectorContent::clipPropertyChanged(magda::ClipId clipId) {
    if (clipId == selectedClipId_) {
        updateFromSelectedClip();
    }
}

void InspectorContent::clipSelectionChanged(magda::ClipId clipId) {
    if (currentSelectionType_ == magda::SelectionType::Clip) {
        selectedClipId_ = clipId;
        updateFromSelectedClip();
    }
}

// ============================================================================
// TimelineStateListener

void InspectorContent::timelineStateChanged(const magda::TimelineState& state,
                                            magda::ChangeFlags changes) {
    if (magda::hasFlag(changes, magda::ChangeFlags::Tempo)) {
        DBG("InspectorContent::timelineStateChanged(Tempo) - bpm="
            << state.tempo.bpm << ", selectionType=" << static_cast<int>(currentSelectionType_));
        if (currentSelectionType_ == magda::SelectionType::Clip) {
            DBG("  -> updating clip display");
            updateFromSelectedClip();
        }
    }
}

// ============================================================================
// SelectionManagerListener
// ============================================================================

void InspectorContent::selectionTypeChanged(magda::SelectionType newType) {
    currentSelectionType_ = newType;

    // Update the appropriate selection ID
    switch (newType) {
        case magda::SelectionType::Track:
            selectedTrackId_ = magda::SelectionManager::getInstance().getSelectedTrack();
            selectedClipId_ = magda::INVALID_CLIP_ID;
            noteSelection_ = magda::NoteSelection{};
            break;

        case magda::SelectionType::Clip:
            selectedClipId_ = magda::SelectionManager::getInstance().getSelectedClip();
            selectedTrackId_ = magda::INVALID_TRACK_ID;
            noteSelection_ = magda::NoteSelection{};
            break;

        case magda::SelectionType::Note:
            noteSelection_ = magda::SelectionManager::getInstance().getNoteSelection();
            selectedTrackId_ = magda::INVALID_TRACK_ID;
            selectedClipId_ = magda::INVALID_CLIP_ID;
            break;

        case magda::SelectionType::Device:
        case magda::SelectionType::ChainNode: {
            // Get track ID from the chain node selection
            const auto& nodePath = magda::SelectionManager::getInstance().getSelectedChainNode();
            selectedTrackId_ = nodePath.trackId;
            selectedClipId_ = magda::INVALID_CLIP_ID;
            noteSelection_ = magda::NoteSelection{};
            break;
        }

        default:
            selectedTrackId_ = magda::INVALID_TRACK_ID;
            selectedClipId_ = magda::INVALID_CLIP_ID;
            noteSelection_ = magda::NoteSelection{};
            break;
    }

    updateSelectionDisplay();
}

void InspectorContent::chainNodeSelectionChanged(const magda::ChainNodePath& path) {
    DBG("InspectorContent::chainNodeSelectionChanged - " + path.toString() +
        " valid=" + juce::String(path.isValid() ? 1 : 0));
    // Store the selected chain node and update display
    selectedChainNode_ = path;
    if (path.isValid()) {
        selectedTrackId_ = path.trackId;
        currentSelectionType_ = magda::SelectionType::ChainNode;
        updateSelectionDisplay();
    }
}

void InspectorContent::modSelectionChanged(const magda::ModSelection& selection) {
    DBG("InspectorContent::modSelectionChanged - modIndex=" + juce::String(selection.modIndex));
    if (selection.isValid()) {
        currentSelectionType_ = magda::SelectionType::Mod;
        updateSelectionDisplay();
    }
}

void InspectorContent::macroSelectionChanged(const magda::MacroSelection& selection) {
    DBG("InspectorContent::macroSelectionChanged - macroIndex=" +
        juce::String(selection.macroIndex));
    if (selection.isValid()) {
        currentSelectionType_ = magda::SelectionType::Macro;
        updateSelectionDisplay();
    }
}

void InspectorContent::paramSelectionChanged(const magda::ParamSelection& selection) {
    DBG("InspectorContent::paramSelectionChanged - paramIndex=" +
        juce::String(selection.paramIndex));
    if (selection.isValid()) {
        currentSelectionType_ = magda::SelectionType::Param;
        updateSelectionDisplay();
    }
}

void InspectorContent::modsPanelSelectionChanged(const magda::ModsPanelSelection& selection) {
    if (selection.isValid()) {
        currentSelectionType_ = magda::SelectionType::ModsPanel;
        updateSelectionDisplay();
    }
}

void InspectorContent::macrosPanelSelectionChanged(const magda::MacrosPanelSelection& selection) {
    if (selection.isValid()) {
        currentSelectionType_ = magda::SelectionType::MacrosPanel;
        updateSelectionDisplay();
    }
}

void InspectorContent::noteSelectionChanged(const magda::NoteSelection& selection) {
    if (currentSelectionType_ == magda::SelectionType::Note) {
        noteSelection_ = selection;
        updateFromSelectedNotes();
    }
}

// ============================================================================
// Update Methods
// ============================================================================

void InspectorContent::updateSelectionDisplay() {
    DBG("InspectorContent::updateSelectionDisplay - type=" +
        juce::String(static_cast<int>(currentSelectionType_)) +
        " trackId=" + juce::String(selectedTrackId_));
    switch (currentSelectionType_) {
        case magda::SelectionType::None:
        case magda::SelectionType::TimeRange:
            showTrackControls(false);
            showClipControls(false);
            showNoteControls(false);
            showChainNodeControls(false);
            noSelectionLabel_.setVisible(true);
            break;

        case magda::SelectionType::Track:
            showClipControls(false);
            showNoteControls(false);
            showChainNodeControls(false);
            noSelectionLabel_.setVisible(false);
            updateFromSelectedTrack();
            break;

        case magda::SelectionType::Clip:
            showTrackControls(false);
            showNoteControls(false);
            showChainNodeControls(false);
            noSelectionLabel_.setVisible(false);
            updateFromSelectedClip();
            break;

        case magda::SelectionType::MultiClip:
            // For multi-clip selection, show "Multiple clips selected" or similar
            showTrackControls(false);
            showClipControls(false);
            showNoteControls(false);
            showChainNodeControls(false);
            noSelectionLabel_.setText("Multiple clips selected", juce::dontSendNotification);
            noSelectionLabel_.setVisible(true);
            break;

        case magda::SelectionType::Note:
            showTrackControls(false);
            showClipControls(false);
            showChainNodeControls(false);
            noSelectionLabel_.setVisible(false);
            updateFromSelectedNotes();
            break;

        case magda::SelectionType::Device:
            // Show track controls when device is selected (device is within track context)
            showClipControls(false);
            showNoteControls(false);
            showChainNodeControls(false);
            noSelectionLabel_.setVisible(false);
            updateFromSelectedTrack();
            break;

        case magda::SelectionType::ChainNode:
            // Show chain node properties (device, rack, or chain)
            showTrackControls(false);
            showClipControls(false);
            showNoteControls(false);
            noSelectionLabel_.setVisible(false);
            updateFromSelectedChainNode();
            break;

        case magda::SelectionType::Mod: {
            showTrackControls(false);
            showClipControls(false);
            showNoteControls(false);
            showChainNodeControls(false);
            auto& modSelection = magda::SelectionManager::getInstance().getModSelection();
            noSelectionLabel_.setText("Mod " + juce::String(modSelection.modIndex + 1) +
                                          " selected",
                                      juce::dontSendNotification);
            noSelectionLabel_.setVisible(true);
            break;
        }

        case magda::SelectionType::Macro: {
            showTrackControls(false);
            showClipControls(false);
            showNoteControls(false);
            showChainNodeControls(false);
            auto& macroSelection = magda::SelectionManager::getInstance().getMacroSelection();
            noSelectionLabel_.setText("Macro " + juce::String(macroSelection.macroIndex + 1) +
                                          " selected",
                                      juce::dontSendNotification);
            noSelectionLabel_.setVisible(true);
            break;
        }

        case magda::SelectionType::Param: {
            showTrackControls(false);
            showClipControls(false);
            showNoteControls(false);
            showChainNodeControls(false);
            auto& paramSelection = magda::SelectionManager::getInstance().getParamSelection();
            noSelectionLabel_.setText("Param " + juce::String(paramSelection.paramIndex + 1) +
                                          " selected",
                                      juce::dontSendNotification);
            noSelectionLabel_.setVisible(true);
            break;
        }

        case magda::SelectionType::ModsPanel: {
            showTrackControls(false);
            showClipControls(false);
            showNoteControls(false);
            showChainNodeControls(false);
            noSelectionLabel_.setText("Mods Panel", juce::dontSendNotification);
            noSelectionLabel_.setVisible(true);
            break;
        }

        case magda::SelectionType::MacrosPanel: {
            showTrackControls(false);
            showClipControls(false);
            showNoteControls(false);
            showChainNodeControls(false);
            noSelectionLabel_.setText("Macros Panel", juce::dontSendNotification);
            noSelectionLabel_.setVisible(true);
            break;
        }
    }

    resized();
    repaint();
}

void InspectorContent::updateFromSelectedTrack() {
    if (selectedTrackId_ == magda::INVALID_TRACK_ID) {
        showTrackControls(false);
        noSelectionLabel_.setVisible(true);
        return;
    }

    const auto* track = magda::TrackManager::getInstance().getTrack(selectedTrackId_);
    if (track) {
        trackNameValue_.setText(track->name, juce::dontSendNotification);
        muteButton_.setToggleState(track->muted, juce::dontSendNotification);
        soloButton_.setToggleState(track->soloed, juce::dontSendNotification);
        recordButton_.setToggleState(track->recordArmed, juce::dontSendNotification);

        // Convert linear gain to dB for display
        float gainDb = (track->volume <= 0.0f) ? -60.0f : 20.0f * std::log10(track->volume);
        gainLabel_->setValue(gainDb, juce::dontSendNotification);
        panLabel_->setValue(track->pan, juce::dontSendNotification);

        // Update clip count
        auto clips = magda::ClipManager::getInstance().getClipsOnTrack(selectedTrackId_);
        int clipCount = static_cast<int>(clips.size());
        juce::String clipText = juce::String(clipCount) + (clipCount == 1 ? " clip" : " clips");
        clipCountLabel_.setText(clipText, juce::dontSendNotification);

        // Update routing selectors to match track state
        updateRoutingSelectorsFromTrack();

        showTrackControls(true);
        noSelectionLabel_.setVisible(false);
    } else {
        showTrackControls(false);
        noSelectionLabel_.setVisible(true);
    }

    resized();
    repaint();
}

void InspectorContent::updateFromSelectedClip() {
    if (selectedClipId_ == magda::INVALID_CLIP_ID) {
        showClipControls(false);
        noSelectionLabel_.setVisible(true);
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
        clipLoopToggle_->setEnabled(true);

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
        clipWarpToggle_->setVisible(isAudioClip);
        if (isAudioClip) {
            clipWarpToggle_->setActive(clip->warpEnabled);
        }

        // Auto-tempo toggle (always visible for audio clips)
        clipAutoTempoToggle_->setVisible(isAudioClip);
        if (isAudioClip) {
            clipAutoTempoToggle_->setActive(clip->autoTempo);
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
        autoPitchToggle_.setVisible(isAudioClip);
        autoPitchModeCombo_.setVisible(isAudioClip);
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
        leftChannelToggle_.setVisible(isAudioClip);
        rightChannelToggle_.setVisible(isAudioClip);
        if (isAudioClip) {
            clipGainValue_->setValue(clip->gainDB, juce::dontSendNotification);
            clipPanValue_->setValue(clip->pan, juce::dontSendNotification);
            reverseToggle_.setToggleState(clip->isReversed, juce::dontSendNotification);
            leftChannelToggle_.setToggleState(clip->leftChannelActive, juce::dontSendNotification);
            rightChannelToggle_.setToggleState(clip->rightChannelActive,
                                               juce::dontSendNotification);
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
        noSelectionLabel_.setVisible(false);
    } else {
        showClipControls(false);
        noSelectionLabel_.setVisible(true);
    }

    resized();
    repaint();
}

void InspectorContent::showTrackControls(bool show) {
    trackNameLabel_.setVisible(show);
    trackNameValue_.setVisible(show);
    muteButton_.setVisible(show);
    soloButton_.setVisible(show);
    recordButton_.setVisible(show);
    gainLabel_->setVisible(show);
    panLabel_->setVisible(show);

    // Routing section
    routingSectionLabel_.setVisible(show);
    audioInSelector_->setVisible(show);
    audioOutSelector_->setVisible(show);
    midiInSelector_->setVisible(show);
    midiOutSelector_->setVisible(show);

    // Send/Receive section
    sendReceiveSectionLabel_.setVisible(show);
    sendsLabel_.setVisible(show);
    receivesLabel_.setVisible(show);

    // Clips section
    clipsSectionLabel_.setVisible(show);
    clipCountLabel_.setVisible(show);
}

void InspectorContent::showClipControls(bool show) {
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
        clipWarpToggle_->setVisible(false);
        clipAutoTempoToggle_->setVisible(false);
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

void InspectorContent::showNoteControls(bool show) {
    noteCountLabel_.setVisible(show && noteSelection_.getCount() > 1);
    notePitchLabel_.setVisible(show);
    notePitchValue_->setVisible(show);
    noteVelocityLabel_.setVisible(show);
    noteVelocityValue_->setVisible(show);
    noteStartLabel_.setVisible(show);
    noteStartValue_.setVisible(show);
    noteLengthLabel_.setVisible(show);
    noteLengthValue_->setVisible(show);
}

void InspectorContent::showChainNodeControls(bool show) {
    chainNodeTypeLabel_.setVisible(show);
    chainNodeNameLabel_.setVisible(show);
    chainNodeNameValue_.setVisible(show);
}

void InspectorContent::updateFromSelectedChainNode() {
    DBG("InspectorContent::updateFromSelectedChainNode - type=" +
        juce::String(static_cast<int>(selectedChainNode_.getType())));

    if (!selectedChainNode_.isValid()) {
        showChainNodeControls(false);
        noSelectionLabel_.setVisible(true);
        return;
    }

    juce::String typeName;
    juce::String nodeName;

    // Handle top-level device (legacy path format)
    if (selectedChainNode_.getType() == magda::ChainNodeType::TopLevelDevice) {
        typeName = "Device";
        const auto* track = magda::TrackManager::getInstance().getTrack(selectedChainNode_.trackId);
        if (track) {
            for (const auto& element : track->chainElements) {
                if (magda::isDevice(element)) {
                    const auto& device = magda::getDevice(element);
                    if (device.id == selectedChainNode_.topLevelDeviceId) {
                        nodeName = device.name;
                        break;
                    }
                }
            }
        }
    } else {
        // Use centralized path resolver for all recursive paths
        auto resolved = magda::TrackManager::getInstance().resolvePath(selectedChainNode_);

        if (!resolved.valid) {
            showChainNodeControls(false);
            noSelectionLabel_.setVisible(true);
            return;
        }

        // Set type name based on final step
        switch (selectedChainNode_.getType()) {
            case magda::ChainNodeType::Rack:
                typeName = "Rack";
                break;
            case magda::ChainNodeType::Chain:
                typeName = "Chain";
                break;
            case magda::ChainNodeType::Device:
                typeName = "Device";
                break;
            default:
                typeName = "Unknown";
                break;
        }

        nodeName = resolved.displayPath;
    }

    DBG("  -> typeName=" + typeName + " nodeName=" + nodeName);

    chainNodeTypeLabel_.setText(typeName, juce::dontSendNotification);
    chainNodeNameValue_.setText(nodeName, juce::dontSendNotification);

    showChainNodeControls(true);
    noSelectionLabel_.setVisible(false);

    // Show device parameters if this is a device
    if (selectedChainNode_.getType() == magda::ChainNodeType::Device ||
        selectedChainNode_.getType() == magda::ChainNodeType::TopLevelDevice) {
        DBG("InspectorContent: Showing device parameters for chain node type="
            << static_cast<int>(selectedChainNode_.getType()));

        // Get the device info
        const magda::DeviceInfo* deviceInfo = nullptr;

        if (selectedChainNode_.getType() == magda::ChainNodeType::TopLevelDevice) {
            // Top-level device - search track's chain elements
            const auto* track =
                magda::TrackManager::getInstance().getTrack(selectedChainNode_.trackId);
            if (track) {
                for (const auto& element : track->chainElements) {
                    if (magda::isDevice(element)) {
                        const auto& device = magda::getDevice(element);
                        if (device.id == selectedChainNode_.topLevelDeviceId) {
                            deviceInfo = &device;
                            DBG("  Found top-level device: " << device.name << " with "
                                                             << device.parameters.size()
                                                             << " parameters");
                            break;
                        }
                    }
                }
            }
        } else {
            // Nested device - use TrackManager's getDevice()
            // First, we need the device ID from the resolved path
            auto resolved = magda::TrackManager::getInstance().resolvePath(selectedChainNode_);
            if (resolved.valid && resolved.device) {
                deviceInfo = resolved.device;
                DBG("  Found nested device: " << deviceInfo->name << " with "
                                              << deviceInfo->parameters.size() << " parameters");
            }
        }

        if (deviceInfo && !deviceInfo->parameters.empty()) {
            DBG("  Creating param controls for " << deviceInfo->parameters.size() << " parameters");
            createDeviceParamControls(*deviceInfo);
            showDeviceParamControls(true);
        } else {
            DBG("  No device info or no parameters - hiding controls");
            showDeviceParamControls(false);
        }
    } else {
        showDeviceParamControls(false);
    }

    resized();
    repaint();
}

void InspectorContent::updateFromSelectedNotes() {
    if (!noteSelection_.isValid()) {
        showNoteControls(false);
        noSelectionLabel_.setVisible(true);
        return;
    }

    const auto* clip = magda::ClipManager::getInstance().getClip(noteSelection_.clipId);
    if (!clip || clip->type != magda::ClipType::MIDI) {
        showNoteControls(false);
        noSelectionLabel_.setVisible(true);
        return;
    }

    if (noteSelection_.isSingleNote()) {
        // Single note - show editable properties
        size_t noteIndex = noteSelection_.noteIndices[0];
        if (noteIndex < clip->midiNotes.size()) {
            const auto& note = clip->midiNotes[noteIndex];

            notePitchValue_->setValue(note.noteNumber, juce::dontSendNotification);
            noteVelocityValue_->setValue(note.velocity, juce::dontSendNotification);

            // Format start as beats
            juce::String startStr = juce::String(note.startBeat, 2) + " beats";
            noteStartValue_.setText(startStr, juce::dontSendNotification);

            noteLengthValue_->setValue(note.lengthBeats, juce::dontSendNotification);
        }
    } else {
        // Multiple notes - show count and common properties
        juce::String countStr = juce::String(noteSelection_.getCount()) + " notes selected";
        noteCountLabel_.setText(countStr, juce::dontSendNotification);

        // For multiple notes, show the first note's values (or could show average/common)
        if (!noteSelection_.noteIndices.empty()) {
            size_t firstIndex = noteSelection_.noteIndices[0];
            if (firstIndex < clip->midiNotes.size()) {
                const auto& note = clip->midiNotes[firstIndex];
                notePitchValue_->setValue(note.noteNumber, juce::dontSendNotification);
                noteVelocityValue_->setValue(note.velocity, juce::dontSendNotification);
                noteStartValue_.setText("--", juce::dontSendNotification);
                noteLengthValue_->setValue(note.lengthBeats, juce::dontSendNotification);
            }
        }
    }

    showNoteControls(true);
    noSelectionLabel_.setVisible(false);

    resized();
    repaint();
}

void InspectorContent::populateRoutingSelectors() {
    populateAudioInputOptions();
    populateAudioOutputOptions();
    populateMidiInputOptions();
    populateMidiOutputOptions();

    // Wire up callbacks to update track routing
    if (!audioEngine_)
        return;

    auto* midiBridge = audioEngine_->getMidiBridge();
    if (!midiBridge)
        return;

    // MIDI Input selector callback
    midiInSelector_->onSelectionChanged = [this, midiBridge](int selectedId) {
        DBG("InspectorContent MIDI input selector changed - selectedId="
            << selectedId << " trackId=" << selectedTrackId_);

        if (selectedTrackId_ == magda::INVALID_TRACK_ID)
            return;

        if (selectedId == 2) {
            // "None" selected
            DBG("  -> Clearing MIDI input via TrackManager");
            magda::TrackManager::getInstance().setTrackMidiInput(selectedTrackId_, "");
        } else if (selectedId == 1) {
            // "All Inputs" selected
            DBG("  -> Setting to All Inputs via TrackManager");
            magda::TrackManager::getInstance().setTrackMidiInput(selectedTrackId_, "all");
        } else if (selectedId >= 10) {
            // Specific device selected
            auto midiInputs = midiBridge->getAvailableMidiInputs();
            int deviceIndex = selectedId - 10;
            if (deviceIndex >= 0 && deviceIndex < static_cast<int>(midiInputs.size())) {
                DBG("  -> Setting to specific device via TrackManager: "
                    << midiInputs[deviceIndex].name);
                magda::TrackManager::getInstance().setTrackMidiInput(selectedTrackId_,
                                                                     midiInputs[deviceIndex].id);
            }
        }
    };

    // MIDI Input enabled/disabled toggle callback
    midiInSelector_->onEnabledChanged = [this, midiBridge](bool enabled) {
        DBG("InspectorContent MIDI input enabled changed - enabled=" << (int)enabled << " trackId="
                                                                     << selectedTrackId_);

        if (selectedTrackId_ == magda::INVALID_TRACK_ID)
            return;

        if (enabled) {
            // Enable: Set to currently selected option or default to "All Inputs"
            int selectedId = midiInSelector_->getSelectedId();
            DBG("  -> Enabling with selectedId=" << selectedId);

            if (selectedId == 1) {
                magda::TrackManager::getInstance().setTrackMidiInput(selectedTrackId_, "all");
            } else if (selectedId >= 10) {
                auto midiInputs = midiBridge->getAvailableMidiInputs();
                int deviceIndex = selectedId - 10;
                if (deviceIndex >= 0 && deviceIndex < static_cast<int>(midiInputs.size())) {
                    magda::TrackManager::getInstance().setTrackMidiInput(
                        selectedTrackId_, midiInputs[deviceIndex].id);
                } else {
                    // Default to "all" if device not found
                    magda::TrackManager::getInstance().setTrackMidiInput(selectedTrackId_, "all");
                }
            } else {
                // Default to "all" for any other case
                DBG("  -> Defaulting to All Inputs");
                magda::TrackManager::getInstance().setTrackMidiInput(selectedTrackId_, "all");
            }
        } else {
            // Disable: Clear MIDI input
            DBG("  -> Disabling (clearing MIDI input)");
            magda::TrackManager::getInstance().setTrackMidiInput(selectedTrackId_, "");
        }
    };

    // MIDI Output enabled/disabled toggle callback
    midiOutSelector_->onEnabledChanged = [this, midiBridge](bool enabled) {
        DBG("InspectorContent MIDI output enabled changed - enabled=" << (int)enabled << " trackId="
                                                                      << selectedTrackId_);

        if (selectedTrackId_ == magda::INVALID_TRACK_ID)
            return;

        if (enabled) {
            int selectedId = midiOutSelector_->getSelectedId();
            if (selectedId >= 10) {
                auto midiOutputs = midiBridge->getAvailableMidiOutputs();
                int deviceIndex = selectedId - 10;
                if (deviceIndex >= 0 && deviceIndex < static_cast<int>(midiOutputs.size())) {
                    magda::TrackManager::getInstance().setTrackMidiOutput(
                        selectedTrackId_, midiOutputs[deviceIndex].id);
                }
            }
        } else {
            magda::TrackManager::getInstance().setTrackMidiOutput(selectedTrackId_, "");
        }
    };

    // Audio Input enabled/disabled toggle callback
    audioInSelector_->onEnabledChanged = [this](bool enabled) {
        DBG("InspectorContent audio input enabled changed - enabled=" << (int)enabled << " trackId="
                                                                      << selectedTrackId_);

        if (selectedTrackId_ == magda::INVALID_TRACK_ID)
            return;

        if (enabled) {
            // TODO: Get selected audio input device
            magda::TrackManager::getInstance().setTrackAudioInput(selectedTrackId_, "default");
        } else {
            magda::TrackManager::getInstance().setTrackAudioInput(selectedTrackId_, "");
        }
    };

    // Audio Output enabled/disabled toggle callback
    audioOutSelector_->onEnabledChanged = [this](bool enabled) {
        DBG("InspectorContent audio output enabled changed - enabled="
            << (int)enabled << " trackId=" << selectedTrackId_);

        if (selectedTrackId_ == magda::INVALID_TRACK_ID)
            return;

        if (enabled) {
            magda::TrackManager::getInstance().setTrackAudioOutput(selectedTrackId_, "master");
        } else {
            magda::TrackManager::getInstance().setTrackAudioOutput(selectedTrackId_, "");
        }
    };

    // MIDI Output selector callback
    midiOutSelector_->onSelectionChanged = [this, midiBridge](int selectedId) {
        DBG("InspectorContent MIDI output selector changed - selectedId="
            << selectedId << " trackId=" << selectedTrackId_);

        if (selectedTrackId_ == magda::INVALID_TRACK_ID)
            return;

        if (selectedId == 2) {
            // "None" selected - clear output
            DBG("  -> Clearing MIDI output via TrackManager");
            magda::TrackManager::getInstance().setTrackMidiOutput(selectedTrackId_, "");
        } else if (selectedId >= 10) {
            // Specific device selected
            auto midiOutputs = midiBridge->getAvailableMidiOutputs();
            int deviceIndex = selectedId - 10;
            if (deviceIndex >= 0 && deviceIndex < static_cast<int>(midiOutputs.size())) {
                DBG("  -> Setting to specific device via TrackManager: "
                    << midiOutputs[deviceIndex].name);
                magda::TrackManager::getInstance().setTrackMidiOutput(selectedTrackId_,
                                                                      midiOutputs[deviceIndex].id);
            }
        }
    };
}

void InspectorContent::populateAudioInputOptions() {
    if (!audioInSelector_ || !audioEngine_) {
        return;
    }

    auto* deviceManager = audioEngine_->getDeviceManager();
    if (!deviceManager) {
        return;
    }

    std::vector<magda::RoutingSelector::RoutingOption> options;

    // Get current audio device
    auto* currentDevice = deviceManager->getCurrentAudioDevice();
    if (currentDevice) {
        // Get only the ACTIVE/ENABLED input channels
        auto activeInputChannels = currentDevice->getActiveInputChannels();

        // Add "None" option
        options.push_back({1, "None"});

        // Count how many channels are actually enabled
        int numActiveChannels = activeInputChannels.countNumberOfSetBits();

        if (numActiveChannels > 0) {
            options.push_back({0, "", true});  // separator

            // Build list of active channel indices
            juce::Array<int> activeIndices;
            for (int i = 0; i < activeInputChannels.getHighestBit() + 1; ++i) {
                if (activeInputChannels[i]) {
                    activeIndices.add(i);
                }
            }

            // Add stereo pairs first (starting from ID 10)
            int id = 10;
            for (int i = 0; i < activeIndices.size(); i += 2) {
                if (i + 1 < activeIndices.size()) {
                    // Stereo pair - show as "1-2", "3-4", etc.
                    int ch1 = activeIndices[i] + 1;
                    int ch2 = activeIndices[i + 1] + 1;
                    juce::String pairName = juce::String(ch1) + "-" + juce::String(ch2);
                    options.push_back({id++, pairName});
                }
            }

            // Add separator before mono channels (only if we have multiple channels)
            if (activeIndices.size() > 1) {
                options.push_back({0, "", true});  // separator
            }

            // Add individual mono channels (starting from ID 100 to avoid conflicts)
            id = 100;
            for (int i = 0; i < activeIndices.size(); ++i) {
                int channelNum = activeIndices[i] + 1;
                options.push_back({id++, juce::String(channelNum) + " (mono)"});
            }
        }
    } else {
        options.push_back({1, "None"});
        options.push_back({2, "(No Device Active)"});
    }

    audioInSelector_->setOptions(options);
}

void InspectorContent::populateAudioOutputOptions() {
    if (!audioOutSelector_ || !audioEngine_) {
        return;
    }

    auto* deviceManager = audioEngine_->getDeviceManager();
    if (!deviceManager) {
        return;
    }

    std::vector<magda::RoutingSelector::RoutingOption> options;

    // Get current audio device
    auto* currentDevice = deviceManager->getCurrentAudioDevice();
    if (currentDevice) {
        // Get only the ACTIVE/ENABLED output channels
        auto activeOutputChannels = currentDevice->getActiveOutputChannels();

        // Add "Master" as default output
        options.push_back({1, "Master"});

        // Count how many channels are actually enabled
        int numActiveChannels = activeOutputChannels.countNumberOfSetBits();

        if (numActiveChannels > 0) {
            options.push_back({0, "", true});  // separator

            // Build list of active channel indices
            juce::Array<int> activeIndices;
            for (int i = 0; i < activeOutputChannels.getHighestBit() + 1; ++i) {
                if (activeOutputChannels[i]) {
                    activeIndices.add(i);
                }
            }

            // Add stereo pairs first (starting from ID 10)
            int id = 10;
            for (int i = 0; i < activeIndices.size(); i += 2) {
                if (i + 1 < activeIndices.size()) {
                    // Stereo pair - show as "1-2", "3-4", etc.
                    int ch1 = activeIndices[i] + 1;
                    int ch2 = activeIndices[i + 1] + 1;
                    juce::String pairName = juce::String(ch1) + "-" + juce::String(ch2);
                    options.push_back({id++, pairName});
                }
            }

            // Add separator before mono channels (only if we have multiple channels)
            if (activeIndices.size() > 1) {
                options.push_back({0, "", true});  // separator
            }

            // Add individual mono channels (starting from ID 100 to avoid conflicts)
            id = 100;
            for (int i = 0; i < activeIndices.size(); ++i) {
                int channelNum = activeIndices[i] + 1;
                options.push_back({id++, juce::String(channelNum) + " (mono)"});
            }
        }
    } else {
        options.push_back({1, "Master"});
        options.push_back({2, "(No Device Active)"});
    }

    audioOutSelector_->setOptions(options);
}

void InspectorContent::populateMidiInputOptions() {
    if (!midiInSelector_ || !audioEngine_) {
        return;
    }

    auto* midiBridge = audioEngine_->getMidiBridge();
    if (!midiBridge) {
        return;
    }

    // Get available MIDI inputs from MidiBridge
    auto midiInputs = midiBridge->getAvailableMidiInputs();

    // Build options list
    std::vector<magda::RoutingSelector::RoutingOption> options;
    options.push_back({1, "All Inputs"});  // ID 1 = all inputs
    options.push_back({2, "None"});        // ID 2 = no input

    if (!midiInputs.empty()) {
        options.push_back({0, "", true});  // separator

        // Add each MIDI device as an option (starting from ID 10)
        int id = 10;
        for (const auto& device : midiInputs) {
            options.push_back({id++, device.name});
        }
    }

    midiInSelector_->setOptions(options);
}

void InspectorContent::populateMidiOutputOptions() {
    if (!midiOutSelector_ || !audioEngine_) {
        return;
    }

    auto* midiBridge = audioEngine_->getMidiBridge();
    if (!midiBridge) {
        return;
    }

    // Get available MIDI outputs from MidiBridge
    auto midiOutputs = midiBridge->getAvailableMidiOutputs();

    // Build options list
    std::vector<magda::RoutingSelector::RoutingOption> options;
    options.push_back({1, "None"});         // ID 1 = no output
    options.push_back({2, "All Outputs"});  // ID 2 = all outputs

    if (!midiOutputs.empty()) {
        options.push_back({0, "", true});  // separator

        // Add each MIDI device as an option (starting from ID 10)
        int id = 10;
        for (const auto& device : midiOutputs) {
            options.push_back({id++, device.name});
        }
    }

    midiOutSelector_->setOptions(options);
}

void InspectorContent::updateRoutingSelectorsFromTrack() {
    if (selectedTrackId_ == magda::INVALID_TRACK_ID || !audioEngine_) {
        DBG("InspectorContent::updateRoutingSelectorsFromTrack - invalid track or no engine");
        return;
    }

    // Get track from TrackManager
    const auto* track = magda::TrackManager::getInstance().getTrack(selectedTrackId_);
    if (!track) {
        DBG("InspectorContent::updateRoutingSelectorsFromTrack - track not found");
        return;
    }

    auto* midiBridge = audioEngine_->getMidiBridge();

    // Update MIDI input selector from track state
    juce::String currentMidiInput = track->midiInputDevice;

    if (currentMidiInput.isEmpty()) {
        midiInSelector_->setSelectedId(2);
        midiInSelector_->setEnabled(false);
    } else if (currentMidiInput == "all") {
        midiInSelector_->setSelectedId(1);
        midiInSelector_->setEnabled(true);
    } else {
        if (midiBridge) {
            auto midiInputs = midiBridge->getAvailableMidiInputs();
            int selectedId = 2;
            for (size_t i = 0; i < midiInputs.size(); ++i) {
                if (midiInputs[i].id == currentMidiInput) {
                    selectedId = 10 + static_cast<int>(i);
                    DBG("  -> Found MIDI In device at index " << i << ", setting to ID "
                                                              << selectedId << " and ENABLED");
                    break;
                }
            }
            midiInSelector_->setSelectedId(selectedId);
            midiInSelector_->setEnabled(selectedId != 2);
        }
    }

    // Update MIDI Output selector
    juce::String currentMidiOutput = track->midiOutputDevice;
    if (currentMidiOutput.isEmpty()) {
        midiOutSelector_->setSelectedId(2);  // "None"
        midiOutSelector_->setEnabled(false);
    } else {
        if (midiBridge) {
            auto midiOutputs = midiBridge->getAvailableMidiOutputs();
            int selectedId = 2;
            for (size_t i = 0; i < midiOutputs.size(); ++i) {
                if (midiOutputs[i].id == currentMidiOutput) {
                    selectedId = 10 + static_cast<int>(i);
                    break;
                }
            }
            midiOutSelector_->setSelectedId(selectedId);
            midiOutSelector_->setEnabled(selectedId != 2);
        }
    }

    // Update Audio Input selector
    juce::String currentAudioInput = track->audioInputDevice;
    if (currentAudioInput.isEmpty()) {
        audioInSelector_->setSelectedId(2);  // "None"
        audioInSelector_->setEnabled(false);
    } else {
        // TODO: Parse audio input and find in list
        audioInSelector_->setEnabled(true);
    }

    // Update Audio Output selector
    juce::String currentAudioOutput = track->audioOutputDevice;
    if (currentAudioOutput.isEmpty()) {
        // No output selected - disabled
        audioOutSelector_->setSelectedId(2);  // "None"
        audioOutSelector_->setEnabled(false);
    } else if (currentAudioOutput == "master") {
        // Master output selected - enabled
        audioOutSelector_->setSelectedId(1);  // Master
        audioOutSelector_->setEnabled(true);
    } else {
        // TODO: Find specific output in list
        audioOutSelector_->setEnabled(true);
    }
}

void InspectorContent::createDeviceParamControls(const magda::DeviceInfo& device) {
    DBG("InspectorContent::createDeviceParamControls - device=" << device.name << " paramCount="
                                                                << device.parameters.size());

    // Clear existing controls
    deviceParamControls_.clear();
    deviceParamsContainer_.removeAllChildren();

    if (device.parameters.empty()) {
        DBG("  No parameters to display");
        deviceParamsContainer_.setSize(0, 0);
        return;
    }

    const int rowHeight = 50;
    const int nameWidth = 120;
    const int valueWidth = 60;
    const int padding = 8;
    const int containerWidth = getWidth() - 20;  // Account for scrollbar

    int y = padding;
    for (size_t i = 0; i < device.parameters.size(); ++i) {
        const auto& param = device.parameters[i];

        auto control = std::make_unique<DeviceParamControl>();
        control->paramIndex = static_cast<int>(i);

        // Name label
        control->nameLabel.setText(param.name, juce::dontSendNotification);
        control->nameLabel.setFont(FontManager::getInstance().getUIFont(11.0f));
        control->nameLabel.setColour(juce::Label::textColourId, DarkTheme::getTextColour());
        control->nameLabel.setBounds(padding, y, nameWidth, 20);
        deviceParamsContainer_.addAndMakeVisible(control->nameLabel);

        // Value label (shows current value + unit)
        juce::String valueText = juce::String(param.currentValue, 2);
        if (param.unit.isNotEmpty()) {
            valueText += " " + param.unit;
        }
        control->valueLabel.setText(valueText, juce::dontSendNotification);
        control->valueLabel.setFont(FontManager::getInstance().getUIFont(10.0f));
        control->valueLabel.setColour(juce::Label::textColourId,
                                      DarkTheme::getSecondaryTextColour());
        control->valueLabel.setJustificationType(juce::Justification::centredRight);
        control->valueLabel.setBounds(containerWidth - valueWidth - padding, y, valueWidth, 20);
        deviceParamsContainer_.addAndMakeVisible(control->valueLabel);

        // Slider
        control->slider.setSliderStyle(juce::Slider::LinearHorizontal);
        control->slider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        control->slider.setColour(juce::Slider::trackColourId,
                                  DarkTheme::getColour(DarkTheme::SURFACE));
        control->slider.setColour(juce::Slider::thumbColourId,
                                  DarkTheme::getColour(DarkTheme::ACCENT_BLUE));

        // Set range based on parameter type
        if (param.scale == magda::ParameterScale::Logarithmic) {
            control->slider.setSkewFactorFromMidPoint(std::sqrt(param.minValue * param.maxValue));
        }
        control->slider.setRange(param.minValue, param.maxValue, 0.0);
        control->slider.setValue(param.currentValue, juce::dontSendNotification);

        // Wire up callback to update parameter via TrackManager
        int paramIndex = static_cast<int>(i);
        control->slider.onValueChange = [this, deviceId = device.id,
                                         trackId = selectedChainNode_.trackId,
                                         devicePath = selectedChainNode_, paramIndex]() {
            auto* ctrl = deviceParamControls_[paramIndex].get();
            if (ctrl) {
                float newValue = static_cast<float>(ctrl->slider.getValue());

                // Update value label
                const auto* dev = magda::TrackManager::getInstance().getDevice(trackId, deviceId);
                if (dev && paramIndex < static_cast<int>(dev->parameters.size())) {
                    const auto& param = dev->parameters[paramIndex];
                    juce::String valueText = juce::String(newValue, 2);
                    if (param.unit.isNotEmpty()) {
                        valueText += " " + param.unit;
                    }
                    ctrl->valueLabel.setText(valueText, juce::dontSendNotification);
                }

                // Push parameter change to audio engine
                magda::TrackManager::getInstance().setDeviceParameterValue(devicePath, paramIndex,
                                                                           newValue);
            }
        };

        control->slider.setBounds(padding, y + 22, containerWidth - 2 * padding, 20);
        deviceParamsContainer_.addAndMakeVisible(control->slider);

        deviceParamControls_.push_back(std::move(control));
        y += rowHeight;
    }

    // Set container size based on parameter count
    deviceParamsContainer_.setSize(containerWidth, y);
    DBG("  Created " << deviceParamControls_.size()
                     << " param controls, container size=" << containerWidth << "x" << y);
}

void InspectorContent::showDeviceParamControls(bool show) {
    DBG("InspectorContent::showDeviceParamControls(" << (show ? "true" : "false") << ")");
    deviceParamsLabel_.setVisible(show);
    deviceParamsViewport_.setVisible(show);
    deviceParamsContainer_.setVisible(show);
}

}  // namespace magda::daw::ui
