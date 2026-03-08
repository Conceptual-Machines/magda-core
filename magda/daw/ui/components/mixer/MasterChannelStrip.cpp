#include "MasterChannelStrip.hpp"

#include <cmath>

#include "../../themes/DarkTheme.hpp"
#include "../../themes/FontManager.hpp"
#include "../../themes/MixerMetrics.hpp"
#include "BinaryData.h"
#include "core/TrackPropertyCommands.hpp"
#include "core/UndoManager.hpp"

namespace magda {

// dB conversion helpers
namespace {
constexpr float MIN_DB = -60.0f;
constexpr float MAX_DB = 6.0f;  // Allow +6 dB headroom

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

// Exponent for power curve scaling - lower values spread out the bottom labels more
constexpr float METER_CURVE_EXPONENT = 2.0f;

// Convert dB to normalized meter position (0-1) with power curve
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

// Resize handle for the send area boundary (matches channel strip's SendResizeHandle)
class MasterChannelStrip::ResizeHandle : public juce::Component {
  public:
    ResizeHandle() {
        setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
    }

    void paint(juce::Graphics& g) override {
        g.setColour(isHovering_ ? DarkTheme::getColour(DarkTheme::ACCENT_BLUE)
                                : DarkTheme::getColour(DarkTheme::SEPARATOR));
        int y = getHeight() / 2;
        g.fillRect(4, y, getWidth() - 8, 2);
    }

    void mouseEnter(const juce::MouseEvent& /*event*/) override {
        isHovering_ = true;
        repaint();
    }

    void mouseExit(const juce::MouseEvent& /*event*/) override {
        isHovering_ = false;
        repaint();
    }

    void mouseDown(const juce::MouseEvent& event) override {
        isDragging_ = true;
        dragStartY_ = event.getScreenY();
    }

    void mouseDrag(const juce::MouseEvent& event) override {
        if (!isDragging_ || !onResize)
            return;
        int deltaY = event.getScreenY() - dragStartY_;
        onResize(deltaY);
        dragStartY_ = event.getScreenY();
    }

    void mouseUp(const juce::MouseEvent& /*event*/) override {
        isDragging_ = false;
        isHovering_ = false;
        repaint();
    }

    std::function<void(int deltaY)> onResize;

  private:
    bool isHovering_ = false;
    bool isDragging_ = false;
    int dragStartY_ = 0;
};

// dB scale component — draws tick marks and dB labels, resizes with fader area
class MasterChannelStrip::DbScale : public juce::Component {
  public:
    DbScale() {
        setInterceptsMouseClicks(false, false);
    }

    void paint(juce::Graphics& g) override {
        auto bounds = getLocalBounds();
        if (bounds.isEmpty())
            return;

        const auto& metrics = MixerMetrics::getInstance();

        const float dbValues[] = {6.0f,   3.0f,   0.0f,   -3.0f,  -6.0f,
                                  -12.0f, -18.0f, -24.0f, -36.0f, -48.0f};

        float paddingTop = metrics.labelTextHeight / 2.0f + 1.0f;
        float paddingBottom = metrics.labelTextHeight / 2.0f;
        float top = paddingTop;
        float height = static_cast<float>(bounds.getHeight()) - paddingTop - paddingBottom;
        float totalWidth = static_cast<float>(bounds.getWidth());

        float tickW = metrics.tickWidth();
        float labelWidth = metrics.labelTextWidth;
        float centre = totalWidth / 2.0f;

        g.setFont(FontManager::getInstance().getUIFont(metrics.labelFontSize));

        float minSpacing = metrics.labelTextHeight + 2.0f;
        float lastDrawnY = -1000.0f;

        for (float db : dbValues) {
            float faderPos = dbToMeterPos(db);
            float y = top + height * (1.0f - faderPos);

            if (std::abs(y - lastDrawnY) < minSpacing)
                continue;
            lastDrawnY = y;

            float tickHeight = metrics.tickHeight();
            g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
            g.fillRect(0.0f, y - tickHeight / 2.0f, tickW - 1.0f, tickHeight);
            g.fillRect(totalWidth - tickW + 1.0f, y - tickHeight / 2.0f, tickW - 1.0f, tickHeight);

            juce::String labelText;
            int dbInt = static_cast<int>(db);
            if (db <= MIN_DB) {
                labelText = juce::String::charToString(0x221E);
            } else {
                labelText = juce::String(std::abs(dbInt));
            }

            float textHeight = metrics.labelTextHeight;
            float textX = centre - labelWidth / 2.0f;
            float textY = y - textHeight / 2.0f;

            g.setColour(DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
            g.drawText(labelText, static_cast<int>(textX), static_cast<int>(textY),
                       static_cast<int>(labelWidth), static_cast<int>(textHeight),
                       juce::Justification::centred, false);
        }
    }
};

// Stereo level meter component (L/R bars)
class MasterChannelStrip::LevelMeter : public juce::Component {
  public:
    void setLevel(float newLevel) {
        setLevels(newLevel, newLevel);
    }

