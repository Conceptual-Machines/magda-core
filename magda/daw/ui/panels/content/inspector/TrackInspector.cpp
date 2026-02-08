#include "TrackInspector.hpp"

#include <cmath>

#include "../../../audio/MidiBridge.hpp"
#include "../../../engine/AudioEngine.hpp"
#include "../../state/TimelineController.hpp"
#include "../../themes/DarkTheme.hpp"
#include "../../themes/FontManager.hpp"
#include "core/ClipManager.hpp"

namespace magda::daw::ui {

TrackInspector::TrackInspector() {
    // Track name
    trackNameLabel_.setText("Name", juce::dontSendNotification);
    trackNameLabel_.setFont(FontManager::getInstance().getUIFont(11.0f));
    trackNameLabel_.setColour(juce::Label::textColourId, DarkTheme::getSecondaryTextColour());
    addAndMakeVisible(trackNameLabel_);

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
    addAndMakeVisible(trackNameValue_);

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
    addAndMakeVisible(muteButton_);

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
    addAndMakeVisible(soloButton_);

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
    addAndMakeVisible(recordButton_);

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
    addAndMakeVisible(*gainLabel_);

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
    addAndMakeVisible(*panLabel_);

    // Routing section
    routingSectionLabel_.setText("Routing", juce::dontSendNotification);
    routingSectionLabel_.setFont(FontManager::getInstance().getUIFont(11.0f));
    routingSectionLabel_.setColour(juce::Label::textColourId, DarkTheme::getSecondaryTextColour());
    addAndMakeVisible(routingSectionLabel_);

    // Input type selector (Audio/MIDI toggle)
    inputTypeSelector_ = std::make_unique<magda::InputTypeSelector>();
    addAndMakeVisible(*inputTypeSelector_);

    // Unified input selector (shows audio or MIDI options based on toggle)
    inputSelector_ = std::make_unique<magda::RoutingSelector>(magda::RoutingSelector::Type::MidiIn);
    addAndMakeVisible(*inputSelector_);

    // Output selector (audio output, always master)
    outputSelector_ =
        std::make_unique<magda::RoutingSelector>(magda::RoutingSelector::Type::AudioOut);
    addAndMakeVisible(*outputSelector_);

    // Send/Receive section
    sendReceiveSectionLabel_.setText("Sends / Receives", juce::dontSendNotification);
    sendReceiveSectionLabel_.setFont(FontManager::getInstance().getUIFont(11.0f));
    sendReceiveSectionLabel_.setColour(juce::Label::textColourId,
                                       DarkTheme::getSecondaryTextColour());
    addAndMakeVisible(sendReceiveSectionLabel_);

    addSendButton_.setButtonText("+ Send");
    addSendButton_.setColour(juce::TextButton::buttonColourId,
                             DarkTheme::getColour(DarkTheme::SURFACE));
    addSendButton_.setColour(juce::TextButton::textColourOffId,
                             DarkTheme::getSecondaryTextColour());
    addSendButton_.onClick = [this]() { showAddSendMenu(); };
    addAndMakeVisible(addSendButton_);

    noSendsLabel_.setText("No sends", juce::dontSendNotification);
    noSendsLabel_.setFont(FontManager::getInstance().getUIFont(10.0f));
    noSendsLabel_.setColour(juce::Label::textColourId, DarkTheme::getSecondaryTextColour());
    addAndMakeVisible(noSendsLabel_);

    receivesLabel_.setText("No receives", juce::dontSendNotification);
    receivesLabel_.setFont(FontManager::getInstance().getUIFont(10.0f));
    receivesLabel_.setColour(juce::Label::textColourId, DarkTheme::getSecondaryTextColour());
    addAndMakeVisible(receivesLabel_);

    // Clips section
    clipsSectionLabel_.setText("Clips", juce::dontSendNotification);
    clipsSectionLabel_.setFont(FontManager::getInstance().getUIFont(11.0f));
    clipsSectionLabel_.setColour(juce::Label::textColourId, DarkTheme::getSecondaryTextColour());
    addAndMakeVisible(clipsSectionLabel_);

    clipCountLabel_.setText("0 clips", juce::dontSendNotification);
    clipCountLabel_.setFont(FontManager::getInstance().getUIFont(12.0f));
    clipCountLabel_.setColour(juce::Label::textColourId, DarkTheme::getTextColour());
    addAndMakeVisible(clipCountLabel_);
}

TrackInspector::~TrackInspector() {
    magda::TrackManager::getInstance().removeListener(this);
}

void TrackInspector::onActivated() {
    magda::TrackManager::getInstance().addListener(this);
    populateRoutingSelectors();
    updateFromSelectedTrack();
}

void TrackInspector::onDeactivated() {
    magda::TrackManager::getInstance().removeListener(this);
}

void TrackInspector::paint(juce::Graphics& g) {
    g.fillAll(DarkTheme::getColour(DarkTheme::BACKGROUND));
}

void TrackInspector::resized() {
    auto bounds = getLocalBounds().reduced(8);

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

    const int toggleWidth = 40;
    const int selectorWidth = 55;
    const int selectorHeight = 18;
    const int selectorGap = 4;

    // Input row: [Audio|MIDI toggle] [Input selector]
    auto inputRow = bounds.removeFromTop(selectorHeight);
    inputTypeSelector_->setBounds(inputRow.removeFromLeft(toggleWidth));
    inputRow.removeFromLeft(selectorGap);
    inputSelector_->setBounds(inputRow.removeFromLeft(selectorWidth));
    bounds.removeFromTop(4);

    // Output row: [Output selector]
    auto outputRow = bounds.removeFromTop(selectorHeight);
    outputSelector_->setBounds(outputRow.removeFromLeft(selectorWidth));
    bounds.removeFromTop(16);

    // Send/Receive section
    auto sendHeaderRow = bounds.removeFromTop(16);
    sendReceiveSectionLabel_.setBounds(sendHeaderRow.removeFromLeft(100));
    addSendButton_.setBounds(sendHeaderRow.removeFromRight(50).withHeight(16));
    bounds.removeFromTop(4);

    if (sendDestLabels_.empty()) {
        noSendsLabel_.setBounds(bounds.removeFromTop(16));
        noSendsLabel_.setVisible(true);
    } else {
        noSendsLabel_.setVisible(false);
        for (size_t i = 0; i < sendDestLabels_.size(); ++i) {
            auto sendRow = bounds.removeFromTop(18);
            sendDestLabels_[i]->setBounds(sendRow.removeFromLeft(60));
            sendRow.removeFromLeft(4);
            sendLevelLabels_[i]->setBounds(sendRow.removeFromLeft(50));
            sendRow.removeFromLeft(4);
            sendDeleteButtons_[i]->setBounds(sendRow.removeFromLeft(18));
            bounds.removeFromTop(2);
        }
    }

    receivesLabel_.setBounds(bounds.removeFromTop(16));
    bounds.removeFromTop(16);

    // Clips section
    clipsSectionLabel_.setBounds(bounds.removeFromTop(16));
    bounds.removeFromTop(4);
    clipCountLabel_.setBounds(bounds.removeFromTop(20));
}

void TrackInspector::setSelectedTrack(magda::TrackId trackId) {
    selectedTrackId_ = trackId;
    updateFromSelectedTrack();
}

// ============================================================================
// TrackManagerListener Interface
// ============================================================================

void TrackInspector::tracksChanged() {
    updateFromSelectedTrack();
}

void TrackInspector::trackPropertyChanged(int trackId) {
    if (static_cast<magda::TrackId>(trackId) == selectedTrackId_) {
        updateFromSelectedTrack();
    }
}

void TrackInspector::trackSelectionChanged(magda::TrackId trackId) {
    // Not used - selection is managed externally
    (void)trackId;
}

void TrackInspector::deviceParameterChanged(magda::DeviceId deviceId, int paramIndex,
                                            float newValue) {
    // Not relevant for track inspector
    (void)deviceId;
    (void)paramIndex;
    (void)newValue;
}

// ============================================================================
// Private Methods
// ============================================================================

void TrackInspector::updateFromSelectedTrack() {
    if (selectedTrackId_ == magda::INVALID_TRACK_ID) {
        showTrackControls(false);
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

        // Update sends UI
        rebuildSendsUI();

        showTrackControls(true);
    } else {
        showTrackControls(false);
    }

    resized();
    repaint();
}

void TrackInspector::showTrackControls(bool show) {
    trackNameLabel_.setVisible(show);
    trackNameValue_.setVisible(show);
    muteButton_.setVisible(show);
    soloButton_.setVisible(show);
    recordButton_.setVisible(show);
    gainLabel_->setVisible(show);
    panLabel_->setVisible(show);

    // Routing section
    routingSectionLabel_.setVisible(show);
    inputTypeSelector_->setVisible(show);
    inputSelector_->setVisible(show);
    outputSelector_->setVisible(show);

    // Send/Receive section
    sendReceiveSectionLabel_.setVisible(show);
    addSendButton_.setVisible(show);
    noSendsLabel_.setVisible(show);
    receivesLabel_.setVisible(show);
    for (auto& l : sendDestLabels_)
        l->setVisible(show);
    for (auto& l : sendLevelLabels_)
        l->setVisible(show);
    for (auto& b : sendDeleteButtons_)
        b->setVisible(show);

    // Clips section
    clipsSectionLabel_.setVisible(show);
    clipCountLabel_.setVisible(show);
}

void TrackInspector::rebuildSendsUI() {
    // Remove existing send UI components
    for (auto& l : sendDestLabels_)
        removeChildComponent(l.get());
    for (auto& l : sendLevelLabels_)
        removeChildComponent(l.get());
    for (auto& b : sendDeleteButtons_)
        removeChildComponent(b.get());
    sendDestLabels_.clear();
    sendLevelLabels_.clear();
    sendDeleteButtons_.clear();

    if (selectedTrackId_ == magda::INVALID_TRACK_ID)
        return;

    const auto* track = magda::TrackManager::getInstance().getTrack(selectedTrackId_);
    if (!track)
        return;

    for (const auto& send : track->sends) {
        // Destination name label
        auto destLabel = std::make_unique<juce::Label>();
        const auto* destTrack = magda::TrackManager::getInstance().getTrack(send.destTrackId);
        destLabel->setText(destTrack ? destTrack->name : "?", juce::dontSendNotification);
        destLabel->setFont(FontManager::getInstance().getUIFont(10.0f));
        destLabel->setColour(juce::Label::textColourId, DarkTheme::getTextColour());
        addAndMakeVisible(*destLabel);
        sendDestLabels_.push_back(std::move(destLabel));

        // Send level label (draggable dB)
        auto levelLabel = std::make_unique<magda::DraggableValueLabel>(
            magda::DraggableValueLabel::Format::Decibels);
        levelLabel->setRange(-60.0, 6.0, 0.0);
        float levelDb = (send.level <= 0.0f) ? -60.0f : 20.0f * std::log10(send.level);
        levelLabel->setValue(levelDb, juce::dontSendNotification);

        int busIndex = send.busIndex;
        magda::TrackId srcId = selectedTrackId_;
        auto* levelLabelPtr = levelLabel.get();
        levelLabel->onValueChange = [srcId, busIndex, levelLabelPtr]() {
            double db = levelLabelPtr->getValue();
            float gain = (db <= -60.0) ? 0.0f : std::pow(10.0f, static_cast<float>(db) / 20.0f);
            magda::TrackManager::getInstance().setSendLevel(srcId, busIndex, gain);
        };
        addAndMakeVisible(*levelLabel);
        sendLevelLabels_.push_back(std::move(levelLabel));

        // Delete button
        auto deleteBtn = std::make_unique<juce::TextButton>("X");
        deleteBtn->setColour(juce::TextButton::buttonColourId,
                             DarkTheme::getColour(DarkTheme::SURFACE));
        deleteBtn->setColour(juce::TextButton::textColourOffId,
                             DarkTheme::getColour(DarkTheme::STATUS_ERROR));
        deleteBtn->onClick = [srcId, busIndex]() {
            magda::TrackManager::getInstance().removeSend(srcId, busIndex);
        };
        addAndMakeVisible(*deleteBtn);
        sendDeleteButtons_.push_back(std::move(deleteBtn));
    }

    resized();
    repaint();
}

void TrackInspector::showAddSendMenu() {
    if (selectedTrackId_ == magda::INVALID_TRACK_ID)
        return;

    const auto* currentTrack = magda::TrackManager::getInstance().getTrack(selectedTrackId_);
    if (!currentTrack)
        return;

    juce::PopupMenu menu;
    const auto& allTracks = magda::TrackManager::getInstance().getTracks();

    int itemId = 1;
    std::vector<magda::TrackId> auxTrackIds;

    for (const auto& track : allTracks) {
        if (track.type != magda::TrackType::Aux)
            continue;
        if (track.id == selectedTrackId_)
            continue;

        // Filter out aux tracks that already have a send from this track
        bool alreadyHasSend = false;
        for (const auto& send : currentTrack->sends) {
            if (send.destTrackId == track.id) {
                alreadyHasSend = true;
                break;
            }
        }
        if (alreadyHasSend)
            continue;

        menu.addItem(itemId, track.name);
        auxTrackIds.push_back(track.id);
        ++itemId;
    }

    if (menu.getNumItems() == 0) {
        menu.addItem(-1, "(No available aux tracks)", false);
    }

    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&addSendButton_),
                       [this, auxTrackIds](int result) {
                           if (result > 0 && result <= static_cast<int>(auxTrackIds.size())) {
                               magda::TrackManager::getInstance().addSend(selectedTrackId_,
                                                                          auxTrackIds[result - 1]);
                           }
                       });
}

