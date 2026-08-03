#include "AudioClipPropertiesContent.hpp"

#include <cmath>

#include "../themes/DarkTheme.hpp"
#include "../themes/FontManager.hpp"
#include "../themes/InspectorComboBoxLookAndFeel.hpp"
#include "../themes/SmallButtonLookAndFeel.hpp"
#include "BinaryData.h"
#include "audio/AudioBridge.hpp"
#include "audio/AudioThumbnailManager.hpp"
#include "core/AudioClipSourceDisplay.hpp"
#include "core/ClipManager.hpp"
#include "core/ClipOperations.hpp"
#include "core/ClipPropertyCommands.hpp"
#include "core/TempoUtils.hpp"
#include "core/TimeStretchModes.hpp"
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

double getAudioFileDurationForProperties(const magda::ClipInfo& clip) {
    if (!clip.isAudio())
        return 0.0;

    double durationSeconds = audioEventRef(clip).sourceDurationSeconds();
    if (auto* thumb = magda::AudioThumbnailManager::getInstance().getThumbnail(
            audioEventRef(clip).sourceFilePath())) {
        const double fileDuration = thumb->getTotalLength();
        if (fileDuration > 0.0)
            durationSeconds = fileDuration;
    }
    return durationSeconds;
}

double getProjectBpmForProperties() {
    if (auto* tc = magda::TimelineController::getCurrent()) {
        const double bpm = tc->getState().tempo.bpm;
        return magda::isValidBpm(bpm) ? bpm : magda::DEFAULT_BPM;
    }
    return magda::DEFAULT_BPM;
}

// Resolve the shared inspector display model (values + which fields are live)
// for this clip, so this panel and the right-panel clip inspector can't drift.
magda::AudioClipSourceDisplay resolveSourceDisplay(const magda::ClipInfo& clip) {
    const double cachedBpm = magda::AudioThumbnailManager::getInstance().getCachedBPM(
        audioEventRef(clip).sourceFilePath());
    return magda::computeAudioClipSourceDisplay(clip, getProjectBpmForProperties(),
                                                getAudioFileDurationForProperties(clip), cachedBpm);
}
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
    if (keyRootCombo_)
        keyRootCombo_->setLookAndFeel(nullptr);
    if (saveLibraryButton_)
        saveLibraryButton_->setLookAndFeel(nullptr);
}

void AudioClipPropertiesContent::onActivated() {
    magda::ClipManager::getInstance().addListener(this);
    clipId_ = magda::ClipManager::getInstance().getSelectedClip();
    updateFromClip();
}

void AudioClipPropertiesContent::onDeactivated() {
    magda::ClipManager::getInstance().removeListener(this);
}

void AudioClipPropertiesContent::setMultiSelection(
    const std::unordered_set<magda::ClipId>& clipIds) {
    auto newSet = clipIds.size() > 1 ? clipIds : std::unordered_set<magda::ClipId>{};
    if (newSet == multiClipIds_)
        return;
    multiClipIds_ = std::move(newSet);
    if (!multiClipIds_.empty())
        clipId_ = magda::INVALID_CLIP_ID;
    updateFromClip();
}

void AudioClipPropertiesContent::clipSelectionChanged(magda::ClipId clipId) {
    clipId_ = clipId;
    if (clipId != magda::INVALID_CLIP_ID)
        multiClipIds_.clear();
    updateFromClip();
}

