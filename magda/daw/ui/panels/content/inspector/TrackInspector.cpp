#include "TrackInspector.hpp"

#include <cmath>

#include "../../../audio/MidiBridge.hpp"
#include "../../../engine/AudioEngine.hpp"
#include "../../components/mixer/RoutingSyncHelper.hpp"
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
            if (selectedTrackId_ == magda::MASTER_TRACK_ID)
                magda::TrackManager::getInstance().setMasterMuted(muteButton_.getToggleState());
            else
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
            double db = gainLabel_->getValue();
            float gain = (db <= -60.0f) ? 0.0f : std::pow(10.0f, static_cast<float>(db) / 20.0f);
            if (selectedTrackId_ == magda::MASTER_TRACK_ID)
                magda::TrackManager::getInstance().setMasterVolume(gain);
            else
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
            float pan = static_cast<float>(panLabel_->getValue());
            if (selectedTrackId_ == magda::MASTER_TRACK_ID)
                magda::TrackManager::getInstance().setMasterPan(pan);
            else
                magda::TrackManager::getInstance().setTrackPan(selectedTrackId_, pan);
        }
    };
    addAndMakeVisible(*panLabel_);

    // Routing section
    routingSectionLabel_.setText("Routing", juce::dontSendNotification);
    routingSectionLabel_.setFont(FontManager::getInstance().getUIFont(11.0f));
    routingSectionLabel_.setColour(juce::Label::textColourId, DarkTheme::getSecondaryTextColour());
    addAndMakeVisible(routingSectionLabel_);

    // Input type selector (hidden, kept for internal state)
    inputTypeSelector_ = std::make_unique<magda::InputTypeSelector>();

    // Audio input selector
    audioInputSelector_ =
        std::make_unique<magda::RoutingSelector>(magda::RoutingSelector::Type::AudioIn);
    audioInputSelector_->setSelectedId(1);
    audioInputSelector_->setEnabled(false);  // Disabled by default (MIDI input active)
    addAndMakeVisible(*audioInputSelector_);

    // MIDI input selector
    inputSelector_ = std::make_unique<magda::RoutingSelector>(magda::RoutingSelector::Type::MidiIn);
    addAndMakeVisible(*inputSelector_);

    // Audio output selector
    outputSelector_ =
        std::make_unique<magda::RoutingSelector>(magda::RoutingSelector::Type::AudioOut);
    addAndMakeVisible(*outputSelector_);

    // MIDI output selector
    midiOutputSelector_ =
        std::make_unique<magda::RoutingSelector>(magda::RoutingSelector::Type::MidiOut);
    midiOutputSelector_->setSelectedId(1);   // "None"
    midiOutputSelector_->setEnabled(false);  // Disabled by default
    addAndMakeVisible(*midiOutputSelector_);

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

    const int selectorWidth = 55;
    const int selectorHeight = 18;
    const int selectorGap = 4;

    // Input row: [Audio In] [MIDI In]
    auto inputRow = bounds.removeFromTop(selectorHeight);
    audioInputSelector_->setBounds(inputRow.removeFromLeft(selectorWidth));
    inputRow.removeFromLeft(selectorGap);
    inputSelector_->setBounds(inputRow.removeFromLeft(selectorWidth));
    bounds.removeFromTop(4);

    // Output row: [Audio Out] [MIDI Out]
    auto outputRow = bounds.removeFromTop(selectorHeight);
    outputSelector_->setBounds(outputRow.removeFromLeft(selectorWidth));
    outputRow.removeFromLeft(selectorGap);
    midiOutputSelector_->setBounds(outputRow.removeFromLeft(selectorWidth));
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

void TrackInspector::trackDevicesChanged(magda::TrackId trackId) {
    if (trackId == selectedTrackId_) {
        rebuildSendsUI();
    }
}

void TrackInspector::trackSelectionChanged(magda::TrackId trackId) {
    // Not used - selection is managed externally
    (void)trackId;
}

void TrackInspector::masterChannelChanged() {
    if (selectedTrackId_ == magda::MASTER_TRACK_ID) {
        updateFromSelectedTrack();
    }
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

    // Master track — show basic controls from MasterChannelState
    if (selectedTrackId_ == magda::MASTER_TRACK_ID) {
        const auto& master = magda::TrackManager::getInstance().getMasterChannel();
        trackNameValue_.setText("Master", juce::dontSendNotification);
        muteButton_.setToggleState(master.muted, juce::dontSendNotification);
        soloButton_.setToggleState(false, juce::dontSendNotification);
        recordButton_.setToggleState(false, juce::dontSendNotification);

        float gainDb = (master.volume <= 0.0f) ? -60.0f : 20.0f * std::log10(master.volume);
        gainLabel_->setValue(gainDb, juce::dontSendNotification);
        panLabel_->setValue(master.pan, juce::dontSendNotification);

        clipCountLabel_.setText("0 clips", juce::dontSendNotification);

        showTrackControls(true);
        resized();
        repaint();
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

        // Update send level values in-place (don't rebuild — that destroys mid-drag labels)
        const auto& sends = track->sends;
        if (sends.size() == sendLevelLabels_.size()) {
            for (size_t i = 0; i < sends.size(); ++i) {
                float levelDb =
                    (sends[i].level <= 0.0f) ? -60.0f : 20.0f * std::log10(sends[i].level);
                sendLevelLabels_[i]->setValue(levelDb, juce::dontSendNotification);
            }
        } else {
            rebuildSendsUI();
        }

        showTrackControls(true);
    } else {
        showTrackControls(false);
    }

    resized();
    repaint();
}

