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

    // Audio input selector
    audioInSelector_ =
        std::make_unique<magda::RoutingSelector>(magda::RoutingSelector::Type::AudioIn);
    addAndMakeVisible(*audioInSelector_);

    // Audio output selector
    audioOutSelector_ =
        std::make_unique<magda::RoutingSelector>(magda::RoutingSelector::Type::AudioOut);
    addAndMakeVisible(*audioOutSelector_);

    // MIDI input selector
    midiInSelector_ =
        std::make_unique<magda::RoutingSelector>(magda::RoutingSelector::Type::MidiIn);
    addAndMakeVisible(*midiInSelector_);

    // MIDI output selector
    midiOutSelector_ =
        std::make_unique<magda::RoutingSelector>(magda::RoutingSelector::Type::MidiOut);
    addAndMakeVisible(*midiOutSelector_);

    // Send/Receive section
    sendReceiveSectionLabel_.setText("Sends / Receives", juce::dontSendNotification);
    sendReceiveSectionLabel_.setFont(FontManager::getInstance().getUIFont(11.0f));
    sendReceiveSectionLabel_.setColour(juce::Label::textColourId,
                                       DarkTheme::getSecondaryTextColour());
    addAndMakeVisible(sendReceiveSectionLabel_);

    sendsLabel_.setText("No sends", juce::dontSendNotification);
    sendsLabel_.setFont(FontManager::getInstance().getUIFont(10.0f));
    sendsLabel_.setColour(juce::Label::textColourId, DarkTheme::getSecondaryTextColour());
    addAndMakeVisible(sendsLabel_);

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

void TrackInspector::populateRoutingSelectors() {
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
        DBG("TrackInspector MIDI input selector changed - selectedId=" << selectedId << " trackId="
                                                                       << selectedTrackId_);

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
        DBG("TrackInspector MIDI input enabled changed - enabled=" << (int)enabled << " trackId="
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
        DBG("TrackInspector MIDI output enabled changed - enabled=" << (int)enabled << " trackId="
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
        DBG("TrackInspector audio input enabled changed - enabled=" << (int)enabled << " trackId="
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
        DBG("TrackInspector audio output enabled changed - enabled=" << (int)enabled << " trackId="
                                                                     << selectedTrackId_);

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
        DBG("TrackInspector MIDI output selector changed - selectedId=" << selectedId << " trackId="
                                                                        << selectedTrackId_);

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

void TrackInspector::populateAudioInputOptions() {
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

void TrackInspector::populateAudioOutputOptions() {
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

void TrackInspector::populateMidiInputOptions() {
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

void TrackInspector::populateMidiOutputOptions() {
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

void TrackInspector::updateRoutingSelectorsFromTrack() {
    if (selectedTrackId_ == magda::INVALID_TRACK_ID || !audioEngine_) {
        DBG("TrackInspector::updateRoutingSelectorsFromTrack - invalid track or no engine");
        return;
    }

    // Get track from TrackManager
    const auto* track = magda::TrackManager::getInstance().getTrack(selectedTrackId_);
    if (!track) {
        DBG("TrackInspector::updateRoutingSelectorsFromTrack - track not found");
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

}  // namespace magda::daw::ui
