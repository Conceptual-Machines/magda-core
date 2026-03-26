#include <cmath>

#include "../../../../../audio/AudioThumbnailManager.hpp"
#include "../../../../components/common/ColourSwatch.hpp"
#include "../../../../state/TimelineController.hpp"
#include "../../../../utils/TimelineUtils.hpp"
#include "../ClipInspector.hpp"
#include "BinaryData.h"
#include "core/TrackManager.hpp"
#include "engine/TracktionEngineWrapper.hpp"

namespace magda::daw::ui {

void ClipInspector::updateFromSelectedClip() {
    auto pid = primaryClipId();
    if (pid == magda::INVALID_CLIP_ID) {
        clipCountLabel_.setVisible(false);
        showClipControls(false);
        return;
    }

    bool isMulti = selectedClipIds_.size() > 1;

    // Multi-clip header
    if (isMulti) {
        clipCountLabel_.setText(juce::String(static_cast<int>(selectedClipIds_.size())) +
                                    " clips selected",
                                juce::dontSendNotification);
        clipCountLabel_.setVisible(true);
        // Hide editable name for multi-selection
        clipNameValue_.setEditable(false);
    } else {
        clipCountLabel_.setVisible(false);
        clipNameValue_.setEditable(true);
    }

    // Sanitize stale audio clip values (e.g. offset past file end from old model)
    // Only for single-clip selection to avoid sanitization conflicts
    if (!isMulti) {
        auto* mutableClip = magda::ClipManager::getInstance().getClip(pid);
        if (mutableClip && mutableClip->type == magda::ClipType::Audio &&
            !mutableClip->audioFilePath.isEmpty()) {
            auto* thumbnail = magda::AudioThumbnailManager::getInstance().getThumbnail(
                mutableClip->audioFilePath);
            if (thumbnail) {
                const double fileDur = thumbnail->getTotalLength();
                if (fileDur > 0.0) {
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
                            clipManager.setOffset(pid, newOffset);
                        }

                        if (newLoopStart != mutableClip->loopStart) {
                            clipManager.setLoopStart(pid, newLoopStart);
                        }

                        if (newLoopLength != mutableClip->loopLength) {
                            clipManager.setLoopLength(pid, newLoopLength);
                        }

                        return;
                    }
                }
            }
        }
    }

    const auto* clip = magda::ClipManager::getInstance().getClip(pid);
    if (clip) {
        clipNameValue_.setText(clip->name, juce::dontSendNotification);

        // Update colour swatch
        auto* swatch = static_cast<magda::ColourSwatch*>(colourSwatch_.get());
        if (clip->colour == juce::Colour(0xFF444444))
            swatch->clearColour();
        else
            swatch->setColour(clip->colour);

        // File path label: show audio filename for arrangement audio clips only.
        if (clip->type == magda::ClipType::Audio && clip->audioFilePath.isNotEmpty() &&
            clip->view != magda::ClipView::Session && !isMulti) {
            juce::File audioFile(clip->audioFilePath);
            clipFilePathLabel_.setText(audioFile.getFileName(), juce::dontSendNotification);
            clipFilePathLabel_.setTooltip(clip->audioFilePath);
        } else {
            clipFilePathLabel_.setText("", juce::dontSendNotification);
            clipFilePathLabel_.setTooltip("");
        }

        // Update type icon based on clip type
        bool isAudioClip = (clip->type == magda::ClipType::Audio);
        bool showAudioProps = isAudioClip && !audioPropsCollapsed_;
        audioPropsCollapseToggle_.setVisible(isAudioClip);
        audioPropsLabel_.setVisible(isAudioClip);

        if (isAudioClip) {
            clipTypeIcon_->updateSvgData(BinaryData::sinewave_svg, BinaryData::sinewave_svgSize);
            clipTypeIcon_->setTooltip("Audio clip");
        } else {
            clipTypeIcon_->updateSvgData(BinaryData::midi_svg, BinaryData::midi_svgSize);
            clipTypeIcon_->setTooltip("MIDI clip");
        }

        // Update view icon based on clip view
        if (clip->view == magda::ClipView::Session) {
            clipViewIcon_->updateSvgData(BinaryData::Session_svg, BinaryData::Session_svgSize);
            clipViewIcon_->setTooltip("Session clip");
        } else {
            clipViewIcon_->updateSvgData(BinaryData::Arrangement_svg,
                                         BinaryData::Arrangement_svgSize);
            clipViewIcon_->setTooltip("Arrangement clip");
        }

        // Show BPM for audio clips (at bottom with WARP)
        // Prefer clip's sourceBPM (may be user-edited), fall back to detected BPM
        if (showAudioProps && !isMulti) {
            double displayBPM = clip->sourceBPM;
            double projectBPM =
                timelineController_ ? timelineController_->getState().tempo.bpm : 120.0;
            if (displayBPM <= 0.0 ||
                (!clip->autoTempo && std::abs(displayBPM - projectBPM) < 0.1)) {
                // sourceBPM is unset or matches project BPM (defaulted) — use detected
                displayBPM =
                    magda::AudioThumbnailManager::getInstance().detectBPM(clip->audioFilePath);
            }
            clipBpmValue_.setVisible(true);
            if (displayBPM > 0.0) {
                clipBpmValue_.setText(juce::String(displayBPM, 1) + " BPM",
                                      juce::dontSendNotification);
            } else {
                clipBpmValue_.setText(juce::String::fromUTF8("\xe2\x80\x94"),  // em dash
                                      juce::dontSendNotification);
            }
        } else {
            clipBpmValue_.setVisible(false);
        }

        // Show length in beats for audio clips with auto-tempo enabled (read-only display)
        if (showAudioProps && clip->autoTempo && !isMulti) {
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
        clipLoopEndValue_->setBeatsPerBar(beatsPerBar);

        if (isSessionClip) {
            // Session clips: hide the position row entirely (no arrangement position)
            clipPositionIcon_->setVisible(false);
            clipStartLabel_.setVisible(false);
            clipStartValue_->setVisible(false);
            clipEndLabel_.setVisible(false);
            clipEndValue_->setVisible(false);
            clipOffsetLabel_.setVisible(false);
            clipContentOffsetValue_->setVisible(false);
        } else {
            // Arrangement clips: start and end as positions in beats
            clipPositionIcon_->setVisible(true);
            clipStartLabel_.setVisible(true);
            clipStartValue_->setVisible(true);
            clipStartValue_->setEnabled(true);
            clipStartValue_->setAlpha(1.0f);
            clipEndLabel_.setVisible(true);
            clipEndValue_->setVisible(true);
            clipOffsetLabel_.setVisible(true);
            clipContentOffsetValue_->setVisible(true);

            clipStartValue_->setValue(clip->getStartBeats(bpm), juce::dontSendNotification);
            clipEndValue_->setValue(clip->getEndBeats(bpm), juce::dontSendNotification);

            // Offset value
            if (clip->type == magda::ClipType::MIDI) {
                clipContentOffsetValue_->setValue(clip->midiOffset, juce::dontSendNotification);
            } else if (clip->type == magda::ClipType::Audio) {
                // Use sourceBPM for source-file positions when available
                double displayBpm = clip->sourceBPM > 0.0 ? clip->sourceBPM : bpm;
                double offsetBeats = magda::TimelineUtils::secondsToBeats(clip->offset, displayBpm);
                clipContentOffsetValue_->setValue(offsetBeats, juce::dontSendNotification);
            }
        }

        clipLoopToggle_->setActive(clip->loopEnabled);
        // Beat mode forces loop on — disable the toggle so user can't turn it off
        clipLoopToggle_->setEnabled(!clip->autoTempo);

        // Loop state determines offset interactivity and loop row visibility
        bool loopOn = isSessionClip || clip->loopEnabled;

        if (loopOn) {
            // Loop ON: offset in position row becomes disabled/greyed
            clipContentOffsetValue_->setEnabled(false);
            clipContentOffsetValue_->setAlpha(0.4f);

            // Show loop row: lstart | lend | phase
            clipLoopStartLabel_.setVisible(true);
            clipLoopStartValue_->setVisible(true);
            clipLoopStartValue_->setBeatsPerBar(beatsPerBar);
            // For audio clips, loop start/end are source-file positions — use sourceBPM
            double loopBpm = (clip->type == magda::ClipType::Audio && clip->sourceBPM > 0.0)
                                 ? clip->sourceBPM
                                 : bpm;
            double loopStartBeats = magda::TimelineUtils::secondsToBeats(clip->loopStart, loopBpm);
            clipLoopStartValue_->setValue(loopStartBeats, juce::dontSendNotification);
            clipLoopStartValue_->setEnabled(true);
            clipLoopStartValue_->setAlpha(1.0f);
            clipLoopStartLabel_.setAlpha(1.0f);

            // Display loop end (loopStart + loopLength) in beats
            double loopLengthDisplayBeats;
            if (clip->autoTempo && clip->loopLengthBeats > 0.0) {
                loopLengthDisplayBeats = clip->loopLengthBeats;
            } else {
                double sourceLength =
                    clip->loopLength > 0.0 ? clip->loopLength : clip->length * clip->speedRatio;
                loopLengthDisplayBeats =
                    magda::TimelineUtils::secondsToBeats(sourceLength, loopBpm);
            }
            double loopEndBeats = loopStartBeats + loopLengthDisplayBeats;
            clipLoopEndLabel_.setVisible(true);
            clipLoopEndValue_->setVisible(true);
            clipLoopEndValue_->setValue(loopEndBeats, juce::dontSendNotification);
            clipLoopEndValue_->setEnabled(true);
            clipLoopEndValue_->setAlpha(1.0f);
            clipLoopEndLabel_.setAlpha(1.0f);

            clipLoopPhaseLabel_.setVisible(true);
            clipLoopPhaseValue_->setVisible(true);
            clipLoopPhaseValue_->setBeatsPerBar(beatsPerBar);
            if (clip->type == magda::ClipType::MIDI) {
                clipLoopPhaseValue_->setValue(clip->midiOffset, juce::dontSendNotification);
            } else {
                double phaseSeconds = clip->offset - clip->loopStart;
                double phaseBeats = magda::TimelineUtils::secondsToBeats(phaseSeconds, loopBpm);
                clipLoopPhaseValue_->setValue(phaseBeats, juce::dontSendNotification);
            }
            clipLoopPhaseValue_->setEnabled(true);
            clipLoopPhaseValue_->setAlpha(1.0f);
            clipLoopPhaseLabel_.setAlpha(1.0f);
        } else {
            // Loop OFF: offset is active, loop row hidden
            clipContentOffsetValue_->setEnabled(true);
            clipContentOffsetValue_->setAlpha(1.0f);

            clipLoopStartLabel_.setVisible(false);
            clipLoopStartValue_->setVisible(false);
            clipLoopEndLabel_.setVisible(false);
            clipLoopEndValue_->setVisible(false);
            clipLoopPhaseLabel_.setVisible(false);
            clipLoopPhaseValue_->setVisible(false);
        }

        // Warp toggle (visible when audio props expanded)
        clipWarpToggle_.setVisible(showAudioProps);
        if (isAudioClip) {
            clipWarpToggle_.setToggleState(clip->warpEnabled, juce::dontSendNotification);
        }

        // Auto-tempo toggle
        clipAutoTempoToggle_.setVisible(showAudioProps);
        if (isAudioClip) {
            clipAutoTempoToggle_.setToggleState(clip->autoTempo, juce::dontSendNotification);
            // Disable stretch control when auto-tempo is enabled (speedRatio must be 1.0)
            if (clip->autoTempo && clipStretchValue_) {
                clipStretchValue_->setEnabled(false);
                clipStretchValue_->setAlpha(0.4f);
            }
        }

        clipStretchValue_->setVisible(showAudioProps && !clip->autoTempo);
        stretchModeCombo_.setVisible(showAudioProps);
        if (isAudioClip) {
            clipStretchValue_->setValue(clip->speedRatio, juce::dontSendNotification);
            // Show effective stretch mode (auto-upgraded when autoTempo/warp is active,
            // or when pitchChange != 0 without analog pitch — TE uses SoundTouch)
            int effectiveMode = clip->timeStretchMode;
            bool isAnalog = clip->isAnalogPitchActive();
            if (!isAnalog && effectiveMode == 0 &&
                (clip->autoTempo || clip->warpEnabled || std::abs(clip->speedRatio - 1.0) > 0.001 ||
                 std::abs(clip->pitchChange) > 0.001f)) {
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

        // Pitch/Transpose section (audio + MIDI clips)
        bool isMidiClip = (clip->type == magda::ClipType::MIDI);
        pitchSectionLabel_.setVisible(showAudioProps);
        autoPitchToggle_.setVisible(false);     // hidden for now
        autoPitchModeCombo_.setVisible(false);  // hidden for now
        pitchChangeValue_->setVisible(showAudioProps);
        midiTransposeUpBtn_.setVisible(isMidiClip);
        midiTransposeDownBtn_.setVisible(isMidiClip);
        midiTransposeLabel_.setVisible(isMidiClip);

        // Analog pitch toggle: visible for audio clips when not in autoTempo/warp mode
        bool canAnalog = showAudioProps && !clip->autoTempo && !clip->warpEnabled;
        analogPitchToggle_.setVisible(canAnalog);
        if (canAnalog) {
            analogPitchToggle_.setToggleState(clip->analogPitch, juce::dontSendNotification);
        }

        if (isAudioClip) {
            autoPitchToggle_.setToggleState(clip->autoPitch, juce::dontSendNotification);
            autoPitchModeCombo_.setSelectedId(clip->autoPitchMode + 1, juce::dontSendNotification);
            pitchChangeValue_->setValue(clip->pitchChange, juce::dontSendNotification);

            // autoPitchMode only meaningful when autoPitch is on
            autoPitchModeCombo_.setEnabled(clip->autoPitch);
            autoPitchModeCombo_.setAlpha(clip->autoPitch ? 1.0f : 0.4f);

            // When analogPitch is active: disable/dim speedRatio control
            bool analogActive = clip->isAnalogPitchActive();

            // When analog pitch is active, dim the speed ratio control
            if (analogActive && clipStretchValue_) {
                clipStretchValue_->setEnabled(false);
                clipStretchValue_->setAlpha(0.4f);
            }
        }

        // Groove section (MIDI clips only)
        grooveSectionLabel_.setVisible(isMidiClip);
        grooveTemplateButton_.setVisible(isMidiClip);
        grooveStrengthLabel_.setVisible(isMidiClip);
        grooveStrengthValue_->setVisible(isMidiClip);
        if (isMidiClip) {
            // Update button text to show current template
            grooveTemplateButton_.setButtonText(
                clip->grooveTemplate.isNotEmpty() ? clip->grooveTemplate : "None");

            grooveStrengthValue_->setValue(clip->grooveStrength, juce::dontSendNotification);

            // Dim strength when no template selected
            bool hasGroove = clip->grooveTemplate.isNotEmpty();
            grooveStrengthValue_->setEnabled(hasGroove);
            grooveStrengthValue_->setAlpha(hasGroove ? 1.0f : 0.4f);
            grooveStrengthLabel_.setAlpha(hasGroove ? 1.0f : 0.4f);
        }

        // Mix section (audio clips only) — includes Volume/Pan/Gain + Reverse/L/R
        clipMixSectionLabel_.setVisible(showAudioProps);
        clipVolumeValue_->setVisible(showAudioProps);
        clipPanValue_->setVisible(showAudioProps);
        clipGainValue_->setVisible(showAudioProps);
        reverseToggle_.setVisible(showAudioProps);
        leftChannelToggle_.setVisible(false);
        rightChannelToggle_.setVisible(false);
        if (isAudioClip) {
            clipVolumeValue_->setValue(clip->volumeDB, juce::dontSendNotification);
            clipPanValue_->setValue(clip->pan, juce::dontSendNotification);
            clipGainValue_->setValue(clip->gainDB, juce::dontSendNotification);
            reverseToggle_.setToggleState(clip->isReversed, juce::dontSendNotification);
        }

        // Playback / Beat Detection section — hidden (all controls moved or unused)
        beatDetectionSectionLabel_.setVisible(false);
        autoDetectBeatsToggle_.setVisible(false);
        beatSensitivityValue_->setVisible(false);

        // Transient sensitivity (audio clips only, single-clip only)
        transientSectionLabel_.setVisible(showAudioProps && !isMulti);
        transientSensitivityLabel_.setVisible(showAudioProps && !isMulti);
        transientSensitivityValue_->setVisible(showAudioProps && !isMulti);

        // Fades section (arrangement audio clips only, hidden for session, collapsible)
        bool showFades = showAudioProps && !isSessionClip;
        fadesSectionLabel_.setVisible(showFades);
        fadeInValue_->setVisible(showFades);
        fadeOutValue_->setVisible(showFades);
        for (int i = 0; i < 4; ++i) {
            fadeInTypeButtons_[i]->setVisible(showFades);
            fadeOutTypeButtons_[i]->setVisible(showFades);
        }
        for (int i = 0; i < 2; ++i) {
            fadeInBehaviourButtons_[i]->setVisible(showFades);
            fadeOutBehaviourButtons_[i]->setVisible(showFades);
        }
        autoCrossfadeToggle_.setVisible(showFades);
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

        // Compute range for multi-clip, set midpoints for display
        computeClipRange();
        if (isMulti && isAudioClip && clipRange_.valid) {
            pitchChangeValue_->setValue((clipRange_.minPitchChange + clipRange_.maxPitchChange) /
                                            2.0,
                                        juce::dontSendNotification);
            clipVolumeValue_->setValue((clipRange_.minVolumeDB + clipRange_.maxVolumeDB) / 2.0,
                                       juce::dontSendNotification);
            clipPanValue_->setValue((clipRange_.minPan + clipRange_.maxPan) / 2.0,
                                    juce::dontSendNotification);
            clipGainValue_->setValue((clipRange_.minGainDB + clipRange_.maxGainDB) / 2.0,
                                     juce::dontSendNotification);
            fadeInValue_->setValue((clipRange_.minFadeIn + clipRange_.maxFadeIn) / 2.0,
                                   juce::dontSendNotification);
            fadeOutValue_->setValue((clipRange_.minFadeOut + clipRange_.maxFadeOut) / 2.0,
                                    juce::dontSendNotification);
            clipStretchValue_->setValue((clipRange_.minSpeedRatio + clipRange_.maxSpeedRatio) / 2.0,
                                        juce::dontSendNotification);
        }

        // Always set drag starts from current control values (works for single & multi)
        if (isAudioClip) {
            multiPitchChangeDragStart_ = pitchChangeValue_->getValue();
            multiVolumeDragStart_ = clipVolumeValue_->getValue();
            multiPanDragStart_ = clipPanValue_->getValue();
            multiGainDragStart_ = clipGainValue_->getValue();
            multiFadeInDragStart_ = fadeInValue_->getValue();
            multiFadeOutDragStart_ = fadeOutValue_->getValue();
            multiSpeedRatioDragStart_ = clipStretchValue_->getValue();
        }
        if (!isSessionClip) {
            multiStartDragStart_ = clipStartValue_->getValue();
            multiEndDragStart_ = clipEndValue_->getValue();
        }
        refreshClipRangeDisplay();

        showClipControls(true);
    } else {
        clipCountLabel_.setVisible(false);
        showClipControls(false);
    }

    resized();
    repaint();
}

void ClipInspector::showClipControls(bool show) {
    clipNameValue_.setVisible(show);
    colourSwatch_->setVisible(show);
    clipFilePathLabel_.setVisible(show);
    clipTypeIcon_->setVisible(show);
    clipViewIcon_->setVisible(show);
    clipPropsViewport_.setVisible(show);

    if (!show) {
        // Hide everything managed by viewport container
        audioPropsCollapseToggle_.setVisible(false);
        audioPropsLabel_.setVisible(false);
        clipBpmValue_.setVisible(false);
        clipBeatsLengthValue_->setVisible(false);
        clipPositionIcon_->setVisible(false);
        clipStartLabel_.setVisible(false);
        clipStartValue_->setVisible(false);
        clipEndLabel_.setVisible(false);
        clipEndValue_->setVisible(false);
        clipOffsetLabel_.setVisible(false);
        clipContentOffsetValue_->setVisible(false);
        clipLoopToggle_->setVisible(false);
        clipLoopStartLabel_.setVisible(false);
        clipLoopStartValue_->setVisible(false);
        clipLoopEndLabel_.setVisible(false);
        clipLoopEndValue_->setVisible(false);
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
        analogPitchToggle_.setVisible(false);
        autoPitchModeCombo_.setVisible(false);
        pitchChangeValue_->setVisible(false);
        midiTransposeUpBtn_.setVisible(false);
        midiTransposeDownBtn_.setVisible(false);
        midiTransposeLabel_.setVisible(false);
        grooveSectionLabel_.setVisible(false);
        grooveTemplateButton_.setVisible(false);
        grooveStrengthLabel_.setVisible(false);
        grooveStrengthValue_->setVisible(false);
        clipMixSectionLabel_.setVisible(false);
        clipVolumeValue_->setVisible(false);
        clipPanValue_->setVisible(false);
        clipGainValue_->setVisible(false);
        beatDetectionSectionLabel_.setVisible(false);
        reverseToggle_.setVisible(false);
        autoDetectBeatsToggle_.setVisible(false);
        beatSensitivityValue_->setVisible(false);
        transientSectionLabel_.setVisible(false);
        transientSensitivityLabel_.setVisible(false);
        transientSensitivityValue_->setVisible(false);
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
        channelsSectionLabel_.setVisible(false);
        leftChannelToggle_.setVisible(false);
        rightChannelToggle_.setVisible(false);
    } else {
        // Show always-visible clip controls (viewport is shown, conditional
        // loop row visibility is managed by updateFromSelectedClip)
        // Session clips have no arrangement position — hide the position row
        const auto* clip = magda::ClipManager::getInstance().getClip(primaryClipId());
        bool isSession = clip && clip->view == magda::ClipView::Session;
        clipPositionIcon_->setVisible(!isSession);
        clipStartLabel_.setVisible(!isSession);
        clipStartValue_->setVisible(!isSession);
        clipEndLabel_.setVisible(!isSession);
        clipEndValue_->setVisible(!isSession);
        clipOffsetLabel_.setVisible(!isSession);
        clipContentOffsetValue_->setVisible(!isSession);
        clipLoopToggle_->setVisible(true);
    }

    // Unused labels/icons always hidden
    playbackColumnLabel_.setVisible(false);
    loopColumnLabel_.setVisible(false);
}

void ClipInspector::computeClipRange() {
    clipRange_ = ClipRange{};

    bool first = true;
    for (auto cid : selectedClipIds_) {
        const auto* c = magda::ClipManager::getInstance().getClip(cid);
        if (!c)
            continue;

        if (c->type != magda::ClipType::Audio)
            clipRange_.allAudio = false;
        if (c->type != magda::ClipType::MIDI)
            clipRange_.allMidi = false;
        if (c->view != magda::ClipView::Arrangement)
            clipRange_.allArrangement = false;
        if (c->view != magda::ClipView::Session)
            clipRange_.allSession = false;

        if (first) {
            clipRange_.valid = true;
            clipRange_.minPitchChange = clipRange_.maxPitchChange = c->pitchChange;
            clipRange_.minVolumeDB = clipRange_.maxVolumeDB = c->volumeDB;
            clipRange_.minPan = clipRange_.maxPan = c->pan;
            clipRange_.minGainDB = clipRange_.maxGainDB = c->gainDB;
            clipRange_.minFadeIn = clipRange_.maxFadeIn = c->fadeIn;
            clipRange_.minFadeOut = clipRange_.maxFadeOut = c->fadeOut;
            clipRange_.minSpeedRatio = clipRange_.maxSpeedRatio = c->speedRatio;
            clipRange_.minStartSeconds = clipRange_.maxStartSeconds = c->startTime;
            clipRange_.minLengthSeconds = clipRange_.maxLengthSeconds = c->length;
            clipRange_.minOffsetSeconds = clipRange_.maxOffsetSeconds = c->offset;
            first = false;
        } else {
            clipRange_.minPitchChange = juce::jmin(clipRange_.minPitchChange, c->pitchChange);
            clipRange_.maxPitchChange = juce::jmax(clipRange_.maxPitchChange, c->pitchChange);
            clipRange_.minVolumeDB = juce::jmin(clipRange_.minVolumeDB, c->volumeDB);
            clipRange_.maxVolumeDB = juce::jmax(clipRange_.maxVolumeDB, c->volumeDB);
            clipRange_.minPan = juce::jmin(clipRange_.minPan, c->pan);
            clipRange_.maxPan = juce::jmax(clipRange_.maxPan, c->pan);
            clipRange_.minGainDB = juce::jmin(clipRange_.minGainDB, c->gainDB);
            clipRange_.maxGainDB = juce::jmax(clipRange_.maxGainDB, c->gainDB);
            clipRange_.minFadeIn = juce::jmin(clipRange_.minFadeIn, c->fadeIn);
            clipRange_.maxFadeIn = juce::jmax(clipRange_.maxFadeIn, c->fadeIn);
            clipRange_.minFadeOut = juce::jmin(clipRange_.minFadeOut, c->fadeOut);
            clipRange_.maxFadeOut = juce::jmax(clipRange_.maxFadeOut, c->fadeOut);
            clipRange_.minSpeedRatio = juce::jmin(clipRange_.minSpeedRatio, c->speedRatio);
            clipRange_.maxSpeedRatio = juce::jmax(clipRange_.maxSpeedRatio, c->speedRatio);
            clipRange_.minStartSeconds = juce::jmin(clipRange_.minStartSeconds, c->startTime);
            clipRange_.maxStartSeconds = juce::jmax(clipRange_.maxStartSeconds, c->startTime);
            clipRange_.minLengthSeconds = juce::jmin(clipRange_.minLengthSeconds, c->length);
            clipRange_.maxLengthSeconds = juce::jmax(clipRange_.maxLengthSeconds, c->length);
            clipRange_.minOffsetSeconds = juce::jmin(clipRange_.minOffsetSeconds, c->offset);
            clipRange_.maxOffsetSeconds = juce::jmax(clipRange_.maxOffsetSeconds, c->offset);
        }
    }
}

void ClipInspector::refreshClipRangeDisplay() {
    if (!clipRange_.valid || selectedClipIds_.size() <= 1) {
        // Single clip: clear any text overrides
        pitchChangeValue_->clearTextOverride();
        clipVolumeValue_->clearTextOverride();
        clipPanValue_->clearTextOverride();
        clipGainValue_->clearTextOverride();
        fadeInValue_->clearTextOverride();
        fadeOutValue_->clearTextOverride();
        if (clipStretchValue_)
            clipStretchValue_->clearTextOverride();
        clipStartValue_->clearTextOverride();
        clipEndValue_->clearTextOverride();
        clipContentOffsetValue_->clearTextOverride();
        clipLoopStartValue_->clearTextOverride();
        clipLoopEndValue_->clearTextOverride();
        clipLoopPhaseValue_->clearTextOverride();
        return;
    }

    static const juce::String multiDash("-");

    pitchChangeValue_->setTextOverride(multiDash);
    clipVolumeValue_->setTextOverride(multiDash);
    clipPanValue_->setTextOverride(multiDash);
    clipGainValue_->setTextOverride(multiDash);
    fadeInValue_->setTextOverride(multiDash);
    fadeOutValue_->setTextOverride(multiDash);
    if (clipStretchValue_)
        clipStretchValue_->setTextOverride(multiDash);
    clipStartValue_->setTextOverride(multiDash);
    clipEndValue_->setTextOverride(multiDash);
    clipContentOffsetValue_->setTextOverride(multiDash);
    clipLoopStartValue_->setTextOverride(multiDash);
    clipLoopEndValue_->setTextOverride(multiDash);
    clipLoopPhaseValue_->setTextOverride(multiDash);
}

}  // namespace magda::daw::ui
