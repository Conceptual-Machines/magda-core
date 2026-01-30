#include "InspectorContent.hpp"

#include "../../../audio/MidiBridge.hpp"
#include "../../../engine/AudioEngine.hpp"
#include "../../state/TimelineController.hpp"
#include "../../themes/DarkTheme.hpp"
#include "../../themes/FontManager.hpp"
#include "../../utils/TimelineUtils.hpp"
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

    // Clip name
    clipNameLabel_.setText("Name", juce::dontSendNotification);
    clipNameLabel_.setFont(FontManager::getInstance().getUIFont(11.0f));
    clipNameLabel_.setColour(juce::Label::textColourId, DarkTheme::getSecondaryTextColour());
    addChildComponent(clipNameLabel_);

    clipNameValue_.setFont(FontManager::getInstance().getUIFont(12.0f));
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

    // Clip type
    clipTypeLabel_.setText("Type", juce::dontSendNotification);
    clipTypeLabel_.setFont(FontManager::getInstance().getUIFont(11.0f));
    clipTypeLabel_.setColour(juce::Label::textColourId, DarkTheme::getSecondaryTextColour());
    addChildComponent(clipTypeLabel_);

    clipTypeValue_.setFont(FontManager::getInstance().getUIFont(12.0f));
    clipTypeValue_.setColour(juce::Label::textColourId, DarkTheme::getTextColour());
    addChildComponent(clipTypeValue_);

    // Clip start
    clipStartLabel_.setText("Start", juce::dontSendNotification);
    clipStartLabel_.setFont(FontManager::getInstance().getUIFont(11.0f));
    clipStartLabel_.setColour(juce::Label::textColourId, DarkTheme::getSecondaryTextColour());
    addChildComponent(clipStartLabel_);

    clipStartValue_.setFont(FontManager::getInstance().getUIFont(12.0f));
    clipStartValue_.setColour(juce::Label::textColourId, DarkTheme::getTextColour());
    addChildComponent(clipStartValue_);

    // Clip length
    clipLengthLabel_.setText("Length", juce::dontSendNotification);
    clipLengthLabel_.setFont(FontManager::getInstance().getUIFont(11.0f));
    clipLengthLabel_.setColour(juce::Label::textColourId, DarkTheme::getSecondaryTextColour());
    addChildComponent(clipLengthLabel_);

    clipLengthValue_.setFont(FontManager::getInstance().getUIFont(12.0f));
    clipLengthValue_.setColour(juce::Label::textColourId, DarkTheme::getTextColour());
    addChildComponent(clipLengthValue_);

    // Loop toggle
    clipLoopToggle_.setButtonText("Loop");
    clipLoopToggle_.setColour(juce::ToggleButton::textColourId, DarkTheme::getTextColour());
    clipLoopToggle_.setColour(juce::ToggleButton::tickColourId,
                              DarkTheme::getColour(DarkTheme::ACCENT_BLUE));
    clipLoopToggle_.onClick = [this]() {
        if (selectedClipId_ != magda::INVALID_CLIP_ID) {
            magda::ClipManager::getInstance().setClipLoopEnabled(selectedClipId_,
                                                                 clipLoopToggle_.getToggleState());
        }
    };
    addChildComponent(clipLoopToggle_);

    // Loop length
    clipLoopLengthLabel_.setText("Loop Length", juce::dontSendNotification);
    clipLoopLengthLabel_.setFont(FontManager::getInstance().getUIFont(11.0f));
    clipLoopLengthLabel_.setColour(juce::Label::textColourId, DarkTheme::getSecondaryTextColour());
    addChildComponent(clipLoopLengthLabel_);

    clipLoopLengthSlider_.setSliderStyle(juce::Slider::LinearHorizontal);
    clipLoopLengthSlider_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 60, 20);
    clipLoopLengthSlider_.setRange(0.25, 64.0, 0.25);
    clipLoopLengthSlider_.setTextValueSuffix(" beats");
    clipLoopLengthSlider_.setColour(juce::Slider::trackColourId,
                                    DarkTheme::getColour(DarkTheme::SURFACE));
    clipLoopLengthSlider_.setColour(juce::Slider::thumbColourId,
                                    DarkTheme::getColour(DarkTheme::ACCENT_BLUE));
    clipLoopLengthSlider_.onValueChange = [this]() {
        if (selectedClipId_ != magda::INVALID_CLIP_ID) {
            magda::ClipManager::getInstance().setClipLoopLength(selectedClipId_,
                                                                clipLoopLengthSlider_.getValue());
        }
    };
    addChildComponent(clipLoopLengthSlider_);

    // ========================================================================
    // Session clip launch properties
    // ========================================================================

    launchModeLabel_.setText("Launch Mode", juce::dontSendNotification);
    launchModeLabel_.setFont(FontManager::getInstance().getUIFont(11.0f));
    launchModeLabel_.setColour(juce::Label::textColourId, DarkTheme::getSecondaryTextColour());
    addChildComponent(launchModeLabel_);

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
    addChildComponent(launchModeCombo_);

    launchQuantizeLabel_.setText("Launch Quantize", juce::dontSendNotification);
    launchQuantizeLabel_.setFont(FontManager::getInstance().getUIFont(11.0f));
    launchQuantizeLabel_.setColour(juce::Label::textColourId, DarkTheme::getSecondaryTextColour());
    addChildComponent(launchQuantizeLabel_);

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
    addChildComponent(launchQuantizeCombo_);

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
    magda::TrackManager::getInstance().removeListener(this);
    magda::ClipManager::getInstance().removeListener(this);
    magda::SelectionManager::getInstance().removeListener(this);
}

