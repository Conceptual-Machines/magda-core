#include "MasterChannelStrip.hpp"

#include <cmath>

#include "../../themes/DarkTheme.hpp"

namespace magica {

// dB conversion helpers
namespace {
constexpr float MIN_DB = -60.0f;
constexpr float MAX_DB = 6.0f;
constexpr float UNITY_DB = 0.0f;

float gainToDb(float gain) {
    if (gain <= 0.0f)
        return MIN_DB;
    return 20.0f * std::log10(gain);
}

float dbToGain(float db) {
    if (db <= MIN_DB)
        return 0.0f;
    return std::pow(10.0f, db / 20.0f);
}

float dbToFaderPos(float db) {
    if (db <= MIN_DB)
        return 0.0f;
    if (db >= MAX_DB)
        return 1.0f;

    if (db < UNITY_DB) {
        return 0.75f * (db - MIN_DB) / (UNITY_DB - MIN_DB);
    } else {
        return 0.75f + 0.25f * (db - UNITY_DB) / (MAX_DB - UNITY_DB);
    }
}

float faderPosToDb(float pos) {
    if (pos <= 0.0f)
        return MIN_DB;
    if (pos >= 1.0f)
        return MAX_DB;

    if (pos < 0.75f) {
        return MIN_DB + (pos / 0.75f) * (UNITY_DB - MIN_DB);
    } else {
        return UNITY_DB + ((pos - 0.75f) / 0.25f) * (MAX_DB - UNITY_DB);
    }
}
}  // namespace

// Level meter component
class MasterChannelStrip::LevelMeter : public juce::Component {
  public:
    void setLevel(float newLevel) {
        level = newLevel;
        repaint();
    }

    void paint(juce::Graphics& g) override {
        auto bounds = getLocalBounds().toFloat();

        // Background
        g.setColour(DarkTheme::getColour(DarkTheme::SURFACE));
        g.fillRoundedRectangle(bounds, 2.0f);

        // Meter level
        float meterHeight = bounds.getHeight() * level;
        auto meterBounds = bounds.removeFromBottom(meterHeight).reduced(1.0f, 1.0f);

        // Gradient from green to yellow to red
        if (level < 0.6f) {
            g.setColour(DarkTheme::getColour(DarkTheme::LEVEL_METER_GREEN));
        } else if (level < 0.85f) {
            g.setColour(DarkTheme::getColour(DarkTheme::LEVEL_METER_YELLOW));
        } else {
            g.setColour(DarkTheme::getColour(DarkTheme::LEVEL_METER_RED));
        }

        g.fillRoundedRectangle(meterBounds, 1.0f);
    }

