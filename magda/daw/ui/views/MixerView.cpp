#include "MixerView.hpp"

#include <cmath>

#include "../../audio/AudioBridge.hpp"
#include "../../audio/MeteringBuffer.hpp"
#include "../../engine/AudioEngine.hpp"
#include "../../engine/TracktionEngineWrapper.hpp"
#include "../../profiling/PerformanceProfiler.hpp"
#include "../themes/DarkTheme.hpp"
#include "../themes/FontManager.hpp"
#include "core/SelectionManager.hpp"
#include "core/ViewModeController.hpp"

namespace magda {

// dB conversion helpers
namespace {
constexpr float MIN_DB = -60.0f;
constexpr float MAX_DB = 6.0f;

// Convert linear gain (0-1) to dB
float gainToDb(float gain) {
    if (gain <= 0.0f)
        return MIN_DB;
    return 20.0f * std::log10(gain);
}

// Convert dB to linear gain
float dbToGain(float db) {
    if (db <= MIN_DB)
        return 0.0f;
    return std::pow(10.0f, db / 20.0f);
}

// Exponent for power curve scaling - lower values spread out the bottom labels more
constexpr float METER_CURVE_EXPONENT = 2.0f;

// Convert dB to normalized meter position (0-1) with power curve
// Used consistently for meters, labels, and faders across the app
float dbToMeterPos(float db) {
    if (db <= MIN_DB)
        return 0.0f;
    if (db >= MAX_DB)
        return 1.0f;

    float normalized = (db - MIN_DB) / (MAX_DB - MIN_DB);
    return std::pow(normalized, METER_CURVE_EXPONENT);
}

// Convert meter position back to dB (inverse of dbToMeterPos)
float meterPosToDb(float pos) {
    if (pos <= 0.0f)
        return MIN_DB;
    if (pos >= 1.0f)
        return MAX_DB;

    float normalized = std::pow(pos, 1.0f / METER_CURVE_EXPONENT);
    return MIN_DB + normalized * (MAX_DB - MIN_DB);
}

}  // namespace

// Stereo level meter component (L/R bars)
class MixerView::ChannelStrip::LevelMeter : public juce::Component {
  public:
    LevelMeter() = default;

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

