#include "MasterChannelStrip.hpp"

#include <cmath>

#include "../../themes/DarkTheme.hpp"
#include "../../themes/FontManager.hpp"
#include "../../themes/MixerMetrics.hpp"
#include "BinaryData.h"

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

// Stereo level meter component (L/R bars)
class MasterChannelStrip::LevelMeter : public juce::Component {
  public:
    void setLevel(float newLevel) {
        // Set both channels to the same level (for mono compatibility)
        setLevels(newLevel, newLevel);
    }

    void setLevels(float left, float right) {
        // Allow up to 2.0 gain (+6 dB)
        leftLevel_ = juce::jlimit(0.0f, 2.0f, left);
        rightLevel_ = juce::jlimit(0.0f, 2.0f, right);
        repaint();
    }

    float getLevel() const {
        return std::max(leftLevel_, rightLevel_);
    }

    void paint(juce::Graphics& g) override {
        auto bounds = getLocalBounds().toFloat();
        const auto& metrics = MixerMetrics::getInstance();

        // Meter uses effective range (with thumbRadius padding) to match fader track and labels
        auto effectiveBounds = bounds.reduced(0.0f, metrics.thumbRadius());

        // Split into L/R with 1px gap
        const float gap = 1.0f;
        float barWidth = (effectiveBounds.getWidth() - gap) / 2.0f;

        auto leftBounds = effectiveBounds.withWidth(barWidth);
        auto rightBounds =
            effectiveBounds.withWidth(barWidth).withX(effectiveBounds.getX() + barWidth + gap);

        // Draw left channel
        drawMeterBar(g, leftBounds, leftLevel_);

        // Draw right channel
        drawMeterBar(g, rightBounds, rightLevel_);
    }

  private:
    float leftLevel_ = 0.0f;
    float rightLevel_ = 0.0f;

    void drawMeterBar(juce::Graphics& g, juce::Rectangle<float> bounds, float level) {
        // Background
        g.setColour(DarkTheme::getColour(DarkTheme::SURFACE));
        g.fillRoundedRectangle(bounds, 1.0f);

        // Meter fill (using dB-scaled level for display)
        float displayLevel = dbToFaderPos(gainToDb(level));
        float meterHeight = bounds.getHeight() * displayLevel;
        auto fillBounds = bounds;
        fillBounds = fillBounds.removeFromBottom(meterHeight);

        // Smooth gradient from green to yellow to red based on dB
        g.setColour(getMeterColour(level));
        g.fillRoundedRectangle(fillBounds, 1.0f);
    }

    static juce::Colour getMeterColour(float level) {
        float dbLevel = gainToDb(level);
        juce::Colour green(0xFF55AA55);
        juce::Colour yellow(0xFFAAAA55);
        juce::Colour red(0xFFAA5555);

        if (dbLevel < -12.0f) {
            return green;
        } else if (dbLevel < 0.0f) {
            float t = (dbLevel + 12.0f) / 12.0f;
            return green.interpolatedWith(yellow, t);
        } else if (dbLevel < 3.0f) {
            float t = dbLevel / 3.0f;
            return yellow.interpolatedWith(red, t);
        } else {
            return red;
        }
    }
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
    peakLabel->setFont(FontManager::getInstance().getUIFont(9.0f));
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
        // DEBUG: Link fader to meter for alignment testing
        if (levelMeter) {
            levelMeter->setLevel(gain);
        }
    };
    addAndMakeVisible(*volumeSlider);

    // Volume value label
    volumeValueLabel = std::make_unique<juce::Label>();
    volumeValueLabel->setText("0.0 dB", juce::dontSendNotification);
    volumeValueLabel->setJustificationType(juce::Justification::centred);
    volumeValueLabel->setColour(juce::Label::textColourId,
                                DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
    volumeValueLabel->setFont(FontManager::getInstance().getUIFont(9.0f));
    addAndMakeVisible(*volumeValueLabel);

    // Mute button with volume icons
    auto volumeOnIcon = juce::Drawable::createFromImageData(BinaryData::volume_up_svg,
                                                            BinaryData::volume_up_svgSize);
    auto volumeOffIcon = juce::Drawable::createFromImageData(BinaryData::volume_off_svg,
                                                             BinaryData::volume_off_svgSize);

    muteButton = std::make_unique<juce::DrawableButton>("Mute", juce::DrawableButton::ImageFitted);
    muteButton->setImages(volumeOnIcon.get(), nullptr, nullptr, nullptr, volumeOffIcon.get());
    muteButton->setClickingTogglesState(true);
    muteButton->setColour(juce::DrawableButton::backgroundColourId,
                          juce::Colours::transparentBlack);
    muteButton->setColour(juce::DrawableButton::backgroundOnColourId,
                          DarkTheme::getColour(DarkTheme::STATUS_WARNING).withAlpha(0.3f));
    muteButton->onClick = [this]() {
        TrackManager::getInstance().setMasterMuted(muteButton->getToggleState());
    };
    addAndMakeVisible(*muteButton);
}

