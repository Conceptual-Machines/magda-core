#include "MixerView.hpp"

#include "../themes/DarkTheme.hpp"

namespace magica {

// Level meter component
class MixerView::ChannelStrip::LevelMeter : public juce::Component {
  public:
    LevelMeter() = default;

    void setLevel(float newLevel) {
        level = juce::jlimit(0.0f, 1.0f, newLevel);
        repaint();
    }

    float getLevel() const {
        return level;
    }

    void paint(juce::Graphics& g) override {
        auto bounds = getLocalBounds().toFloat();

        // Background
        g.setColour(DarkTheme::getColour(DarkTheme::SURFACE));
        g.fillRoundedRectangle(bounds, 2.0f);

        // Meter fill
        float meterHeight = bounds.getHeight() * level;
        auto meterBounds = bounds.removeFromBottom(meterHeight);

        // Gradient from green to yellow to red
        if (level < 0.6f) {
            g.setColour(juce::Colour(0xFF55AA55));  // Green
        } else if (level < 0.85f) {
            g.setColour(juce::Colour(0xFFAAAA55));  // Yellow
        } else {
            g.setColour(juce::Colour(0xFFAA5555));  // Red
        }
        g.fillRoundedRectangle(meterBounds, 2.0f);

        // Segment lines
        g.setColour(DarkTheme::getColour(DarkTheme::BACKGROUND).withAlpha(0.5f));
        int numSegments = 20;
        float segmentHeight = bounds.getHeight() / numSegments;
        for (int i = 1; i < numSegments; ++i) {
            float y = getHeight() - i * segmentHeight;
            g.drawHorizontalLine(static_cast<int>(y), 0.0f, static_cast<float>(getWidth()));
        }
    }

