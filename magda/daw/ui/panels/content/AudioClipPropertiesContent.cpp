#include "AudioClipPropertiesContent.hpp"

#include "../themes/DarkTheme.hpp"
#include "../themes/FontManager.hpp"
#include "../themes/SmallButtonLookAndFeel.hpp"
#include "core/ClipPropertyCommands.hpp"
#include "core/UndoManager.hpp"

namespace magda::daw::ui {

namespace {
constexpr int ROW_HEIGHT = 20;
constexpr int ROW_GAP = 3;
constexpr int SECTION_LABEL_HEIGHT = 18;
constexpr int SEPARATOR_PAD = 5;
constexpr int TOGGLE_WIDTH = 46;
constexpr int LABEL_WIDTH = 40;
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
    if (autoPitchToggle_)
        autoPitchToggle_->setLookAndFeel(nullptr);
    if (reverseToggle_)
        reverseToggle_->setLookAndFeel(nullptr);
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
    autoTempoToggle_->setTooltip("Lock clip to musical time.\nUse Inspector for full control.");
    autoTempoToggle_->setEnabled(false);

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
    bpmValue_->setEnabled(false);
    addAndMakeVisible(*bpmValue_);

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
    pitchValue_->onValueChange = [this]() {
        if (clipId_ == magda::INVALID_CLIP_ID)
            return;
        magda::UndoManager::getInstance().executeCommand(
            std::make_unique<magda::SetClipPitchCommand>(
                clipId_, static_cast<float>(pitchValue_->getValue())));
    };
    addAndMakeVisible(*pitchValue_);

    autoPitchToggle_ = makeToggle("AUTO");
    autoPitchToggle_->setTooltip("Enable auto-pitch detection");
    autoPitchToggle_->onClick = [this]() {
        if (clipId_ == magda::INVALID_CLIP_ID)
            return;
        const auto* clip = magda::ClipManager::getInstance().getClip(clipId_);
        if (!clip)
            return;
        magda::ClipManager::getInstance().setAutoPitch(clipId_, !clip->autoPitch);
    };

    // ===================== MIX SECTION =====================
    mixSectionLabel_ = makeSectionLabel("Mix");

    volLabel_ = makeLabel("Vol");
    volumeValue_ = std::make_unique<DraggableValueLabel>(DraggableValueLabel::Format::Decibels);
    volumeValue_->setRange(-100.0, 0.0, 0.0);
    volumeValue_->setDrawBackground(false);
    volumeValue_->setDrawBorder(true);
    volumeValue_->setShowFillIndicator(false);
    volumeValue_->setFontSize(11.0f);
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
    autoPitchToggle_->setToggleState(hasClip && clip->autoPitch, juce::dontSendNotification);
    reverseToggle_->setToggleState(hasClip && clip->isReversed, juce::dontSendNotification);

    if (hasClip) {
        stretchValue_->setValue(clip->speedRatio, juce::dontSendNotification);
        stretchModeCombo_->setSelectedId(clip->timeStretchMode + 1, juce::dontSendNotification);
        bpmValue_->setValue(clip->sourceBPM > 0.0 ? clip->sourceBPM : 120.0,
                            juce::dontSendNotification);
        pitchValue_->setValue(static_cast<double>(clip->pitchChange), juce::dontSendNotification);
        volumeValue_->setValue(static_cast<double>(clip->volumeDB), juce::dontSendNotification);
        gainValue_->setValue(static_cast<double>(clip->gainDB), juce::dontSendNotification);
        panValue_->setValue(static_cast<double>(clip->pan), juce::dontSendNotification);
    }

    bool enabled = hasClip;
    stretchValue_->setEnabled(enabled);
    stretchModeCombo_->setEnabled(enabled);
    pitchValue_->setEnabled(enabled);
    volumeValue_->setEnabled(enabled);
    gainValue_->setEnabled(enabled);
    panValue_->setEnabled(enabled);
    warpToggle_->setEnabled(enabled);
    autoPitchToggle_->setEnabled(enabled);
    reverseToggle_->setEnabled(enabled);

