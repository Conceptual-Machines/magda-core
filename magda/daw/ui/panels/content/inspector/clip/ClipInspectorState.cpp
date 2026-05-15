#include <cmath>

#include "../../../../../audio/AudioThumbnailManager.hpp"
#include "../../../../components/common/ColourSwatch.hpp"
#include "../../../../state/TimelineController.hpp"
#include "../../../../utils/TimelineUtils.hpp"
#include "../ClipInspector.hpp"
#include "BinaryData.h"
#include "core/ClipDisplayInfo.hpp"
#include "core/TempoUtils.hpp"
#include "core/TrackManager.hpp"
#include "engine/TracktionEngineWrapper.hpp"

namespace magda::daw::ui {
namespace {

double getAudioFileDurationForInspector(const magda::ClipInfo& clip) {
    if (!clip.isAudio() || clip.audio().source.filePath.isEmpty())
        return 0.0;

    if (auto* thumbnail = magda::AudioThumbnailManager::getInstance().getThumbnail(
            clip.audio().source.filePath)) {
        return thumbnail->getTotalLength();
    }

    return clip.audio().source.durationSeconds;
}

double timelineStartSeconds(const magda::ClipInfo& clip, double bpm) {
    return clip.getTimelineStart(bpm);
}

double timelineLengthSeconds(const magda::ClipInfo& clip, double bpm) {
    return clip.getTimelineLength(bpm);
}

}  // namespace

void ClipInspector::updateAudioSourceValueDisplays(const magda::ClipInfo& clip) {
    if (!clip.isAudio()) {
        return;
    }

    const bool showAudioProps = !audioPropsCollapsed_ && clip.isAudio();
    if (showAudioProps) {
        double displayBPM = clip.audio().interpretation.bpm;
        double projectBPM = timelineController_ ? timelineController_->getState().tempo.bpm : 120.0;
        if (displayBPM <= 0.0 || (!clip.autoTempo && std::abs(displayBPM - projectBPM) < 0.1)) {
            displayBPM = magda::AudioThumbnailManager::getInstance().getCachedBPM(
                clip.audio().source.filePath);
        }

        if (displayBPM > 0.0) {
            clipBpmValue_.setText(juce::String(displayBPM, 1), juce::dontSendNotification);
        } else {
            clipBpmValue_.setText(juce::String::fromUTF8("\xe2\x80\x94"),
                                  juce::dontSendNotification);
        }
    }

    if (showAudioProps && clip.autoTempo && clipBeatsLengthValue_ &&
        !clipBeatsLengthValue_->isDragging()) {
        clipBeatsLengthValue_->setValue(clip.audio().interpretation.totalBeats > 0.0
                                            ? clip.audio().interpretation.totalBeats
                                            : 4.0,
                                        juce::dontSendNotification);
    }
}

void ClipInspector::updateLoopValueDisplays(const magda::ClipInfo& clip, double projectBPM,
                                            int beatsPerBar) {
    if (!clipLoopStartValue_ || !clipLoopEndValue_ || !clipLoopPhaseValue_)
        return;

    clipLoopStartValue_->setBeatsPerBar(beatsPerBar);
    clipLoopEndValue_->setBeatsPerBar(beatsPerBar);
    clipLoopPhaseValue_->setBeatsPerBar(beatsPerBar);

    double loopBpm = magda::isValidBpm(projectBPM) ? projectBPM : magda::DEFAULT_BPM;

    if (clip.isAudio()) {
        const auto info =
            magda::ClipDisplayInfo::from(clip, loopBpm, getAudioFileDurationForInspector(clip));
        const bool loopOn =
            clip.view == magda::ClipView::Session || clip.loopEnabled || clip.autoTempo;

        clipLoopStartValue_->setValue(
            magda::TimelineUtils::secondsToBeats(info.loopStartPositionSeconds, loopBpm),
            juce::dontSendNotification);
        clipLoopEndValue_->setValue(
            magda::TimelineUtils::secondsToBeats(info.loopEndPositionSeconds, loopBpm),
            juce::dontSendNotification);

        clipLoopPhaseLabel_.setText(loopOn ? "phase" : "offset", juce::dontSendNotification);
        const double displayPositionSeconds =
            loopOn ? info.loopPhasePositionSeconds - info.loopStartPositionSeconds
                   : info.offsetPositionSeconds;
        clipLoopPhaseValue_->setValue(
            magda::TimelineUtils::secondsToBeats(juce::jmax(0.0, displayPositionSeconds), loopBpm),
            juce::dontSendNotification);
        return;
    }

    const double loopStartBeats = magda::TimelineUtils::secondsToBeats(clip.loopStart, loopBpm);
    clipLoopStartValue_->setValue(loopStartBeats, juce::dontSendNotification);

    double loopLengthDisplayBeats = 0.0;
    if (clip.autoTempo && clip.loopLengthBeats > 0.0) {
        loopLengthDisplayBeats = clip.loopLengthBeats;
    } else {
        double projectBPM = timelineController_ ? timelineController_->getState().tempo.bpm : 120.0;
        const double sourceLength = clip.getSourceLoopLength() > 0.0
                                        ? clip.getSourceLoopLength()
                                        : clip.timelineToSource(clip.getTimelineLength(projectBPM));
        loopLengthDisplayBeats = magda::TimelineUtils::secondsToBeats(sourceLength, loopBpm);
    }
    clipLoopEndValue_->setValue(loopStartBeats + loopLengthDisplayBeats,
                                juce::dontSendNotification);

    const bool loopOn = clip.view == magda::ClipView::Session || clip.loopEnabled || clip.autoTempo;
    clipLoopPhaseLabel_.setText(loopOn ? "phase" : "offset", juce::dontSendNotification);

    if (clip.isMidi()) {
        clipLoopPhaseValue_->setValue(clip.midiOffset, juce::dontSendNotification);
    } else {
        const double sourcePositionSeconds = loopOn ? clip.offset - clip.loopStart : clip.offset;
        const double sourcePositionBeats =
            magda::TimelineUtils::secondsToBeats(juce::jmax(0.0, sourcePositionSeconds), loopBpm);
        clipLoopPhaseValue_->setValue(sourcePositionBeats, juce::dontSendNotification);
    }
}

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
        if (mutableClip && mutableClip->isAudio() &&
            !mutableClip->audio().source.filePath.isEmpty()) {
            auto* thumbnail = magda::AudioThumbnailManager::getInstance().getThumbnail(
                mutableClip->audio().source.filePath);
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
        if (clip->isAudio() && clip->audio().source.filePath.isNotEmpty() &&
            clip->view != magda::ClipView::Session && !isMulti) {
            juce::File audioFile(clip->audio().source.filePath);
            clipFilePathLabel_.setText(audioFile.getFileName(), juce::dontSendNotification);
            clipFilePathLabel_.setTooltip(clip->audio().source.filePath);
        } else {
            clipFilePathLabel_.setText("", juce::dontSendNotification);
            clipFilePathLabel_.setTooltip("");
        }

        // Update type icon based on clip type
        bool isAudioClip = (clip->isAudio());
        bool showAudioProps = isAudioClip && !audioPropsCollapsed_;
        audioPropsCollapseToggle_.setVisible(isAudioClip);
        audioPropsLabel_.setVisible(isAudioClip);

        if (isAudioClip) {
            clipTypeIcon_->updateSvgData(BinaryData::audio_clip_svg,
                                         BinaryData::audio_clip_svgSize);
            clipTypeIcon_->setTooltip("Audio clip");
        } else {
            clipTypeIcon_->updateSvgData(BinaryData::midi_clip_svg, BinaryData::midi_clip_svgSize);
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
        // Prefer clip's source interpretation BPM (may be user-edited), fall back to detected BPM
        if (showAudioProps && !isMulti) {
            double displayBPM = clip->audio().interpretation.bpm;
            double projectBPM =
                timelineController_ ? timelineController_->getState().tempo.bpm : 120.0;
            if (displayBPM <= 0.0 ||
                (!clip->autoTempo && std::abs(displayBPM - projectBPM) < 0.1)) {
                // source interpretation BPM is unset or matches project BPM (defaulted) — use
                // detected. Read cached value only; if missing, kick off async detection and
                // refresh the inspector via the existing clipPropertyChanged listener path.
                auto& thumbs = magda::AudioThumbnailManager::getInstance();
                displayBPM = thumbs.getCachedBPM(clip->audio().source.filePath);
                if (displayBPM <= 0.0) {
                    auto cid = pid;
                    thumbs.requestBPMDetection(clip->audio().source.filePath, [cid](double bpm) {
                        if (bpm <= 0.0)
                            return;
                        magda::ClipManager::getInstance().forceNotifyClipPropertyChanged(cid);
                    });
                }
            }
            clipBpmValue_.setVisible(true);
            clipBpmUnitLabel_.setVisible(true);
            updateAudioSourceValueDisplays(*clip);
        } else {
            clipBpmValue_.setVisible(false);
            clipBpmUnitLabel_.setVisible(false);
        }

        // Show source interpretation total beats for audio clips with auto-tempo enabled.
        // Clip placement length is already represented by start/end and by the clip body itself.
        if (showAudioProps && clip->autoTempo && !isMulti) {
            clipBeatsLengthValue_->setVisible(true);
            clipBeatsUnitLabel_.setVisible(true);
            clipBeatsLengthValue_->setEnabled(true);
            clipBeatsLengthValue_->setAlpha(1.0f);
            updateAudioSourceValueDisplays(*clip);
        } else {
            clipBeatsLengthValue_->setVisible(false);
            clipBeatsUnitLabel_.setVisible(false);
        }

        // Get tempo from TimelineController, fallback to 120 BPM if not available
        double bpm = 120.0;
        int beatsPerBar = magda::DEFAULT_TIME_SIGNATURE_NUMERATOR;
        if (timelineController_) {
            const auto& state = timelineController_->getState();
            bpm = state.tempo.bpm;
            beatsPerBar = state.tempo.timeSignatureNumerator;
        }

        bool isSessionClip = (clip->view == magda::ClipView::Session);

        // Update beatsPerBar on all draggable labels
        clipStartValue_->setBeatsPerBar(beatsPerBar);
        clipEndValue_->setBeatsPerBar(beatsPerBar);
        clipLengthValue_->setBeatsPerBar(beatsPerBar);
        clipLoopEndValue_->setBeatsPerBar(beatsPerBar);

        if (isSessionClip) {
            // Session clips: hide the position row entirely (no arrangement position)
            clipPositionIcon_->setVisible(false);
            clipStartLabel_.setVisible(false);
            clipStartValue_->setVisible(false);
            clipEndLabel_.setVisible(false);
            clipEndValue_->setVisible(false);
            clipLengthLabel_.setVisible(false);
            clipLengthValue_->setVisible(false);
        } else {
            // Arrangement clips: start/end as positions, len as duration in beats
            clipPositionIcon_->setVisible(true);
            clipStartLabel_.setVisible(true);
            clipStartValue_->setVisible(true);
            clipStartValue_->setEnabled(true);
            clipStartValue_->setAlpha(1.0f);
            clipEndLabel_.setVisible(true);
            clipEndValue_->setVisible(true);
            clipLengthLabel_.setVisible(true);
            clipLengthValue_->setVisible(true);
            clipLengthLabel_.setText("len", juce::dontSendNotification);
            clipLengthValue_->setEnabled(true);
            clipLengthValue_->setAlpha(1.0f);

            clipStartValue_->setValue(clip->getStartBeats(bpm), juce::dontSendNotification);
            clipEndValue_->setValue(clip->getEndBeats(bpm), juce::dontSendNotification);
            clipLengthValue_->setValue(clip->getLengthInBeats(bpm), juce::dontSendNotification);
        }

        clipLoopToggle_->setActive(clip->loopEnabled || clip->autoTempo);
        // Beat mode forces loop on, but the button should still read as active rather than
        // disabled. Click handling ignores attempts to toggle it while auto-tempo owns looping.
        clipLoopToggle_->setEnabled(true);

        // Loop state determines source-row labels/interactivity.
        bool loopOn = isSessionClip || clip->loopEnabled || clip->autoTempo;
        updateLoopValueDisplays(*clip, bpm, beatsPerBar);

        if (loopOn) {
            // Show loop row: lstart | lend | phase
            clipLoopStartLabel_.setVisible(true);
            clipLoopStartValue_->setVisible(true);
            clipLoopStartValue_->setEnabled(true);
            clipLoopStartValue_->setAlpha(1.0f);
            clipLoopStartLabel_.setAlpha(1.0f);

            clipLoopEndLabel_.setVisible(true);
            clipLoopEndValue_->setVisible(true);
            clipLoopEndValue_->setEnabled(true);
            clipLoopEndValue_->setAlpha(1.0f);
            clipLoopEndLabel_.setAlpha(1.0f);

            clipLoopPhaseLabel_.setVisible(true);
            clipLoopPhaseValue_->setVisible(true);
            clipLoopPhaseValue_->setEnabled(true);
            clipLoopPhaseValue_->setAlpha(1.0f);
            clipLoopPhaseLabel_.setAlpha(1.0f);
        } else {
            // Loop OFF: loop start/end are shown but greyed out; offset remains editable.
            clipLoopStartLabel_.setVisible(true);
            clipLoopStartValue_->setVisible(true);
            clipLoopStartValue_->setEnabled(false);
            clipLoopStartValue_->setAlpha(0.4f);
            clipLoopStartLabel_.setAlpha(0.4f);

            clipLoopEndLabel_.setVisible(true);
            clipLoopEndValue_->setVisible(true);
            clipLoopEndValue_->setEnabled(false);
            clipLoopEndValue_->setAlpha(0.4f);
            clipLoopEndLabel_.setAlpha(0.4f);

            clipLoopPhaseLabel_.setVisible(true);
            clipLoopPhaseValue_->setVisible(true);
            clipLoopPhaseValue_->setEnabled(true);
            clipLoopPhaseValue_->setAlpha(1.0f);
            clipLoopPhaseLabel_.setAlpha(1.0f);
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
        followActionLabel_.setVisible(isSessionClip);
        followActionCombo_.setVisible(isSessionClip);
        const bool showFollowControls =
            isSessionClip && clip->followAction != magda::FollowAction::None;
        followActionDelayLabel_.setVisible(showFollowControls);
        followActionDelaySlider_.setVisible(showFollowControls);
        followActionLoopCountLabel_.setVisible(showFollowControls);
        followActionLoopCountSlider_.setVisible(showFollowControls);

        if (isSessionClip) {
            launchQuantizeCombo_.setSelectedId(static_cast<int>(clip->launchQuantize) + 1,
                                               juce::dontSendNotification);
            followActionCombo_.setSelectedId(static_cast<int>(clip->followAction) + 1,
                                             juce::dontSendNotification);
            followActionDelaySlider_.setValue(clip->followActionDelayBeats,
                                              juce::dontSendNotification);
            followActionLoopCountSlider_.setValue(clip->followActionLoopCount,
                                                  juce::dontSendNotification);
        }

        // ====================================================================
        // New audio clip property sections
        // ====================================================================

        // Pitch/Transpose section (audio + MIDI clips)
        bool isMidiClip = (clip->isMidi());
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

        // Fades section — ClipFadesSection handles session vs arrangement internally
        fadesSection_->setSelectedClips(selectedClipIds_);
        fadesSection_->setVisible(showAudioProps);

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
            clipStretchValue_->setValue((clipRange_.minSpeedRatio + clipRange_.maxSpeedRatio) / 2.0,
                                        juce::dontSendNotification);
        }

        // Always set drag starts from current control values (works for single & multi)
        if (isAudioClip) {
            multiPitchChangeDragStart_ = pitchChangeValue_->getValue();
            multiVolumeDragStart_ = clipVolumeValue_->getValue();
            multiPanDragStart_ = clipPanValue_->getValue();
            multiGainDragStart_ = clipGainValue_->getValue();
            multiSpeedRatioDragStart_ = clipStretchValue_->getValue();
        }
        if (!isSessionClip) {
            multiStartDragStart_ = clipStartValue_->getValue();
            multiEndDragStart_ = clipEndValue_->getValue();
            multiLengthDragStart_ = clipLengthValue_->getValue();
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
        clipBpmUnitLabel_.setVisible(false);
        clipBeatsLengthValue_->setVisible(false);
        clipBeatsUnitLabel_.setVisible(false);
        clipPositionIcon_->setVisible(false);
        clipStartLabel_.setVisible(false);
        clipStartValue_->setVisible(false);
        clipEndLabel_.setVisible(false);
        clipEndValue_->setVisible(false);
        clipLengthLabel_.setVisible(false);
        clipLengthValue_->setVisible(false);
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
        followActionLabel_.setVisible(false);
        followActionCombo_.setVisible(false);
        followActionDelayLabel_.setVisible(false);
        followActionDelaySlider_.setVisible(false);
        followActionLoopCountLabel_.setVisible(false);
        followActionLoopCountSlider_.setVisible(false);
        if (fadesSection_)
            fadesSection_->setVisible(false);

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
        clipLengthLabel_.setVisible(!isSession);
        clipLengthValue_->setVisible(!isSession);
        clipLoopToggle_->setVisible(true);
    }

    // Unused labels/icons always hidden
    playbackColumnLabel_.setVisible(false);
    loopColumnLabel_.setVisible(false);
}

void ClipInspector::computeClipRange() {
    clipRange_ = ClipRange{};
    const double bpm = timelineController_ ? timelineController_->getState().tempo.bpm : 120.0;

    bool first = true;
    for (auto cid : selectedClipIds_) {
        const auto* c = magda::ClipManager::getInstance().getClip(cid);
        if (!c)
            continue;

        if (!c->isAudio())
            clipRange_.allAudio = false;
        if (!c->isMidi())
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
            clipRange_.minSpeedRatio = clipRange_.maxSpeedRatio = c->speedRatio;
            clipRange_.minStartSeconds = clipRange_.maxStartSeconds = timelineStartSeconds(*c, bpm);
            clipRange_.minLengthSeconds = clipRange_.maxLengthSeconds =
                timelineLengthSeconds(*c, bpm);
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
            clipRange_.minSpeedRatio = juce::jmin(clipRange_.minSpeedRatio, c->speedRatio);
            clipRange_.maxSpeedRatio = juce::jmax(clipRange_.maxSpeedRatio, c->speedRatio);
            const double startSeconds = timelineStartSeconds(*c, bpm);
            const double lengthSeconds = timelineLengthSeconds(*c, bpm);
            clipRange_.minStartSeconds = juce::jmin(clipRange_.minStartSeconds, startSeconds);
            clipRange_.maxStartSeconds = juce::jmax(clipRange_.maxStartSeconds, startSeconds);
            clipRange_.minLengthSeconds = juce::jmin(clipRange_.minLengthSeconds, lengthSeconds);
            clipRange_.maxLengthSeconds = juce::jmax(clipRange_.maxLengthSeconds, lengthSeconds);
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
        if (clipStretchValue_)
            clipStretchValue_->clearTextOverride();
        clipStartValue_->clearTextOverride();
        clipEndValue_->clearTextOverride();
        clipLengthValue_->clearTextOverride();
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
    if (clipStretchValue_)
        clipStretchValue_->setTextOverride(multiDash);
    clipStartValue_->setTextOverride(multiDash);
    clipEndValue_->setTextOverride(multiDash);
    clipLengthValue_->setTextOverride(multiDash);
    clipLoopStartValue_->setTextOverride(multiDash);
    clipLoopEndValue_->setTextOverride(multiDash);
    clipLoopPhaseValue_->setTextOverride(multiDash);
}

}  // namespace magda::daw::ui