void TrackInspector::showTrackControls(bool show) {
    bool isMaster = show && selectedTrackId_ == magda::MASTER_TRACK_ID;
    bool isAux = false;
    if (show && selectedTrackId_ != magda::INVALID_TRACK_ID &&
        selectedTrackId_ != magda::MASTER_TRACK_ID) {
        const auto* track = magda::TrackManager::getInstance().getTrack(selectedTrackId_);
        if (track && track->type == magda::TrackType::Aux)
            isAux = true;
    }

    trackNameLabel_.setVisible(show);
    trackNameValue_.setVisible(show);
    muteButton_.setVisible(show);
    soloButton_.setVisible(show && !isMaster);
    recordButton_.setVisible(show && !isMaster && !isAux);
    gainLabel_->setVisible(show);
    panLabel_->setVisible(show);

    // Routing section — hidden for master and aux
    bool showRouting = show && !isMaster && !isAux;
    routingSectionLabel_.setVisible(showRouting);
    audioInputSelector_->setVisible(showRouting);
    inputSelector_->setVisible(showRouting);
    outputSelector_->setVisible(showRouting);
    midiOutputSelector_->setVisible(showRouting);

    // Send/Receive section — hidden for master and aux tracks
    bool showSends = show && !isMaster && !isAux;
    sendReceiveSectionLabel_.setVisible(showSends);
    addSendButton_.setVisible(showSends);
    noSendsLabel_.setVisible(showSends);
    receivesLabel_.setVisible(showSends);
    for (auto& l : sendDestLabels_)
        l->setVisible(showSends);
    for (auto& l : sendLevelLabels_)
        l->setVisible(showSends);
    for (auto& b : sendDeleteButtons_)
        b->setVisible(showSends);

    // Clips section — hidden for master
    clipsSectionLabel_.setVisible(show && !isMaster);
    clipCountLabel_.setVisible(show && !isMaster);
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

    // Aux tracks don't have sends
    if (track->type == magda::TrackType::Aux)
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

    // Aux tracks can't have sends
    if (currentTrack->type == magda::TrackType::Aux)
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

    // Capture selectedTrackId_ by value to avoid stale reference if selection
    // changes while the async menu is open
    TrackId sourceTrackId = selectedTrackId_;
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&addSendButton_),
                       [sourceTrackId, auxTrackIds](int result) {
                           if (result > 0 && result <= static_cast<int>(auxTrackIds.size())) {
                               magda::TrackManager::getInstance().addSend(sourceTrackId,
                                                                          auxTrackIds[result - 1]);
                           }
                       });
}

