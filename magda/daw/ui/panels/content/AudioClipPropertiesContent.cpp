#include "AudioClipPropertiesContent.hpp"

#include "../themes/DarkTheme.hpp"
#include "../themes/FontManager.hpp"
#include "../themes/InspectorComboBoxLookAndFeel.hpp"
#include "../themes/SmallButtonLookAndFeel.hpp"
#include "BinaryData.h"
#include "audio/AudioBridge.hpp"
#include "audio/AudioThumbnailManager.hpp"
#include "core/ClipOperations.hpp"
#include "core/ClipPropertyCommands.hpp"
#include "core/UndoManager.hpp"
#include "engine/AudioEngine.hpp"
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

        if (enable && clip->type == magda::ClipType::Audio) {
            // Cached BPM applies immediately; cache miss kicks off background
            // detection and patches the clip when the result arrives. Beat mode
            // enables optimistically with the existing sourceBPM in the meantime.
            auto& thumbs = magda::AudioThumbnailManager::getInstance();
            double cached = thumbs.getCachedBPM(clip->audioFilePath);
            if (cached > 0.0) {
                double sourceDuration = clip->getSourceLength();
                clip->sourceBPM = cached;
                if (sourceDuration > 0.0)
                    clip->sourceNumBeats = sourceDuration * cached / 60.0;
            } else {
                auto cid = clipId_;
                thumbs.requestBPMDetection(clip->audioFilePath, [cid](double detectedBPM) {
                    if (detectedBPM <= 0.0)
                        return;
                    auto* c = magda::ClipManager::getInstance().getClip(cid);
                    if (!c)
                        return;
                    c->sourceBPM = detectedBPM;
                    double sd = c->getSourceLength();
                    if (sd > 0.0)
                        c->sourceNumBeats = sd * detectedBPM / 60.0;
                    magda::ClipManager::getInstance().forceNotifyClipPropertyChanged(cid);
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
        double sourceDuration = clip->getSourceLength();
        clip->sourceBPM = newBPM;
        if (sourceDuration > 0.0)
            clip->sourceNumBeats = sourceDuration * newBPM / 60.0;

        if (clip->autoTempo) {
            double bpm = 120.0;
            if (auto* tc = magda::TimelineController::getCurrent())
                bpm = tc->getState().tempo.bpm;
            clip->lengthBeats = clip->sourceNumBeats;
            clip->loopLengthBeats = clip->sourceNumBeats;
            double newLength = clip->lengthBeats * 60.0 / bpm;
            magda::ClipManager::getInstance().resizeClip(clipId_, newLength, false, bpm);
        } else {
            magda::ClipManager::getInstance().forceNotifyClipPropertyChanged(clipId_);
        }
    };
    addAndMakeVisible(*bpmValue_);

    beatsLabel_ = makeLabel("Beats");
    beatsValue_ = std::make_unique<DraggableValueLabel>(DraggableValueLabel::Format::Raw);
    beatsValue_->setRange(0.25, 128.0, 4.0);
    beatsValue_->setDecimalPlaces(2);
    beatsValue_->setSuffix(" beats");
    beatsValue_->setSnapToInteger(true);
    beatsValue_->setDrawBackground(false);
    beatsValue_->setDrawBorder(true);
    beatsValue_->setShowFillIndicator(false);
    beatsValue_->setFontSize(11.0f);
    beatsValue_->setDoubleClickResetsValue(false);
    beatsValue_->onValueChange = [this]() {
        if (clipId_ == magda::INVALID_CLIP_ID)
            return;
        double bpm = 120.0;
        if (auto* tc = magda::TimelineController::getCurrent())
            bpm = tc->getState().tempo.bpm;
        magda::UndoManager::getInstance().executeCommand(
            std::make_unique<magda::SetClipLengthBeatsCommand>(clipId_, beatsValue_->getValue(),
                                                               bpm));
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
    fadesSectionLabel_ = makeSectionLabel("Fades");

    fadeInLabel_ = makeLabel("In");
    fadeInValue_ = std::make_unique<DraggableValueLabel>(DraggableValueLabel::Format::Raw);
    fadeInValue_->setRange(0.0, 10.0, 0.0);
    fadeInValue_->setDecimalPlaces(3);
    fadeInValue_->setSuffix("s");
    fadeInValue_->setDrawBackground(false);
    fadeInValue_->setDrawBorder(true);
    fadeInValue_->setShowFillIndicator(false);
    fadeInValue_->setFontSize(11.0f);
    fadeInValue_->setDoubleClickResetsValue(false);
    fadeInValue_->onValueChange = [this]() {
        if (clipId_ == magda::INVALID_CLIP_ID)
            return;
        magda::UndoManager::getInstance().executeCommand(
            std::make_unique<magda::SetClipFadeInCommand>(clipId_, fadeInValue_->getValue()));
    };
    addAndMakeVisible(*fadeInValue_);

    fadeOutLabel_ = makeLabel("Out");
    fadeOutValue_ = std::make_unique<DraggableValueLabel>(DraggableValueLabel::Format::Raw);
    fadeOutValue_->setRange(0.0, 10.0, 0.0);
    fadeOutValue_->setDecimalPlaces(3);
    fadeOutValue_->setSuffix("s");
    fadeOutValue_->setDrawBackground(false);
    fadeOutValue_->setDrawBorder(true);
    fadeOutValue_->setShowFillIndicator(false);
    fadeOutValue_->setFontSize(11.0f);
    fadeOutValue_->setDoubleClickResetsValue(false);
    fadeOutValue_->onValueChange = [this]() {
        if (clipId_ == magda::INVALID_CLIP_ID)
            return;
        magda::UndoManager::getInstance().executeCommand(
            std::make_unique<magda::SetClipFadeOutCommand>(clipId_, fadeOutValue_->getValue()));
    };
    addAndMakeVisible(*fadeOutValue_);

    // Fade curve type buttons (Linear, Convex, Concave, S-Curve)
    {
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
            btn->setBorderColor(DarkTheme::getColour(DarkTheme::BORDER));
            btn->setBorderThickness(1.0f);
            btn->setTooltip(icon.tooltip);
            btn->setClickingTogglesState(false);
            addAndMakeVisible(*btn);
        };

        for (int i = 0; i < 4; ++i) {
            setupFadeTypeButton(fadeInTypeButtons_[i], fadeTypeIcons[i]);
            int fadeType = i + 1;
            fadeInTypeButtons_[i]->onClick = [this, i, fadeType]() {
                if (clipId_ == magda::INVALID_CLIP_ID)
                    return;
                magda::UndoManager::getInstance().executeCommand(
                    std::make_unique<magda::SetClipFadeInTypeCommand>(clipId_, fadeType));
                for (int j = 0; j < 4; ++j)
                    fadeInTypeButtons_[j]->setActive(j == i);
            };

            setupFadeTypeButton(fadeOutTypeButtons_[i], fadeTypeIcons[i]);
            fadeOutTypeButtons_[i]->onClick = [this, i, fadeType]() {
                if (clipId_ == magda::INVALID_CLIP_ID)
                    return;
                magda::UndoManager::getInstance().executeCommand(
                    std::make_unique<magda::SetClipFadeOutTypeCommand>(clipId_, fadeType));
                for (int j = 0; j < 4; ++j)
                    fadeOutTypeButtons_[j]->setActive(j == i);
            };
        }
    }

    // Fade behaviour buttons (Gain Fade, Speed Ramp)
    {
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
            btn->setBorderColor(DarkTheme::getColour(DarkTheme::BORDER));
            btn->setBorderThickness(1.0f);
            btn->setTooltip(icon.tooltip);
            btn->setClickingTogglesState(false);
            addAndMakeVisible(*btn);
        };

        for (int i = 0; i < 2; ++i) {
            setupFadeBehaviourButton(fadeInBehaviourButtons_[i], fadeBehaviourIcons[i]);
            fadeInBehaviourButtons_[i]->onClick = [this, i]() {
                if (clipId_ == magda::INVALID_CLIP_ID)
                    return;
                magda::UndoManager::getInstance().executeCommand(
                    std::make_unique<magda::SetClipFadeInBehaviourCommand>(clipId_, i));
                for (int j = 0; j < 2; ++j)
                    fadeInBehaviourButtons_[j]->setActive(j == i);
            };

            setupFadeBehaviourButton(fadeOutBehaviourButtons_[i], fadeBehaviourIcons[i]);
            fadeOutBehaviourButtons_[i]->onClick = [this, i]() {
                if (clipId_ == magda::INVALID_CLIP_ID)
                    return;
                magda::UndoManager::getInstance().executeCommand(
                    std::make_unique<magda::SetClipFadeOutBehaviourCommand>(clipId_, i));
                for (int j = 0; j < 2; ++j)
                    fadeOutBehaviourButtons_[j]->setActive(j == i);
            };
        }
    }

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
    bool hasClip = clip != nullptr && clip->type == magda::ClipType::Audio;

    warpToggle_->setToggleState(hasClip && clip->warpEnabled, juce::dontSendNotification);
    autoTempoToggle_->setToggleState(hasClip && clip->autoTempo, juce::dontSendNotification);
    analogPitchToggle_->setToggleState(hasClip && clip->analogPitch, juce::dontSendNotification);
    reverseToggle_->setToggleState(hasClip && clip->isReversed, juce::dontSendNotification);

    if (hasClip) {
        stretchValue_->setValue(clip->speedRatio, juce::dontSendNotification);
        stretchModeCombo_->setSelectedId(clip->timeStretchMode + 1, juce::dontSendNotification);
        bpmValue_->setValue(clip->sourceBPM > 0.0 ? clip->sourceBPM : 120.0,
                            juce::dontSendNotification);
        beatsValue_->setValue(clip->lengthBeats > 0.0 ? clip->lengthBeats : 4.0,
                              juce::dontSendNotification);
        pitchValue_->setValue(static_cast<double>(clip->pitchChange), juce::dontSendNotification);
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

        volumeValue_->setValue(static_cast<double>(clip->volumeDB), juce::dontSendNotification);
        gainValue_->setValue(static_cast<double>(clip->gainDB), juce::dontSendNotification);
        panValue_->setValue(static_cast<double>(clip->pan), juce::dontSendNotification);
    }

    bool enabled = hasClip;
    bool isAutoTempo = hasClip && clip->autoTempo;
    stretchValue_->setEnabled(enabled && !isAutoTempo);
    stretchModeCombo_->setEnabled(enabled);
    bpmValue_->setEnabled(enabled);
    beatsValue_->setEnabled(enabled && isAutoTempo);
    pitchValue_->setEnabled(enabled);
    analogPitchToggle_->setEnabled(enabled && !isAutoTempo && !(hasClip && clip->warpEnabled));
    fadeInValue_->setEnabled(enabled);
    fadeOutValue_->setEnabled(enabled);
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
    int iconBtnSize = ROW_HEIGHT;

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

    auto layoutIconButtons = [&](juce::Rectangle<int> row, auto* buttons, int count) {
        for (int i = 0; i < count; ++i) {
            safeBounds(*buttons[i], row.removeFromLeft(iconBtnSize));
            if (i < count - 1)
                row.removeFromLeft(2);
        }
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

    fadesSectionLabel_->setBounds(addRow(rightCol, SECTION_LABEL_HEIGHT));

    layoutLabelValue(addRow(rightCol, ROW_HEIGHT), *fadeInLabel_, *fadeInValue_, labelW);
    {
        auto row = addRow(rightCol, ROW_HEIGHT);
        row.removeFromLeft(labelW + 2);
        layoutIconButtons(row, fadeInTypeButtons_, 4);
    }
    {
        auto row = addRow(rightCol, ROW_HEIGHT);
        row.removeFromLeft(labelW + 2);
        layoutIconButtons(row, fadeInBehaviourButtons_, 2);
    }

    rightCol.removeFromTop(ROW_GAP);

    layoutLabelValue(addRow(rightCol, ROW_HEIGHT), *fadeOutLabel_, *fadeOutValue_, labelW);
    {
        auto row = addRow(rightCol, ROW_HEIGHT);
        row.removeFromLeft(labelW + 2);
        layoutIconButtons(row, fadeOutTypeButtons_, 4);
    }
    {
        auto row = addRow(rightCol, ROW_HEIGHT);
        row.removeFromLeft(labelW + 2);
        layoutIconButtons(row, fadeOutBehaviourButtons_, 2);
    }

    addSeparator(rightCol);

    mixSectionLabel_->setBounds(addRow(rightCol, SECTION_LABEL_HEIGHT));
    layoutLabelValue(addRow(rightCol, ROW_HEIGHT), *volLabel_, *volumeValue_, labelW);
    layoutLabelValue(addRow(rightCol, ROW_HEIGHT), *gainLabel_, *gainValue_, labelW);
    layoutLabelValue(addRow(rightCol, ROW_HEIGHT), *panLabel_, *panValue_, labelW);
}

}  // namespace magda::daw::ui