void AudioClipPropertiesContent::clipPropertyChanged(magda::ClipId clipId) {
    if (clipId == clipId_ || multiClipIds_.count(clipId) > 0)
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
        magda::ClipManager::getInstance().setClipWarpEnabled(
            clipId_, !magda::audioEventRef(*clip).warpEnabled);
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

        bool enable = !magda::audioEventRef(*clip).autoTempo;

        const double bpm = getProjectBpmForProperties();

        const bool sourceInterpretationBpmLooksDefaulted =
            magda::audioEventRef(*clip).interpBpm <= 0.0 ||
            std::abs(magda::audioEventRef(*clip).interpBpm - bpm) < 0.1;
        if (enable && clip->isAudio() && sourceInterpretationBpmLooksDefaulted) {
            // Issue #1157: only seed from AudioThumbnailManager when the
            // file didn't carry tempo metadata. setSourceMetadata (from TE's
            // loopInfo) is authoritative when present.
            auto& thumbs = magda::AudioThumbnailManager::getInstance();
            auto* event = clip->primaryEvent();
            double cached = event != nullptr ? thumbs.getCachedBPM(event->sourceFilePath()) : 0.0;
            if (event != nullptr && cached > 0.0) {
                event->interpBpm = cached;
                if (auto* thumb = thumbs.getThumbnail(event->sourceFilePath())) {
                    double fileDuration = thumb->getTotalLength();
                    if (fileDuration > 0.0) {
                        if (auto* src =
                                magda::SourcePool::getInstance().getMutable(event->sourceId);
                            src != nullptr && src->durationSeconds <= 0.0) {
                            src->durationSeconds = fileDuration;
                        }
                        event->interpTotalBeats = fileDuration * cached / 60.0;
                    }
                }
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
            std::make_unique<magda::SetClipReversedCommand>(clipId_,
                                                            !magda::audioEventRef(*clip).reversed));
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
    // Combo IDs are persisted mode values plus one because JUCE reserves ID 0.
    stretchModeCombo_->addItem("Off", time_stretch_mode::kDisabled + 1);
    stretchModeCombo_->addItem("Signalsmith", time_stretch_mode::kSignalsmith + 1);
    stretchModeCombo_->addItem("SoundTouch", time_stretch_mode::kSoundTouchNormal + 1);
    stretchModeCombo_->addItem("SoundTouch HQ", time_stretch_mode::kSoundTouchBetter + 1);
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
        if (magda::audioEventRef(*clip).autoTempo) {
            double bpm = 120.0;
            if (auto* tc = magda::TimelineController::getCurrent())
                bpm = tc->getState().tempo.bpm;
            magda::ClipManager::AudioClipBeatsUpdate u;
            u.interpretationBpm = newBPM;
            double durationSeconds = magda::audioEventRef(*clip).sourceDurationSeconds();
            if (auto* thumb = magda::AudioThumbnailManager::getInstance().getThumbnail(
                    magda::audioEventRef(*clip).sourceFilePath())) {
                double fileDuration = thumb->getTotalLength();
                if (fileDuration > 0.0)
                    durationSeconds = fileDuration;
                if (fileDuration > 0.0 &&
                    magda::audioEventRef(*clip).sourceDurationSeconds() <= 0.0)
                    u.sourceDurationSeconds = fileDuration;
            }
            if (durationSeconds > 0.0) {
                u.interpretationTotalBeats = durationSeconds * newBPM / 60.0;
                u.lockInterpretationTotalBeats = true;
            }
            auto& mgr = magda::ClipManager::getInstance();
            mgr.applyAudioClipBeats(clipId_, u, bpm);
        } else {
            // Non-autoTempo audio: source interpretation BPM is just stored metadata.
            auto* event = clip->primaryEvent();
            if (event != nullptr)
                event->interpBpm = newBPM;
            if (auto* thumb = magda::AudioThumbnailManager::getInstance().getThumbnail(
                    magda::audioEventRef(*clip).sourceFilePath())) {
                double fileDuration = thumb->getTotalLength();
                if (fileDuration > 0.0) {
                    if (auto* src = magda::SourcePool::getInstance().getMutable(
                            magda::audioEventRef(*clip).sourceId);
                        src != nullptr && src->durationSeconds <= 0.0) {
                        src->durationSeconds = fileDuration;
                    }
                }
            }
            auto& mgr = magda::ClipManager::getInstance();
            mgr.forceNotifyClipPropertyChanged(clipId_);
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

        double durationSeconds = magda::audioEventRef(*clip).sourceDurationSeconds();
        if (durationSeconds <= 0.0) {
            if (auto* thumb = magda::AudioThumbnailManager::getInstance().getThumbnail(
                    magda::audioEventRef(*clip).sourceFilePath())) {
                durationSeconds = thumb->getTotalLength();
            }
        }

        magda::ClipManager::AudioClipBeatsUpdate u;
        u.interpretationTotalBeats = newSourceBeats;
        u.lockInterpretationTotalBeats = true;
        if (durationSeconds > 0.0)
            u.interpretationBpm = newSourceBeats * 60.0 / durationSeconds;
        if (durationSeconds > 0.0 && magda::audioEventRef(*clip).sourceDurationSeconds() <= 0.0)
            u.sourceDurationSeconds = durationSeconds;

        magda::ClipManager::getInstance().applyAudioClipBeats(clipId_, u, projectBpm);
    };
    addAndMakeVisible(*beatsValue_);

    // ----- Source key root (mirrors clip inspector) -----
    keyLabel_ = makeLabel("Key");
    keyRootCombo_ = std::make_unique<juce::ComboBox>();
    keyRootCombo_->setColour(juce::ComboBox::backgroundColourId,
                             DarkTheme::getColour(DarkTheme::SURFACE));
    keyRootCombo_->setColour(juce::ComboBox::textColourId, DarkTheme::getTextColour());
    keyRootCombo_->setColour(juce::ComboBox::outlineColourId,
                             DarkTheme::getColour(DarkTheme::BORDER));
    static constexpr const char* kKeyRoots[] = {"C",  "C#", "D",  "D#", "E",  "F",
                                                "F#", "G",  "G#", "A",  "A#", "B"};
    keyRootCombo_->addItem("--", 1);
    for (int i = 0; i < 12; ++i) {
        keyRootCombo_->addItem(kKeyRoots[i], i + 2);
    }
    keyRootCombo_->setSelectedId(1, juce::dontSendNotification);
    keyRootCombo_->setLookAndFeel(&InspectorComboBoxLookAndFeel::getInstance());
    keyRootCombo_->onChange = [this]() {
        if (clipId_ == magda::INVALID_CLIP_ID) {
            return;
        }
        auto* clip = magda::ClipManager::getInstance().getClip(clipId_);
        if (clip == nullptr || !clip->isAudio()) {
            return;
        }
        const int rootId = keyRootCombo_->getSelectedId();
        std::string root;
        if (rootId >= 2 && rootId <= 13) {
            root = kKeyRoots[rootId - 2];
        }
        if (auto* event = clip->primaryEvent())
            event->keyRoot = root;
        magda::ClipManager::getInstance().forceNotifyClipPropertyChanged(clipId_);
    };
    addAndMakeVisible(*keyRootCombo_);

    saveLibraryButton_ = std::make_unique<juce::TextButton>("Save to library");
    saveLibraryButton_->setLookAndFeel(&smallLF);
    saveLibraryButton_->setTooltip(
        "Save current clip BPM, beats, BEAT mode, key, and warp markers to the media library");
    saveLibraryButton_->onClick = [this]() {
        auto* clip = magda::ClipManager::getInstance().getClip(clipId_);
        if (clip == nullptr || !clip->isAudio()) {
            return;
        }
        auto* event = clip->primaryEvent();
        const double displayedBpm = bpmValue_ ? bpmValue_->getValue() : 0.0;
        if (event != nullptr && magda::isValidBpm(displayedBpm)) {
            event->interpBpm = displayedBpm;
        }
        const double displayedBeats = beatsValue_ ? beatsValue_->getValue() : 0.0;
        if (event != nullptr && displayedBeats > 0.0) {
            event->interpTotalBeats = displayedBeats;
            event->interpTotalBeatsLocked = true;
        }

        std::optional<std::vector<magda::WarpMarker>> markers;
        if (magda::audioEventRef(*clip).warpEnabled) {
            markers = std::vector<magda::WarpMarker>{};
            if (auto* engine = magda::TrackManager::getInstance().getAudioEngine()) {
                if (auto* bridge = engine->getAudioBridge()) {
                    const auto liveMarkers = bridge->getWarpMarkers(clipId_);
                    markers->reserve(liveMarkers.size());
                    for (const auto& marker : liveMarkers) {
                        markers->push_back({marker.sourceTime, marker.warpTime});
                    }
                }
            }
            if (markers->empty()) {
                *markers = magda::audioEventRef(*clip).warpMarkers;
            }
        }

        const bool saved =
            magda::ClipManager::getInstance().saveClipToLibrary(clipId_, std::move(markers));
        updateFromClip();
        saveLibraryButton_->setButtonText(saved ? "Saved" : "Save failed");
        juce::Timer::callAfterDelay(
            1200, [safeThis = juce::Component::SafePointer<AudioClipPropertiesContent>(this)] {
                if (safeThis != nullptr && safeThis->saveLibraryButton_) {
                    safeThis->saveLibraryButton_->setButtonText("Save to library");
                }
            });
    };
    addAndMakeVisible(*saveLibraryButton_);

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
        magda::ClipManager::getInstance().setAnalogPitch(clipId_,
                                                         !magda::audioEventRef(*clip).analogPitch);
    };

    // ===================== TAKES SECTION =====================
    takesSection_ = std::make_unique<ClipTakesSection>();
    addAndMakeVisible(*takesSection_);

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

    warpToggle_->setToggleState(hasClip && magda::audioEventRef(*clip).warpEnabled,
                                juce::dontSendNotification);
    autoTempoToggle_->setToggleState(hasClip && magda::audioEventRef(*clip).autoTempo,
                                     juce::dontSendNotification);
    analogPitchToggle_->setToggleState(hasClip && magda::audioEventRef(*clip).analogPitch,
                                       juce::dontSendNotification);
    reverseToggle_->setToggleState(hasClip && magda::audioEventRef(*clip).reversed,
                                   juce::dontSendNotification);

    if (hasClip) {
        stretchValue_->setValue(magda::audioEventRef(*clip).speedRatio, juce::dontSendNotification);
        stretchModeCombo_->setSelectedId(magda::audioEventRef(*clip).getEffectiveTimeStretchMode() +
                                             1,
                                         juce::dontSendNotification);
        const auto sourceDisplay = resolveSourceDisplay(*clip);
        bpmValue_->setValue(sourceDisplay.bpm > 0.0 ? sourceDisplay.bpm : magda::DEFAULT_BPM,
                            juce::dontSendNotification);
        beatsValue_->setValue(sourceDisplay.totalBeats > 0.0 ? sourceDisplay.totalBeats : 4.0,
                              juce::dontSendNotification);
        // Mirror the clip's source key root into the combo (-- when unknown).
        {
            const auto& root = magda::audioEventRef(*clip).keyRoot;
            static constexpr const char* kRoots[] = {"C",  "C#", "D",  "D#", "E",  "F",
                                                     "F#", "G",  "G#", "A",  "A#", "B"};
            int rootId = 1;
            for (int i = 0; i < 12; ++i) {
                if (root == kRoots[i]) {
                    rootId = i + 2;
                    break;
                }
            }
            keyRootCombo_->setSelectedId(rootId, juce::dontSendNotification);
        }
        pitchValue_->setValue(static_cast<double>(magda::audioEventRef(*clip).pitchChange),
                              juce::dontSendNotification);
        volumeValue_->setValue(static_cast<double>(clip->volumeDB), juce::dontSendNotification);
        gainValue_->setValue(static_cast<double>(clip->gainDB), juce::dontSendNotification);
        panValue_->setValue(static_cast<double>(clip->pan), juce::dontSendNotification);
    }

    const bool isMulti = multiClipIds_.size() > 1;
    if (isMulti)
        fadesSection_->setSelectedClips(multiClipIds_);
    else
        fadesSection_->setClip(clipId_);
    takesSection_->setClip(isMulti ? magda::INVALID_CLIP_ID : clipId_);

    bool enabled = hasClip;
    bool isAutoTempo = hasClip && magda::audioEventRef(*clip).autoTempo;
    // Speed is live in time-based mode; Source BPM / Beats are live only in beat
    // mode (autoTempo) — they're inert otherwise, so grey them out. Mirrors the
    // right-panel clip inspector.
    stretchValue_->setEnabled(enabled && !isAutoTempo);
    stretchModeCombo_->setEnabled(enabled);
    bpmValue_->setEnabled(enabled && isAutoTempo);
    beatsValue_->setEnabled(enabled && isAutoTempo);
    pitchValue_->setEnabled(enabled);
    analogPitchToggle_->setEnabled(enabled && !isAutoTempo &&
                                   !(hasClip && magda::audioEventRef(*clip).warpEnabled));
    transientSensValue_->setEnabled(enabled);
    saveLibraryButton_->setEnabled(enabled &&
                                   magda::ClipManager::getInstance().canSaveClipToLibrary(clipId_));
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

    if (clipId_ == magda::INVALID_CLIP_ID && multiClipIds_.empty()) {
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

    // Takes section (shared component; only renders for multi-take audio clips).
    if (takesSection_) {
        int ph = takesSection_->getPreferredHeight();
        if (ph > 0) {
            addSeparator(leftCol);
            takesSection_->setBounds(addRow(leftCol, ph));
        }
    }

    addSeparator(leftCol);

    stretchSectionLabel_->setBounds(addRow(leftCol, SECTION_LABEL_HEIGHT));
    layoutLabelValue(addRow(leftCol, ROW_HEIGHT), *modeLabel_, *stretchModeCombo_, labelW);
    layoutLabelValue(addRow(leftCol, ROW_HEIGHT), *speedLabel_, *stretchValue_, labelW);
    layoutLabelValue(addRow(leftCol, ROW_HEIGHT), *beatsLabel_, *beatsValue_, labelW);
    layoutLabelValue(addRow(leftCol, ROW_HEIGHT), *bpmLabel_, *bpmValue_, labelW);
    layoutLabelValue(addRow(leftCol, ROW_HEIGHT), *keyLabel_, *keyRootCombo_, labelW);
    saveLibraryButton_->setBounds(addRow(leftCol, ROW_HEIGHT));

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