        // Meter fill (using consistent scaling across all views)
        float displayLevel = dbToMeterPos(gainToDb(level));
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

// Channel strip implementation
MixerView::ChannelStrip::ChannelStrip(const TrackInfo& track, juce::LookAndFeel* faderLookAndFeel,
                                      bool isMaster)
    : trackId_(track.id),
      isMaster_(isMaster),
      trackColour_(track.colour),
      trackName_(track.name),
      faderLookAndFeel_(faderLookAndFeel) {
    setupControls();
    updateFromTrack(track);
}

MixerView::ChannelStrip::~ChannelStrip() {
    // Clear look and feel before destruction to avoid dangling pointer issues
    if (volumeFader) {
        volumeFader->setLookAndFeel(nullptr);
    }
    if (panKnob) {
        panKnob->setLookAndFeel(nullptr);
    }
}

void MixerView::ChannelStrip::updateFromTrack(const TrackInfo& track) {
    trackColour_ = track.colour;
    trackName_ = track.name;

    if (trackLabel) {
        trackLabel->setText(isMaster_ ? "Master" : track.name, juce::dontSendNotification);
    }
    if (volumeFader) {
        // Convert linear gain to fader position (using consistent meter scaling)
        float db = gainToDb(track.volume);
        float faderPos = dbToMeterPos(db);
        volumeFader->setValue(faderPos, juce::dontSendNotification);
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
        // Update pan label
        if (panValueLabel) {
            float pan = static_cast<float>(panKnob->getValue());
            juce::String panText;
            if (std::abs(pan) < 0.01f) {
                panText = "C";
            } else if (pan < 0) {
                panText = juce::String(static_cast<int>(std::abs(pan) * 100)) + "L";
            } else {
                panText = juce::String(static_cast<int>(pan * 100)) + "R";
            }
            panValueLabel->setText(panText, juce::dontSendNotification);
        }
    };
    // Apply custom look and feel for knob styling
    if (faderLookAndFeel_) {
        panKnob->setLookAndFeel(faderLookAndFeel_);
    }
    addAndMakeVisible(*panKnob);

    // Pan value label
    panValueLabel = std::make_unique<juce::Label>();
    panValueLabel->setText("C", juce::dontSendNotification);
    panValueLabel->setJustificationType(juce::Justification::centred);
    panValueLabel->setColour(juce::Label::textColourId,
                             DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
    panValueLabel->setFont(FontManager::getInstance().getUIFont(10.0f));
    addAndMakeVisible(*panValueLabel);

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

    // Volume fader - using dB scale with unity at 0.75 position
    volumeFader =
        std::make_unique<juce::Slider>(juce::Slider::LinearVertical, juce::Slider::NoTextBox);
    volumeFader->setRange(0.0, 1.0, 0.001);             // Internal 0-1 range
    volumeFader->setValue(0.75);                        // Unity gain (0 dB) at 75%
    volumeFader->setSliderSnapsToMousePosition(false);  // Relative drag, not jump to click
    volumeFader->setColour(juce::Slider::trackColourId, DarkTheme::getColour(DarkTheme::SURFACE));
    volumeFader->setColour(juce::Slider::backgroundColourId,
                           DarkTheme::getColour(DarkTheme::SURFACE));
    volumeFader->setColour(juce::Slider::thumbColourId,
                           DarkTheme::getColour(DarkTheme::ACCENT_BLUE));
    volumeFader->onValueChange = [this]() {
        // Convert fader position to dB, then to linear gain for TrackManager
        float faderPos = static_cast<float>(volumeFader->getValue());
        float db = meterPosToDb(faderPos);
        float gain = dbToGain(db);
        TrackManager::getInstance().setTrackVolume(trackId_, gain);
        // Update fader label
        if (faderValueLabel) {
            juce::String dbText;
            if (db <= MIN_DB) {
                dbText = "-inf";
            } else {
                dbText = juce::String(db, 1) + " dB";
            }
            faderValueLabel->setText(dbText, juce::dontSendNotification);
        }
    };
    // Apply custom look and feel for fader styling
    if (faderLookAndFeel_) {
        volumeFader->setLookAndFeel(faderLookAndFeel_);
    }
    addAndMakeVisible(*volumeFader);

    // Fader value label
    faderValueLabel = std::make_unique<juce::Label>();
    faderValueLabel->setText("0.0 dB", juce::dontSendNotification);
    faderValueLabel->setJustificationType(juce::Justification::centred);
    faderValueLabel->setColour(juce::Label::textColourId,
                               DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
    faderValueLabel->setFont(FontManager::getInstance().getUIFont(9.0f));
    addAndMakeVisible(*faderValueLabel);

    // Mute button (square corners, compact)
    muteButton = std::make_unique<juce::TextButton>("M");
    muteButton->setConnectedEdges(juce::Button::ConnectedOnLeft | juce::Button::ConnectedOnRight |
                                  juce::Button::ConnectedOnTop | juce::Button::ConnectedOnBottom);
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

    // Solo button (square corners, compact)
    soloButton = std::make_unique<juce::TextButton>("S");
    soloButton->setConnectedEdges(juce::Button::ConnectedOnLeft | juce::Button::ConnectedOnRight |
                                  juce::Button::ConnectedOnTop | juce::Button::ConnectedOnBottom);
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
        recordButton->setConnectedEdges(
            juce::Button::ConnectedOnLeft | juce::Button::ConnectedOnRight |
            juce::Button::ConnectedOnTop | juce::Button::ConnectedOnBottom);
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

        // Audio/MIDI routing selectors (toggle + dropdown, not on master)
        audioInSelector = std::make_unique<RoutingSelector>(RoutingSelector::Type::AudioIn);
        audioInSelector->setOptions({
            {1, "Input 1"},
            {2, "Input 2"},
            {3, "Input 1+2 (Stereo)"},
            {0, "", true},  // Separator
            {10, "External Sidechain"},
        });
        audioInSelector->setSelectedId(1);
        addAndMakeVisible(*audioInSelector);

        audioOutSelector = std::make_unique<RoutingSelector>(RoutingSelector::Type::AudioOut);
        audioOutSelector->setOptions({
            {1, "Master"},
            {2, "Bus 1"},
            {3, "Bus 2"},
            {0, "", true},  // Separator
            {10, "Hardware Out"},
        });
        audioOutSelector->setSelectedId(1);
        addAndMakeVisible(*audioOutSelector);

        midiInSelector = std::make_unique<RoutingSelector>(RoutingSelector::Type::MidiIn);
        // Options should be populated from MidiBridge
        midiInSelector->setSelectedId(1);
        addAndMakeVisible(*midiInSelector);

        midiOutSelector = std::make_unique<RoutingSelector>(RoutingSelector::Type::MidiOut);
        // Options should be populated from MidiBridge
        midiOutSelector->setSelectedId(1);
        addAndMakeVisible(*midiOutSelector);
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

void MixerView::ChannelStrip::drawDbLabels(juce::Graphics& g) {
    if (labelArea_.isEmpty() || !volumeFader)
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
        // Convert dB to Y position using consistent meter scaling
        float faderPos = dbToMeterPos(db);
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

void MixerView::ChannelStrip::resized() {
    const auto& metrics = MixerMetrics::getInstance();
    auto bounds = getLocalBounds().reduced(metrics.channelPadding);

    // Color indicator space
    bounds.removeFromTop(6);

    // Track label at top
    trackLabel->setBounds(bounds.removeFromTop(24));
    bounds.removeFromTop(metrics.controlSpacing);

    // Pan knob
    auto panArea = bounds.removeFromTop(metrics.knobSize);
    panKnob->setBounds(panArea.withSizeKeepingCentre(metrics.knobSize, metrics.knobSize));

    // Pan value label below knob
    auto panLabelArea = bounds.removeFromTop(14);
    panValueLabel->setBounds(panLabelArea);
    bounds.removeFromTop(metrics.controlSpacing);

    // M/S/R buttons at bottom
    auto buttonArea = bounds.removeFromBottom(metrics.buttonSize);
    int numButtons = isMaster_ ? 2 : 3;
    int buttonWidth = (buttonArea.getWidth() - (numButtons - 1) * 2) / numButtons;

    muteButton->setBounds(buttonArea.removeFromLeft(buttonWidth));
    buttonArea.removeFromLeft(2);
    soloButton->setBounds(buttonArea.removeFromLeft(buttonWidth));
    if (recordButton) {
        buttonArea.removeFromLeft(2);
        recordButton->setBounds(buttonArea.removeFromLeft(buttonWidth));
    }

    // Routing selectors above M/S/R (2 rows: Audio In/Out, MIDI In/Out)
    if (audioInSelector && audioOutSelector && midiInSelector && midiOutSelector) {
        bounds.removeFromBottom(2);  // Small gap

        // MIDI row (Mi/Mo)
        auto midiRow = bounds.removeFromBottom(16);
        int halfWidth = (midiRow.getWidth() - 2) / 2;
        midiInSelector->setBounds(midiRow.removeFromLeft(halfWidth));
        midiRow.removeFromLeft(2);
        midiOutSelector->setBounds(midiRow.removeFromLeft(halfWidth));

        bounds.removeFromBottom(2);  // Small gap

        // Audio row (Ai/Ao)
        auto audioRow = bounds.removeFromBottom(16);
        halfWidth = (audioRow.getWidth() - 2) / 2;
        audioInSelector->setBounds(audioRow.removeFromLeft(halfWidth));
        audioRow.removeFromLeft(2);
        audioOutSelector->setBounds(audioRow.removeFromLeft(halfWidth));
    }

    bounds.removeFromBottom(metrics.controlSpacing);

    // Use percentage of remaining height for fader
    int faderHeight = static_cast<int>(bounds.getHeight() * metrics.faderHeightRatio / 100.0f);
    int extraSpace = bounds.getHeight() - faderHeight;
    bounds.removeFromTop(extraSpace / 2);
    bounds.setHeight(faderHeight);

    // Layout: [fader] [gap] [leftTicks] [labels] [rightTicks] [gap] [meter]
    // Calculate widths from metrics
    int faderWidth = metrics.faderWidth;
    int meterWidthVal = metrics.meterWidth;
    int tickWidth = static_cast<int>(std::ceil(metrics.tickWidth()));
    int gap = metrics.tickToFaderGap;

    // Store the entire fader region for border drawing
    faderRegion_ = bounds;

    // Position value labels right above the fader region top border
    const int labelHeight = 12;
    auto valueLabelArea =
        juce::Rectangle<int>(faderRegion_.getX(), faderRegion_.getY() - labelHeight,
                             faderRegion_.getWidth(), labelHeight);
    faderValueLabel->setBounds(valueLabelArea.removeFromLeft(valueLabelArea.getWidth() / 2));
    peakLabel->setBounds(valueLabelArea);

    // Add vertical padding inside the border
    const int borderPadding = 6;
    bounds.removeFromTop(borderPadding);
    bounds.removeFromBottom(borderPadding);

    auto layoutArea = bounds;

    // Fader on left
    faderArea_ = layoutArea.removeFromLeft(faderWidth);
    volumeFader->setBounds(faderArea_);

    // Meter on right
    meterArea_ = layoutArea.removeFromRight(meterWidthVal);
    levelMeter->setBounds(meterArea_);

    // Position tick areas with gap from fader/meter
    int meterGap = metrics.tickToMeterGap;

    // Left ticks: positioned after fader + gap
    leftTickArea_ = juce::Rectangle<int>(faderArea_.getRight() + gap, layoutArea.getY(), tickWidth,
                                         layoutArea.getHeight());

    // Right ticks: positioned before meter - meterGap
    rightTickArea_ = juce::Rectangle<int>(meterArea_.getX() - tickWidth - meterGap,
                                          layoutArea.getY(), tickWidth, layoutArea.getHeight());

    // Label area between ticks
    int tickToLabelGap = metrics.tickToLabelGap;
    int labelLeft = leftTickArea_.getRight() + tickToLabelGap;
    int labelRight = rightTickArea_.getX() - tickToLabelGap;
    labelArea_ = juce::Rectangle<int>(labelLeft, layoutArea.getY(), labelRight - labelLeft,
                                      layoutArea.getHeight());
}

void MixerView::ChannelStrip::setMeterLevel(float level) {
    setMeterLevels(level, level);
}

void MixerView::ChannelStrip::setMeterLevels(float leftLevel, float rightLevel) {
    meterLevel = std::max(leftLevel, rightLevel);
    if (levelMeter) {
        levelMeter->setLevels(leftLevel, rightLevel);
    }

    // Update peak value
    float maxLevel = std::max(leftLevel, rightLevel);
    if (maxLevel > peakValue_) {
        peakValue_ = maxLevel;
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
MixerView::MixerView(AudioEngine* audioEngine) : audioEngine_(audioEngine) {
    // Get current view mode
    currentViewMode_ = ViewModeController::getInstance().getViewMode();

    // Create channel container
    channelContainer = std::make_unique<juce::Component>();

    // Create viewport for scrollable channels
    channelViewport = std::make_unique<juce::Viewport>();
    channelViewport->setViewedComponent(channelContainer.get(), false);
    channelViewport->setScrollBarsShown(false, true);  // Horizontal scroll only
    addAndMakeVisible(*channelViewport);

    // Create aux container (fixed, between channels and master)
    auxContainer = std::make_unique<juce::Component>();
    addAndMakeVisible(*auxContainer);

    // Create master strip (uses shared MasterChannelStrip component)
    masterStrip = std::make_unique<MasterChannelStrip>(MasterChannelStrip::Orientation::Vertical);
    addAndMakeVisible(*masterStrip);

    // Create channel resize handle
    channelResizeHandle_ = std::make_unique<ChannelResizeHandle>();
    channelResizeHandle_->onResize = [this](int deltaX) {
        auto& metrics = MixerMetrics::getInstance();
        int newWidth =
            juce::jlimit(minChannelWidth_, maxChannelWidth_, metrics.channelWidth + deltaX);
        if (metrics.channelWidth != newWidth) {
            metrics.channelWidth = newWidth;
            resized();
        }
    };
    addAndMakeVisible(*channelResizeHandle_);

    // Register as TrackManager listener
    TrackManager::getInstance().addListener(this);

    // Register as ViewModeController listener
    ViewModeController::getInstance().addListener(this);

    // Build channel strips from TrackManager
    rebuildChannelStrips();

    // Debug panel disabled - remove F12 toggle
    // debugPanel_ = std::make_unique<MixerDebugPanel>();
    // debugPanel_->setVisible(false);
    // debugPanel_->onMetricsChanged = [this]() { rebuildChannelStrips(); };
    // addAndMakeVisible(*debugPanel_);

    // Start timer for meter animation (30fps)
    startTimer(33);
}

MixerView::~MixerView() {
    stopTimer();
    TrackManager::getInstance().removeListener(this);
    ViewModeController::getInstance().removeListener(this);

    // Explicitly clear all UI components before automatic member destruction
    // This ensures components release their LookAndFeel references before
    // mixerLookAndFeel_ is destroyed (member destruction happens in reverse order)
    channelStrips.clear();
    auxChannelStrips.clear();
    masterStrip.reset();
    debugPanel_.reset();
    auxContainer.reset();
    channelContainer.reset();
    channelViewport.reset();
    channelResizeHandle_.reset();
}

void MixerView::rebuildChannelStrips() {
    // Clear existing strips
    channelStrips.clear();

    const auto& tracks = TrackManager::getInstance().getTracks();

    for (const auto& track : tracks) {
        // Only show tracks visible in the current view mode
        if (!track.isVisibleIn(currentViewMode_)) {
            continue;
        }

        if (track.type == TrackType::Aux)
            continue;  // Aux strips handled separately

        auto strip = std::make_unique<ChannelStrip>(track, &mixerLookAndFeel_, false);
        strip->onClicked = [this](int trackId, bool isMaster) {
            // Find the index of this track in the visible strips
            for (size_t i = 0; i < channelStrips.size(); ++i) {
                if (channelStrips[i]->getTrackId() == trackId) {
                    selectChannel(static_cast<int>(i), isMaster);
                    break;
                }
            }
        };
        channelContainer->addAndMakeVisible(*strip);
        channelStrips.push_back(std::move(strip));
    }

    // Build aux channel strips separately
    auxChannelStrips.clear();
    for (const auto& track : tracks) {
        if (track.type != TrackType::Aux || !track.isVisibleIn(currentViewMode_))
            continue;
        auto strip = std::make_unique<ChannelStrip>(track, &mixerLookAndFeel_, false);
        strip->onClicked = [this](int trackId, bool isMaster) {
            for (size_t i = 0; i < channelStrips.size(); ++i) {
                if (channelStrips[i]->getTrackId() == trackId) {
                    selectChannel(static_cast<int>(i), isMaster);
                    return;
                }
            }
            // Check aux strips — use negative index offset for identification
            for (size_t i = 0; i < auxChannelStrips.size(); ++i) {
                if (auxChannelStrips[i]->getTrackId() == trackId) {
                    // Select via TrackManager directly
                    SelectionManager::getInstance().selectTrack(trackId);
                    return;
                }
            }
        };
        auxContainer->addAndMakeVisible(*strip);
        auxChannelStrips.push_back(std::move(strip));
    }

    // Update master strip visibility
    const auto& master = TrackManager::getInstance().getMasterChannel();
    bool masterVisible = master.isVisibleIn(currentViewMode_);
    masterStrip->setVisible(masterVisible);

    // Sync selection with TrackManager's current selection
    trackSelectionChanged(TrackManager::getInstance().getSelectedTrack());

    resized();
}

void MixerView::tracksChanged() {
    // Rebuild all channel strips when tracks are added/removed/reordered
    rebuildChannelStrips();
}

void MixerView::trackPropertyChanged(int trackId) {
    // Update the specific channel strip - find it by track ID since indices may differ
    const auto* track = TrackManager::getInstance().getTrack(trackId);
    if (!track)
        return;

    for (auto& strip : channelStrips) {
        if (strip->getTrackId() == trackId) {
            strip->updateFromTrack(*track);
            return;
        }
    }

    // Check aux strips
    for (auto& strip : auxChannelStrips) {
        if (strip->getTrackId() == trackId) {
            strip->updateFromTrack(*track);
            return;
        }
    }
}

void MixerView::viewModeChanged(ViewMode mode, const AudioEngineProfile& /*profile*/) {
    currentViewMode_ = mode;
    rebuildChannelStrips();
}

void MixerView::masterChannelChanged() {
    // Update master strip visibility
    const auto& master = TrackManager::getInstance().getMasterChannel();
    bool masterVisible = master.isVisibleIn(currentViewMode_);
    masterStrip->setVisible(masterVisible);
    resized();
}

void MixerView::paint(juce::Graphics& g) {
    MAGDA_MONITOR_SCOPE("UIFrame");
    g.fillAll(DarkTheme::getColour(DarkTheme::BACKGROUND));
}

void MixerView::resized() {
    const auto& metrics = MixerMetrics::getInstance();
    auto bounds = getLocalBounds();

    // Master strip on the right (only if visible)
    if (masterStrip->isVisible()) {
        masterStrip->setBounds(bounds.removeFromRight(metrics.masterWidth));

        // Resize handle between channels and master
        const int handleWidth = 8;
        channelResizeHandle_->setBounds(bounds.removeFromRight(handleWidth));
    }

    // Aux channel strips between regular channels and master
    int numAux = static_cast<int>(auxChannelStrips.size());
    int auxWidth = numAux * metrics.channelWidth;
    if (auxWidth > 0) {
        auto auxArea = bounds.removeFromRight(auxWidth);
        auxContainer->setBounds(auxArea);
        for (int i = 0; i < numAux; ++i) {
            auxChannelStrips[i]->setBounds(i * metrics.channelWidth, 0, metrics.channelWidth,
                                           auxArea.getHeight());
        }
    } else {
        auxContainer->setBounds(0, 0, 0, 0);
    }

    // Channel viewport takes remaining space
    channelViewport->setBounds(bounds);

    // Size the channel container
    int numChannels = static_cast<int>(channelStrips.size());
    int containerWidth = numChannels * metrics.channelWidth;
    int containerHeight = bounds.getHeight();
    channelContainer->setSize(containerWidth, containerHeight);

    // Position channel strips
    for (int i = 0; i < numChannels; ++i) {
        channelStrips[i]->setBounds(i * metrics.channelWidth, 0, metrics.channelWidth,
                                    containerHeight);
    }
}

void MixerView::timerCallback() {
    // Read metering data from AudioBridge
    if (!audioEngine_)
        return;

    auto* teWrapper = dynamic_cast<TracktionEngineWrapper*>(audioEngine_);
    if (!teWrapper)
        return;

    auto* bridge = teWrapper->getAudioBridge();
    if (!bridge)
        return;

    auto& meteringBuffer = bridge->getMeteringBuffer();

    // Update channel strip meters
    for (auto& strip : channelStrips) {
        int trackId = strip->getTrackId();
        MeterData data;
        if (meteringBuffer.popLevels(trackId, data)) {
            strip->setMeterLevels(data.peakL, data.peakR);
        }
    }

    // Update aux channel strip meters
    for (auto& strip : auxChannelStrips) {
        int trackId = strip->getTrackId();
        MeterData data;
        if (meteringBuffer.popLevels(trackId, data)) {
            strip->setMeterLevels(data.peakL, data.peakR);
        }
    }

    // Update master strip meters
    if (masterStrip) {
        float masterPeakL = bridge->getMasterPeakL();
        float masterPeakR = bridge->getMasterPeakR();
        masterStrip->setPeakLevels(masterPeakL, masterPeakR);
    }
}

bool MixerView::keyPressed(const juce::KeyPress& /*key*/) {
    // Debug panel disabled
    return false;
}

bool MixerView::isInChannelResizeZone(const juce::Point<int>& /*pos*/) const {
    // Not used anymore - resize handle component handles this
    return false;
}

void MixerView::mouseMove(const juce::MouseEvent& /*event*/) {
    // Resize handled by ChannelResizeHandle
}

void MixerView::mouseDown(const juce::MouseEvent& /*event*/) {
    // Resize handled by ChannelResizeHandle
}

void MixerView::mouseDrag(const juce::MouseEvent& /*event*/) {
    // Resize handled by ChannelResizeHandle
}

void MixerView::mouseUp(const juce::MouseEvent& /*event*/) {
    // Resize handled by ChannelResizeHandle
}

// ChannelResizeHandle implementation
MixerView::ChannelResizeHandle::ChannelResizeHandle() {
    setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
}

void MixerView::ChannelResizeHandle::paint(juce::Graphics& g) {
    // Draw a subtle line, more visible when hovering
    float alpha = (isHovering_ || isDragging_) ? 0.8f : 0.3f;
    g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_PURPLE).withAlpha(alpha));
    g.fillRect(getWidth() / 2 - 1, 0, 2, getHeight());
}

void MixerView::ChannelResizeHandle::mouseEnter(const juce::MouseEvent& /*event*/) {
    isHovering_ = true;
    repaint();
}

void MixerView::ChannelResizeHandle::mouseExit(const juce::MouseEvent& /*event*/) {
    isHovering_ = false;
    repaint();
}

void MixerView::ChannelResizeHandle::mouseDown(const juce::MouseEvent& event) {
    isDragging_ = true;
    dragStartX_ = event.getScreenX();
    repaint();
}

void MixerView::ChannelResizeHandle::mouseDrag(const juce::MouseEvent& event) {
    if (isDragging_ && onResize) {
        int deltaX = event.getScreenX() - dragStartX_;
        onResize(deltaX);
        dragStartX_ = event.getScreenX();  // Incremental updates
    }
}

void MixerView::ChannelResizeHandle::mouseUp(const juce::MouseEvent& /*event*/) {
    isDragging_ = false;
    if (onResizeEnd) {
        onResizeEnd();
    }
    repaint();
}

void MixerView::selectChannel(int index, bool isMaster) {
    // Deselect all channel strips
    for (auto& strip : channelStrips) {
        strip->setSelected(false);
    }

    // Select the clicked channel
    if (isMaster) {
        // Master strip selection is visual-only (no setSelected on MasterChannelStrip)
        selectedChannelIndex = -1;
        selectedIsMaster = true;
        // Master track doesn't have a TrackId, so we clear selection
        SelectionManager::getInstance().clearSelection();
    } else {
        if (index >= 0 && index < static_cast<int>(channelStrips.size())) {
            channelStrips[index]->setSelected(true);
            // Notify SelectionManager of selection (which syncs with TrackManager)
            SelectionManager::getInstance().selectTrack(channelStrips[index]->getTrackId());
        }
        selectedChannelIndex = index;
        selectedIsMaster = false;
    }

    // Notify listener
    if (onChannelSelected) {
        onChannelSelected(selectedChannelIndex, selectedIsMaster);
    }
}

void MixerView::trackSelectionChanged(TrackId trackId) {
    // Sync our visual selection with TrackManager's selection
    // Deselect all first
    for (auto& strip : channelStrips) {
        strip->setSelected(false);
    }
    for (auto& strip : auxChannelStrips) {
        strip->setSelected(false);
    }
    selectedIsMaster = false;
    selectedChannelIndex = -1;

    if (trackId == INVALID_TRACK_ID) {
        return;
    }

    // Find and select the matching channel strip
    for (size_t i = 0; i < channelStrips.size(); ++i) {
        if (channelStrips[i]->getTrackId() == trackId) {
            channelStrips[i]->setSelected(true);
            selectedChannelIndex = static_cast<int>(i);
            return;
        }
    }

    // Check aux strips
    for (size_t i = 0; i < auxChannelStrips.size(); ++i) {
        if (auxChannelStrips[i]->getTrackId() == trackId) {
            auxChannelStrips[i]->setSelected(true);
            return;
        }
    }
}

}  // namespace magda
