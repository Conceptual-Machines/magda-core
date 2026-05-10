#include "AudioClipPropertiesContent.hpp"

#include "../themes/DarkTheme.hpp"
#include "../themes/FontManager.hpp"
#include "../themes/InspectorComboBoxLookAndFeel.hpp"
#include "../themes/SmallButtonLookAndFeel.hpp"
#include "BinaryData.h"
#include "audio/AudioBridge.hpp"
#include "audio/AudioThumbnailManager.hpp"
#include "core/ClipManager.hpp"
#include "core/ClipOperations.hpp"
#include "core/ClipPropertyCommands.hpp"
#include "core/UndoManager.hpp"
#include "engine/AudioEngine.hpp"
#include "project/ProjectManager.hpp"
#include "state/TimelineController.hpp"

namespace magda::daw::ui {

namespace {
constexpr int ROW_HEIGHT = 20;
constexpr int ROW_GAP = 3;
constexpr int SECTION_LABEL_HEIGHT = 18;
constexpr int SEPARATOR_PAD = 5;
constexpr int TOGGLE_WIDTH = 46;
constexpr int H_PAD = 8;
constexpr int V_PAD = 4;
}  // namespace

AudioClipPropertiesContent::AudioClipPropertiesContent() {
    setName("Audio Clip Properties");
    createControls();
}

AudioClipPropertiesContent::~AudioClipPropertiesContent() {
    magda::ClipManager::getInstance().removeListener(this);

    if (warpToggle_)
        warpToggle_->setLookAndFeel(nullptr);
    if (autoTempoToggle_)
        autoTempoToggle_->setLookAndFeel(nullptr);
    if (analogPitchToggle_)
        analogPitchToggle_->setLookAndFeel(nullptr);
    if (reverseToggle_)
        reverseToggle_->setLookAndFeel(nullptr);
    if (stretchModeCombo_)
        stretchModeCombo_->setLookAndFeel(nullptr);
}

void AudioClipPropertiesContent::onActivated() {
    magda::ClipManager::getInstance().addListener(this);
    clipId_ = magda::ClipManager::getInstance().getSelectedClip();
    updateFromClip();
}

void AudioClipPropertiesContent::onDeactivated() {
    magda::ClipManager::getInstance().removeListener(this);
}

void AudioClipPropertiesContent::clipSelectionChanged(magda::ClipId clipId) {
    clipId_ = clipId;
    updateFromClip();
}

void AudioClipPropertiesContent::clipPropertyChanged(magda::ClipId clipId) {
    if (clipId == clipId_)
        updateFromClip();
}

void AudioClipPropertiesContent::clipsChanged() {
    updateFromClip();
}

void AudioClipPropertiesContent::createControls() {
    auto& smallLF = SmallButtonLookAndFeel::getInstance();
    auto sectionFont = FontManager::getInstance().getUIFont(11.0f);
    auto labelFont = FontManager::getInstance().getUIFont(11.0f);

    // --- Section label factory ---
    auto makeSectionLabel = [&](const juce::String& text) {
        auto label = std::make_unique<juce::Label>("", text);
        label->setFont(sectionFont);
        label->setColour(juce::Label::textColourId,
                         DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
        label->setJustificationType(juce::Justification::centredLeft);
        addAndMakeVisible(*label);
        return label;
    };

    // --- Row label factory ---
    auto makeLabel = [&](const juce::String& text) {
        auto label = std::make_unique<juce::Label>("", text);
        label->setFont(labelFont);
        label->setColour(juce::Label::textColourId,
                         DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
        label->setJustificationType(juce::Justification::centredRight);
        addAndMakeVisible(*label);
        return label;
    };

    // --- Toggle button factory ---
    auto makeToggle = [&](const juce::String& text) {
        auto btn = std::make_unique<juce::TextButton>(text);
        btn->setLookAndFeel(&smallLF);
        btn->setColour(juce::TextButton::buttonColourId, DarkTheme::getColour(DarkTheme::SURFACE));
        btn->setColour(juce::TextButton::buttonOnColourId,
                       DarkTheme::getAccentColour().withAlpha(0.3f));
        btn->setColour(juce::TextButton::textColourOffId,
                       DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
        btn->setColour(juce::TextButton::textColourOnId, DarkTheme::getAccentColour());
        btn->setClickingTogglesState(false);
        btn->setConnectedEdges(juce::Button::ConnectedOnLeft | juce::Button::ConnectedOnRight |
                               juce::Button::ConnectedOnTop | juce::Button::ConnectedOnBottom);
        btn->setWantsKeyboardFocus(false);
        addAndMakeVisible(*btn);
        return btn;
    };

    // ===================== CLIP SECTION =====================
    clipSectionLabel_ = makeSectionLabel("Clip");

    warpToggle_ = makeToggle("WARP");
    warpToggle_->onClick = [this]() {
        if (clipId_ == magda::INVALID_CLIP_ID)
            return;
        const auto* clip = magda::ClipManager::getInstance().getClip(clipId_);
        if (!clip)
            return;
        magda::ClipManager::getInstance().setClipWarpEnabled(clipId_, !clip->warpEnabled);
    };

    autoTempoToggle_ = makeToggle("BEAT");
    autoTempoToggle_->setTooltip(
        "Lock clip to musical time (bars/beats) instead of absolute time.\n"
        "Clip length changes with tempo to maintain fixed beat length.");
    autoTempoToggle_->onClick = [this]() {
        if (clipId_ == magda::INVALID_CLIP_ID)
            return;
        auto* clip = magda::ClipManager::getInstance().getClip(clipId_);
        if (!clip)
            return;

        bool enable = !clip->autoTempo;

        double bpm = 120.0;
        if (auto* tc = magda::TimelineController::getCurrent())
            bpm = tc->getState().tempo.bpm;

        const bool sourceInterpretationBpmLooksDefaulted =
            clip->audio().interpretation.bpm <= 0.0 ||
            (bpm > 0.0 && std::abs(clip->audio().interpretation.bpm - bpm) < 0.1);
        if (enable && clip->isAudio() && sourceInterpretationBpmLooksDefaulted) {
            // Issue #1157: only seed from AudioThumbnailManager when the
            // file didn't carry tempo metadata. setSourceMetadata (from TE's
            // loopInfo) is authoritative when present.
            auto& thumbs = magda::AudioThumbnailManager::getInstance();
            double cached = thumbs.getCachedBPM(clip->audio().source.filePath);
            if (cached > 0.0) {
                clip->audio().interpretation.bpm = cached;
                if (auto* thumb = thumbs.getThumbnail(clip->audio().source.filePath)) {
                    double fileDuration = thumb->getTotalLength();
                    if (fileDuration > 0.0) {
                        if (clip->audio().source.durationSeconds <= 0.0)
                            clip->audio().source.durationSeconds = fileDuration;
                        clip->audio().interpretation.totalBeats = fileDuration * cached / 60.0;
                    }
                }
            } else {
                auto cid = clipId_;
                thumbs.requestBPMDetection(
                    clip->audio().source.filePath, [cid](double detectedBPM) {
                        if (detectedBPM <= 0.0)
                            return;
                        auto& mgr = magda::ClipManager::getInstance();
                        auto* c = mgr.getClip(cid);
                        if (!c)
                            return;
                        // Issue #1157: file metadata wins over audio analysis.
                        double live =
                            magda::ProjectManager::getInstance().getCurrentProjectInfo().tempo;
                        bool existingLooksDefaulted =
                            c->audio().interpretation.bpm > 0.0 && live > 0.0 &&
                            std::abs(c->audio().interpretation.bpm - live) < 0.1;
                        if (c->audio().interpretation.bpm > 0.0 && !existingLooksDefaulted)
                            return;
                        magda::ClipManager::AudioClipBeatsUpdate u;
                        u.interpretationBpm = detectedBPM;
                        if (auto* thumb = magda::AudioThumbnailManager::getInstance().getThumbnail(
                                c->audio().source.filePath)) {
                            double fileDuration = thumb->getTotalLength();
                            if (fileDuration > 0.0) {
                                u.sourceDurationSeconds = fileDuration;
                                u.interpretationTotalBeats = fileDuration * detectedBPM / 60.0;
                            }
                        }
                        mgr.applyAudioClipBeats(cid, u, live);
                    });
            }
        }

        magda::ClipManager::getInstance().setAutoTempo(clipId_, enable, bpm);
    };

    reverseToggle_ = makeToggle("REV");
    reverseToggle_->setTooltip("Reverse playback");
    reverseToggle_->onClick = [this]() {
        if (clipId_ == magda::INVALID_CLIP_ID)
            return;
        const auto* clip = magda::ClipManager::getInstance().getClip(clipId_);
        if (!clip)
            return;
        magda::UndoManager::getInstance().executeCommand(
            std::make_unique<magda::SetClipReversedCommand>(clipId_, !clip->isReversed));
    };

    // ===================== STRETCH SECTION =====================
    stretchSectionLabel_ = makeSectionLabel("Stretch");

    speedLabel_ = makeLabel("Speed");
    stretchValue_ = std::make_unique<DraggableValueLabel>(DraggableValueLabel::Format::Raw);
    stretchValue_->setRange(0.25, 4.0, 1.0);
    stretchValue_->setDecimalPlaces(3);
    stretchValue_->setSuffix("x");
    stretchValue_->setDrawBackground(false);
    stretchValue_->setDrawBorder(true);
    stretchValue_->setShowFillIndicator(false);
    stretchValue_->setFontSize(11.0f);
    stretchValue_->setDoubleClickResetsValue(false);
    stretchValue_->onValueChange = [this]() {
        if (clipId_ == magda::INVALID_CLIP_ID)
            return;
        magda::UndoManager::getInstance().executeCommand(
            std::make_unique<magda::SetClipSpeedRatioCommand>(clipId_, stretchValue_->getValue()));
    };
    addAndMakeVisible(*stretchValue_);

    modeLabel_ = makeLabel("Mode");
    stretchModeCombo_ = std::make_unique<juce::ComboBox>();
    stretchModeCombo_->setColour(juce::ComboBox::backgroundColourId,
                                 DarkTheme::getColour(DarkTheme::SURFACE));
    stretchModeCombo_->setColour(juce::ComboBox::textColourId, DarkTheme::getTextColour());
    stretchModeCombo_->setColour(juce::ComboBox::outlineColourId,
                                 DarkTheme::getColour(DarkTheme::BORDER));
    stretchModeCombo_->addItem("Off", 1);
    stretchModeCombo_->addItem("SoundTouch", 4);
    stretchModeCombo_->addItem("SoundTouch HQ", 5);
    stretchModeCombo_->setSelectedId(1, juce::dontSendNotification);
    stretchModeCombo_->setLookAndFeel(&InspectorComboBoxLookAndFeel::getInstance());
    stretchModeCombo_->onChange = [this]() {
        if (clipId_ == magda::INVALID_CLIP_ID)
            return;
        int mode = stretchModeCombo_->getSelectedId() - 1;
        magda::UndoManager::getInstance().executeCommand(
            std::make_unique<magda::SetClipStretchModeCommand>(clipId_, mode));
    };
    addAndMakeVisible(*stretchModeCombo_);

    bpmLabel_ = makeLabel("BPM");
    bpmValue_ = std::make_unique<DraggableValueLabel>(DraggableValueLabel::Format::Raw);
    bpmValue_->setRange(20.0, 300.0, 120.0);
    bpmValue_->setDecimalPlaces(1);
    bpmValue_->setDrawBackground(false);
    bpmValue_->setDrawBorder(true);
    bpmValue_->setShowFillIndicator(false);
    bpmValue_->setFontSize(11.0f);
    bpmValue_->setDoubleClickResetsValue(false);
    bpmValue_->onValueChange = [this]() {
        if (clipId_ == magda::INVALID_CLIP_ID)
            return;
        auto* clip = magda::ClipManager::getInstance().getClip(clipId_);
        if (!clip)
            return;

        double newBPM = bpmValue_->getValue();

        // BPM and Beats are two editable views of the same fixed-duration source
        // interpretation. Editing either one must keep the other coherent.
        if (clip->autoTempo) {
            double bpm = 120.0;
            if (auto* tc = magda::TimelineController::getCurrent())
                bpm = tc->getState().tempo.bpm;
            magda::ClipManager::AudioClipBeatsUpdate u;
            u.interpretationBpm = newBPM;
            double durationSeconds = clip->audio().source.durationSeconds;
            if (auto* thumb = magda::AudioThumbnailManager::getInstance().getThumbnail(
                    clip->audio().source.filePath)) {
                double fileDuration = thumb->getTotalLength();
                if (fileDuration > 0.0)
                    durationSeconds = fileDuration;
                if (fileDuration > 0.0 && clip->audio().source.durationSeconds <= 0.0)
                    u.sourceDurationSeconds = fileDuration;
            }
            if (durationSeconds > 0.0) {
                u.interpretationTotalBeats = durationSeconds * newBPM / 60.0;
                u.lockInterpretationTotalBeats = true;
            }
            magda::ClipManager::getInstance().applyAudioClipBeats(clipId_, u, bpm);
        } else {
            // Non-autoTempo audio: source interpretation BPM is just stored metadata.
            clip->audio().interpretation.bpm = newBPM;
            if (auto* thumb = magda::AudioThumbnailManager::getInstance().getThumbnail(
                    clip->audio().source.filePath)) {
                double fileDuration = thumb->getTotalLength();
                if (fileDuration > 0.0) {
                    if (clip->audio().source.durationSeconds <= 0.0)
                        clip->audio().source.durationSeconds = fileDuration;
                }
            }
            magda::ClipManager::getInstance().forceNotifyClipPropertyChanged(clipId_);
        }
    };
    addAndMakeVisible(*bpmValue_);

    beatsLabel_ = makeLabel("Beats");
    beatsValue_ = std::make_unique<DraggableValueLabel>(DraggableValueLabel::Format::Raw);
    beatsValue_->setRange(0.25, 4096.0, 4.0);
    beatsValue_->setDecimalPlaces(2);
    beatsValue_->setSuffix("");
    beatsValue_->setSnapToInteger(true);
    beatsValue_->setDrawBackground(false);
    beatsValue_->setDrawBorder(true);
    beatsValue_->setShowFillIndicator(false);
    beatsValue_->setFontSize(11.0f);
    beatsValue_->setDoubleClickResetsValue(false);
    beatsValue_->onValueChange = [this]() {
        if (clipId_ == magda::INVALID_CLIP_ID)
            return;
        auto* clip = magda::ClipManager::getInstance().getClip(clipId_);
        if (!clip || !clip->isAudio())
            return;

        const double newSourceBeats = beatsValue_->getValue();
        double projectBpm = 120.0;
        if (auto* tc = magda::TimelineController::getCurrent())
            projectBpm = tc->getState().tempo.bpm;

        double durationSeconds = clip->audio().source.durationSeconds;
        if (durationSeconds <= 0.0) {
            if (auto* thumb = magda::AudioThumbnailManager::getInstance().getThumbnail(
                    clip->audio().source.filePath)) {
                durationSeconds = thumb->getTotalLength();
            }
        }

        magda::ClipManager::AudioClipBeatsUpdate u;
        u.interpretationTotalBeats = newSourceBeats;
        u.lockInterpretationTotalBeats = true;
        if (durationSeconds > 0.0)
            u.interpretationBpm = newSourceBeats * 60.0 / durationSeconds;
        if (durationSeconds > 0.0 && clip->audio().source.durationSeconds <= 0.0)
            u.sourceDurationSeconds = durationSeconds;

        magda::ClipManager::getInstance().applyAudioClipBeats(clipId_, u, projectBpm);
    };
    addAndMakeVisible(*beatsValue_);

    // ===================== PITCH SECTION =====================
    pitchSectionLabel_ = makeSectionLabel("Pitch");

    pitchLabel_ = makeLabel("Semi");
    pitchValue_ = std::make_unique<DraggableValueLabel>(DraggableValueLabel::Format::Raw);
    pitchValue_->setRange(-48.0, 48.0, 0.0);
    pitchValue_->setDecimalPlaces(2);
    pitchValue_->setSuffix("st");
    pitchValue_->setDrawBackground(false);
    pitchValue_->setDrawBorder(true);
    pitchValue_->setShowFillIndicator(false);
    pitchValue_->setFontSize(11.0f);
    pitchValue_->setDoubleClickResetsValue(false);
    pitchValue_->onValueChange = [this]() {
        if (clipId_ == magda::INVALID_CLIP_ID)
            return;
        magda::UndoManager::getInstance().executeCommand(
            std::make_unique<magda::SetClipPitchCommand>(
                clipId_, static_cast<float>(pitchValue_->getValue())));
    };
    addAndMakeVisible(*pitchValue_);

    analogPitchToggle_ = makeToggle("ANALOG");
    analogPitchToggle_->setTooltip("Analog pitch: resample instead of time-stretch");
    analogPitchToggle_->onClick = [this]() {
        if (clipId_ == magda::INVALID_CLIP_ID)
            return;
        const auto* clip = magda::ClipManager::getInstance().getClip(clipId_);
        if (!clip)
            return;
        magda::ClipManager::getInstance().setAnalogPitch(clipId_, !clip->analogPitch);
    };

    // ===================== FADES SECTION =====================
    fadesSection_ = std::make_unique<ClipFadesSection>();
    addAndMakeVisible(*fadesSection_);

    // ===================== TRANSIENT DETECTION SECTION =====================
    transientSectionLabel_ = makeSectionLabel("Transient Detection");

    transientSensLabel_ = makeLabel("Sens");
    transientSensValue_ =
        std::make_unique<DraggableValueLabel>(DraggableValueLabel::Format::Percentage);
    transientSensValue_->setRange(0.0, 1.0, 0.01);
    transientSensValue_->setValue(0.5, juce::dontSendNotification);
    transientSensValue_->setDoubleClickResetsValue(true);
    transientSensValue_->setDrawBackground(false);
    transientSensValue_->setDrawBorder(true);
    transientSensValue_->setShowFillIndicator(false);
    transientSensValue_->setFontSize(11.0f);
    transientSensValue_->onValueChange = [this]() {
        if (clipId_ == magda::INVALID_CLIP_ID)
            return;
        auto* audioEngine = magda::TrackManager::getInstance().getAudioEngine();
        if (!audioEngine)
            return;
        auto* bridge = audioEngine->getAudioBridge();
        if (!bridge)
            return;
        bridge->setTransientSensitivity(clipId_,
                                        static_cast<float>(transientSensValue_->getValue()));
        magda::ClipManager::getInstance().forceNotifyClipPropertyChanged(clipId_);
    };
    addAndMakeVisible(*transientSensValue_);

    // ===================== MIX SECTION =====================
    mixSectionLabel_ = makeSectionLabel("Mix");

    volLabel_ = makeLabel("Vol");
    volumeValue_ = std::make_unique<DraggableValueLabel>(DraggableValueLabel::Format::Decibels);
    volumeValue_->setRange(-100.0, 0.0, 0.0);
    volumeValue_->setDrawBackground(false);
    volumeValue_->setDrawBorder(true);
    volumeValue_->setShowFillIndicator(false);
    volumeValue_->setFontSize(11.0f);
    volumeValue_->setDoubleClickResetsValue(false);
    volumeValue_->onValueChange = [this]() {
        if (clipId_ == magda::INVALID_CLIP_ID)
            return;
        magda::UndoManager::getInstance().executeCommand(
            std::make_unique<magda::SetClipVolumeDBCommand>(
                clipId_, static_cast<float>(volumeValue_->getValue())));
    };
    addAndMakeVisible(*volumeValue_);

    gainLabel_ = makeLabel("Gain");
    gainValue_ = std::make_unique<DraggableValueLabel>(DraggableValueLabel::Format::Raw);
    gainValue_->setRange(0.0, 24.0, 0.0);
    gainValue_->setDecimalPlaces(1);
    gainValue_->setSuffix(" dB");
    gainValue_->setDrawBackground(false);
    gainValue_->setDrawBorder(true);
    gainValue_->setShowFillIndicator(false);
    gainValue_->setFontSize(11.0f);
    gainValue_->setDoubleClickResetsValue(false);
    gainValue_->onValueChange = [this]() {
        if (clipId_ == magda::INVALID_CLIP_ID)
            return;
        magda::UndoManager::getInstance().executeCommand(
            std::make_unique<magda::SetClipGainDBCommand>(
                clipId_, static_cast<float>(gainValue_->getValue())));
    };
    addAndMakeVisible(*gainValue_);

    panLabel_ = makeLabel("Pan");
    panValue_ = std::make_unique<DraggableValueLabel>(DraggableValueLabel::Format::Pan);
    panValue_->setRange(-1.0, 1.0, 0.0);
    panValue_->setDrawBackground(false);
    panValue_->setDrawBorder(true);
    panValue_->setShowFillIndicator(false);
    panValue_->setFontSize(11.0f);
    panValue_->setDoubleClickResetsValue(false);
    panValue_->onValueChange = [this]() {
        if (clipId_ == magda::INVALID_CLIP_ID)
            return;
        magda::UndoManager::getInstance().executeCommand(std::make_unique<magda::SetClipPanCommand>(
            clipId_, static_cast<float>(panValue_->getValue())));
    };
    addAndMakeVisible(*panValue_);
}

void AudioClipPropertiesContent::updateFromClip() {
    const auto* clip = magda::ClipManager::getInstance().getClip(clipId_);
    bool hasClip = clip != nullptr && clip->isAudio();

    warpToggle_->setToggleState(hasClip && clip->warpEnabled, juce::dontSendNotification);
    autoTempoToggle_->setToggleState(hasClip && clip->autoTempo, juce::dontSendNotification);
    analogPitchToggle_->setToggleState(hasClip && clip->analogPitch, juce::dontSendNotification);
    reverseToggle_->setToggleState(hasClip && clip->isReversed, juce::dontSendNotification);

    if (hasClip) {
        stretchValue_->setValue(clip->speedRatio, juce::dontSendNotification);
        stretchModeCombo_->setSelectedId(clip->timeStretchMode + 1, juce::dontSendNotification);
        bpmValue_->setValue(
            clip->audio().interpretation.bpm > 0.0 ? clip->audio().interpretation.bpm : 120.0,
            juce::dontSendNotification);
        beatsValue_->setValue(clip->audio().interpretation.totalBeats > 0.0
                                  ? clip->audio().interpretation.totalBeats
                                  : 4.0,
                              juce::dontSendNotification);
        pitchValue_->setValue(static_cast<double>(clip->pitchChange), juce::dontSendNotification);
        volumeValue_->setValue(static_cast<double>(clip->volumeDB), juce::dontSendNotification);
        gainValue_->setValue(static_cast<double>(clip->gainDB), juce::dontSendNotification);
        panValue_->setValue(static_cast<double>(clip->pan), juce::dontSendNotification);
    }

    fadesSection_->setClip(clipId_);

    bool enabled = hasClip;
    bool isAutoTempo = hasClip && clip->autoTempo;
    stretchValue_->setEnabled(enabled && !isAutoTempo);
    stretchModeCombo_->setEnabled(enabled);
    bpmValue_->setEnabled(enabled);
    beatsValue_->setEnabled(enabled);
    pitchValue_->setEnabled(enabled);
    analogPitchToggle_->setEnabled(enabled && !isAutoTempo && !(hasClip && clip->warpEnabled));
    transientSensValue_->setEnabled(enabled);
    volumeValue_->setEnabled(enabled);
    gainValue_->setEnabled(enabled);
    panValue_->setEnabled(enabled);
    warpToggle_->setEnabled(enabled);
    reverseToggle_->setEnabled(enabled);

    if (getWidth() > 0 && getHeight() > 0) {
        resized();
        repaint();
    }
}

void AudioClipPropertiesContent::paint(juce::Graphics& g) {
    g.fillAll(DarkTheme::getPanelBackgroundColour());

    if (clipId_ == magda::INVALID_CLIP_ID) {
        g.setColour(DarkTheme::getColour(DarkTheme::TEXT_SECONDARY).withAlpha(0.5f));
        g.setFont(FontManager::getInstance().getUIFont(13.0f));
        g.drawText("No audio clip selected", getLocalBounds(), juce::Justification::centred);
        return;
    }

    // Vertical divider between columns
    g.setColour(DarkTheme::getColour(DarkTheme::SEPARATOR));
    int divX = getWidth() / 2;
    g.drawVerticalLine(divX, static_cast<float>(V_PAD), static_cast<float>(getHeight() - V_PAD));
}

void AudioClipPropertiesContent::resized() {
    auto bounds = getLocalBounds().reduced(H_PAD, V_PAD);
    separatorYPositions_.clear();

    // Need minimum width for two-column layout to avoid zero-sized children
    if (bounds.getWidth() < 100 || bounds.getHeight() < 20)
        return;

    int toggleW = TOGGLE_WIDTH;
    int gap = 4;
    int colGap = 8;

    auto addRow = [&](juce::Rectangle<int>& area, int height) -> juce::Rectangle<int> {
        auto row = area.removeFromTop(height);
        area.removeFromTop(ROW_GAP);
        return row;
    };

    auto safeBounds = [](juce::Component& comp, juce::Rectangle<int> rect) {
        if (rect.getWidth() < 1 || rect.getHeight() < 1)
            rect = rect.withSize(juce::jmax(1, rect.getWidth()), juce::jmax(1, rect.getHeight()));
        comp.setBounds(rect);
    };

    auto layoutLabelValue = [&](juce::Rectangle<int> row, juce::Component& label,
                                juce::Component& value, int labelW) {
        safeBounds(label, row.removeFromLeft(labelW));
        row.removeFromLeft(2);
        safeBounds(value, row);
    };

    auto addSeparator = [&](juce::Rectangle<int>& area) {
        area.removeFromTop(SEPARATOR_PAD);
        area.removeFromTop(SEPARATOR_PAD);
    };

    // ===== TWO-COLUMN LAYOUT =====
    int halfW = (bounds.getWidth() - colGap) / 2;
    int labelW = 40;
    auto leftCol = bounds.removeFromLeft(halfW);
    bounds.removeFromLeft(colGap);
    auto rightCol = bounds;

    // --- LEFT COLUMN: Clip, Stretch, Transient, Pitch ---

    clipSectionLabel_->setBounds(addRow(leftCol, SECTION_LABEL_HEIGHT));
    {
        auto row = addRow(leftCol, ROW_HEIGHT);
        warpToggle_->setBounds(row.removeFromLeft(toggleW));
        row.removeFromLeft(gap);
        autoTempoToggle_->setBounds(row.removeFromLeft(toggleW));
        row.removeFromLeft(gap);
        reverseToggle_->setBounds(row.removeFromLeft(toggleW));
    }

    addSeparator(leftCol);

    stretchSectionLabel_->setBounds(addRow(leftCol, SECTION_LABEL_HEIGHT));
    layoutLabelValue(addRow(leftCol, ROW_HEIGHT), *modeLabel_, *stretchModeCombo_, labelW);
    layoutLabelValue(addRow(leftCol, ROW_HEIGHT), *speedLabel_, *stretchValue_, labelW);
    layoutLabelValue(addRow(leftCol, ROW_HEIGHT), *beatsLabel_, *beatsValue_, labelW);
    layoutLabelValue(addRow(leftCol, ROW_HEIGHT), *bpmLabel_, *bpmValue_, labelW);

    addSeparator(leftCol);

    transientSectionLabel_->setBounds(addRow(leftCol, SECTION_LABEL_HEIGHT));
    layoutLabelValue(addRow(leftCol, ROW_HEIGHT), *transientSensLabel_, *transientSensValue_,
                     labelW);

    addSeparator(leftCol);

    pitchSectionLabel_->setBounds(addRow(leftCol, SECTION_LABEL_HEIGHT));
    {
        auto row = addRow(leftCol, ROW_HEIGHT);
        safeBounds(*pitchLabel_, row.removeFromLeft(labelW));
        row.removeFromLeft(2);
        safeBounds(*analogPitchToggle_, row.removeFromRight(toggleW + 4));
        row.removeFromRight(gap);
        safeBounds(*pitchValue_, row);
    }

    // --- RIGHT COLUMN: Fades, Mix ---

    {
        int ph = fadesSection_ ? fadesSection_->getPreferredHeight() : 0;
        if (ph > 0)
            fadesSection_->setBounds(addRow(rightCol, ph));
    }

    addSeparator(rightCol);

    mixSectionLabel_->setBounds(addRow(rightCol, SECTION_LABEL_HEIGHT));
    layoutLabelValue(addRow(rightCol, ROW_HEIGHT), *volLabel_, *volumeValue_, labelW);
    layoutLabelValue(addRow(rightCol, ROW_HEIGHT), *gainLabel_, *gainValue_, labelW);
    layoutLabelValue(addRow(rightCol, ROW_HEIGHT), *panLabel_, *panValue_, labelW);
}

}  // namespace magda::daw::ui