    repaint();
}

void AudioClipPropertiesContent::paint(juce::Graphics& g) {
    g.fillAll(DarkTheme::getPanelBackgroundColour());

    if (clipId_ == magda::INVALID_CLIP_ID) {
        g.setColour(DarkTheme::getColour(DarkTheme::TEXT_SECONDARY).withAlpha(0.5f));
        g.setFont(FontManager::getInstance().getUIFont(13.0f));
        g.drawText("No audio clip selected", getLocalBounds(), juce::Justification::centred);
        return;
    }

    // Draw separator lines between sections
    g.setColour(DarkTheme::getColour(DarkTheme::SEPARATOR));
    for (int y : separatorYPositions_) {
        g.drawHorizontalLine(y, 0.0f, static_cast<float>(getWidth()));
    }
}

void AudioClipPropertiesContent::resized() {
    auto bounds = getLocalBounds().reduced(H_PAD, V_PAD);
    separatorYPositions_.clear();

    auto addRow = [&](int height) -> juce::Rectangle<int> {
        auto row = bounds.removeFromTop(height);
        bounds.removeFromTop(ROW_GAP);
        return row;
    };

    auto addSeparator = [&]() {
        bounds.removeFromTop(SEPARATOR_PAD);
        separatorYPositions_.push_back(bounds.getY());
        bounds.removeFromTop(SEPARATOR_PAD);
    };

    int labelW = LABEL_WIDTH;
    int toggleW = TOGGLE_WIDTH;
    int gap = 4;

    // ===== CLIP SECTION =====
    clipSectionLabel_->setBounds(addRow(SECTION_LABEL_HEIGHT));

    {
        auto row = addRow(ROW_HEIGHT);
        warpToggle_->setBounds(row.removeFromLeft(toggleW));
        row.removeFromLeft(gap);
        autoTempoToggle_->setBounds(row.removeFromLeft(toggleW));
        row.removeFromLeft(gap);
        reverseToggle_->setBounds(row.removeFromLeft(toggleW));
    }

    addSeparator();

    // ===== STRETCH SECTION =====
    stretchSectionLabel_->setBounds(addRow(SECTION_LABEL_HEIGHT));

    {
        auto row = addRow(ROW_HEIGHT);
        speedLabel_->setBounds(row.removeFromLeft(labelW));
        row.removeFromLeft(2);
        stretchValue_->setBounds(row);
    }
    {
        auto row = addRow(ROW_HEIGHT);
        modeLabel_->setBounds(row.removeFromLeft(labelW));
        row.removeFromLeft(2);
        stretchModeCombo_->setBounds(row);
    }
    {
        auto row = addRow(ROW_HEIGHT);
        bpmLabel_->setBounds(row.removeFromLeft(labelW));
        row.removeFromLeft(2);
        bpmValue_->setBounds(row);
    }

    addSeparator();

    // ===== PITCH SECTION =====
    pitchSectionLabel_->setBounds(addRow(SECTION_LABEL_HEIGHT));

    {
        auto row = addRow(ROW_HEIGHT);
        pitchLabel_->setBounds(row.removeFromLeft(labelW));
        row.removeFromLeft(2);
        autoPitchToggle_->setBounds(row.removeFromRight(toggleW));
        row.removeFromRight(gap);
        pitchValue_->setBounds(row);
    }

    addSeparator();

    // ===== MIX SECTION =====
    mixSectionLabel_->setBounds(addRow(SECTION_LABEL_HEIGHT));

    {
        auto row = addRow(ROW_HEIGHT);
        volLabel_->setBounds(row.removeFromLeft(labelW));
        row.removeFromLeft(2);
        volumeValue_->setBounds(row);
    }
    {
        auto row = addRow(ROW_HEIGHT);
        gainLabel_->setBounds(row.removeFromLeft(labelW));
        row.removeFromLeft(2);
        gainValue_->setBounds(row);
    }
    {
        auto row = addRow(ROW_HEIGHT);
        panLabel_->setBounds(row.removeFromLeft(labelW));
        row.removeFromLeft(2);
        panValue_->setBounds(row);
    }
}

}  // namespace magda::daw::ui