    void setLevels(float left, float right) {
        leftLevel_ = juce::jlimit(0.0f, 2.0f, left);
        rightLevel_ = juce::jlimit(0.0f, 2.0f, right);
        repaint();
    }

    float getLevel() const {
        return std::max(leftLevel_, rightLevel_);
    }

    void paint(juce::Graphics& g) override {
        auto bounds = getLocalBounds().toFloat();

        const float gap = 1.0f;
        float barWidth = (bounds.getWidth() - gap) / 2.0f;

        auto leftBounds = bounds.withWidth(barWidth);
        auto rightBounds = bounds.withWidth(barWidth).withX(bounds.getX() + barWidth + gap);

        drawMeterBar(g, leftBounds, leftLevel_);
        drawMeterBar(g, rightBounds, rightLevel_);
    }

  private:
    float leftLevel_ = 0.0f;
    float rightLevel_ = 0.0f;

    void drawMeterBar(juce::Graphics& g, juce::Rectangle<float> bounds, float level) {
        g.setColour(DarkTheme::getColour(DarkTheme::SURFACE));
        g.fillRoundedRectangle(bounds, 1.0f);

        float db = gainToDb(level);
        float displayLevel = dbToMeterPos(db);
        float meterHeight = bounds.getHeight() * displayLevel;
        auto fillBounds = bounds;
        fillBounds = fillBounds.removeFromBottom(meterHeight);

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
}

void MasterChannelStrip::setupControls() {
    // Title label
    titleLabel = std::make_unique<juce::Label>("Master", "Master");
    titleLabel->setColour(juce::Label::textColourId, DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
    titleLabel->setFont(FontManager::getInstance().getUIFont(12.0f));
    titleLabel->setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(*titleLabel);

    // Peak meter
    peakMeter = std::make_unique<LevelMeter>();
    addAndMakeVisible(*peakMeter);

    // Peak value label
    peakValueLabel = std::make_unique<juce::Label>();
    peakValueLabel->setText("-inf", juce::dontSendNotification);
    peakValueLabel->setJustificationType(juce::Justification::centred);
    peakValueLabel->setColour(juce::Label::textColourId,
                              DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
    peakValueLabel->setFont(FontManager::getInstance().getUIFont(9.0f));
    addAndMakeVisible(*peakValueLabel);

    // Volume slider - TextSlider with vertical orientation and dB display
    volumeSlider = std::make_unique<daw::ui::TextSlider>(daw::ui::TextSlider::Format::Decibels);
    volumeSlider->setOrientation(daw::ui::TextSlider::Orientation::Vertical);
    volumeSlider->setRange(0.0, 1.0, 0.001);
    volumeSlider->setValue(dbToMeterPos(0.0f), juce::dontSendNotification);
    volumeSlider->setFont(FontManager::getInstance().getUIFont(9.0f));

    // Custom formatter: normalized position (0-1) -> dB string
    volumeSlider->setValueFormatter([](double pos) -> juce::String {
        float db = meterPosToDb(static_cast<float>(pos));
        if (db <= MIN_DB)
            return "-inf";
        return juce::String(db, 1);
    });

    // Custom parser: user input text -> normalized position (0-1)
    volumeSlider->setValueParser([](const juce::String& text) -> double {
        auto t = text.trim();
        if (t.endsWithIgnoreCase("db"))
            t = t.dropLastCharacters(2).trim();
        if (t.equalsIgnoreCase("-inf") || t.equalsIgnoreCase("inf"))
            return 0.0;
        float db = t.getFloatValue();
        return static_cast<double>(dbToMeterPos(db));
    });

    volumeSlider->onValueChanged = [this](double pos) {
        float db = meterPosToDb(static_cast<float>(pos));
        float gain = dbToGain(db);
        UndoManager::getInstance().executeCommand(std::make_unique<SetMasterVolumeCommand>(gain));
    };
    addAndMakeVisible(*volumeSlider);

    // DbScale component
    dbScale_ = std::make_unique<DbScale>();
    addAndMakeVisible(*dbScale_);

    // Send area resize handle
    resizeHandle_ = std::make_unique<ResizeHandle>();
    resizeHandle_->onResize = [this](int deltaY) {
        auto& metrics = MixerMetrics::getInstance();
        int newHeight =
            juce::jlimit(MixerMetrics::minSendAreaHeight, MixerMetrics::maxSendAreaHeight,
                         metrics.sendAreaHeight + deltaY);
        if (metrics.sendAreaHeight != newHeight) {
            metrics.sendAreaHeight = newHeight;
            if (onSendAreaResized)
                onSendAreaResized();
        }
    };
    addAndMakeVisible(*resizeHandle_);

    // Headphone icon (non-interactive, just a label)
    auto hpIcon = juce::Drawable::createFromImageData(BinaryData::headphones_svg,
                                                      BinaryData::headphones_svgSize);
    headphoneIcon_ =
        std::make_unique<juce::DrawableButton>("Headphones", juce::DrawableButton::ImageFitted);
    headphoneIcon_->setImages(hpIcon.get());
    headphoneIcon_->setClickingTogglesState(false);
    headphoneIcon_->setInterceptsMouseClicks(false, false);
    headphoneIcon_->setColour(juce::DrawableButton::backgroundColourId,
                              juce::Colours::transparentBlack);
    addAndMakeVisible(*headphoneIcon_);

    // Cue volume slider (horizontal)
    cueVolumeSlider_ = std::make_unique<daw::ui::TextSlider>(daw::ui::TextSlider::Format::Decibels);
    cueVolumeSlider_->setOrientation(daw::ui::TextSlider::Orientation::Horizontal);
    cueVolumeSlider_->setRange(0.0, 1.0, 0.001);
    cueVolumeSlider_->setValue(0.0, juce::dontSendNotification);  // -inf by default
    cueVolumeSlider_->setFont(FontManager::getInstance().getUIFont(9.0f));

    cueVolumeSlider_->setValueFormatter([](double pos) -> juce::String {
        float db = meterPosToDb(static_cast<float>(pos));
        if (db <= MIN_DB)
            return "-inf";
        return juce::String(db, 1);
    });

    cueVolumeSlider_->setValueParser([](const juce::String& text) -> double {
        auto t = text.trim();
        if (t.endsWithIgnoreCase("db"))
            t = t.dropLastCharacters(2).trim();
        if (t.equalsIgnoreCase("-inf") || t.equalsIgnoreCase("inf"))
            return 0.0;
        float db = t.getFloatValue();
        return static_cast<double>(dbToMeterPos(db));
    });

    // TODO: Wire to cue bus volume when implemented
    cueVolumeSlider_->onValueChanged = [](double /*pos*/) {};
    addAndMakeVisible(*cueVolumeSlider_);

    // Speaker on/off button (toggles master mute)
    auto speakerOnIcon = juce::Drawable::createFromImageData(BinaryData::volume_up_svg,
                                                             BinaryData::volume_up_svgSize);
    auto speakerOffIcon = juce::Drawable::createFromImageData(BinaryData::volume_off_svg,
                                                              BinaryData::volume_off_svgSize);

    speakerButton =
        std::make_unique<juce::DrawableButton>("Speaker", juce::DrawableButton::ImageFitted);
    speakerButton->setImages(speakerOnIcon.get(), nullptr, nullptr, nullptr, speakerOffIcon.get());
    speakerButton->setClickingTogglesState(true);
    speakerButton->setColour(juce::DrawableButton::backgroundColourId,
                             juce::Colours::transparentBlack);
    speakerButton->setColour(juce::DrawableButton::backgroundOnColourId,
                             DarkTheme::getColour(DarkTheme::STATUS_ERROR).withAlpha(0.3f));
    speakerButton->onClick = [this]() {
        UndoManager::getInstance().executeCommand(
            std::make_unique<SetMasterMuteCommand>(speakerButton->getToggleState()));
    };
    addAndMakeVisible(*speakerButton);
}

void MasterChannelStrip::paint(juce::Graphics& g) {
    g.fillAll(DarkTheme::getColour(DarkTheme::PANEL_BACKGROUND));

    // Draw border
    g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
    g.drawRect(getLocalBounds(), 1);

    // Draw fader region border (top and bottom lines)
    if (!faderRegion_.isEmpty()) {
        g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
        g.fillRect(faderRegion_.getX(), faderRegion_.getY(), faderRegion_.getWidth(), 1);
        g.fillRect(faderRegion_.getX(), faderRegion_.getBottom() - 1, faderRegion_.getWidth(), 1);
    }
}

void MasterChannelStrip::resized() {
    const auto& metrics = MixerMetrics::getInstance();
    auto bounds = getLocalBounds().reduced(metrics.channelPadding);

    if (orientation_ == Orientation::Vertical) {
        // Color indicator space (matches channel strip)
        bounds.removeFromTop(6);

        // Title row: [speaker icon] [Master label]
        auto titleRow = bounds.removeFromTop(24);
        speakerButton->setBounds(titleRow.removeFromLeft(20).withSizeKeepingCentre(18, 18));
        titleRow.removeFromLeft(2);
        titleLabel->setBounds(titleRow);

        bounds.removeFromTop(metrics.controlSpacing);

        // Send area space — reserve same height as channel strips for alignment
        const int sendAreaHeight = metrics.sendAreaHeight;
        bounds.removeFromTop(2);  // Gap between header and sends/handle
        bounds.removeFromTop(sendAreaHeight);

        // Resize handle overlapping the bottom of the send area space
        if (resizeHandle_) {
            int handleH = 8;
            int handleOverlap = 6;
            resizeHandle_->setBounds(bounds.getX(), bounds.getY() - handleH - handleOverlap,
                                     bounds.getWidth(), handleH);
            resizeHandle_->setAlwaysOnTop(true);
        }

        // Cue volume at bottom: [headphone icon] [slider]
        bounds.removeFromBottom(2);
        auto cueRow = bounds.removeFromBottom(20);
        headphoneIcon_->setBounds(cueRow.removeFromLeft(18));
        cueRow.removeFromLeft(2);
        cueVolumeSlider_->setBounds(cueRow);
        bounds.removeFromBottom(2);  // Gap between cue row and fader region

        // Small gap before fader region
        bounds.removeFromTop(2);

        // Calculate proportional widths like channel strips
        int availWidth = bounds.getWidth();
        int faderWidth = juce::jlimit(20, 60, availWidth * 40 / 100);
        int meterWidthVal = faderWidth;  // Same width as fader
        int gap = metrics.tickToFaderGap;
        int meterGapVal = metrics.tickToMeterGap;

        // Store the entire fader region for border drawing
        faderRegion_ = bounds;

        // Position peak label above the fader region top border
        const int labelHeight = 12;
        auto valueLabelArea =
            juce::Rectangle<int>(faderRegion_.getX(), faderRegion_.getY() - labelHeight,
                                 faderRegion_.getWidth(), labelHeight);
        peakValueLabel->setBounds(valueLabelArea);

        // Add vertical padding inside the border
        bounds.removeFromTop(6);
        bounds.removeFromBottom(3);

        auto layoutArea = bounds;

        // Fader on left
        faderArea_ = layoutArea.removeFromLeft(faderWidth);
        volumeSlider->setBounds(faderArea_);

        // Peak meter on right
        peakMeterArea_ = layoutArea.removeFromRight(meterWidthVal);
        peakMeter->setBounds(peakMeterArea_);

        // DbScale component — extends above/below for label overflow
        int labelPad = static_cast<int>(metrics.labelTextHeight / 2.0f + 1.0f);
        int scaleLeft = faderArea_.getRight() + gap;
        int scaleRight = peakMeterArea_.getX() - meterGapVal;
        dbScale_->setBounds(scaleLeft, layoutArea.getY() - labelPad, scaleRight - scaleLeft,
                            layoutArea.getHeight() + labelPad * 2);
    } else {
        // Horizontal layout (for Arrange view - at bottom of track content)
        titleLabel->setBounds(bounds.removeFromLeft(60));
        bounds.removeFromLeft(8);

        // Mute button
        speakerButton->setBounds(bounds.removeFromLeft(28).withSizeKeepingCentre(24, 24));
        bounds.removeFromLeft(8);

        // Value label above meter
        auto labelArea = bounds.removeFromTop(12);
        peakValueLabel->setBounds(labelArea.removeFromRight(40));

        // Single meter on right
        peakMeter->setBounds(bounds.removeFromRight(6));
        bounds.removeFromRight(4);
        volumeSlider->setBounds(bounds);

        // Hide components not used in horizontal mode
        dbScale_->setBounds(juce::Rectangle<int>());
        resizeHandle_->setBounds(juce::Rectangle<int>());
        headphoneIcon_->setBounds(juce::Rectangle<int>());
        cueVolumeSlider_->setBounds(juce::Rectangle<int>());

        // Clear vertical layout regions
        faderRegion_ = juce::Rectangle<int>();
        faderArea_ = juce::Rectangle<int>();
        peakMeterArea_ = juce::Rectangle<int>();
    }
}

void MasterChannelStrip::masterChannelChanged() {
    updateFromMasterState();
}

void MasterChannelStrip::updateFromMasterState() {
    const auto& master = TrackManager::getInstance().getMasterChannel();

    // Convert linear gain to fader position
    float db = gainToDb(master.volume);
    float faderPos = dbToMeterPos(db);
    volumeSlider->setValue(faderPos, juce::dontSendNotification);

    // Update mute button
    if (speakerButton) {
        speakerButton->setToggleState(master.muted, juce::dontSendNotification);
    }
}

void MasterChannelStrip::setPeakLevels(float leftPeak, float rightPeak) {
    if (peakMeter) {
        peakMeter->setLevels(leftPeak, rightPeak);
    }

    // Update peak value display (show max of both channels)
    float maxPeak = std::max(leftPeak, rightPeak);
    if (maxPeak > peakValue_) {
        peakValue_ = maxPeak;
        if (peakValueLabel) {
            float db = gainToDb(peakValue_);
            juce::String peakText;
            if (db <= MIN_DB) {
                peakText = "-inf";
            } else {
                peakText = juce::String(db, 1);
            }
            peakValueLabel->setText(peakText, juce::dontSendNotification);
        }
    }
}

}  // namespace magda