  private:
    float level = 0.0f;
};

MasterChannelStrip::MasterChannelStrip(Orientation orientation) : orientation_(orientation) {
    setupControls();

    // Register as TrackManager listener
    TrackManager::getInstance().addListener(this);

    // Load initial state
    updateFromMasterState();
}

MasterChannelStrip::~MasterChannelStrip() {
    TrackManager::getInstance().removeListener(this);
    // Clear look and feel before destruction
    if (volumeSlider) {
        volumeSlider->setLookAndFeel(nullptr);
    }
}

void MasterChannelStrip::setupControls() {
    // Title label
    titleLabel = std::make_unique<juce::Label>("Master", "Master");
    titleLabel->setColour(juce::Label::textColourId, DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
    titleLabel->setJustificationType(juce::Justification::centred);
    addAndMakeVisible(*titleLabel);

    // Level meter
    levelMeter = std::make_unique<LevelMeter>();
    addAndMakeVisible(*levelMeter);

    // Peak label
    peakLabel = std::make_unique<juce::Label>();
    peakLabel->setText("-inf", juce::dontSendNotification);
    peakLabel->setJustificationType(juce::Justification::centred);
    peakLabel->setColour(juce::Label::textColourId,
                         DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
    peakLabel->setFont(juce::FontOptions(9.0f));
    addAndMakeVisible(*peakLabel);

    // Volume slider - using dB scale with unity at 0.75 position
    volumeSlider = std::make_unique<juce::Slider>(orientation_ == Orientation::Vertical
                                                      ? juce::Slider::LinearVertical
                                                      : juce::Slider::LinearHorizontal,
                                                  juce::Slider::NoTextBox);
    volumeSlider->setRange(0.0, 1.0, 0.001);
    volumeSlider->setValue(0.75);  // Unity gain (0 dB)
    volumeSlider->setSliderSnapsToMousePosition(false);
    volumeSlider->setColour(juce::Slider::trackColourId, DarkTheme::getColour(DarkTheme::SURFACE));
    volumeSlider->setColour(juce::Slider::backgroundColourId,
                            DarkTheme::getColour(DarkTheme::SURFACE));
    volumeSlider->setColour(juce::Slider::thumbColourId,
                            DarkTheme::getColour(DarkTheme::ACCENT_BLUE));
    volumeSlider->setLookAndFeel(&mixerLookAndFeel_);
    volumeSlider->onValueChange = [this]() {
        float faderPos = static_cast<float>(volumeSlider->getValue());
        float db = faderPosToDb(faderPos);
        float gain = dbToGain(db);
        TrackManager::getInstance().setMasterVolume(gain);
        // Update volume label
        if (volumeValueLabel) {
            juce::String dbText;
            if (db <= MIN_DB) {
                dbText = "-inf";
            } else {
                dbText = juce::String(db, 1) + " dB";
            }
            volumeValueLabel->setText(dbText, juce::dontSendNotification);
        }
    };
    addAndMakeVisible(*volumeSlider);

    // Volume value label
    volumeValueLabel = std::make_unique<juce::Label>();
    volumeValueLabel->setText("0.0 dB", juce::dontSendNotification);
    volumeValueLabel->setJustificationType(juce::Justification::centred);
    volumeValueLabel->setColour(juce::Label::textColourId,
                                DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
    volumeValueLabel->setFont(juce::FontOptions(9.0f));
    addAndMakeVisible(*volumeValueLabel);
}

void MasterChannelStrip::paint(juce::Graphics& g) {
    g.fillAll(DarkTheme::getColour(DarkTheme::PANEL_BACKGROUND));

    // Draw border
    g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
    g.drawRect(getLocalBounds(), 1);
}

void MasterChannelStrip::resized() {
    auto bounds = getLocalBounds().reduced(4);

    if (orientation_ == Orientation::Vertical) {
        // Vertical layout (for MixerView and SessionView)
        titleLabel->setBounds(bounds.removeFromTop(24));
        bounds.removeFromTop(4);

        // Value labels above fader area
        auto valueLabelArea = bounds.removeFromTop(12);
        volumeValueLabel->setBounds(valueLabelArea.removeFromLeft(valueLabelArea.getWidth() / 2));
        peakLabel->setBounds(valueLabelArea);

        // Fader and meter take remaining space
        auto faderMeterArea = bounds;
        levelMeter->setBounds(faderMeterArea.removeFromRight(12));
        faderMeterArea.removeFromRight(4);
        volumeSlider->setBounds(faderMeterArea);
    } else {
        // Horizontal layout (for Arrange view - at bottom of track content)
        titleLabel->setBounds(bounds.removeFromLeft(60));
        bounds.removeFromLeft(8);

        // Value label above meter
        auto labelArea = bounds.removeFromTop(12);
        volumeValueLabel->setBounds(labelArea.removeFromRight(40));
        peakLabel->setBounds(juce::Rectangle<int>());  // Hidden in horizontal

        levelMeter->setBounds(bounds.removeFromRight(12));
        bounds.removeFromRight(4);
        volumeSlider->setBounds(bounds);
    }
}

void MasterChannelStrip::masterChannelChanged() {
    updateFromMasterState();
}

void MasterChannelStrip::updateFromMasterState() {
    const auto& master = TrackManager::getInstance().getMasterChannel();

    // Convert linear gain to fader position
    float db = gainToDb(master.volume);
    float faderPos = dbToFaderPos(db);
    volumeSlider->setValue(faderPos, juce::dontSendNotification);

    // Update volume label
    if (volumeValueLabel) {
        juce::String dbText;
        if (db <= MIN_DB) {
            dbText = "-inf";
        } else {
            dbText = juce::String(db, 1) + " dB";
        }
        volumeValueLabel->setText(dbText, juce::dontSendNotification);
    }
}

void MasterChannelStrip::setMeterLevel(float level) {
    levelMeter->setLevel(level);

    // Update peak value
    if (level > peakValue_) {
        peakValue_ = level;
        if (peakLabel) {
            float db = gainToDb(peakValue_);
            juce::String peakText;
            if (db <= MIN_DB) {
                peakText = "-inf";
            } else {
                peakText = juce::String(db, 1);
            }
            peakLabel->setText(peakText, juce::dontSendNotification);
        }
    }
}

}  // namespace magica