void TrackInspector::populateRoutingSelectors() {
    if (!audioEngine_)
        return;

    // Populate output selector (always audio out)
    populateAudioOutputOptions();

    // Populate input selector based on current type
    if (inputTypeSelector_->getInputType() == magda::InputTypeSelector::InputType::Audio) {
        populateAudioInputOptions();
    } else {
        populateMidiInputOptions();
    }

    auto* midiBridge = audioEngine_->getMidiBridge();

    // Input type toggle callback
    inputTypeSelector_->onInputTypeChanged = [this](magda::InputTypeSelector::InputType type) {
        if (selectedTrackId_ == magda::INVALID_TRACK_ID)
            return;

        if (type == magda::InputTypeSelector::InputType::Audio) {
            // Switching to Audio: clear MIDI input, populate audio options
            magda::TrackManager::getInstance().setTrackMidiInput(selectedTrackId_, "");
            populateAudioInputOptions();
            int firstChannel = inputSelector_->getFirstChannelOptionId();
            inputSelector_->setSelectedId(firstChannel > 0 ? firstChannel : 1);
            inputSelector_->setEnabled(true);
            magda::TrackManager::getInstance().setTrackAudioInput(selectedTrackId_, "default");
        } else {
            // Switching to MIDI: clear audio input, populate MIDI options
            magda::TrackManager::getInstance().setTrackAudioInput(selectedTrackId_, "");
            populateMidiInputOptions();
            // Default to "All Inputs"
            inputSelector_->setSelectedId(1);
            inputSelector_->setEnabled(true);
            magda::TrackManager::getInstance().setTrackMidiInput(selectedTrackId_, "all");
        }
    };

    // Unified input selector callbacks
    inputSelector_->onSelectionChanged = [this, midiBridge](int selectedId) {
        if (selectedTrackId_ == magda::INVALID_TRACK_ID)
            return;

        if (inputTypeSelector_->getInputType() == magda::InputTypeSelector::InputType::Audio) {
            // Audio input selection
            if (selectedId == 1) {
                magda::TrackManager::getInstance().setTrackAudioInput(selectedTrackId_, "");
            } else if (selectedId >= 10) {
                magda::TrackManager::getInstance().setTrackAudioInput(selectedTrackId_, "default");
            }
        } else {
            // MIDI input selection
            if (selectedId == 2) {
                magda::TrackManager::getInstance().setTrackMidiInput(selectedTrackId_, "");
            } else if (selectedId == 1) {
                magda::TrackManager::getInstance().setTrackMidiInput(selectedTrackId_, "all");
            } else if (selectedId >= 10 && midiBridge) {
                auto midiInputs = midiBridge->getAvailableMidiInputs();
                int deviceIndex = selectedId - 10;
                if (deviceIndex >= 0 && deviceIndex < static_cast<int>(midiInputs.size())) {
                    magda::TrackManager::getInstance().setTrackMidiInput(
                        selectedTrackId_, midiInputs[deviceIndex].id);
                }
            }
        }
    };

    inputSelector_->onEnabledChanged = [this, midiBridge](bool enabled) {
        if (selectedTrackId_ == magda::INVALID_TRACK_ID)
            return;

        if (inputTypeSelector_->getInputType() == magda::InputTypeSelector::InputType::Audio) {
            if (enabled) {
                magda::TrackManager::getInstance().setTrackAudioInput(selectedTrackId_, "default");
            } else {
                magda::TrackManager::getInstance().setTrackAudioInput(selectedTrackId_, "");
            }
        } else {
            if (enabled) {
                int selectedId = inputSelector_->getSelectedId();
                if (selectedId == 1) {
                    magda::TrackManager::getInstance().setTrackMidiInput(selectedTrackId_, "all");
                } else if (selectedId >= 10 && midiBridge) {
                    auto midiInputs = midiBridge->getAvailableMidiInputs();
                    int deviceIndex = selectedId - 10;
                    if (deviceIndex >= 0 && deviceIndex < static_cast<int>(midiInputs.size())) {
                        magda::TrackManager::getInstance().setTrackMidiInput(
                            selectedTrackId_, midiInputs[deviceIndex].id);
                    } else {
                        magda::TrackManager::getInstance().setTrackMidiInput(selectedTrackId_,
                                                                             "all");
                    }
                } else {
                    magda::TrackManager::getInstance().setTrackMidiInput(selectedTrackId_, "all");
                }
            } else {
                magda::TrackManager::getInstance().setTrackMidiInput(selectedTrackId_, "");
            }
        }
    };

    // Output selector callbacks
    outputSelector_->onEnabledChanged = [this](bool enabled) {
        if (selectedTrackId_ == magda::INVALID_TRACK_ID)
            return;

        if (enabled) {
            magda::TrackManager::getInstance().setTrackAudioOutput(selectedTrackId_, "master");
        } else {
            magda::TrackManager::getInstance().setTrackAudioOutput(selectedTrackId_, "");
        }
    };

    outputSelector_->onSelectionChanged = [this](int selectedId) {
        if (selectedTrackId_ == magda::INVALID_TRACK_ID)
            return;

        if (selectedId == 1) {
            // Master
            magda::TrackManager::getInstance().setTrackAudioOutput(selectedTrackId_, "master");
        } else if (selectedId >= 200 && selectedId < 400) {
            // Group or Aux track destination
            auto it = outputTrackMapping_.find(selectedId);
            if (it != outputTrackMapping_.end()) {
                magda::TrackManager::getInstance().setTrackAudioOutput(
                    selectedTrackId_, "track:" + juce::String(it->second));
            }
        } else if (selectedId >= 10) {
            // Hardware output
            magda::TrackManager::getInstance().setTrackAudioOutput(selectedTrackId_, "master");
        }
    };
}