void MasterChannelStrip::paint(juce::Graphics& g) {
    g.fillAll(DarkTheme::getColour(DarkTheme::PANEL_BACKGROUND));

    // Draw border
    g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
    g.drawRect(getLocalBounds(), 1);

    // Draw fader region border (top and bottom lines)
    if (!faderRegion_.isEmpty()) {
        g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
        // Top border
        g.fillRect(faderRegion_.getX(), faderRegion_.getY(), faderRegion_.getWidth(), 1);
        // Bottom border
        g.fillRect(faderRegion_.getX(), faderRegion_.getBottom() - 1, faderRegion_.getWidth(), 1);
    }

    // Draw dB labels with ticks
    drawDbLabels(g);
}

void MasterChannelStrip::resized() {
    const auto& metrics = MixerMetrics::getInstance();
    auto bounds = getLocalBounds().reduced(4);

    if (orientation_ == Orientation::Vertical) {
        // Vertical layout (for MixerView and SessionView)
        titleLabel->setBounds(bounds.removeFromTop(24));
        bounds.removeFromTop(4);

        // Mute button
        auto muteArea = bounds.removeFromTop(28);
        muteButton->setBounds(muteArea.withSizeKeepingCentre(24, 24));
        bounds.removeFromTop(4);

        // Use percentage of remaining height for fader
        int faderHeight = static_cast<int>(bounds.getHeight() * metrics.faderHeightRatio / 100.0f);
        int extraSpace = bounds.getHeight() - faderHeight;
        bounds.removeFromTop(extraSpace / 2);
        bounds.setHeight(faderHeight);

        // Layout: [fader] [faderGap] [leftTicks] [labels] [rightTicks] [meterGap] [meter]
        // Use same widths as channel strip for consistency
        int faderWidth = metrics.faderWidth;
        int meterWidthVal = metrics.meterWidth;
        int tickWidth = static_cast<int>(std::ceil(metrics.tickWidth()));
        int gap = metrics.tickToFaderGap;
        int meterGapVal = metrics.tickToMeterGap;
        int tickToLabelGap = metrics.tickToLabelGap;
        int labelTextWidth = static_cast<int>(metrics.labelTextWidth);

        // Calculate total width needed for the fader layout
        int totalLayoutWidth = faderWidth + gap + tickWidth + tickToLabelGap + labelTextWidth +
                               tickToLabelGap + tickWidth + meterGapVal + meterWidthVal;

        // Center the layout within bounds
        int leftMargin = (bounds.getWidth() - totalLayoutWidth) / 2;
        auto centeredBounds = bounds.withTrimmedLeft(leftMargin).withWidth(totalLayoutWidth);

        // Store the entire fader region for border drawing (use centered bounds)
        faderRegion_ = centeredBounds;

        // Position value labels right above the fader region top border
        const int labelHeight = 12;
        auto valueLabelArea =
            juce::Rectangle<int>(faderRegion_.getX(), faderRegion_.getY() - labelHeight,
                                 faderRegion_.getWidth(), labelHeight);
        volumeValueLabel->setBounds(valueLabelArea.removeFromLeft(valueLabelArea.getWidth() / 2));
        peakLabel->setBounds(valueLabelArea);

        // Add vertical padding inside the border
        const int borderPadding = 6;
        centeredBounds.removeFromTop(borderPadding);
        centeredBounds.removeFromBottom(borderPadding);

        auto layoutArea = centeredBounds;

        // Fader on left
        faderArea_ = layoutArea.removeFromLeft(faderWidth);
        volumeSlider->setBounds(faderArea_);

        // Meter on right
        meterArea_ = layoutArea.removeFromRight(meterWidthVal);
        levelMeter->setBounds(meterArea_);

        // Position tick areas with gap from fader/meter
        leftTickArea_ = juce::Rectangle<int>(faderArea_.getRight() + gap, layoutArea.getY(),
                                             tickWidth, layoutArea.getHeight());

        rightTickArea_ = juce::Rectangle<int>(meterArea_.getX() - tickWidth - meterGapVal,
                                              layoutArea.getY(), tickWidth, layoutArea.getHeight());

        // Label area between ticks
        int labelLeft = leftTickArea_.getRight() + tickToLabelGap;
        int labelRight = rightTickArea_.getX() - tickToLabelGap;
        labelArea_ = juce::Rectangle<int>(labelLeft, layoutArea.getY(), labelRight - labelLeft,
                                          layoutArea.getHeight());
    } else {
        // Horizontal layout (for Arrange view - at bottom of track content)
        titleLabel->setBounds(bounds.removeFromLeft(60));
        bounds.removeFromLeft(8);

        // Mute button
        muteButton->setBounds(bounds.removeFromLeft(28).withSizeKeepingCentre(24, 24));
        bounds.removeFromLeft(8);

        // Value label above meter
        auto labelArea = bounds.removeFromTop(12);
        volumeValueLabel->setBounds(labelArea.removeFromRight(40));
        peakLabel->setBounds(juce::Rectangle<int>());  // Hidden in horizontal

        levelMeter->setBounds(bounds.removeFromRight(12));
        bounds.removeFromRight(4);
        volumeSlider->setBounds(bounds);

        // Clear vertical layout regions
        faderRegion_ = juce::Rectangle<int>();
        faderArea_ = juce::Rectangle<int>();
        leftTickArea_ = juce::Rectangle<int>();
        labelArea_ = juce::Rectangle<int>();
        rightTickArea_ = juce::Rectangle<int>();
        meterArea_ = juce::Rectangle<int>();
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

    // Update mute button
    if (muteButton) {
        muteButton->setToggleState(master.muted, juce::dontSendNotification);
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

void MasterChannelStrip::drawDbLabels(juce::Graphics& g) {
    if (labelArea_.isEmpty() || !volumeSlider)
        return;

    const auto& metrics = MixerMetrics::getInstance();

    // dB values to display with ticks
    const std::vector<float> dbValues = {6.0f,   3.0f,   0.0f,   -3.0f,  -6.0f, -12.0f,
                                         -18.0f, -24.0f, -36.0f, -48.0f, -60.0f};

    // Labels mark where the thumb CENTER is at each dB value.
    // JUCE reduces slider bounds by thumbRadius, so the thumb center range is:
    // - Top: faderArea_.getY() + thumbRadius
    // - Bottom: faderArea_.getBottom() - thumbRadius
    float thumbRadius = metrics.thumbRadius();
    float effectiveTop = faderArea_.getY() + thumbRadius;
    float effectiveHeight = faderArea_.getHeight() - 2.0f * thumbRadius;

    g.setFont(FontManager::getInstance().getUIFont(metrics.labelFontSize));

    for (float db : dbValues) {
        // Convert dB to Y position - MUST match JUCE's formula exactly:
        // sliderPos = sliderRegionStart + (1 - valueProportional) * sliderRegionSize
        float faderPos = dbToFaderPos(db);
        float yNorm = 1.0f - faderPos;
        float y = effectiveTop + yNorm * effectiveHeight;

        // Draw ticks in their designated areas
        float tickHeight = metrics.tickHeight();
        g.setColour(DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));

        // Left tick: draw within leftTickArea_, right-aligned
        float leftTickX = static_cast<float>(leftTickArea_.getRight()) - metrics.tickWidth();
        g.fillRect(leftTickX, y - tickHeight / 2.0f, metrics.tickWidth(), tickHeight);

        // Right tick: draw within rightTickArea_, left-aligned
        float rightTickX = static_cast<float>(rightTickArea_.getX());
        g.fillRect(rightTickX, y - tickHeight / 2.0f, metrics.tickWidth(), tickHeight);

        // Draw label text centered - no signs, infinity symbol at bottom
        juce::String labelText;
        int dbInt = static_cast<int>(db);
        if (db <= MIN_DB) {
            labelText = juce::String::charToString(0x221E);  // ∞ infinity symbol
        } else {
            labelText = juce::String(std::abs(dbInt));
        }

        float textWidth = metrics.labelTextWidth;
        float textHeight = metrics.labelTextHeight;
        float textX = labelArea_.getCentreX() - textWidth / 2.0f;
        float textY = y - textHeight / 2.0f;

        g.setColour(DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
        g.drawText(labelText, static_cast<int>(textX), static_cast<int>(textY),
                   static_cast<int>(textWidth), static_cast<int>(textHeight),
                   juce::Justification::centred, false);
    }
}

}  // namespace magica
