#include "InspectorContent.hpp"

#include "../../state/TimelineController.hpp"
#include "../../themes/DarkTheme.hpp"
#include "../../themes/FontManager.hpp"
#include "../../utils/TimelineUtils.hpp"

namespace magica::daw::ui {

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
        if (selectedTrackId_ != magica::INVALID_TRACK_ID) {
            magica::TrackManager::getInstance().setTrackName(selectedTrackId_,
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
        if (selectedTrackId_ != magica::INVALID_TRACK_ID) {
            magica::TrackManager::getInstance().setTrackMuted(selectedTrackId_,
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
        if (selectedTrackId_ != magica::INVALID_TRACK_ID) {
            magica::TrackManager::getInstance().setTrackSoloed(selectedTrackId_,
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
    gainLabel_ = std::make_unique<magica::DraggableValueLabel>(
        magica::DraggableValueLabel::Format::Decibels);
    gainLabel_->setRange(-60.0, 6.0, 0.0);  // -60 to +6 dB, default 0 dB
    gainLabel_->onValueChange = [this]() {
        if (selectedTrackId_ != magica::INVALID_TRACK_ID) {
            // Convert dB to linear gain
            double db = gainLabel_->getValue();
            float gain = (db <= -60.0f) ? 0.0f : std::pow(10.0f, static_cast<float>(db) / 20.0f);
            magica::TrackManager::getInstance().setTrackVolume(selectedTrackId_, gain);
        }
    };
    addChildComponent(*gainLabel_);

    // Pan label (TCP style - draggable L/C/R display)
    panLabel_ =
        std::make_unique<magica::DraggableValueLabel>(magica::DraggableValueLabel::Format::Pan);
    panLabel_->setRange(-1.0, 1.0, 0.0);  // -1 (L) to +1 (R), default center
    panLabel_->onValueChange = [this]() {
        if (selectedTrackId_ != magica::INVALID_TRACK_ID) {
            magica::TrackManager::getInstance().setTrackPan(
                selectedTrackId_, static_cast<float>(panLabel_->getValue()));
        }
    };
    addChildComponent(*panLabel_);

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
        if (selectedClipId_ != magica::INVALID_CLIP_ID) {
            magica::ClipManager::getInstance().setClipName(selectedClipId_,
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
        if (selectedClipId_ != magica::INVALID_CLIP_ID) {
            magica::ClipManager::getInstance().setClipLoopEnabled(selectedClipId_,
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
    clipLoopLengthSlider_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 50, 20);
    clipLoopLengthSlider_.setRange(0.25, 64.0, 0.25);
    clipLoopLengthSlider_.setColour(juce::Slider::trackColourId,
                                    DarkTheme::getColour(DarkTheme::SURFACE));
    clipLoopLengthSlider_.setColour(juce::Slider::thumbColourId,
                                    DarkTheme::getColour(DarkTheme::ACCENT_BLUE));
    clipLoopLengthSlider_.onValueChange = [this]() {
        if (selectedClipId_ != magica::INVALID_CLIP_ID) {
            magica::ClipManager::getInstance().setClipLoopLength(selectedClipId_,
                                                                 clipLoopLengthSlider_.getValue());
        }
    };
    addChildComponent(clipLoopLengthSlider_);

    // Register as listeners
    magica::TrackManager::getInstance().addListener(this);
    magica::ClipManager::getInstance().addListener(this);
    magica::SelectionManager::getInstance().addListener(this);

    // Check if there's already a selection
    currentSelectionType_ = magica::SelectionManager::getInstance().getSelectionType();
    selectedTrackId_ = magica::SelectionManager::getInstance().getSelectedTrack();
    selectedClipId_ = magica::SelectionManager::getInstance().getSelectedClip();
    updateSelectionDisplay();
}

InspectorContent::~InspectorContent() {
    magica::TrackManager::getInstance().removeListener(this);
    magica::ClipManager::getInstance().removeListener(this);
    magica::SelectionManager::getInstance().removeListener(this);
}

void InspectorContent::setTimelineController(magica::TimelineController* controller) {
    timelineController_ = controller;
    // Refresh display with new tempo info if a clip is selected
    if (currentSelectionType_ == magica::SelectionType::Clip) {
        updateFromSelectedClip();
    }
}

void InspectorContent::paint(juce::Graphics& g) {
    g.fillAll(DarkTheme::getPanelBackgroundColour());
}

void InspectorContent::resized() {
    auto bounds = getLocalBounds().reduced(10);

    titleLabel_.setBounds(bounds.removeFromTop(24));
    bounds.removeFromTop(8);  // Spacing

    if (currentSelectionType_ == magica::SelectionType::None) {
        // Center the no-selection label
        noSelectionLabel_.setBounds(bounds);
    } else if (currentSelectionType_ == magica::SelectionType::Track) {
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
    } else if (currentSelectionType_ == magica::SelectionType::Clip) {
        // Clip properties layout
        clipNameLabel_.setBounds(bounds.removeFromTop(16));
        clipNameValue_.setBounds(bounds.removeFromTop(24));
        bounds.removeFromTop(12);

        // Type (read-only)
        clipTypeLabel_.setBounds(bounds.removeFromTop(16));
        clipTypeValue_.setBounds(bounds.removeFromTop(20));
        bounds.removeFromTop(12);

        // Start time (read-only for now)
        clipStartLabel_.setBounds(bounds.removeFromTop(16));
        clipStartValue_.setBounds(bounds.removeFromTop(20));
        bounds.removeFromTop(12);

        // Length (read-only for now)
        clipLengthLabel_.setBounds(bounds.removeFromTop(16));
        clipLengthValue_.setBounds(bounds.removeFromTop(20));
        bounds.removeFromTop(12);

        // Loop toggle
        clipLoopToggle_.setBounds(bounds.removeFromTop(24));
        bounds.removeFromTop(8);

        // Loop length
        clipLoopLengthLabel_.setBounds(bounds.removeFromTop(16));
        clipLoopLengthSlider_.setBounds(bounds.removeFromTop(24));
    }
}

void InspectorContent::onActivated() {
    // Refresh from current selection
    currentSelectionType_ = magica::SelectionManager::getInstance().getSelectionType();
    selectedTrackId_ = magica::SelectionManager::getInstance().getSelectedTrack();
    selectedClipId_ = magica::SelectionManager::getInstance().getSelectedClip();
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
    if (selectedTrackId_ != magica::INVALID_TRACK_ID) {
        const auto* track = magica::TrackManager::getInstance().getTrack(selectedTrackId_);
        if (!track) {
            selectedTrackId_ = magica::INVALID_TRACK_ID;
            updateSelectionDisplay();
        }
    }
}

void InspectorContent::trackPropertyChanged(int trackId) {
    if (static_cast<magica::TrackId>(trackId) == selectedTrackId_) {
        updateFromSelectedTrack();
    }
}

void InspectorContent::trackSelectionChanged(magica::TrackId trackId) {
    if (currentSelectionType_ == magica::SelectionType::Track) {
        selectedTrackId_ = trackId;
        updateFromSelectedTrack();
    }
}

// ============================================================================
// ClipManagerListener
// ============================================================================

void InspectorContent::clipsChanged() {
    // Clip may have been deleted
    if (selectedClipId_ != magica::INVALID_CLIP_ID) {
        const auto* clip = magica::ClipManager::getInstance().getClip(selectedClipId_);
        if (!clip) {
            selectedClipId_ = magica::INVALID_CLIP_ID;
            updateSelectionDisplay();
        }
    }
}

void InspectorContent::clipPropertyChanged(magica::ClipId clipId) {
    if (clipId == selectedClipId_) {
        updateFromSelectedClip();
    }
}

void InspectorContent::clipSelectionChanged(magica::ClipId clipId) {
    if (currentSelectionType_ == magica::SelectionType::Clip) {
        selectedClipId_ = clipId;
        updateFromSelectedClip();
    }
}

// ============================================================================
// SelectionManagerListener
// ============================================================================

void InspectorContent::selectionTypeChanged(magica::SelectionType newType) {
    currentSelectionType_ = newType;

    // Update the appropriate selection ID
    switch (newType) {
        case magica::SelectionType::Track:
            selectedTrackId_ = magica::SelectionManager::getInstance().getSelectedTrack();
            selectedClipId_ = magica::INVALID_CLIP_ID;
            break;

        case magica::SelectionType::Clip:
            selectedClipId_ = magica::SelectionManager::getInstance().getSelectedClip();
            selectedTrackId_ = magica::INVALID_TRACK_ID;
            break;

        default:
            selectedTrackId_ = magica::INVALID_TRACK_ID;
            selectedClipId_ = magica::INVALID_CLIP_ID;
            break;
    }

    updateSelectionDisplay();
}

// ============================================================================
// Update Methods
// ============================================================================

void InspectorContent::updateSelectionDisplay() {
    switch (currentSelectionType_) {
        case magica::SelectionType::None:
        case magica::SelectionType::TimeRange:
            showTrackControls(false);
            showClipControls(false);
            noSelectionLabel_.setVisible(true);
            break;

        case magica::SelectionType::Track:
            showClipControls(false);
            noSelectionLabel_.setVisible(false);
            updateFromSelectedTrack();
            break;

        case magica::SelectionType::Clip:
            showTrackControls(false);
            noSelectionLabel_.setVisible(false);
            updateFromSelectedClip();
            break;

        case magica::SelectionType::MultiClip:
            // For multi-clip selection, show "Multiple clips selected" or similar
            showTrackControls(false);
            showClipControls(false);
            noSelectionLabel_.setText("Multiple clips selected", juce::dontSendNotification);
            noSelectionLabel_.setVisible(true);
            break;
    }

    resized();
    repaint();
}

void InspectorContent::updateFromSelectedTrack() {
    if (selectedTrackId_ == magica::INVALID_TRACK_ID) {
        showTrackControls(false);
        noSelectionLabel_.setVisible(true);
        return;
    }

    const auto* track = magica::TrackManager::getInstance().getTrack(selectedTrackId_);
    if (track) {
        trackNameValue_.setText(track->name, juce::dontSendNotification);
        muteButton_.setToggleState(track->muted, juce::dontSendNotification);
        soloButton_.setToggleState(track->soloed, juce::dontSendNotification);
        recordButton_.setToggleState(track->recordArmed, juce::dontSendNotification);

        // Convert linear gain to dB for display
        float gainDb = (track->volume <= 0.0f) ? -60.0f : 20.0f * std::log10(track->volume);
        gainLabel_->setValue(gainDb, juce::dontSendNotification);
        panLabel_->setValue(track->pan, juce::dontSendNotification);

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
    if (selectedClipId_ == magica::INVALID_CLIP_ID) {
        showClipControls(false);
        noSelectionLabel_.setVisible(true);
        return;
    }

    const auto* clip = magica::ClipManager::getInstance().getClip(selectedClipId_);
    if (clip) {
        clipNameValue_.setText(clip->name, juce::dontSendNotification);
        clipTypeValue_.setText(magica::getClipTypeName(clip->type), juce::dontSendNotification);

        // Get tempo from TimelineController, fallback to 120 BPM if not available
        double bpm = 120.0;
        int beatsPerBar = 4;
        if (timelineController_) {
            const auto& state = timelineController_->getState();
            bpm = state.tempo.bpm;
            beatsPerBar = state.tempo.timeSignatureNumerator;
        }

        // Format start time as bars.beats.ticks
        auto startStr =
            magica::TimelineUtils::formatTimeAsBarsBeats(clip->startTime, bpm, beatsPerBar);
        clipStartValue_.setText(juce::String(startStr), juce::dontSendNotification);

        // Format length as bars and beats
        auto lengthStr =
            magica::TimelineUtils::formatDurationAsBarsBeats(clip->length, bpm, beatsPerBar);
        clipLengthValue_.setText(juce::String(lengthStr), juce::dontSendNotification);

        clipLoopToggle_.setToggleState(clip->internalLoopEnabled, juce::dontSendNotification);
        clipLoopLengthSlider_.setValue(clip->internalLoopLength, juce::dontSendNotification);

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
}

}  // namespace magica::daw::ui