void InspectorContent::setTimelineController(magda::TimelineController* controller) {
    timelineController_ = controller;
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

void InspectorContent::paint(juce::Graphics& g) {
    g.fillAll(DarkTheme::getPanelBackgroundColour());
}

void InspectorContent::resized() {
    auto bounds = getLocalBounds().reduced(10);

    titleLabel_.setBounds(bounds.removeFromTop(24));
    bounds.removeFromTop(8);  // Spacing

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
        // Clip properties layout
        clipNameLabel_.setBounds(bounds.removeFromTop(16));
        clipNameValue_.setBounds(bounds.removeFromTop(24));
        bounds.removeFromTop(12);

        // Type (read-only)
        clipTypeLabel_.setBounds(bounds.removeFromTop(16));
        clipTypeValue_.setBounds(bounds.removeFromTop(20));
        bounds.removeFromTop(12);

        // Start and Length — side by side
        {
            auto labelRow = bounds.removeFromTop(16);
            auto halfWidth = labelRow.getWidth() / 2;
            clipStartLabel_.setBounds(labelRow.removeFromLeft(halfWidth));
            clipLengthLabel_.setBounds(labelRow);

            auto valueRow = bounds.removeFromTop(20);
            halfWidth = valueRow.getWidth() / 2;
            clipStartValue_.setBounds(valueRow.removeFromLeft(halfWidth));
            clipLengthValue_.setBounds(valueRow);
            bounds.removeFromTop(12);
        }

        // Loop controls (hidden for session clips)
        if (clipLoopToggle_.isVisible()) {
            clipLoopToggle_.setBounds(bounds.removeFromTop(24));
            bounds.removeFromTop(8);

            clipLoopLengthLabel_.setBounds(bounds.removeFromTop(16));
            clipLoopLengthSlider_.setBounds(bounds.removeFromTop(24));
            bounds.removeFromTop(12);
        }

        // Session clip launch properties (only for session clips)
        if (launchQuantizeLabel_.isVisible()) {
            launchQuantizeLabel_.setBounds(bounds.removeFromTop(16));
            launchQuantizeCombo_.setBounds(bounds.removeFromTop(24));
        }
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

    const auto* clip = magda::ClipManager::getInstance().getClip(selectedClipId_);
    if (clip) {
        clipNameValue_.setText(clip->name, juce::dontSendNotification);
        clipTypeValue_.setText(magda::getClipTypeName(clip->type), juce::dontSendNotification);

        // Get tempo from TimelineController, fallback to 120 BPM if not available
        double bpm = 120.0;
        int beatsPerBar = 4;
        if (timelineController_) {
            const auto& state = timelineController_->getState();
            bpm = state.tempo.bpm;
            beatsPerBar = state.tempo.timeSignatureNumerator;
        }

        bool isSessionClip = (clip->view == magda::ClipView::Session);

        if (isSessionClip) {
            // Session clips: start is always 1.1.000, length from loop length
            clipStartValue_.setText("1.1.000", juce::dontSendNotification);
            auto lengthStr =
                magda::TimelineUtils::formatBeatsAsBarsBeats(clip->internalLoopLength, beatsPerBar);
            clipLengthValue_.setText(juce::String(lengthStr), juce::dontSendNotification);
        } else {
            // Arrangement clips: position and duration in bars.beats.ticks
            auto startStr =
                magda::TimelineUtils::formatTimeAsBarsBeats(clip->startTime, bpm, beatsPerBar);
            clipStartValue_.setText(juce::String(startStr), juce::dontSendNotification);
            double lengthBeats = magda::TimelineUtils::secondsToBeats(clip->length, bpm);
            auto lengthStr = magda::TimelineUtils::formatBeatsAsBarsBeats(lengthBeats, beatsPerBar);
            clipLengthValue_.setText(juce::String(lengthStr), juce::dontSendNotification);
        }

        clipLoopToggle_.setToggleState(clip->internalLoopEnabled, juce::dontSendNotification);
        clipLoopLengthSlider_.setValue(clip->internalLoopLength, juce::dontSendNotification);

        // Session clip launch properties
        launchModeLabel_.setVisible(false);
        launchModeCombo_.setVisible(false);
        launchQuantizeLabel_.setVisible(isSessionClip);
        launchQuantizeCombo_.setVisible(isSessionClip);

        if (isSessionClip) {
            launchQuantizeCombo_.setSelectedId(static_cast<int>(clip->launchQuantize) + 1,
                                               juce::dontSendNotification);
        }

        showClipControls(true);
        noSelectionLabel_.setVisible(false);

        // Session clips: hide loop controls (length field replaces loop length),
        // show start/length side by side
        if (isSessionClip) {
            clipLoopToggle_.setVisible(false);
            clipLoopLengthLabel_.setVisible(false);
            clipLoopLengthSlider_.setVisible(false);
        }
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
    clipNameLabel_.setVisible(show);
    clipNameValue_.setVisible(show);
    clipTypeLabel_.setVisible(show);
    clipTypeValue_.setVisible(show);
    clipStartLabel_.setVisible(show);
    clipStartValue_.setVisible(show);
    clipLengthLabel_.setVisible(show);
    clipLengthValue_.setVisible(show);
    clipLoopToggle_.setVisible(show);
    clipLoopLengthLabel_.setVisible(show);
    clipLoopLengthSlider_.setVisible(show);

    // Session launch controls are conditionally shown in updateFromSelectedClip
    if (!show) {
        launchModeLabel_.setVisible(false);
        launchModeCombo_.setVisible(false);
        launchQuantizeLabel_.setVisible(false);
        launchQuantizeCombo_.setVisible(false);
    }
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

    // Get tempo info
    double bpm = 120.0;
    if (timelineController_) {
        const auto& state = timelineController_->getState();
        bpm = state.tempo.bpm;
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