void TrackInspector::populateAudioInputOptions() {
    if (!inputSelector_ || !audioEngine_) {
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

    inputSelector_->setOptions(options);
}

void TrackInspector::populateAudioOutputOptions() {
    if (!outputSelector_ || !audioEngine_) {
        return;
    }

    auto* deviceManager = audioEngine_->getDeviceManager();
    if (!deviceManager) {
        return;
    }

    std::vector<magda::RoutingSelector::RoutingOption> options;

    // Add "Master" as default output
    options.push_back({1, "Master"});

    // Add Group and Aux tracks as routing destinations
    auto& trackManager = magda::TrackManager::getInstance();
    const auto& allTracks = trackManager.getTracks();

    // Collect descendants to prevent routing cycles
    std::vector<magda::TrackId> descendants;
    if (selectedTrackId_ != magda::INVALID_TRACK_ID) {
        descendants = trackManager.getAllDescendants(selectedTrackId_);
    }

    // Group tracks (ID 200+)
    {
        std::vector<magda::RoutingSelector::RoutingOption> groupOptions;
        int id = 200;
        for (const auto& t : allTracks) {
            if (t.type == magda::TrackType::Group && t.id != selectedTrackId_) {
                if (std::find(descendants.begin(), descendants.end(), t.id) != descendants.end())
                    continue;
                groupOptions.push_back({id++, t.name});
            }
        }
        if (!groupOptions.empty()) {
            options.push_back({0, "", true});
            for (auto& opt : groupOptions)
                options.push_back(std::move(opt));
        }
    }

    // Aux tracks (ID 300+)
    {
        std::vector<magda::RoutingSelector::RoutingOption> auxOptions;
        int id = 300;
        for (const auto& t : allTracks) {
            if (t.type == magda::TrackType::Aux && t.id != selectedTrackId_) {
                auxOptions.push_back({id++, t.name});
            }
        }
        if (!auxOptions.empty()) {
            options.push_back({0, "", true});
            for (auto& opt : auxOptions)
                options.push_back(std::move(opt));
        }
    }

    // Store track mapping for output selection callbacks
    outputTrackMapping_.clear();
    {
        int id = 200;
        for (const auto& t : allTracks) {
            if (t.type == magda::TrackType::Group && t.id != selectedTrackId_) {
                if (std::find(descendants.begin(), descendants.end(), t.id) != descendants.end())
                    continue;
                outputTrackMapping_[id++] = t.id;
            }
        }
        id = 300;
        for (const auto& t : allTracks) {
            if (t.type == magda::TrackType::Aux && t.id != selectedTrackId_) {
                outputTrackMapping_[id++] = t.id;
            }
        }
    }

    // Hardware output channels
    auto* currentDevice = deviceManager->getCurrentAudioDevice();
    if (currentDevice) {
        auto activeOutputChannels = currentDevice->getActiveOutputChannels();
        int numActiveChannels = activeOutputChannels.countNumberOfSetBits();

        if (numActiveChannels > 0) {
            options.push_back({0, "", true});

            juce::Array<int> activeIndices;
            for (int i = 0; i < activeOutputChannels.getHighestBit() + 1; ++i) {
                if (activeOutputChannels[i]) {
                    activeIndices.add(i);
                }
            }

            int id = 10;
            for (int i = 0; i < activeIndices.size(); i += 2) {
                if (i + 1 < activeIndices.size()) {
                    int ch1 = activeIndices[i] + 1;
                    int ch2 = activeIndices[i + 1] + 1;
                    juce::String pairName = juce::String(ch1) + "-" + juce::String(ch2);
                    options.push_back({id++, pairName});
                }
            }

            if (activeIndices.size() > 1) {
                options.push_back({0, "", true});
            }

            id = 100;
            for (int i = 0; i < activeIndices.size(); ++i) {
                int channelNum = activeIndices[i] + 1;
                options.push_back({id++, juce::String(channelNum) + " (mono)"});
            }
        }
    }

    outputSelector_->setOptions(options);
}

void TrackInspector::populateMidiInputOptions() {
    if (!inputSelector_ || !audioEngine_) {
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

    inputSelector_->setOptions(options);
}

void TrackInspector::populateMidiOutputOptions() {
    // MIDI output routing is kept in code but hidden from this UI
    // (no midiOutSelector_ anymore)
}

void TrackInspector::updateRoutingSelectorsFromTrack() {
    if (selectedTrackId_ == magda::INVALID_TRACK_ID || !audioEngine_) {
        return;
    }

    const auto* track = magda::TrackManager::getInstance().getTrack(selectedTrackId_);
    if (!track) {
        return;
    }

    auto* midiBridge = audioEngine_->getMidiBridge();

    // Determine which input type is active
    bool hasAudioInput = !track->audioInputDevice.isEmpty();
    bool hasMidiInput = !track->midiInputDevice.isEmpty();

    if (hasAudioInput) {
        inputTypeSelector_->setInputTypeSilently(magda::InputTypeSelector::InputType::Audio);
        int currentId = inputSelector_->getSelectedId();
        populateAudioInputOptions();
        if (currentId < 10) {
            int firstChannel = inputSelector_->getFirstChannelOptionId();
            inputSelector_->setSelectedId(firstChannel > 0 ? firstChannel : 1);
        }
        inputSelector_->setEnabled(true);
    } else {
        inputTypeSelector_->setInputTypeSilently(magda::InputTypeSelector::InputType::MIDI);
        populateMidiInputOptions();

        if (!hasMidiInput) {
            inputSelector_->setSelectedId(2);  // "None"
            inputSelector_->setEnabled(false);
        } else if (track->midiInputDevice == "all") {
            inputSelector_->setSelectedId(1);  // "All Inputs"
            inputSelector_->setEnabled(true);
        } else if (midiBridge) {
            auto midiInputs = midiBridge->getAvailableMidiInputs();
            int selectedId = 2;
            for (size_t i = 0; i < midiInputs.size(); ++i) {
                if (midiInputs[i].id == track->midiInputDevice) {
                    selectedId = 10 + static_cast<int>(i);
                    break;
                }
            }
            inputSelector_->setSelectedId(selectedId);
            inputSelector_->setEnabled(selectedId != 2);
        }
    }

    // Update Audio Output selector
    juce::String currentAudioOutput = track->audioOutputDevice;
    if (currentAudioOutput.isEmpty()) {
        outputSelector_->setSelectedId(2);  // "None"
        outputSelector_->setEnabled(false);
    } else if (currentAudioOutput == "master") {
        outputSelector_->setSelectedId(1);  // Master
        outputSelector_->setEnabled(true);
    } else if (currentAudioOutput.startsWith("track:")) {
        // Track-to-track routing — find the option ID for this destination
        magda::TrackId destId =
            currentAudioOutput.fromFirstOccurrenceOf("track:", false, false).getIntValue();
        int optionId = -1;
        for (const auto& [oid, tid] : outputTrackMapping_) {
            if (tid == destId) {
                optionId = oid;
                break;
            }
        }
        if (optionId > 0) {
            outputSelector_->setSelectedId(optionId);
        }
        outputSelector_->setEnabled(true);
    } else {
        outputSelector_->setEnabled(true);
    }
}

}  // namespace magda::daw::ui