  private:
    float level = 0.0f;
};

// Channel strip implementation
MixerView::ChannelStrip::ChannelStrip(const TrackInfo& track, bool isMaster)
    : trackId_(track.id), isMaster_(isMaster), trackColour_(track.colour), trackName_(track.name) {
    setupControls();
    updateFromTrack(track);
}

void MixerView::ChannelStrip::updateFromTrack(const TrackInfo& track) {
    trackColour_ = track.colour;
    trackName_ = track.name;

    if (trackLabel) {
        trackLabel->setText(isMaster_ ? "Master" : track.name, juce::dontSendNotification);
    }
    if (volumeFader) {
        volumeFader->setValue(track.volume, juce::dontSendNotification);
    }
    if (panKnob) {
        panKnob->setValue(track.pan, juce::dontSendNotification);
    }
    if (muteButton) {
        muteButton->setToggleState(track.muted, juce::dontSendNotification);
    }
    if (soloButton) {
        soloButton->setToggleState(track.soloed, juce::dontSendNotification);
    }
    if (recordButton) {
        recordButton->setToggleState(track.recordArmed, juce::dontSendNotification);
    }

    repaint();
}

void MixerView::ChannelStrip::setupControls() {
    // Track label
    trackLabel = std::make_unique<juce::Label>();
    trackLabel->setText(isMaster_ ? "Master" : trackName_, juce::dontSendNotification);
    trackLabel->setJustificationType(juce::Justification::centred);
    trackLabel->setColour(juce::Label::textColourId, DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
    trackLabel->setColour(juce::Label::backgroundColourId,
                          DarkTheme::getColour(DarkTheme::PANEL_BACKGROUND));
    addAndMakeVisible(*trackLabel);

    // Pan knob
    panKnob = std::make_unique<juce::Slider>(juce::Slider::RotaryHorizontalVerticalDrag,
                                             juce::Slider::NoTextBox);
    panKnob->setRange(-1.0, 1.0, 0.01);
    panKnob->setValue(0.0);
    panKnob->setColour(juce::Slider::rotarySliderFillColourId,
                       DarkTheme::getColour(DarkTheme::ACCENT_BLUE));
    panKnob->setColour(juce::Slider::rotarySliderOutlineColourId,
                       DarkTheme::getColour(DarkTheme::SURFACE));
    panKnob->setColour(juce::Slider::thumbColourId, DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
    panKnob->onValueChange = [this]() {
        TrackManager::getInstance().setTrackPan(trackId_, static_cast<float>(panKnob->getValue()));
    };
    addAndMakeVisible(*panKnob);

    // Level meter
    levelMeter = std::make_unique<LevelMeter>();
    addAndMakeVisible(*levelMeter);

    // Volume fader
    volumeFader =
        std::make_unique<juce::Slider>(juce::Slider::LinearVertical, juce::Slider::NoTextBox);
    volumeFader->setRange(0.0, 1.0, 0.01);
    volumeFader->setValue(0.75);
    volumeFader->setColour(juce::Slider::trackColourId, DarkTheme::getColour(DarkTheme::SURFACE));
    volumeFader->setColour(juce::Slider::backgroundColourId,
                           DarkTheme::getColour(DarkTheme::SURFACE));
    volumeFader->setColour(juce::Slider::thumbColourId,
                           DarkTheme::getColour(DarkTheme::ACCENT_BLUE));
    volumeFader->onValueChange = [this]() {
        TrackManager::getInstance().setTrackVolume(trackId_,
                                                   static_cast<float>(volumeFader->getValue()));
    };
    addAndMakeVisible(*volumeFader);

    // Mute button
    muteButton = std::make_unique<juce::TextButton>("M");
    muteButton->setColour(juce::TextButton::buttonColourId,
                          DarkTheme::getColour(DarkTheme::BUTTON_NORMAL));
    muteButton->setColour(juce::TextButton::buttonOnColourId,
                          juce::Colour(0xFFAA8855));  // Orange when active
    muteButton->setColour(juce::TextButton::textColourOffId,
                          DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
    muteButton->setColour(juce::TextButton::textColourOnId,
                          DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
    muteButton->setClickingTogglesState(true);
    muteButton->onClick = [this]() {
        TrackManager::getInstance().setTrackMuted(trackId_, muteButton->getToggleState());
    };
    addAndMakeVisible(*muteButton);

    // Solo button
    soloButton = std::make_unique<juce::TextButton>("S");
    soloButton->setColour(juce::TextButton::buttonColourId,
                          DarkTheme::getColour(DarkTheme::BUTTON_NORMAL));
    soloButton->setColour(juce::TextButton::buttonOnColourId,
                          juce::Colour(0xFFAAAA55));  // Yellow when active
    soloButton->setColour(juce::TextButton::textColourOffId,
                          DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
    soloButton->setColour(juce::TextButton::textColourOnId,
                          DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
    soloButton->setClickingTogglesState(true);
    soloButton->onClick = [this]() {
        TrackManager::getInstance().setTrackSoloed(trackId_, soloButton->getToggleState());
    };
    addAndMakeVisible(*soloButton);

    // Record arm button (not on master)
    if (!isMaster_) {
        recordButton = std::make_unique<juce::TextButton>("R");
        recordButton->setColour(juce::TextButton::buttonColourId,
                                DarkTheme::getColour(DarkTheme::BUTTON_NORMAL));
        recordButton->setColour(juce::TextButton::buttonOnColourId,
                                DarkTheme::getColour(DarkTheme::STATUS_ERROR));  // Red when armed
        recordButton->setColour(juce::TextButton::textColourOffId,
                                DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
        recordButton->setColour(juce::TextButton::textColourOnId,
                                DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
        recordButton->setClickingTogglesState(true);
        recordButton->onClick = [this]() {
            TrackManager::getInstance().setTrackRecordArmed(trackId_,
                                                            recordButton->getToggleState());
        };
        addAndMakeVisible(*recordButton);
    }
}

void MixerView::ChannelStrip::paint(juce::Graphics& g) {
    auto bounds = getLocalBounds();

    // Background - slightly brighter if selected
    if (selected) {
        g.setColour(DarkTheme::getColour(DarkTheme::SURFACE));
    } else {
        g.setColour(DarkTheme::getColour(DarkTheme::PANEL_BACKGROUND));
    }
    g.fillRect(bounds);

    // Selection border
    if (selected) {
        g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_BLUE));
        g.drawRect(bounds, 2);
    }

    // Border on right side (separator) - only if not selected
    if (!selected) {
        g.setColour(DarkTheme::getColour(DarkTheme::SEPARATOR));
        g.fillRect(bounds.getRight() - 1, 0, 1, bounds.getHeight());
    }

    // Channel color indicator at top
    if (!isMaster_) {
        g.setColour(trackColour_);
        g.fillRect(selected ? 2 : 0, selected ? 2 : 0, getWidth() - (selected ? 3 : 1), 4);
    } else {
        g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_BLUE));
        g.fillRect(selected ? 2 : 0, selected ? 2 : 0, getWidth() - (selected ? 3 : 1), 4);
    }
}

void MixerView::ChannelStrip::resized() {
    auto bounds = getLocalBounds().reduced(4);

    // Color indicator space
    bounds.removeFromTop(6);

    // Track label at top
    trackLabel->setBounds(bounds.removeFromTop(24));
    bounds.removeFromTop(4);

    // Pan knob
    auto panArea = bounds.removeFromTop(KNOB_SIZE);
    panKnob->setBounds(panArea.withSizeKeepingCentre(KNOB_SIZE, KNOB_SIZE));
    bounds.removeFromTop(4);

    // Buttons at bottom
    auto buttonArea = bounds.removeFromBottom(BUTTON_SIZE);
    int numButtons = isMaster_ ? 2 : 3;
    int buttonWidth = (buttonArea.getWidth() - (numButtons - 1) * 2) / numButtons;

    muteButton->setBounds(buttonArea.removeFromLeft(buttonWidth));
    buttonArea.removeFromLeft(2);
    soloButton->setBounds(buttonArea.removeFromLeft(buttonWidth));
    if (recordButton) {
        buttonArea.removeFromLeft(2);
        recordButton->setBounds(buttonArea.removeFromLeft(buttonWidth));
    }

    bounds.removeFromBottom(4);

    // Fader and meter in remaining space
    int faderWidth = 24;
    int meterWidth = METER_WIDTH;
    int totalWidth = faderWidth + 4 + meterWidth;
    int xOffset = (bounds.getWidth() - totalWidth) / 2;

    auto faderMeterArea = bounds;
    faderMeterArea.setX(bounds.getX() + xOffset);
    faderMeterArea.setWidth(totalWidth);

    // Meter on left
    levelMeter->setBounds(faderMeterArea.removeFromLeft(meterWidth));
    faderMeterArea.removeFromLeft(4);

    // Fader on right
    volumeFader->setBounds(faderMeterArea);
}

void MixerView::ChannelStrip::setMeterLevel(float level) {
    meterLevel = level;
    if (levelMeter) {
        levelMeter->setLevel(level);
    }
}

void MixerView::ChannelStrip::setSelected(bool shouldBeSelected) {
    if (selected != shouldBeSelected) {
        selected = shouldBeSelected;
        repaint();
    }
}

void MixerView::ChannelStrip::mouseDown(const juce::MouseEvent& /*event*/) {
    if (onClicked) {
        onClicked(trackId_, isMaster_);
    }
}

// MixerView implementation
MixerView::MixerView() {
    // Create channel container
    channelContainer = std::make_unique<juce::Component>();

    // Create viewport for scrollable channels
    channelViewport = std::make_unique<juce::Viewport>();
    channelViewport->setViewedComponent(channelContainer.get(), false);
    channelViewport->setScrollBarsShown(false, true);  // Horizontal scroll only
    addAndMakeVisible(*channelViewport);

    // Create master strip (using a dummy track info for master)
    TrackInfo masterTrack;
    masterTrack.id = -1;
    masterTrack.name = "Master";
    masterTrack.colour = DarkTheme::getColour(DarkTheme::ACCENT_BLUE);
    masterStrip = std::make_unique<ChannelStrip>(masterTrack, true);
    masterStrip->onClicked = [this](int /*id*/, bool isMaster) { selectChannel(-1, isMaster); };
    addAndMakeVisible(*masterStrip);

    // Register as TrackManager listener
    TrackManager::getInstance().addListener(this);

    // Build channel strips from TrackManager
    rebuildChannelStrips();

    // Start meter animation timer
    startTimerHz(30);
}

MixerView::~MixerView() {
    stopTimer();
    TrackManager::getInstance().removeListener(this);
}

void MixerView::rebuildChannelStrips() {
    // Clear existing strips
    channelStrips.clear();

    const auto& tracks = TrackManager::getInstance().getTracks();

    for (const auto& track : tracks) {
        auto strip = std::make_unique<ChannelStrip>(track, false);
        strip->onClicked = [this](int trackId, bool isMaster) {
            // Find the index of this track
            int index = TrackManager::getInstance().getTrackIndex(trackId);
            selectChannel(index, isMaster);
        };
        channelContainer->addAndMakeVisible(*strip);
        channelStrips.push_back(std::move(strip));
    }

    // Restore selection if valid
    if (selectedChannelIndex >= 0 &&
        selectedChannelIndex < static_cast<int>(channelStrips.size())) {
        channelStrips[selectedChannelIndex]->setSelected(true);
    } else if (!channelStrips.empty()) {
        selectedChannelIndex = 0;
        channelStrips[0]->setSelected(true);
    }

    resized();
}

void MixerView::tracksChanged() {
    // Rebuild all channel strips when tracks are added/removed/reordered
    rebuildChannelStrips();
}

void MixerView::trackPropertyChanged(int trackId) {
    // Update the specific channel strip
    const auto* track = TrackManager::getInstance().getTrack(trackId);
    if (!track)
        return;

    int index = TrackManager::getInstance().getTrackIndex(trackId);
    if (index >= 0 && index < static_cast<int>(channelStrips.size())) {
        channelStrips[index]->updateFromTrack(*track);
    }
}

void MixerView::paint(juce::Graphics& g) {
    g.fillAll(DarkTheme::getColour(DarkTheme::BACKGROUND));
}

void MixerView::resized() {
    auto bounds = getLocalBounds();

    // Master strip on the right
    masterStrip->setBounds(bounds.removeFromRight(MASTER_WIDTH));

    // Separator between channels and master
    bounds.removeFromRight(2);

    // Channel viewport takes remaining space
    channelViewport->setBounds(bounds);

    // Size the channel container
    int numChannels = static_cast<int>(channelStrips.size());
    int containerWidth = numChannels * CHANNEL_WIDTH;
    int containerHeight = bounds.getHeight();
    channelContainer->setSize(containerWidth, containerHeight);

    // Position channel strips
    for (int i = 0; i < numChannels; ++i) {
        channelStrips[i]->setBounds(i * CHANNEL_WIDTH, 0, CHANNEL_WIDTH, containerHeight);
    }
}

void MixerView::timerCallback() {
    simulateMeterLevels();
}

void MixerView::selectChannel(int index, bool isMaster) {
    // Deselect all channels
    for (auto& strip : channelStrips) {
        strip->setSelected(false);
    }
    masterStrip->setSelected(false);

    // Select the clicked channel
    if (isMaster) {
        masterStrip->setSelected(true);
        selectedChannelIndex = -1;
        selectedIsMaster = true;
    } else {
        if (index >= 0 && index < static_cast<int>(channelStrips.size())) {
            channelStrips[index]->setSelected(true);
        }
        selectedChannelIndex = index;
        selectedIsMaster = false;
    }

    // Notify listener
    if (onChannelSelected) {
        onChannelSelected(selectedChannelIndex, selectedIsMaster);
    }

    DBG("Selected channel: " << (isMaster ? "Master" : juce::String(index + 1)));
}

void MixerView::simulateMeterLevels() {
    // Simulate meter activity with random levels (for demo)
    auto& random = juce::Random::getSystemRandom();

    for (auto& strip : channelStrips) {
        float currentLevel = strip->getMeterLevel();
        float targetLevel = random.nextFloat() * 0.7f + 0.1f;

        // Smooth attack, fast decay
        float newLevel;
        if (targetLevel > currentLevel) {
            newLevel = currentLevel + (targetLevel - currentLevel) * 0.3f;  // Fast attack
        } else {
            newLevel = currentLevel * 0.85f;  // Smooth decay
        }

        strip->setMeterLevel(newLevel);
    }

    // Master level (slightly higher)
    float masterLevel = masterStrip->getMeterLevel();
    float targetMaster = random.nextFloat() * 0.8f + 0.15f;
    float newMaster = (targetMaster > masterLevel)
                          ? masterLevel + (targetMaster - masterLevel) * 0.3f
                          : masterLevel * 0.85f;
    masterStrip->setMeterLevel(newMaster);
}

}  // namespace magica
