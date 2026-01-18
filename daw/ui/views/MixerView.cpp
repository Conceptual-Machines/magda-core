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
MixerView::ChannelStrip::ChannelStrip(int index, bool isMaster)
    : channelIndex(index), isMaster_(isMaster) {
    setupControls();
}

void MixerView::ChannelStrip::setupControls() {
    // Track label
    trackLabel = std::make_unique<juce::Label>();
    if (isMaster_) {
        trackLabel->setText("Master", juce::dontSendNotification);
    } else {
        trackLabel->setText(juce::String(channelIndex + 1) + " Track", juce::dontSendNotification);
    }
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
    muteButton->onClick = [this]() { DBG("Mute toggled on channel " << channelIndex); };
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
    soloButton->onClick = [this]() { DBG("Solo toggled on channel " << channelIndex); };
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
        recordButton->onClick = [this]() { DBG("Record arm toggled on channel " << channelIndex); };
        addAndMakeVisible(*recordButton);
    }
}

void MixerView::ChannelStrip::paint(juce::Graphics& g) {
    auto bounds = getLocalBounds();

    // Background
    g.setColour(DarkTheme::getColour(DarkTheme::PANEL_BACKGROUND));
    g.fillRect(bounds);

    // Border on right side (separator)
    g.setColour(DarkTheme::getColour(DarkTheme::SEPARATOR));
    g.fillRect(bounds.getRight() - 1, 0, 1, bounds.getHeight());

    // Channel color indicator at top
    if (!isMaster_) {
        const std::array<juce::uint32, 8> trackColors = {
            0xFF5588AA, 0xFF55AA88, 0xFF88AA55, 0xFFAAAA55,
            0xFFAA8855, 0xFFAA5555, 0xFFAA55AA, 0xFF5555AA,
        };
        g.setColour(juce::Colour(trackColors[channelIndex % trackColors.size()]));
        g.fillRect(0, 0, getWidth() - 1, 4);
    } else {
        g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_BLUE));
        g.fillRect(0, 0, getWidth() - 1, 4);
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

// MixerView implementation
MixerView::MixerView() {
    // Create channel container
    channelContainer = std::make_unique<juce::Component>();

    // Create viewport for scrollable channels
    channelViewport = std::make_unique<juce::Viewport>();
    channelViewport->setViewedComponent(channelContainer.get(), false);
    channelViewport->setScrollBarsShown(false, true);  // Horizontal scroll only
    addAndMakeVisible(*channelViewport);

    // Create master strip (not in viewport)
    masterStrip = std::make_unique<ChannelStrip>(0, true);
    addAndMakeVisible(*masterStrip);

    setupChannels();

    // Start meter animation timer
    startTimerHz(30);
}

MixerView::~MixerView() {
    stopTimer();
}

void MixerView::setupChannels() {
    for (int i = 0; i < NUM_CHANNELS; ++i) {
        channelStrips[i] = std::make_unique<ChannelStrip>(i);
        channelContainer->addAndMakeVisible(*channelStrips[i]);
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
    int containerWidth = NUM_CHANNELS * CHANNEL_WIDTH;
    int containerHeight = bounds.getHeight();
    channelContainer->setSize(containerWidth, containerHeight);

    // Position channel strips
    for (int i = 0; i < NUM_CHANNELS; ++i) {
        channelStrips[i]->setBounds(i * CHANNEL_WIDTH, 0, CHANNEL_WIDTH, containerHeight);
    }
}

void MixerView::timerCallback() {
    simulateMeterLevels();
}

void MixerView::simulateMeterLevels() {
    // Simulate meter activity with random levels (for demo)
    auto& random = juce::Random::getSystemRandom();

    for (int i = 0; i < NUM_CHANNELS; ++i) {
        float currentLevel = channelStrips[i]->getMeterLevel();
        float targetLevel = random.nextFloat() * 0.7f + 0.1f;

        // Smooth attack, fast decay
        float newLevel;
        if (targetLevel > currentLevel) {
            newLevel = currentLevel + (targetLevel - currentLevel) * 0.3f;  // Fast attack
        } else {
            newLevel = currentLevel * 0.85f;  // Smooth decay
        }

        channelStrips[i]->setMeterLevel(newLevel);
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