void TrackInspector::populateRoutingSelectors() {
    if (!audioEngine_)
        return;

    // Populate all routing selectors
    populateAudioInputOptions();
    populateMidiInputOptions();
    populateAudioOutputOptions();
    populateMidiOutputOptions();

    auto* midiBridge = audioEngine_->getMidiBridge();

    // Audio input selector callbacks (mutually exclusive with MIDI input)
    audioInputSelector_->onEnabledChanged = [this](bool enabled) {
        if (selectedTrackId_ == magda::INVALID_TRACK_ID)
            return;

        if (enabled) {
            // Disable MIDI input (mutually exclusive)
            inputSelector_->setEnabled(false);
            magda::TrackManager::getInstance().setTrackMidiInput(selectedTrackId_, "");
            magda::TrackManager::getInstance().setTrackAudioInput(selectedTrackId_, "default");
        } else {
            magda::TrackManager::getInstance().setTrackAudioInput(selectedTrackId_, "");
        }
    };

    audioInputSelector_->onSelectionChanged = [this](int selectedId) {
        if (selectedTrackId_ == magda::INVALID_TRACK_ID)
            return;

        if (selectedId == 1) {
            magda::TrackManager::getInstance().setTrackAudioInput(selectedTrackId_, "");
        } else if (selectedId >= 10) {
            magda::TrackManager::getInstance().setTrackAudioInput(selectedTrackId_, "default");
        }
    };

    // MIDI input selector callbacks (mutually exclusive with audio input)
    inputSelector_->onEnabledChanged = [this, midiBridge](bool enabled) {
        if (selectedTrackId_ == magda::INVALID_TRACK_ID)
            return;

        if (enabled) {
            // Disable audio input (mutually exclusive)
            audioInputSelector_->setEnabled(false);
            magda::TrackManager::getInstance().setTrackAudioInput(selectedTrackId_, "");
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
                    magda::TrackManager::getInstance().setTrackMidiInput(selectedTrackId_, "all");
                }
            } else {
                magda::TrackManager::getInstance().setTrackMidiInput(selectedTrackId_, "all");
            }
        } else {
            magda::TrackManager::getInstance().setTrackMidiInput(selectedTrackId_, "");
        }
    };

    inputSelector_->onSelectionChanged = [this, midiBridge](int selectedId) {
        if (selectedTrackId_ == magda::INVALID_TRACK_ID)
            return;

        if (selectedId == 2) {
            magda::TrackManager::getInstance().setTrackMidiInput(selectedTrackId_, "");
        } else if (selectedId == 1) {
            magda::TrackManager::getInstance().setTrackMidiInput(selectedTrackId_, "all");
        } else if (selectedId >= 10 && midiBridge) {
            auto midiInputs = midiBridge->getAvailableMidiInputs();
            int deviceIndex = selectedId - 10;
            if (deviceIndex >= 0 && deviceIndex < static_cast<int>(midiInputs.size())) {
                magda::TrackManager::getInstance().setTrackMidiInput(selectedTrackId_,
                                                                     midiInputs[deviceIndex].id);
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

    // MIDI output selector callbacks
    midiOutputSelector_->onEnabledChanged = [this](bool enabled) {
        if (selectedTrackId_ == magda::INVALID_TRACK_ID)
            return;

        if (!enabled) {
            magda::TrackManager::getInstance().setTrackMidiOutput(selectedTrackId_, "");
        }
        // When enabling, don't set anything yet — user picks a device from dropdown
    };

    midiOutputSelector_->onSelectionChanged = [this, midiBridge](int selectedId) {
        if (selectedTrackId_ == magda::INVALID_TRACK_ID)
            return;

        if (selectedId == 1) {
            magda::TrackManager::getInstance().setTrackMidiOutput(selectedTrackId_, "");
        } else if (selectedId >= 10 && midiBridge) {
            auto midiOutputs = midiBridge->getAvailableMidiOutputs();
            int deviceIndex = selectedId - 10;
            if (deviceIndex >= 0 && deviceIndex < static_cast<int>(midiOutputs.size())) {
                magda::TrackManager::getInstance().setTrackMidiOutput(selectedTrackId_,
                                                                      midiOutputs[deviceIndex].id);
            }
        }
    };
}

void TrackInspector::populateAudioInputOptions() {
    if (!audioInputSelector_ || !audioEngine_)
        return;
    auto* deviceManager = audioEngine_->getDeviceManager();
    if (!deviceManager)
        return;
    magda::RoutingSyncHelper::populateAudioInputOptions(audioInputSelector_.get(),
                                                        deviceManager->getCurrentAudioDevice());
}

void TrackInspector::populateAudioOutputOptions() {
    if (!outputSelector_ || !audioEngine_)
        return;
    auto* deviceManager = audioEngine_->getDeviceManager();
    if (!deviceManager)
        return;
    magda::RoutingSyncHelper::populateAudioOutputOptions(outputSelector_.get(), selectedTrackId_,
                                                         deviceManager->getCurrentAudioDevice(),
                                                         outputTrackMapping_);
}

void TrackInspector::populateMidiInputOptions() {
    if (!inputSelector_ || !audioEngine_)
        return;
    magda::RoutingSyncHelper::populateMidiInputOptions(inputSelector_.get(),
                                                       audioEngine_->getMidiBridge());
}

void TrackInspector::populateMidiOutputOptions() {
    if (!midiOutputSelector_ || !audioEngine_)
        return;
    magda::RoutingSyncHelper::populateMidiOutputOptions(midiOutputSelector_.get(),
                                                        audioEngine_->getMidiBridge());
}

void TrackInspector::updateRoutingSelectorsFromTrack() {
    if (selectedTrackId_ == magda::INVALID_TRACK_ID || !audioEngine_)
        return;

    const auto* track = magda::TrackManager::getInstance().getTrack(selectedTrackId_);
    if (!track)
        return;

    auto* deviceManager = audioEngine_->getDeviceManager();
    auto* device = deviceManager ? deviceManager->getCurrentAudioDevice() : nullptr;
    magda::RoutingSyncHelper::syncSelectorsFromTrack(
        *track, audioInputSelector_.get(), inputSelector_.get(), outputSelector_.get(),
        midiOutputSelector_.get(), audioEngine_->getMidiBridge(), device, selectedTrackId_,
        outputTrackMapping_);
}

}  // namespace magda::daw::ui
