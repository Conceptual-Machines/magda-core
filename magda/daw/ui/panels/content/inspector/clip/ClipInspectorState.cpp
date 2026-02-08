#include <cmath>

#include "../../../../../audio/AudioThumbnailManager.hpp"
#include "../../../../state/TimelineController.hpp"
#include "../../../../utils/TimelineUtils.hpp"
#include "../ClipInspector.hpp"
#include "BinaryData.h"

namespace magda::daw::ui {

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
            const double fileDur = thumbnail->getTotalLength();
            if (fileDur > 0.0) {
                // Work on local copies to avoid mutating ClipInfo directly from the UI
                double newOffset = mutableClip->offset;
                double newLoopStart = mutableClip->loopStart;
                double newLoopLength = mutableClip->loopLength;

                bool fixed = false;

                if (newOffset > fileDur) {
                    newOffset = juce::jmin(newOffset, fileDur);
                    fixed = true;
                }

                if (newLoopStart > fileDur) {
                    newLoopStart = 0.0;
                    fixed = true;
                }

                const double avail = fileDur - newLoopStart;
                if (newLoopLength > avail) {
                    newLoopLength = avail;
                    fixed = true;
                }

                if (fixed) {
                    auto& clipManager = magda::ClipManager::getInstance();

                    if (newOffset != mutableClip->offset) {
                        clipManager.setOffset(selectedClipId_, newOffset);
                    }

                    if (newLoopStart != mutableClip->loopStart) {
                        clipManager.setLoopStart(selectedClipId_, newLoopStart);
                    }

                    if (newLoopLength != mutableClip->loopLength) {
                        clipManager.setLoopLength(selectedClipId_, newLoopLength);
                    }

                    // The ClipManager setters are responsible for notification and
                    // any additional sanitization (e.g. beat-domain fields).
                    // This function will be called again with fixed values.
                    return;
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

}  // namespace magda::daw::ui
