#include "TrackInspector.hpp"

#include <BinaryData.h>

#include <algorithm>
#include <cmath>
#include <vector>

#include "../../../audio/AudioBridge.hpp"
#include "../../../audio/MidiBridge.hpp"
#include "../../../engine/AudioEngine.hpp"
#include "../../components/common/ColourSwatch.hpp"
#include "../../components/mixer/RoutingSyncHelper.hpp"
#include "../../state/TimelineController.hpp"
#include "../../themes/DarkTheme.hpp"
#include "../../themes/FontManager.hpp"
#include "../../themes/SmallButtonLookAndFeel.hpp"
#include "core/ClipManager.hpp"
#include "core/Config.hpp"
#include "core/TrackPropertyCommands.hpp"
#include "core/UndoManager.hpp"

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
            magda::UndoManager::getInstance().executeCommand(
                std::make_unique<magda::SetTrackNameCommand>(selectedTrackId_,
                                                             trackNameValue_.getText()));
        }
    };
    addAndMakeVisible(trackNameValue_);

    // Colour swatch
    colourSwatch_ = std::make_unique<magda::ColourSwatch>();
    auto* swatch = static_cast<magda::ColourSwatch*>(colourSwatch_.get());
    swatch->onColourClicked = [this, swatch]() {
        if (selectedTrackId_ == magda::INVALID_TRACK_ID && selectedTrackIds_.empty())
            return;

        auto menu = juce::PopupMenu();
        menu.addItem(1, "None");
        menu.addSeparator();

        // Helper to create a colour chip icon for menu items
        auto makeChip = [](juce::Colour colour) {
            juce::Image chip(juce::Image::ARGB, 14, 14, true);
            juce::Graphics cg(chip);
            cg.setColour(colour);
            cg.fillRoundedRectangle(0.0f, 0.0f, 14.0f, 14.0f, 2.0f);
            auto drawable = std::make_unique<juce::DrawableImage>();
            drawable->setImage(chip);
            return drawable;
        };

        // Default colours (always available)
        for (size_t i = 0; i < magda::Config::defaultColourPalette.size(); ++i) {
            auto colour = juce::Colour(magda::Config::defaultColourPalette[i].colour);
            menu.addItem(static_cast<int>(i + 2), magda::Config::defaultColourPalette[i].name, true,
                         false, makeChip(colour));
        }

        // Custom colours from Config (user-defined)
        const auto customPalette = magda::Config::getInstance().getTrackColourPalette();
        const int customOffset = static_cast<int>(magda::Config::defaultColourPalette.size()) + 2;
        if (!customPalette.empty()) {
            menu.addSeparator();
            for (size_t i = 0; i < customPalette.size(); ++i) {
                auto colour = juce::Colour(customPalette[i].colour);
                menu.addItem(customOffset + static_cast<int>(i),
                             juce::String(customPalette[i].name), true, false, makeChip(colour));
            }
        }

        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(swatch), [this, swatch,
                                                                                    customPalette](
                                                                                       int result) {
            if (result == 0)
                return;
            const int customOff = static_cast<int>(magda::Config::defaultColourPalette.size()) + 2;
            auto trackIds = selectedTrackIds_.empty()
                                ? std::unordered_set<magda::TrackId>{selectedTrackId_}
                                : selectedTrackIds_;
            if (result == 1) {
                // "None"
                swatch->clearColour();
                for (auto tid : trackIds) {
                    magda::UndoManager::getInstance().executeCommand(
                        std::make_unique<magda::SetTrackColourCommand>(tid,
                                                                       juce::Colour(0xFF444444)));
                }
            } else if (result >= 2 && result < customOff) {
                // Default colour
                auto colour = juce::Colour(magda::Config::getDefaultColour(result - 2));
                swatch->setColour(colour);
                for (auto tid : trackIds) {
                    magda::UndoManager::getInstance().executeCommand(
                        std::make_unique<magda::SetTrackColourCommand>(tid, colour));
                }
            } else {
                // Custom colour
                auto idx = static_cast<size_t>(result - customOff);
                if (idx < customPalette.size()) {
                    auto colour = juce::Colour(customPalette[idx].colour);
                    swatch->setColour(colour);
                    for (auto tid : trackIds) {
                        magda::UndoManager::getInstance().executeCommand(
                            std::make_unique<magda::SetTrackColourCommand>(tid, colour));
                    }
                }
            }
        });
    };
    addAndMakeVisible(*colourSwatch_);

    // Mute button (TCP style)
    muteButton_.setButtonText("M");
    muteButton_.setLookAndFeel(&magda::daw::ui::SmallButtonLookAndFeel::getInstance());
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
                magda::UndoManager::getInstance().executeCommand(
                    std::make_unique<magda::SetMasterMuteCommand>(muteButton_.getToggleState()));
            else
                magda::UndoManager::getInstance().executeCommand(
                    std::make_unique<magda::SetTrackMuteCommand>(selectedTrackId_,
                                                                 muteButton_.getToggleState()));
        }
    };
    addAndMakeVisible(muteButton_);

    // Speaker icon button (used for master mute instead of "M" text)
    auto speakerOnIcon = juce::Drawable::createFromImageData(BinaryData::volume_up_svg,
                                                             BinaryData::volume_up_svgSize);
    auto speakerOffIcon = juce::Drawable::createFromImageData(BinaryData::volume_off_svg,
                                                              BinaryData::volume_off_svgSize);
    speakerButton_ =
        std::make_unique<juce::DrawableButton>("Speaker", juce::DrawableButton::ImageFitted);
    speakerButton_->setImages(speakerOnIcon.get(), nullptr, nullptr, nullptr, speakerOffIcon.get());
    speakerButton_->setClickingTogglesState(true);
    speakerButton_->setColour(juce::DrawableButton::backgroundColourId,
                              DarkTheme::getColour(DarkTheme::SURFACE));
    speakerButton_->setColour(juce::DrawableButton::backgroundOnColourId,
                              DarkTheme::getColour(DarkTheme::STATUS_ERROR).withAlpha(0.3f));
    speakerButton_->setEdgeIndent(2);
    speakerButton_->onClick = [this]() {
        magda::UndoManager::getInstance().executeCommand(
            std::make_unique<magda::SetMasterMuteCommand>(speakerButton_->getToggleState()));
    };
    addChildComponent(*speakerButton_);  // Hidden by default

    // Solo button (TCP style)
    soloButton_.setButtonText("S");
    soloButton_.setLookAndFeel(&magda::daw::ui::SmallButtonLookAndFeel::getInstance());
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
            magda::UndoManager::getInstance().executeCommand(
                std::make_unique<magda::SetTrackSoloCommand>(selectedTrackId_,
                                                             soloButton_.getToggleState()));
        }
    };
    addAndMakeVisible(soloButton_);

    // Record button (TCP style)
    recordButton_.setButtonText("R");
    recordButton_.setLookAndFeel(&magda::daw::ui::SmallButtonLookAndFeel::getInstance());
    recordButton_.setColour(juce::TextButton::buttonColourId,
                            DarkTheme::getColour(DarkTheme::SURFACE));
    recordButton_.setColour(juce::TextButton::buttonOnColourId,
                            DarkTheme::getColour(DarkTheme::STATUS_ERROR));  // Red when armed
    recordButton_.setColour(juce::TextButton::textColourOffId,
                            DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
    recordButton_.setColour(juce::TextButton::textColourOnId,
                            DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
    recordButton_.setClickingTogglesState(true);
    recordButton_.onClick = [this]() {
        DBG("TrackInspector::recordButton clicked - trackId="
            << selectedTrackId_ << " toggleState=" << (int)recordButton_.getToggleState());
        if (selectedTrackId_ != magda::INVALID_TRACK_ID) {
            magda::TrackManager::getInstance().setTrackRecordArmed(selectedTrackId_,
                                                                   recordButton_.getToggleState());
        }
    };
    addAndMakeVisible(recordButton_);

    // Monitor button (3-state: Off → In → Auto → Off)
    monitorButton_.setButtonText("-");
    monitorButton_.setLookAndFeel(&magda::daw::ui::SmallButtonLookAndFeel::getInstance());
    monitorButton_.setColour(juce::TextButton::buttonColourId,
                             DarkTheme::getColour(DarkTheme::SURFACE));
    monitorButton_.setColour(juce::TextButton::buttonOnColourId,
                             DarkTheme::getColour(DarkTheme::ACCENT_GREEN));
    monitorButton_.setColour(juce::TextButton::textColourOffId,
                             DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
    monitorButton_.setColour(juce::TextButton::textColourOnId,
                             DarkTheme::getColour(DarkTheme::BACKGROUND));
    monitorButton_.setTooltip("Input monitoring (Off/In/Auto)");
    monitorButton_.onClick = [this]() {
        if (selectedTrackId_ == magda::INVALID_TRACK_ID ||
            selectedTrackId_ == magda::MASTER_TRACK_ID)
            return;
        auto* track = magda::TrackManager::getInstance().getTrack(selectedTrackId_);
        if (!track)
            return;
        magda::InputMonitorMode nextMode;
        switch (track->inputMonitor) {
            case magda::InputMonitorMode::Off:
                nextMode = magda::InputMonitorMode::In;
                break;
            case magda::InputMonitorMode::In:
                nextMode = magda::InputMonitorMode::Auto;
                break;
            case magda::InputMonitorMode::Auto:
                nextMode = magda::InputMonitorMode::Off;
                break;
        }
        magda::UndoManager::getInstance().executeCommand(
            std::make_unique<magda::SetTrackInputMonitorCommand>(selectedTrackId_, nextMode));
    };
    addAndMakeVisible(monitorButton_);

    // Gain label (TCP style - draggable dB display)
    gainLabel_ =
        std::make_unique<magda::DraggableValueLabel>(magda::DraggableValueLabel::Format::Decibels);
    gainLabel_->setRange(-60.0, 6.0, 0.0);  // -60 to +6 dB, default 0 dB
    gainLabel_->onValueChange = [this]() {
        if (selectedTrackId_ != magda::INVALID_TRACK_ID) {
            double db = gainLabel_->getValue();
            float gain = (db <= -60.0f) ? 0.0f : std::pow(10.0f, static_cast<float>(db) / 20.0f);
            if (selectedTrackId_ == magda::MASTER_TRACK_ID)
                magda::UndoManager::getInstance().executeCommand(
                    std::make_unique<magda::SetMasterVolumeCommand>(gain));
            else
                magda::UndoManager::getInstance().executeCommand(
                    std::make_unique<magda::SetTrackVolumeCommand>(selectedTrackId_, gain));
        }
    };
    addAndMakeVisible(*gainLabel_);

    // Pan label (TCP style - draggable L/C/R display)
    panLabel_ =
        std::make_unique<magda::DraggableValueLabel>(magda::DraggableValueLabel::Format::Pan);
    panLabel_->setRange(-1.0, 1.0, 0.0);  // -1 (L) to +1 (R), default center
    panLabel_->onValueChange = [this]() {
        if (selectedTrackId_ == magda::INVALID_TRACK_ID)
            return;
        float pan = static_cast<float>(panLabel_->getValue());
        if (panLabel_->isDragging()) {
            // Apply directly during drag (no undo command per pixel)
            if (selectedTrackId_ == magda::MASTER_TRACK_ID)
                magda::TrackManager::getInstance().setMasterPan(pan);
            else
                magda::TrackManager::getInstance().setTrackPan(selectedTrackId_, pan);
        } else {
            // Non-drag changes (keyboard edit, double-click reset)
            if (selectedTrackId_ == magda::MASTER_TRACK_ID)
                magda::UndoManager::getInstance().executeCommand(
                    std::make_unique<magda::SetMasterPanCommand>(pan));
            else
                magda::UndoManager::getInstance().executeCommand(
                    std::make_unique<magda::SetTrackPanCommand>(selectedTrackId_, pan));
        }
    };
    panLabel_->onDragEnd = [this](double startValue) {
        if (selectedTrackId_ == magda::INVALID_TRACK_ID)
            return;
        float oldPan = static_cast<float>(startValue);
        float newPan = static_cast<float>(panLabel_->getValue());
        if (selectedTrackId_ == magda::MASTER_TRACK_ID)
            magda::UndoManager::getInstance().executeCommand(
                std::make_unique<magda::SetMasterPanCommand>(oldPan, newPan));
        else
            magda::UndoManager::getInstance().executeCommand(
                std::make_unique<magda::SetTrackPanCommand>(selectedTrackId_, oldPan, newPan));
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

    // Column header labels for routing selectors
    audioColumnLabel_.setText("Audio", juce::dontSendNotification);
    audioColumnLabel_.setFont(FontManager::getInstance().getUIFont(9.0f));
    audioColumnLabel_.setColour(juce::Label::textColourId, DarkTheme::getSecondaryTextColour());
    audioColumnLabel_.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(audioColumnLabel_);

    midiColumnLabel_.setText("MIDI", juce::dontSendNotification);
    midiColumnLabel_.setFont(FontManager::getInstance().getUIFont(9.0f));
    midiColumnLabel_.setColour(juce::Label::textColourId, DarkTheme::getSecondaryTextColour());
    midiColumnLabel_.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(midiColumnLabel_);

    // I/O routing icons (non-interactive visual indicators)
    auto inputDrawable =
        std::make_unique<juce::DrawableButton>("inputIcon", juce::DrawableButton::ImageFitted);
    if (auto svg =
            juce::Drawable::createFromImageData(BinaryData::Input_svg, BinaryData::Input_svgSize)) {
        inputDrawable->setImages(svg.get());
    }
    inputDrawable->setInterceptsMouseClicks(false, false);
    inputIcon_ = std::move(inputDrawable);
    addAndMakeVisible(*inputIcon_);

    auto outputDrawable =
        std::make_unique<juce::DrawableButton>("outputIcon", juce::DrawableButton::ImageFitted);
    if (auto svg = juce::Drawable::createFromImageData(BinaryData::Output_svg,
                                                       BinaryData::Output_svgSize)) {
        outputDrawable->setImages(svg.get());
    }
    outputDrawable->setInterceptsMouseClicks(false, false);
    outputIcon_ = std::move(outputDrawable);
    addAndMakeVisible(*outputIcon_);

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

    // Latency display
    latencyLabel_.setText("Latency", juce::dontSendNotification);
    latencyLabel_.setFont(FontManager::getInstance().getUIFont(11.0f));
    latencyLabel_.setColour(juce::Label::textColourId, DarkTheme::getSecondaryTextColour());
    addAndMakeVisible(latencyLabel_);

    latencyValue_.setFont(FontManager::getInstance().getUIFont(12.0f));
    latencyValue_.setColour(juce::Label::textColourId, DarkTheme::getTextColour());
    addAndMakeVisible(latencyValue_);
}

void TrackInspector::midiDeviceListChanged() {
    juce::MessageManager::callAsync([this]() { populateMidiInputOptions(); });
}

TrackInspector::~TrackInspector() {
    if (audioEngine_) {
        if (auto* mb = audioEngine_->getMidiBridge())
            mb->removeMidiDeviceListListener(this);
    }
    stopTimer();
    magda::TrackManager::getInstance().removeListener(this);
}

void TrackInspector::timerCallback() {
    if (!audioEngine_)
        return;

    auto* midiBridge = audioEngine_->getMidiBridge();
    if (midiBridge) {
        size_t inputCount = midiBridge->getAvailableMidiInputs().size();
        size_t outputCount = midiBridge->getAvailableMidiOutputs().size();

        if (inputCount != lastMidiInputCount_ || outputCount != lastMidiOutputCount_) {
            lastMidiInputCount_ = inputCount;
            lastMidiOutputCount_ = outputCount;
            populateMidiInputOptions();
            populateMidiOutputOptions();
            if (selectedTrackId_ != magda::INVALID_TRACK_ID)
                updateRoutingSelectorsFromTrack();
        }
    }
}

void TrackInspector::onActivated() {
    magda::TrackManager::getInstance().addListener(this);
    populateRoutingSelectors();
    updateFromSelectedTrack();
    // Poll for MIDI device changes every 2 seconds (matching TrackHeadersPanel)
    startTimerHz(1);
}

void TrackInspector::onDeactivated() {
    stopTimer();
    magda::TrackManager::getInstance().removeListener(this);
}

void TrackInspector::paint(juce::Graphics& g) {
    g.fillAll(DarkTheme::getColour(DarkTheme::BACKGROUND));

    // Draw section separators
    g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
    auto area = getLocalBounds().reduced(8);
    for (int y : sectionSeparatorYs_) {
        g.drawHorizontalLine(y, static_cast<float>(area.getX()),
                             static_cast<float>(area.getRight()));
    }
}

void TrackInspector::resized() {
    auto bounds = getLocalBounds().reduced(8);
    sectionSeparatorYs_.clear();
    const int separatorPadding = 6;

    // Track properties layout (TCP style)
    trackNameLabel_.setBounds(bounds.removeFromTop(16));
    auto nameRow = bounds.removeFromTop(24);
    colourSwatch_->setBounds(nameRow.removeFromRight(24));
    nameRow.removeFromRight(4);
    trackNameValue_.setBounds(nameRow);
    bounds.removeFromTop(separatorPadding);
    sectionSeparatorYs_.push_back(bounds.getY());
    bounds.removeFromTop(separatorPadding);

    const int selectorGap = 4;

    const int availableWidth = bounds.getWidth();
    const int stackThreshold = 100;
    const int buttonGap = 2;
    const int controlRowHeight = 24;
    const int wideThreshold = 160;

    bool showPan = panLabel_->isVisible();
    bool showSpeaker = speakerButton_->isVisible();
    // Count visible buttons for layout
    int visibleButtons = 0;
    if (muteButton_.isVisible() || showSpeaker)
        visibleButtons++;
    if (soloButton_.isVisible())
        visibleButtons++;
    if (recordButton_.isVisible())
        visibleButtons++;
    if (monitorButton_.isVisible())
        visibleButtons++;

    // Helper lambda to lay out the button row
    auto layoutButtons = [&](juce::Rectangle<int>& row, int gap) {
        if (visibleButtons <= 0)
            return;
        if (showSpeaker) {
            // Speaker icon: fixed square size
            speakerButton_->setBounds(row.removeFromLeft(controlRowHeight));
        } else {
            const int btnWidth = (row.getWidth() - (visibleButtons - 1) * gap) / visibleButtons;
            muteButton_.setBounds(row.removeFromLeft(btnWidth));
            if (soloButton_.isVisible()) {
                row.removeFromLeft(gap);
                soloButton_.setBounds(row.removeFromLeft(btnWidth));
            }
            if (recordButton_.isVisible()) {
                row.removeFromLeft(gap);
                recordButton_.setBounds(row.removeFromLeft(btnWidth));
            }
            if (monitorButton_.isVisible()) {
                row.removeFromLeft(gap);
                monitorButton_.setBounds(row);
            }
        }
    };

    if (availableWidth >= wideThreshold) {
        // Wide: Vol [Pan] buttons — all on one row
        auto row = bounds.removeFromTop(controlRowHeight);
        const int gap = 2;
        if (showSpeaker) {
            // Master: volume takes most space, speaker icon is fixed size at end
            auto speakerArea = row.removeFromRight(36);
            row.removeFromRight(gap);
            gainLabel_->setBounds(row);
            speakerButton_->setBounds(speakerArea);
        } else {
            const int mixPortion = row.getWidth() * 60 / 100;
            if (showPan) {
                const int volWidth = (mixPortion - gap) * 80 / 100;
                gainLabel_->setBounds(row.removeFromLeft(volWidth));
                row.removeFromLeft(gap);
                panLabel_->setBounds(row.removeFromLeft(mixPortion - volWidth - gap));
            } else {
                gainLabel_->setBounds(row.removeFromLeft(mixPortion));
            }
            row.removeFromLeft(gap);
            layoutButtons(row, gap);
        }
    } else if (availableWidth >= stackThreshold) {
        // Medium: Vol [Pan] on one row, buttons on second row
        auto mixRow = bounds.removeFromTop(controlRowHeight);
        if (showSpeaker) {
            auto speakerArea = mixRow.removeFromRight(36);
            mixRow.removeFromRight(buttonGap);
            gainLabel_->setBounds(mixRow);
            speakerButton_->setBounds(speakerArea);
        } else {
            if (showPan) {
                const int mixGap = 4;
                const int volWidth = (mixRow.getWidth() - mixGap) * 80 / 100;
                gainLabel_->setBounds(mixRow.removeFromLeft(volWidth));
                mixRow.removeFromLeft(mixGap);
                panLabel_->setBounds(mixRow);
            } else {
                gainLabel_->setBounds(mixRow);
            }
            bounds.removeFromTop(4);

            auto buttonRow = bounds.removeFromTop(controlRowHeight);
            layoutButtons(buttonRow, buttonGap);
        }
    } else {
        // Narrow: Volume, [Pan], and buttons all stacked
        auto volRow = bounds.removeFromTop(controlRowHeight);
        if (showSpeaker) {
            auto speakerArea = volRow.removeFromRight(36);
            volRow.removeFromRight(buttonGap);
            gainLabel_->setBounds(volRow);
            speakerButton_->setBounds(speakerArea);
        } else {
            gainLabel_->setBounds(volRow);
            if (showPan) {
                bounds.removeFromTop(2);
                panLabel_->setBounds(bounds.removeFromTop(controlRowHeight));
            }
            bounds.removeFromTop(4);

            auto buttonRow = bounds.removeFromTop(controlRowHeight);
            layoutButtons(buttonRow, buttonGap);
        }
    }
    bounds.removeFromTop(separatorPadding);
    sectionSeparatorYs_.push_back(bounds.getY());
    bounds.removeFromTop(separatorPadding);

    // Routing section — only lay out if visible
    if (outputSelector_->isVisible()) {
        const int selectorHeight = 18;
        const int columnHeaderHeight = 14;
        const int iconSize = 16;
        const int dropdownGap = selectorGap;
        const int dropdownWidth = (bounds.getWidth() - dropdownGap - dropdownGap - iconSize) / 2;

        // Column headers: [Audio] [MIDI]
        if (audioInputSelector_->isVisible()) {
            auto headerRow = bounds.removeFromTop(columnHeaderHeight);
            audioColumnLabel_.setBounds(headerRow.removeFromLeft(dropdownWidth));
            headerRow.removeFromLeft(dropdownGap);
            midiColumnLabel_.setBounds(headerRow.removeFromLeft(dropdownWidth));
            bounds.removeFromTop(2);
        }

        // Input row: [Audio In] [MIDI In] [inputIcon] — hidden for multi-out child tracks
        if (audioInputSelector_->isVisible()) {
            auto inputRow = bounds.removeFromTop(selectorHeight);
            audioInputSelector_->setBounds(inputRow.removeFromLeft(dropdownWidth));
            inputRow.removeFromLeft(dropdownGap);
            inputSelector_->setBounds(inputRow.removeFromLeft(dropdownWidth));
            inputRow.removeFromLeft(dropdownGap);
            inputIcon_->setBounds(inputRow.removeFromLeft(iconSize));
            bounds.removeFromTop(4);
        }

        // Output row: [Audio Out] [MIDI Out] [outputIcon]
        auto outputRow = bounds.removeFromTop(selectorHeight);
        outputSelector_->setBounds(outputRow.removeFromLeft(dropdownWidth));
        outputRow.removeFromLeft(dropdownGap);
        midiOutputSelector_->setBounds(outputRow.removeFromLeft(dropdownWidth));
        outputRow.removeFromLeft(dropdownGap);
        outputIcon_->setBounds(outputRow.removeFromLeft(iconSize));
        bounds.removeFromTop(separatorPadding);
        sectionSeparatorYs_.push_back(bounds.getY());
        bounds.removeFromTop(separatorPadding);
    }

    // Send/Receive section — only lay out if visible
    if (sendReceiveSectionLabel_.isVisible()) {
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
        bounds.removeFromTop(separatorPadding);
        sectionSeparatorYs_.push_back(bounds.getY());
        bounds.removeFromTop(separatorPadding);
    }

    // Clips section — only lay out if visible
    if (clipsSectionLabel_.isVisible()) {
        clipsSectionLabel_.setBounds(bounds.removeFromTop(16));
        bounds.removeFromTop(4);
        clipCountLabel_.setBounds(bounds.removeFromTop(20));
    }

    // Latency — only lay out if visible
    if (latencyLabel_.isVisible()) {
        bounds.removeFromTop(separatorPadding);
        sectionSeparatorYs_.push_back(bounds.getY());
        bounds.removeFromTop(separatorPadding);
        latencyLabel_.setBounds(bounds.removeFromTop(16));
        bounds.removeFromTop(4);
        latencyValue_.setBounds(bounds.removeFromTop(20));
    }
}

void TrackInspector::setSelectedTrack(magda::TrackId trackId) {
    bool wasMulti = isMultiTrackMode_;
    isMultiTrackMode_ = false;
    selectedTrackIds_.clear();
    selectedTrackId_ = trackId;

    // Bind automation targets so the inspector gain/pan mirror the track
    // header's purple/grey state automatically via the observer. Skip the
    // master track (no automation lanes for master volume/pan).
    if (trackId != magda::INVALID_TRACK_ID && trackId != magda::MASTER_TRACK_ID) {
        magda::AutomationTarget volTarget;
        volTarget.type = magda::AutomationTargetType::TrackVolume;
        volTarget.trackId = trackId;
        gainLabel_->setAutomationTarget(volTarget);
        magda::AutomationTarget panTarget;
        panTarget.type = magda::AutomationTargetType::TrackPan;
        panTarget.trackId = trackId;
        panLabel_->setAutomationTarget(panTarget);
    } else {
        gainLabel_->clearAutomationTarget();
        panLabel_->clearAutomationTarget();
    }

    // Restore single-track callbacks if switching from multi-track mode
    if (wasMulti) {
        muteButton_.onClick = [this]() {
            if (selectedTrackId_ != magda::INVALID_TRACK_ID) {
                if (selectedTrackId_ == magda::MASTER_TRACK_ID)
                    magda::UndoManager::getInstance().executeCommand(
                        std::make_unique<magda::SetMasterMuteCommand>(
                            muteButton_.getToggleState()));
                else
                    magda::UndoManager::getInstance().executeCommand(
                        std::make_unique<magda::SetTrackMuteCommand>(selectedTrackId_,
                                                                     muteButton_.getToggleState()));
            }
        };
        soloButton_.onClick = [this]() {
            if (selectedTrackId_ != magda::INVALID_TRACK_ID) {
                magda::UndoManager::getInstance().executeCommand(
                    std::make_unique<magda::SetTrackSoloCommand>(selectedTrackId_,
                                                                 soloButton_.getToggleState()));
            }
        };
        trackNameValue_.setEditable(true);

        gainLabel_->clearTextOverride();
        gainLabel_->onValueChange = [this]() {
            if (selectedTrackId_ != magda::INVALID_TRACK_ID) {
                double db = gainLabel_->getValue();
                float gain =
                    (db <= -60.0f) ? 0.0f : std::pow(10.0f, static_cast<float>(db) / 20.0f);
                if (selectedTrackId_ == magda::MASTER_TRACK_ID)
                    magda::UndoManager::getInstance().executeCommand(
                        std::make_unique<magda::SetMasterVolumeCommand>(gain));
                else
                    magda::UndoManager::getInstance().executeCommand(
                        std::make_unique<magda::SetTrackVolumeCommand>(selectedTrackId_, gain));
            }
        };

        panLabel_->clearTextOverride();
        panLabel_->onValueChange = [this]() {
            if (selectedTrackId_ == magda::INVALID_TRACK_ID)
                return;
            float pan = static_cast<float>(panLabel_->getValue());
            if (panLabel_->isDragging()) {
                if (selectedTrackId_ == magda::MASTER_TRACK_ID)
                    magda::TrackManager::getInstance().setMasterPan(pan);
                else
                    magda::TrackManager::getInstance().setTrackPan(selectedTrackId_, pan);
            } else {
                if (selectedTrackId_ == magda::MASTER_TRACK_ID)
                    magda::UndoManager::getInstance().executeCommand(
                        std::make_unique<magda::SetMasterPanCommand>(pan));
                else
                    magda::UndoManager::getInstance().executeCommand(
                        std::make_unique<magda::SetTrackPanCommand>(selectedTrackId_, pan));
            }
        };
        panLabel_->onDragEnd = [this](double startValue) {
            if (selectedTrackId_ == magda::INVALID_TRACK_ID)
                return;
            float oldPan = static_cast<float>(startValue);
            float newPan = static_cast<float>(panLabel_->getValue());
            if (selectedTrackId_ == magda::MASTER_TRACK_ID)
                magda::UndoManager::getInstance().executeCommand(
                    std::make_unique<magda::SetMasterPanCommand>(oldPan, newPan));
            else
                magda::UndoManager::getInstance().executeCommand(
                    std::make_unique<magda::SetTrackPanCommand>(selectedTrackId_, oldPan, newPan));
        };

        recordButton_.onClick = [this]() {
            if (selectedTrackId_ != magda::INVALID_TRACK_ID) {
                magda::TrackManager::getInstance().setTrackRecordArmed(
                    selectedTrackId_, recordButton_.getToggleState());
            }
        };
        monitorButton_.onClick = [this]() {
            if (selectedTrackId_ == magda::INVALID_TRACK_ID ||
                selectedTrackId_ == magda::MASTER_TRACK_ID)
                return;
            auto* track = magda::TrackManager::getInstance().getTrack(selectedTrackId_);
            if (!track)
                return;
            magda::InputMonitorMode nextMode;
            switch (track->inputMonitor) {
                case magda::InputMonitorMode::Off:
                    nextMode = magda::InputMonitorMode::In;
                    break;
                case magda::InputMonitorMode::In:
                    nextMode = magda::InputMonitorMode::Auto;
                    break;
                case magda::InputMonitorMode::Auto:
                    nextMode = magda::InputMonitorMode::Off;
                    break;
            }
            magda::UndoManager::getInstance().executeCommand(
                std::make_unique<magda::SetTrackInputMonitorCommand>(selectedTrackId_, nextMode));
        };
    }

    updateFromSelectedTrack();
}

void TrackInspector::setSelectedTracks(const std::unordered_set<magda::TrackId>& trackIds) {
    isMultiTrackMode_ = true;
    selectedTrackIds_ = trackIds;
    selectedTrackId_ = magda::INVALID_TRACK_ID;
    // Multi-track selection has no single target to mirror, so clear any
    // binding left from single-track mode.
    gainLabel_->clearAutomationTarget();
    panLabel_->clearAutomationTarget();
    updateFromMultiTrackSelection();
}

// ============================================================================
// TrackManagerListener Interface
// ============================================================================

void TrackInspector::tracksChanged() {
    updateFromSelectedTrack();
}

void TrackInspector::trackPropertyChanged(int trackId) {
    if (isMultiTrackMode_) {
        // Don't refresh during an active drag — it would reset text overrides and base values
        if (gainLabel_->isDragging() || panLabel_->isDragging())
            return;
        if (selectedTrackIds_.count(static_cast<magda::TrackId>(trackId)) > 0) {
            updateFromMultiTrackSelection();
        }
        return;
    }
    if (static_cast<magda::TrackId>(trackId) == selectedTrackId_) {
        if (gainLabel_->isDragging() || panLabel_->isDragging())
            return;
        updateFromSelectedTrack();
    }
}

void TrackInspector::trackDevicesChanged(magda::TrackId trackId) {
    if (trackId == selectedTrackId_) {
        rebuildSendsUI();

        // Refresh latency (devices added/removed/loaded)
        if (latencyLabel_.isVisible()) {
            double latency =
                magda::TrackManager::getInstance().getTrackLatencySeconds(selectedTrackId_);
            auto latencyMs = latency * 1000.0;
            latencyValue_.setText((latency > 0.0) ? juce::String(latencyMs, 1) + " ms" : "0 ms",
                                  juce::dontSendNotification);
            latencyValue_.repaint();
        }
    }
}

void TrackInspector::trackSelectionChanged(magda::TrackId trackId) {
    // Not used - selection is managed externally
    (void)trackId;
}

void TrackInspector::masterChannelChanged() {
    if (selectedTrackId_ == magda::MASTER_TRACK_ID) {
        if (gainLabel_->isDragging())
            return;
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
        speakerButton_->setToggleState(master.muted, juce::dontSendNotification);
        soloButton_.setToggleState(false, juce::dontSendNotification);
        recordButton_.setToggleState(false, juce::dontSendNotification);

        float gainDb = (master.volume <= 0.0f) ? -60.0f : 20.0f * std::log10(master.volume);
        gainLabel_->setValue(gainDb, juce::dontSendNotification);

        clipCountLabel_.setText("0 clips", juce::dontSendNotification);

        showTrackControls(true);
        resized();
        repaint();
        return;
    }

    const auto* track = magda::TrackManager::getInstance().getTrack(selectedTrackId_);
    if (track) {
        // Update colour swatch
        auto* swatch = static_cast<magda::ColourSwatch*>(colourSwatch_.get());
        if (track->colour == juce::Colour(0xFF444444))
            swatch->clearColour();
        else
            swatch->setColour(track->colour);

        trackNameValue_.setText(track->name, juce::dontSendNotification);
        muteButton_.setToggleState(track->muted, juce::dontSendNotification);
        soloButton_.setToggleState(track->soloed, juce::dontSendNotification);
        recordButton_.setToggleState(track->recordArmed, juce::dontSendNotification);

        // Update monitor button
        switch (track->inputMonitor) {
            case magda::InputMonitorMode::Off:
                monitorButton_.setButtonText("-");
                break;
            case magda::InputMonitorMode::In:
                monitorButton_.setButtonText("I");
                break;
            case magda::InputMonitorMode::Auto:
                monitorButton_.setButtonText("A");
                break;
        }
        monitorButton_.setToggleState(track->inputMonitor != magda::InputMonitorMode::Off,
                                      juce::dontSendNotification);

        // Convert linear gain to dB for display
        float gainDb = (track->volume <= 0.0f) ? -60.0f : 20.0f * std::log10(track->volume);
        gainLabel_->setValue(gainDb, juce::dontSendNotification);
        panLabel_->setValue(track->pan, juce::dontSendNotification);

        // Update clip count
        auto clips = magda::ClipManager::getInstance().getClipsOnTrack(selectedTrackId_);
        int clipCount = static_cast<int>(clips.size());
        juce::String clipText = juce::String(clipCount) + (clipCount == 1 ? " clip" : " clips");
        clipCountLabel_.setText(clipText, juce::dontSendNotification);

        // Update track latency
        double latency =
            magda::TrackManager::getInstance().getTrackLatencySeconds(selectedTrackId_);
        if (latency > 0.0) {
            auto latencyMs = latency * 1000.0;
            latencyValue_.setText(juce::String(latencyMs, 1) + " ms", juce::dontSendNotification);
        } else {
            latencyValue_.setText("0 ms", juce::dontSendNotification);
        }

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

void TrackInspector::updateFromMultiTrackSelection() {
    if (selectedTrackIds_.empty()) {
        showTrackControls(false);
        resized();
        repaint();
        return;
    }

    auto& tm = magda::TrackManager::getInstance();

    // Header: "N tracks selected"
    int count = static_cast<int>(selectedTrackIds_.size());
    trackNameLabel_.setText("Selection", juce::dontSendNotification);
    trackNameValue_.setText(juce::String(count) + " tracks selected", juce::dontSendNotification);
    trackNameValue_.setEditable(false);

    // Check button states: "on" only if ALL selected tracks share that state
    bool allMuted = true;
    bool allSoloed = true;
    bool allRecordArmed = true;
    bool allMonitorOn = true;
    for (auto tid : selectedTrackIds_) {
        const auto* track = tm.getTrack(tid);
        if (!track)
            continue;
        if (!track->muted)
            allMuted = false;
        if (!track->soloed)
            allSoloed = false;
        if (!track->recordArmed)
            allRecordArmed = false;
        if (track->inputMonitor == magda::InputMonitorMode::Off)
            allMonitorOn = false;
    }

    muteButton_.setToggleState(allMuted, juce::dontSendNotification);
    soloButton_.setToggleState(allSoloed, juce::dontSendNotification);
    recordButton_.setToggleState(allRecordArmed, juce::dontSendNotification);
    monitorButton_.setToggleState(allMonitorOn, juce::dontSendNotification);

    // Rewire button callbacks for multi-track mode
    muteButton_.onClick = [this]() {
        bool newState = muteButton_.getToggleState();
        for (auto tid : selectedTrackIds_) {
            magda::UndoManager::getInstance().executeCommand(
                std::make_unique<magda::SetTrackMuteCommand>(tid, newState));
        }
    };
    soloButton_.onClick = [this]() {
        bool newState = soloButton_.getToggleState();
        for (auto tid : selectedTrackIds_) {
            magda::UndoManager::getInstance().executeCommand(
                std::make_unique<magda::SetTrackSoloCommand>(tid, newState));
        }
    };
    recordButton_.onClick = [this]() {
        bool newState = recordButton_.getToggleState();
        for (auto tid : selectedTrackIds_) {
            magda::TrackManager::getInstance().setTrackRecordArmed(tid, newState);
        }
    };
    monitorButton_.onClick = [this]() {
        // Cycle all selected tracks to the same next mode based on current button state
        auto nextMode = monitorButton_.getToggleState() ? magda::InputMonitorMode::In
                                                        : magda::InputMonitorMode::Off;
        for (auto tid : selectedTrackIds_) {
            magda::UndoManager::getInstance().executeCommand(
                std::make_unique<magda::SetTrackInputMonitorCommand>(tid, nextMode));
        }
    };

    // Volume/Pan: check if all values are the same or mixed
    float firstVolDb = 0.0f;
    float firstPan = 0.0f;
    bool volumeMixed = false;
    bool panMixed = false;
    bool first = true;
    for (auto tid : selectedTrackIds_) {
        const auto* track = tm.getTrack(tid);
        if (!track)
            continue;
        float volDb = (track->volume <= 0.0f) ? -60.0f : 20.0f * std::log10(track->volume);
        if (first) {
            firstVolDb = volDb;
            firstPan = track->pan;
            first = false;
        } else {
            if (std::abs(volDb - firstVolDb) > 0.01f)
                volumeMixed = true;
            if (std::abs(track->pan - firstPan) > 0.01f)
                panMixed = true;
        }
    }

    if (volumeMixed) {
        gainLabel_->setTextOverride("mixed");
    } else {
        gainLabel_->clearTextOverride();
        gainLabel_->setValue(firstVolDb, juce::dontSendNotification);
    }

    if (panMixed) {
        panLabel_->setTextOverride("mixed");
    } else {
        panLabel_->clearTextOverride();
        panLabel_->setValue(firstPan, juce::dontSendNotification);
    }

    // Wire up volume/pan for relative multi-track adjustment
    // Capture base values when drag starts, then apply delta to all tracks
    gainLabel_->onValueChange = [this]() {
        // On first call of a new drag, capture base values
        if (multiTrackBaseVolumes_.empty()) {
            auto& tmInner = magda::TrackManager::getInstance();
            for (auto tid : selectedTrackIds_) {
                const auto* track = tmInner.getTrack(tid);
                if (track)
                    multiTrackBaseVolumes_[tid] = track->volume;
            }
            multiTrackDragStartDb_ = gainLabel_->getValue();
        }
        double delta = gainLabel_->getValue() - multiTrackDragStartDb_;
        for (auto& [tid, baseVol] : multiTrackBaseVolumes_) {
            float baseDb = (baseVol <= 0.0f) ? -60.0f : 20.0f * std::log10(baseVol);
            float newDb = juce::jlimit(-60.0f, 6.0f, static_cast<float>(baseDb + delta));
            float newGain = (newDb <= -60.0f) ? 0.0f : std::pow(10.0f, newDb / 20.0f);
            magda::UndoManager::getInstance().executeCommand(
                std::make_unique<magda::SetTrackVolumeCommand>(tid, newGain));
        }
        gainLabel_->clearTextOverride();
        // Clear base values when drag ends so next drag re-captures
        if (!gainLabel_->isDragging())
            multiTrackBaseVolumes_.clear();
    };

    panLabel_->onValueChange = [this]() {
        if (multiTrackBasePans_.empty()) {
            auto& tmInner = magda::TrackManager::getInstance();
            for (auto tid : selectedTrackIds_) {
                const auto* track = tmInner.getTrack(tid);
                if (track)
                    multiTrackBasePans_[tid] = track->pan;
            }
            multiTrackDragStartPan_ = panLabel_->getValue();
        }
        double delta = panLabel_->getValue() - multiTrackDragStartPan_;
        for (auto& [tid, basePan] : multiTrackBasePans_) {
            float newPan = juce::jlimit(-1.0f, 1.0f, static_cast<float>(basePan + delta));
            magda::TrackManager::getInstance().setTrackPan(tid, newPan);
        }
        panLabel_->clearTextOverride();
        if (!panLabel_->isDragging())
            multiTrackBasePans_.clear();
    };
    panLabel_->onDragEnd = [this](double /*startValue*/) {
        // Create undo commands for all tracks using pre-drag base values
        for (auto& [tid, basePan] : multiTrackBasePans_) {
            auto* track = magda::TrackManager::getInstance().getTrack(tid);
            if (track)
                magda::UndoManager::getInstance().executeCommand(
                    std::make_unique<magda::SetTrackPanCommand>(tid, basePan, track->pan));
        }
        multiTrackBasePans_.clear();
    };

    // Show name + buttons + volume/pan; hide routing/sends/clips
    trackNameLabel_.setVisible(true);
    trackNameValue_.setVisible(true);
    colourSwatch_->setVisible(true);
    muteButton_.setVisible(true);
    speakerButton_->setVisible(false);
    soloButton_.setVisible(true);
    recordButton_.setVisible(true);
    monitorButton_.setVisible(true);
    gainLabel_->setVisible(true);
    panLabel_->setVisible(true);

    routingSectionLabel_.setVisible(false);
    audioInputSelector_->setVisible(false);
    inputSelector_->setVisible(false);
    audioColumnLabel_.setVisible(false);
    midiColumnLabel_.setVisible(false);
    inputIcon_->setVisible(false);
    outputIcon_->setVisible(false);
    outputSelector_->setVisible(false);
    midiOutputSelector_->setVisible(false);

    sendReceiveSectionLabel_.setVisible(false);
    addSendButton_.setVisible(false);
    noSendsLabel_.setVisible(false);
    receivesLabel_.setVisible(false);
    for (auto& l : sendDestLabels_)
        l->setVisible(false);
    for (auto& l : sendLevelLabels_)
        l->setVisible(false);
    for (auto& b : sendDeleteButtons_)
        b->setVisible(false);

    clipsSectionLabel_.setVisible(false);
    clipCountLabel_.setVisible(false);
    latencyLabel_.setVisible(false);
    latencyValue_.setVisible(false);

    resized();
    repaint();
}

void TrackInspector::showTrackControls(bool show) {
    bool isMaster = show && selectedTrackId_ == magda::MASTER_TRACK_ID;
    bool isAux = false;
    bool isMultiOut = false;
    if (show && selectedTrackId_ != magda::INVALID_TRACK_ID &&
        selectedTrackId_ != magda::MASTER_TRACK_ID) {
        const auto* track = magda::TrackManager::getInstance().getTrack(selectedTrackId_);
        if (track && track->type == magda::TrackType::Aux)
            isAux = true;
        if (track && track->type == magda::TrackType::MultiOut)
            isMultiOut = true;
    }

    trackNameLabel_.setVisible(show);
    trackNameValue_.setVisible(show);
    colourSwatch_->setVisible(show && !isMaster);
    muteButton_.setVisible(show && !isMaster);
    speakerButton_->setVisible(isMaster);
    soloButton_.setVisible(show && !isMaster);
    recordButton_.setVisible(show && !isMaster && !isAux && !isMultiOut);
    monitorButton_.setVisible(show && !isMaster && !isAux && !isMultiOut);
    gainLabel_->setVisible(show);
    panLabel_->setVisible(show && !isMaster);

    // Routing section — hidden for master and aux; input selectors hidden for multi-out
    bool showRouting = show && !isMaster && !isAux;
    routingSectionLabel_.setVisible(false);
    audioInputSelector_->setVisible(showRouting && !isMultiOut);
    inputSelector_->setVisible(showRouting && !isMultiOut);
    audioColumnLabel_.setVisible(showRouting && !isMultiOut);
    midiColumnLabel_.setVisible(showRouting && !isMultiOut);
    inputIcon_->setVisible(showRouting && !isMultiOut);
    outputSelector_->setVisible(showRouting);
    midiOutputSelector_->setVisible(showRouting && !isMultiOut);
    outputIcon_->setVisible(showRouting);

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

    // Latency — shown for all tracks (including master)
    latencyLabel_.setVisible(show);
    latencyValue_.setVisible(show);
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
            magda::UndoManager::getInstance().executeCommand(
                std::make_unique<magda::SetSendLevelCommand>(srcId, busIndex, gain));
        };
        addAndMakeVisible(*levelLabel);
        sendLevelLabels_.push_back(std::move(levelLabel));

        // Delete button
        auto deleteBtn = std::make_unique<juce::TextButton>("x");
        deleteBtn->setConnectedEdges(juce::Button::ConnectedOnLeft |
                                     juce::Button::ConnectedOnRight | juce::Button::ConnectedOnTop |
                                     juce::Button::ConnectedOnBottom);
        deleteBtn->setColour(juce::TextButton::buttonColourId,
                             DarkTheme::getColour(DarkTheme::BUTTON_NORMAL));
        deleteBtn->setColour(juce::TextButton::textColourOffId,
                             DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
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
    auto& trackManager = magda::TrackManager::getInstance();
    const auto& allTracks = trackManager.getTracks();

    // Collect descendants to prevent routing cycles
    std::vector<magda::TrackId> descendants;
    if (selectedTrackId_ != magda::INVALID_TRACK_ID) {
        descendants = trackManager.getAllDescendants(selectedTrackId_);
    }

    int itemId = 1;
    std::vector<magda::TrackId> destTrackIds;

    auto addTracksOfType = [&](magda::TrackType type) {
        bool addedSeparator = false;
        for (const auto& track : allTracks) {
            if (track.type != type)
                continue;
            if (track.id == selectedTrackId_)
                continue;
            if (track.type == magda::TrackType::Master)
                continue;
            if (std::find(descendants.begin(), descendants.end(), track.id) != descendants.end())
                continue;

            // Filter out tracks that already have a send from this track
            bool alreadyHasSend = false;
            for (const auto& send : currentTrack->sends) {
                if (send.destTrackId == track.id) {
                    alreadyHasSend = true;
                    break;
                }
            }
            if (alreadyHasSend)
                continue;

            if (!addedSeparator && itemId > 1) {
                menu.addSeparator();
                addedSeparator = true;
            }
            if (!addedSeparator) {
                addedSeparator = true;
            }

            menu.addItem(itemId, track.name);
            destTrackIds.push_back(track.id);
            ++itemId;
        }
    };

    addTracksOfType(magda::TrackType::Aux);
    addTracksOfType(magda::TrackType::Group);
    addTracksOfType(magda::TrackType::Audio);

    if (menu.getNumItems() == 0) {
        menu.addItem(-1, "(No available tracks)", false);
    }

    // Capture selectedTrackId_ by value to avoid stale reference if selection
    // changes while the async menu is open
    TrackId sourceTrackId = selectedTrackId_;
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&addSendButton_),
                       [sourceTrackId, destTrackIds](int result) {
                           if (result > 0 && result <= static_cast<int>(destTrackIds.size())) {
                               magda::TrackManager::getInstance().addSend(sourceTrackId,
                                                                          destTrackIds[result - 1]);
                           }
                       });
}

void TrackInspector::populateRoutingSelectors() {
    if (!audioEngine_)
        return;

    // Register for device list changes (QWERTY keyboard toggle, etc.)
    if (auto* mb = audioEngine_->getMidiBridge())
        mb->addMidiDeviceListListener(this);

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
            // Preserve existing track input if already set, otherwise default
            auto* trackInfo = magda::TrackManager::getInstance().getTrack(selectedTrackId_);
            if (trackInfo && trackInfo->audioInputDevice.startsWith("track:"))
                magda::TrackManager::getInstance().setTrackAudioInput(selectedTrackId_,
                                                                      trackInfo->audioInputDevice);
            else
                magda::TrackManager::getInstance().setTrackAudioInput(selectedTrackId_, "default");
        } else {
            magda::TrackManager::getInstance().setTrackAudioInput(selectedTrackId_, "");
        }
    };

    audioInputSelector_->onSelectionChanged = [this](int selectedId) {
        if (selectedTrackId_ == magda::INVALID_TRACK_ID)
            return;

        DBG("TrackInspector::audioInput onSelectionChanged - selectedId=" << selectedId);

        if (selectedId == 1) {
            magda::TrackManager::getInstance().setTrackAudioInput(selectedTrackId_, "");
        } else if (selectedId >= 200) {
            // Track-as-input (resampling)
            auto it = inputTrackMapping_.find(selectedId);
            if (it != inputTrackMapping_.end()) {
                magda::TrackManager::getInstance().setTrackAudioInput(
                    selectedTrackId_, "track:" + juce::String(it->second));
            }
        } else if (selectedId >= 10) {
            // Map to specific TE wave device name
            auto it = inputChannelMapping_.find(selectedId);
            if (it != inputChannelMapping_.end()) {
                // Copy the string — the map can be repopulated during setTrackAudioInput
                // (via notifyTrackPropertyChanged → updateRoutingSelectorsFromTrack)
                juce::String deviceName = it->second;
                DBG("  -> mapped to device: '" << deviceName << "'");
                magda::TrackManager::getInstance().setTrackAudioInput(selectedTrackId_, deviceName);
            } else {
                DBG("  -> no mapping found, using default");
                magda::TrackManager::getInstance().setTrackAudioInput(selectedTrackId_, "default");
            }
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
        } else if (selectedId == 2) {
            // None
            magda::TrackManager::getInstance().setTrackAudioOutput(selectedTrackId_, "");
        } else if (selectedId >= 200) {
            // Track destination (Group, Aux, Audio, Instrument)
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
        } else if (selectedId >= 200) {
            // Track destination
            auto it = midiOutputTrackMapping_.find(selectedId);
            if (it != midiOutputTrackMapping_.end()) {
                magda::TrackManager::getInstance().setTrackMidiOutput(
                    selectedTrackId_, "track:" + juce::String(it->second));
            }
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
    juce::BigInteger enabledInputChannels;
    std::map<int, juce::String> teInputDeviceNames;
    if (auto* bridge = audioEngine_->getAudioBridge()) {
        enabledInputChannels = bridge->getEnabledInputChannels();
        teInputDeviceNames = bridge->getInputDeviceNamesByChannel();
    }
    magda::RoutingSyncHelper::populateAudioInputOptions(
        audioInputSelector_.get(), deviceManager->getCurrentAudioDevice(), selectedTrackId_,
        &inputTrackMapping_, enabledInputChannels, &inputChannelMapping_, teInputDeviceNames);
}

void TrackInspector::populateAudioOutputOptions() {
    if (!outputSelector_ || !audioEngine_)
        return;
    auto* deviceManager = audioEngine_->getDeviceManager();
    if (!deviceManager)
        return;
    juce::BigInteger enabledOutputChannels;
    if (auto* bridge = audioEngine_->getAudioBridge())
        enabledOutputChannels = bridge->getEnabledOutputChannels();
    magda::RoutingSyncHelper::populateAudioOutputOptions(
        outputSelector_.get(), selectedTrackId_, deviceManager->getCurrentAudioDevice(),
        outputTrackMapping_, enabledOutputChannels);
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
    magda::RoutingSyncHelper::populateMidiOutputOptions(
        midiOutputSelector_.get(), audioEngine_->getMidiBridge(), midiOutputTrackMapping_);
}

void TrackInspector::updateRoutingSelectorsFromTrack() {
    if (selectedTrackId_ == magda::INVALID_TRACK_ID || !audioEngine_)
        return;

    const auto* track = magda::TrackManager::getInstance().getTrack(selectedTrackId_);
    if (!track)
        return;

    // Always re-populate audio input options so track-as-input entries are current
    populateAudioInputOptions();

    auto* deviceManager = audioEngine_->getDeviceManager();
    auto* device = deviceManager ? deviceManager->getCurrentAudioDevice() : nullptr;
    juce::BigInteger enabledIn, enabledOut;
    std::map<int, juce::String> teInputDeviceNames;
    if (auto* bridge = audioEngine_->getAudioBridge()) {
        enabledIn = bridge->getEnabledInputChannels();
        enabledOut = bridge->getEnabledOutputChannels();
        teInputDeviceNames = bridge->getInputDeviceNamesByChannel();
    }
    magda::RoutingSyncHelper::syncSelectorsFromTrack(
        *track, audioInputSelector_.get(), inputSelector_.get(), outputSelector_.get(),
        midiOutputSelector_.get(), audioEngine_->getMidiBridge(), device, selectedTrackId_,
        outputTrackMapping_, midiOutputTrackMapping_, &inputTrackMapping_, enabledIn, enabledOut,
        &inputChannelMapping_, teInputDeviceNames);
}

}  // namespace magda::daw::ui
