#include <cmath>

#include "../../../../../audio/AudioThumbnailManager.hpp"
#include "../../../../components/common/ColourSwatch.hpp"
#include "../../../../state/TimelineController.hpp"
#include "../../../../utils/TimelineUtils.hpp"
#include "../ClipInspector.hpp"
#include "audio/AraTracks.hpp"
#include "core/AudioClipSourceDisplay.hpp"
#include "core/ClipDisplayInfo.hpp"
#include "core/TempoUtils.hpp"
#include "core/TrackManager.hpp"

namespace magda::daw::ui {
namespace {

double getAudioFileDurationForInspector(const magda::ClipInfo& clip) {
    if (!clip.isAudio() || audioEventRef(clip).sourceFilePath().isEmpty())
        return 0.0;

    if (auto* thumbnail = magda::AudioThumbnailManager::getInstance().getThumbnail(
            audioEventRef(clip).sourceFilePath())) {
        return thumbnail->getTotalLength();
    }

    return audioEventRef(clip).sourceDurationSeconds();
}

bool isChordTrackClip(const magda::ClipInfo& clip) {
    const auto* track = magda::TrackManager::getInstance().getTrack(clip.trackId);
    return track != nullptr && track->type == magda::TrackType::Chord;
}

/** @brief "44.1k", "48k": a sample rate as the header shows it. */
juce::String formatSampleRate(double rate) {
    const double k = rate / 1000.0;
    return (std::abs(k - std::round(k)) < 0.05 ? juce::String(juce::roundToInt(k))
                                               : juce::String(k, 1)) +
           "k";
}

/** @brief The audio source's name when it is not the file: a comp or one of several takes. */
juce::String takeLabel(const magda::ClipInfo& clip) {
    const auto& audio = clip.audio();
    if (audio.compActive)
        return "Comp";
    if (audio.takes.size() > 1)
        return "Take " + juce::String(audio.currentTakeIndex + 1) + " / " +
               juce::String(static_cast<int>(audio.takes.size()));
    return {};
}

}  // namespace

juce::String ClipInspector::sublineFor(const magda::ClipInfo& clip) const {
    juce::StringArray parts;
    parts.add(isChordTrackClip(clip) ? "Chord" : clip.isAudio() ? "Audio" : "MIDI");
    if (clip.view == magda::ClipView::Session)
        parts.add("Session");
    if (clip.isAudio())
        parts.addIfNotAlreadyThere(takeLabel(clip));

    const int numerator = timelineController_
                              ? timelineController_->getState().tempo.timeSignatureNumerator
                              : magda::DEFAULT_TIME_SIGNATURE_NUMERATOR;
    const double bars = clip.getLengthInBeats() / juce::jmax(1, numerator);
    const bool wholeBars = std::abs(bars - std::round(bars)) < 0.01;
    const auto barsText = wholeBars ? juce::String(juce::roundToInt(bars)) : juce::String(bars, 2);
    parts.add(barsText + (wholeBars && juce::roundToInt(bars) == 1 ? " bar" : " bars"));

    if (clip.isAudio()) {
        const double rate = magda::audioEventRef(clip).sourceSampleRate();
        if (rate > 0.0)
            parts.add(formatSampleRate(rate));
    } else {
        const auto notes = static_cast<int>(clip.midiNotes.size());
        parts.add(juce::String(notes) + (notes == 1 ? " note" : " notes"));
    }
    parts.removeEmptyStrings();
    return parts.joinIntoString(juce::String::fromUTF8(" \xc2\xb7 "));
}

void ClipInspector::updateAudioSourceValueDisplays(const magda::ClipInfo& clip) {
    if (!clip.isAudio() || warpHeader_.isCollapsed())
        return;

    // Shared with the audio-editor inspector so the two can't drift.
    const double cachedBpm = magda::AudioThumbnailManager::getInstance().getCachedBPM(
        audioEventRef(clip).sourceFilePath());
    const auto display = magda::computeAudioClipSourceDisplay(
        clip, projectBpm(), getAudioFileDurationForInspector(clip), cachedBpm);
    displayedSourceBpm_ = display.bpm;

    if (clipBpmValue_.isBeingEdited()) {
        // Never under the user's cursor: setText would close the editor.
    } else if (display.bpm > 0.0) {
        clipBpmValue_.setText(juce::String(display.bpm, 2), juce::dontSendNotification);
    } else {
        clipBpmValue_.setText(juce::String::fromUTF8("\xe2\x80\x94"), juce::dontSendNotification);
    }

    if (clipBeatsLengthValue_ && !clipBeatsLengthValue_->isDragging()) {
        clipBeatsLengthValue_->setValue(display.totalBeats > 0.0 ? display.totalBeats : 4.0,
                                        juce::dontSendNotification);
    }
}

void ClipInspector::updatePitchDisplays(float pitchChange) {
    const double semitones = std::trunc(pitchChange);
    const double cents = std::round((pitchChange - semitones) * 100.0);
    pitchSemitonesValue_->setValue(semitones, juce::dontSendNotification);
    pitchCentsValue_->setValue(cents, juce::dontSendNotification);
    multiSemitonesDragStart_ = semitones;
    multiCentsDragStart_ = cents;
}

void ClipInspector::updateLoopValueDisplays(const magda::ClipInfo& clip, double projectBPM,
                                            int numerator, int denominator) {
    if (!clipLoopStartValue_ || !clipLoopEndValue_ || !clipLoopPhaseValue_)
        return;

    clipLoopStartValue_->setTimeSignature(numerator, denominator);
    clipLoopEndValue_->setTimeSignature(numerator, denominator);
    clipLoopPhaseValue_->setTimeSignature(numerator, denominator);

    double loopBpm = magda::isValidBpm(projectBPM) ? projectBPM : magda::DEFAULT_BPM;

    if (clip.isAudio()) {
        const auto info =
            magda::ClipDisplayInfo::from(clip, loopBpm, getAudioFileDurationForInspector(clip));
        const bool loopOn = clip.view == magda::ClipView::Session || clip.loopEnabled ||
                            audioEventRef(clip).autoTempo;

        clipLoopStartValue_->setValue(
            magda::TimelineUtils::secondsToBeats(info.loopStartPositionSeconds, loopBpm),
            juce::dontSendNotification);
        clipLoopEndValue_->setValue(
            magda::TimelineUtils::secondsToBeats(info.loopEndPositionSeconds, loopBpm),
            juce::dontSendNotification);

        clipLoopPhaseLabel_.setText(loopOn ? "PHASE" : "OFFSET", juce::dontSendNotification);
        const double displayPositionSeconds =
            loopOn ? info.loopPhasePositionSeconds - info.loopStartPositionSeconds
                   : info.offsetPositionSeconds;
        clipLoopPhaseValue_->setValue(
            magda::TimelineUtils::secondsToBeats(juce::jmax(0.0, displayPositionSeconds), loopBpm),
            juce::dontSendNotification);
        return;
    }

    // MIDI loops clip beats on the clip.
    clipLoopStartValue_->setValue(clip.loopStartBeats, juce::dontSendNotification);

    double loopLengthDisplayBeats = clip.loopLengthInBeats(loopBpm);
    if (loopLengthDisplayBeats <= 0.0)
        loopLengthDisplayBeats = clip.getLengthInBeats();
    clipLoopEndValue_->setValue(clip.loopStartBeats + loopLengthDisplayBeats,
                                juce::dontSendNotification);

    const bool loopOn = clip.view == magda::ClipView::Session || clip.loopEnabled;
    clipLoopPhaseLabel_.setText(loopOn ? "PHASE" : "OFFSET", juce::dontSendNotification);
    clipLoopPhaseValue_->setValue(clip.midiOffset, juce::dontSendNotification);
}

void ClipInspector::updateFromSelectedClip() {
    const auto* clip = magda::ClipManager::getInstance().getClip(primaryClipId());
    if (clip == nullptr) {
        showClipControls(false);
        return;
    }
    showClipControls(true);
    computeClipRange();

    const bool isMulti = selectedClipIds_.size() > 1;
    const bool isAudio = clip->isAudio();
    const bool isMidi = clip->isMidi();
    const bool isSession = clip->view == magda::ClipView::Session;
    const bool isChordClip = isChordTrackClip(*clip);
    const auto& event = magda::audioEventRef(*clip);

    double bpm = 120.0;
    int numerator = magda::DEFAULT_TIME_SIGNATURE_NUMERATOR;
    int denominator = magda::DEFAULT_TIME_SIGNATURE_DENOMINATOR;
    if (timelineController_) {
        const auto& state = timelineController_->getState();
        bpm = state.tempo.bpm;
        numerator = state.tempo.timeSignatureNumerator;
        denominator = state.tempo.timeSignatureDenominator;
    }

    // ---- Header ----------------------------------------------------------
    if (isMulti) {
        const auto kind = clipRange_.allAudio ? "audio" : clipRange_.allMidi ? "MIDI" : "mixed";
        clipNameValue_.setText(juce::String(static_cast<int>(selectedClipIds_.size())) +
                                   " clips selected",
                               juce::dontSendNotification);
        clipSubline_.setText(juce::String("Multiple ") + kind + " clips",
                             juce::dontSendNotification);
        clipSubline_.setTooltip({});
    } else {
        clipNameValue_.setText(clip->name, juce::dontSendNotification);
        clipSubline_.setText(sublineFor(*clip), juce::dontSendNotification);
        clipSubline_.setTooltip(isAudio ? event.sourceFilePath() : clip->midi().sourceFilePath);
    }
    clipNameValue_.setEditable(false, !isMulti);

    auto* swatch = static_cast<magda::ColourSwatch*>(colourSwatch_.get());
    if (clip->colour == juce::Colour(0xFF444444))
        swatch->clearColour();
    else
        swatch->setColour(clip->colour);
    applyHeaderStyle();

    clipEnabledToggle_->setActive(clip->enabled);

    const auto araPlayer = magda::araPluginForClip(clip->id);
    araChip_.setVisible(araPlayer.has_value());
    if (araPlayer) {
        araChip_.setLetter(araPlayer->substring(0, 1).toUpperCase());
        araChip_.setTooltip("Played by " + *araPlayer);
    }

    const auto ghostSiblings = magda::ClipManager::getInstance().getLinkGroupSiblings(clip->id);
    clipGhostIcon_->setVisible(!ghostSiblings.empty());
    if (!ghostSiblings.empty()) {
        const int groupIndex = magda::ClipManager::getInstance().getLinkGroupIndex(clip->id);
        clipGhostIcon_->setTooltip("Ghost clip " + juce::String(groupIndex) + " of " +
                                   juce::String(static_cast<int>(ghostSiblings.size()) + 1) +
                                   ": content and name mirror the other members");
    }

    saveLibraryButton_.setVisible(!isMulti);
    saveLibraryButton_.setEnabled(!isMulti &&
                                  magda::ClipManager::getInstance().canSaveClipToLibrary(clip->id));

    // ---- Clip: position and loop ------------------------------------------
    const bool clipOpen = !clipHeader_.isCollapsed();
    const bool showPosition = clipOpen && !isSession;
    for (juce::Component* c : std::initializer_list<juce::Component*>{
             &startColumnLabel_, &endColumnLabel_, &lengthColumnLabel_, &loopRowLabel_,
             &clipLoopSwitch_, &clipLoopPhaseLabel_, clipLoopStartValue_.get(),
             clipLoopEndValue_.get(), clipLoopPhaseValue_.get()})
        c->setVisible(clipOpen);
    for (juce::Component* c : std::initializer_list<juce::Component*>{
             &clipRowLabel_, clipStartValue_.get(), clipEndValue_.get(), clipLengthValue_.get()})
        c->setVisible(showPosition);

    clipStartValue_->setTimeSignature(numerator, denominator);
    clipEndValue_->setTimeSignature(numerator, denominator);
    clipLengthValue_->setTimeSignature(numerator, denominator);
    if (!isSession) {
        clipStartValue_->setValue(clip->getStartBeats(bpm), juce::dontSendNotification);
        clipEndValue_->setValue(clip->getEndBeats(bpm), juce::dontSendNotification);
        clipLengthValue_->setValue(clip->getLengthInBeats(), juce::dontSendNotification);
        multiStartDragStart_ = clipStartValue_->getValue();
        multiEndDragStart_ = clipEndValue_->getValue();
        multiLengthDragStart_ = clipLengthValue_->getValue();
    }

    clipLoopSwitch_.setOn(clip->loopEnabled);
    updateLoopValueDisplays(*clip, bpm, numerator, denominator);
    // Loop off: start and end show but rest; the offset stays editable.
    const bool loopOn = isSession || clip->loopEnabled;
    for (auto* value : {clipLoopStartValue_.get(), clipLoopEndValue_.get()}) {
        value->setEnabled(loopOn);
        value->setAlpha(loopOn ? 1.0f : 0.4f);
    }
    loopRowLabel_.setAlpha(loopOn ? 1.0f : 0.6f);

    takesSection_->setSelectedClips(selectedClipIds_);
    takesSection_->setVisible(clipOpen && takesSection_->hasContent());
    chordProgressionSection_->setSelectedClips(selectedClipIds_);
    chordProgressionSection_->setVisible(clipOpen && chordProgressionSection_->hasContent());

    // ---- Mix (audio) ------------------------------------------------------
    mixHeader_.setVisible(isAudio);
    const bool mixOpen = isAudio && !mixHeader_.isCollapsed();
    for (juce::Component* c : std::initializer_list<juce::Component*>{
             &volumeLabel_, &panLabel_, &gainLabel_, clipVolumeValue_.get(), clipPanValue_.get(),
             clipGainValue_.get(), &reverseToggle_, &gainEnvelopeToggle_})
        c->setVisible(mixOpen);
    if (isAudio) {
        clipVolumeValue_->setValue(clip->volumeDB, juce::dontSendNotification);
        clipPanValue_->setValue(clip->pan, juce::dontSendNotification);
        clipGainValue_->setValue(clip->gainDB, juce::dontSendNotification);
        reverseToggle_.setToggleState(event.reversed, juce::dontSendNotification);
        gainEnvelopeToggle_.setToggleState(clip->gainEnvelopeShown, juce::dontSendNotification);
    }

    // ---- Warp & pitch (audio) ----------------------------------------------
    warpHeader_.setVisible(isAudio);
    const bool warpOpen = isAudio && !warpHeader_.isCollapsed();
    const bool beatMode = isAudio && event.autoTempo;
    const bool canAnalog = warpOpen && !event.autoTempo && !event.warpEnabled;

    clipWarpSwitch_.setOn(isAudio && event.warpEnabled);
    playbackModeStrip_.setVisible(warpOpen);
    playbackModeStrip_.setSelectedIndex(beatMode ? 0 : 1);
    stretchModeCombo_.setVisible(warpOpen);
    if (isAudio) {
        // The effective mode: beat mode, warp, speed or pitch engage stretching silently.
        stretchModeCombo_.setSelectedId(event.getEffectiveTimeStretchMode() + 1,
                                        juce::dontSendNotification);
    }

    const bool showTransients = warpOpen && !isMulti;
    transientSensitivityLabel_.setVisible(showTransients);
    transientSensitivityValue_->setVisible(showTransients);

    // Tempo and length are live once beat mode is asked for, granted or waiting on a tempo
    // (#2676). A raw clip has no tempo to state (#2791).
    const bool showTempo = warpOpen && !isMulti;
    const bool tempoLive = isAudio && event.wantsBeatMode();
    for (juce::Component* c : std::initializer_list<juce::Component*>{
             &tempoLabel_, &clipBpmValue_, &clipBpmUnitLabel_, &halveTempoButton_,
             &doubleTempoButton_, &sourceLengthLabel_, clipBeatsLengthValue_.get()}) {
        c->setVisible(showTempo);
        c->setEnabled(tempoLive);
        c->setAlpha(tempoLive ? 1.0f : 0.5f);
    }
    if (showTempo)
        updateAudioSourceValueDisplays(*clip);

    const bool showSpeed = warpOpen && !beatMode;
    speedLabel_.setVisible(showSpeed);
    clipStretchValue_->setVisible(showSpeed);
    analogPitchToggle_.setVisible(showSpeed);
    analogPitchToggle_.setEnabled(canAnalog);
    analogPitchToggle_.setToggleState(isAudio && event.analogPitch, juce::dontSendNotification);
    if (isAudio) {
        clipStretchValue_->setValue(event.speedRatio, juce::dontSendNotification);
        const bool speedEditable = !event.isAnalogPitchActive();
        clipStretchValue_->setEnabled(speedEditable);
        clipStretchValue_->setAlpha(speedEditable ? 1.0f : 0.4f);
    }

    pitchLabel_.setVisible(warpOpen);
    pitchSemitonesValue_->setVisible(warpOpen);
    pitchCentsValue_->setVisible(warpOpen);
    if (isAudio)
        updatePitchDisplays(event.pitchChange);

    // On a track an ARA plugin plays, the plugin renders the raw file: MAGDA's mix, stretch
    // and pitch do not apply there, so their controls stand down (#1579). The values stay.
    const auto araPlugin = isAudio ? araPluginForClip(clip->id) : std::nullopt;
    araNoteInMix_ = mixOpen;
    araNoteLabel_.setVisible(araPlugin.has_value() && (mixOpen || warpOpen));
    if (araPlugin)
        araNoteLabel_.setText(*araPlugin +
                                  " plays the raw clip: mix, loop, warp and pitch don't apply",
                              juce::dontSendNotification);

    // Controls nothing above sets each refresh come back only when the stand-down ends, so a
    // switch disabled for its own reason (the strip's tempo lookup) is left alone.
    const std::initializer_list<juce::Component*> unmanaged{&clipLoopSwitch_,
                                                            clipLoopPhaseValue_.get(),
                                                            &volumeLabel_,
                                                            &panLabel_,
                                                            &gainLabel_,
                                                            clipVolumeValue_.get(),
                                                            clipPanValue_.get(),
                                                            clipGainValue_.get(),
                                                            &reverseToggle_,
                                                            &gainEnvelopeToggle_,
                                                            fadesSection_.get(),
                                                            &clipWarpSwitch_,
                                                            &playbackModeStrip_,
                                                            &stretchModeCombo_,
                                                            &transientSensitivityLabel_,
                                                            transientSensitivityValue_.get(),
                                                            &speedLabel_,
                                                            &pitchLabel_,
                                                            pitchSemitonesValue_.get(),
                                                            pitchCentsValue_.get()};
    if (araPlugin) {
        for (juce::Component* c : std::initializer_list<juce::Component*>{
                 &tempoLabel_, &clipBpmValue_, &clipBpmUnitLabel_, &halveTempoButton_,
                 &doubleTempoButton_, &sourceLengthLabel_, clipBeatsLengthValue_.get(),
                 clipStretchValue_.get(), &analogPitchToggle_, clipLoopStartValue_.get(),
                 clipLoopEndValue_.get(), &loopRowLabel_}) {
            c->setEnabled(false);
            c->setAlpha(0.4f);
        }
        for (auto* c : unmanaged) {
            c->setEnabled(false);
            c->setAlpha(0.4f);
        }
    } else if (araStoodDown_) {
        for (auto* c : unmanaged) {
            c->setEnabled(true);
            c->setAlpha(1.0f);
        }
        analogPitchToggle_.setAlpha(1.0f);
    }
    araStoodDown_ = araPlugin.has_value();

    const bool showKey = warpOpen && !isMulti;
    clipKeyLabel_.setVisible(showKey);
    clipKeyRootCombo_.setVisible(showKey);
    if (showKey) {
        static constexpr const char* kRoots[] = {"C",  "C#", "D",  "D#", "E",  "F",
                                                 "F#", "G",  "G#", "A",  "A#", "B"};
        int rootId = 1;
        for (int i = 0; i < 12; ++i) {
            if (event.keyRoot == kRoots[i]) {
                rootId = i + 2;
                break;
            }
        }
        clipKeyRootCombo_.setSelectedId(rootId, juce::dontSendNotification);
    }

    // ---- Pitch (MIDI) -----------------------------------------------------
    midiPitchHeader_.setVisible(isMidi);
    const bool midiPitchOpen = isMidi && !midiPitchHeader_.isCollapsed();
    midiTransposeLabel_.setVisible(midiPitchOpen);
    midiTransposeHint_.setVisible(midiPitchOpen);
    for (auto& button : midiTransposeButtons_)
        button.setVisible(midiPitchOpen);

    // ---- Fades (audio) ----------------------------------------------------
    fadesSection_->setSelectedClips(selectedClipIds_);
    fadesSection_->setVisible(isAudio);

    // ---- Labels (audio) ---------------------------------------------------
    labelsSection_->setSelectedClips(selectedClipIds_);
    timeBendSection_->setSelectedClips(selectedClipIds_);
    // Not on a chord progression, whose notes are generated.
    timeBendSection_->setVisible(!isChordClip &&
                                 timeBendSection_->getPreferredHeight(getWidth()) > 0);
    labelsSection_->setVisible(isAudio && selectedClipIds_.size() == 1);

    // ---- Launch (session) -------------------------------------------------
    launchHeader_.setVisible(isSession);
    const bool launchOpen = isSession && !launchHeader_.isCollapsed();
    const bool showFollowControls = launchOpen && clip->followAction != magda::FollowAction::None;
    launchQuantizeLabel_.setVisible(launchOpen);
    launchQuantizeCombo_.setVisible(launchOpen);
    followActionLabel_.setVisible(launchOpen);
    followActionCombo_.setVisible(launchOpen);
    followActionDelayLabel_.setVisible(showFollowControls);
    followActionDelaySlider_.setVisible(showFollowControls);
    followActionLoopCountLabel_.setVisible(showFollowControls);
    followActionLoopCountSlider_.setVisible(showFollowControls);
    if (isSession) {
        launchQuantizeCombo_.setSelectedId(static_cast<int>(clip->launchQuantize) + 1,
                                           juce::dontSendNotification);
        followActionCombo_.setSelectedId(static_cast<int>(clip->followAction) + 1,
                                         juce::dontSendNotification);
        followActionDelaySlider_.setValue(clip->followActionDelayBeats, juce::dontSendNotification);
        followActionLoopCountSlider_.setValue(clip->followActionLoopCount,
                                              juce::dontSendNotification);
    }

    // ---- Multi-selection: midpoints, with "-" where the clips differ --------
    if (isMulti && isAudio && clipRange_.valid) {
        updatePitchDisplays((clipRange_.minPitchChange + clipRange_.maxPitchChange) / 2.0f);
        clipVolumeValue_->setValue((clipRange_.minVolumeDB + clipRange_.maxVolumeDB) / 2.0,
                                   juce::dontSendNotification);
        clipPanValue_->setValue((clipRange_.minPan + clipRange_.maxPan) / 2.0,
                                juce::dontSendNotification);
        clipGainValue_->setValue((clipRange_.minGainDB + clipRange_.maxGainDB) / 2.0,
                                 juce::dontSendNotification);
        clipStretchValue_->setValue((clipRange_.minSpeedRatio + clipRange_.maxSpeedRatio) / 2.0,
                                    juce::dontSendNotification);
    }
    if (isAudio) {
        multiVolumeDragStart_ = clipVolumeValue_->getValue();
        multiPanDragStart_ = clipPanValue_->getValue();
        multiGainDragStart_ = clipGainValue_->getValue();
        multiSpeedRatioDragStart_ = clipStretchValue_->getValue();
    }
    refreshClipRangeDisplay();

    resized();
    repaint();
}

void ClipInspector::showClipControls(bool show) {
    for (juce::Component* c : std::initializer_list<juce::Component*>{
             &clipNameValue_, &clipSubline_, colourSwatch_.get(), clipEnabledToggle_.get(),
             &clipPropsViewport_})
        c->setVisible(show);
    if (!show) {
        clipGhostIcon_->setVisible(false);
        saveLibraryButton_.setVisible(false);
    }
}

void ClipInspector::computeClipRange() {
    clipRange_ = ClipRange{};
    clipRange_.allAudio = clipRange_.allMidi = true;

    bool first = true;
    for (auto cid : selectedClipIds_) {
        const auto* c = magda::ClipManager::getInstance().getClip(cid);
        if (!c)
            continue;

        clipRange_.allAudio = clipRange_.allAudio && c->isAudio();
        clipRange_.allMidi = clipRange_.allMidi && c->isMidi();

        const auto& event = magda::audioEventRef(*c);
        if (first) {
            clipRange_.valid = true;
            clipRange_.minPitchChange = clipRange_.maxPitchChange = event.pitchChange;
            clipRange_.minVolumeDB = clipRange_.maxVolumeDB = c->volumeDB;
            clipRange_.minPan = clipRange_.maxPan = c->pan;
            clipRange_.minGainDB = clipRange_.maxGainDB = c->gainDB;
            clipRange_.minSpeedRatio = clipRange_.maxSpeedRatio = event.speedRatio;
            first = false;
        } else {
            clipRange_.minPitchChange = juce::jmin(clipRange_.minPitchChange, event.pitchChange);
            clipRange_.maxPitchChange = juce::jmax(clipRange_.maxPitchChange, event.pitchChange);
            clipRange_.minVolumeDB = juce::jmin(clipRange_.minVolumeDB, c->volumeDB);
            clipRange_.maxVolumeDB = juce::jmax(clipRange_.maxVolumeDB, c->volumeDB);
            clipRange_.minPan = juce::jmin(clipRange_.minPan, c->pan);
            clipRange_.maxPan = juce::jmax(clipRange_.maxPan, c->pan);
            clipRange_.minGainDB = juce::jmin(clipRange_.minGainDB, c->gainDB);
            clipRange_.maxGainDB = juce::jmax(clipRange_.maxGainDB, c->gainDB);
            clipRange_.minSpeedRatio = juce::jmin(clipRange_.minSpeedRatio, event.speedRatio);
            clipRange_.maxSpeedRatio = juce::jmax(clipRange_.maxSpeedRatio, event.speedRatio);
        }
    }
}

void ClipInspector::refreshClipRangeDisplay() {
    std::initializer_list<magda::DraggableValueLabel*> values = {
        pitchSemitonesValue_.get(), pitchCentsValue_.get(), clipVolumeValue_.get(),
        clipPanValue_.get(),        clipGainValue_.get(),   clipStretchValue_.get()};
    std::initializer_list<magda::BarsBeatsTicksLabel*> positions = {
        clipStartValue_.get(),     clipEndValue_.get(),     clipLengthValue_.get(),
        clipLoopStartValue_.get(), clipLoopEndValue_.get(), clipLoopPhaseValue_.get()};

    if (!clipRange_.valid || selectedClipIds_.size() <= 1) {
        for (auto* v : values)
            v->clearTextOverride();
        for (auto* p : positions)
            p->clearTextOverride();
        return;
    }

    static const juce::String multiDash("-");
    for (auto* v : values)
        v->setTextOverride(multiDash);
    for (auto* p : positions)
        p->setTextOverride(multiDash);
}

}  // namespace magda::daw::ui
