#include "EqualiserUI.hpp"

#include "ui/themes/DarkTheme.hpp"
#include "ui/themes/FontManager.hpp"

namespace magda::daw::ui {

// =============================================================================
// Helper
// =============================================================================

static void setupLabelStatic(juce::Label& label, const juce::String& text,
                             juce::Component* parent) {
    label.setText(text, juce::dontSendNotification);
    label.setFont(FontManager::getInstance().getUIFont(9.0f));
    label.setColour(juce::Label::textColourId, DarkTheme::getSecondaryTextColour());
    label.setJustificationType(juce::Justification::centred);
    parent->addAndMakeVisible(label);
}

// =============================================================================
// EqualiserUI
// =============================================================================

EqualiserUI::EqualiserUI() : curveDisplay_(*this) {
    // Band colours
    bandColours_[0] = juce::Colour(0xFFE06C75);  // Red - Low Shelf
    bandColours_[1] = juce::Colour(0xFF61AFEF);  // Blue - Mid 1
    bandColours_[2] = juce::Colour(0xFF98C379);  // Green - Mid 2
    bandColours_[3] = juce::Colour(0xFFE5C07B);  // Yellow - High Shelf

    addAndMakeVisible(curveDisplay_);

    const juce::String bandNames[] = {"LOW", "MID 1", "MID 2", "HIGH"};
    for (int i = 0; i < kNumBands; ++i)
        setupBandControls(i, bandNames[i]);

    // Phase invert toggle (virtual param index 12, after 4 bands x 3 params)
    setupLabelStatic(phaseInvertLabel_, "PHASE", this);
    addAndMakeVisible(phaseInvertSlider_);
    phaseInvertSlider_.setRange(0.0, 1.0, 1.0);
    phaseInvertSlider_.setValueFormatter(
        [](double value) -> juce::String { return value >= 0.5 ? "INV" : "NRM"; });
    phaseInvertSlider_.setValueParser([](const juce::String& text) -> double {
        return text.trim().toLowerCase() == "inv" ? 1.0 : 0.0;
    });
    phaseInvertSlider_.onValueChanged = [this](double value) {
        if (onParameterChanged)
            onParameterChanged(12, static_cast<float>(value));
    };

    // Repaint curve at ~15 Hz
    startTimerHz(15);
}

void EqualiserUI::setupBandControls(int bandIndex, const juce::String& name) {
    auto& b = bands_[bandIndex];

    // Name label
    b.nameLabel.setText(name, juce::dontSendNotification);
    b.nameLabel.setFont(FontManager::getInstance().getUIFontBold(9.0f));
    b.nameLabel.setColour(juce::Label::textColourId, bandColours_[bandIndex]);
    b.nameLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(b.nameLabel);

    // Freq slider (log-scaled drag for perceptual consistency)
    b.freqSlider.setRange(20.0, 20000.0, 1.0);
    b.freqSlider.setSkewForCentre(1000.0);
    b.freqSlider.setValueFormatter([](double val) -> juce::String {
        if (val >= 1000.0)
            return juce::String(val / 1000.0, 1) + "k";
        return juce::String(static_cast<int>(val));
    });
    b.freqSlider.setValueParser([](const juce::String& text) -> double {
        auto t = text.trim().toLowerCase();
        if (t.endsWithChar('k'))
            return t.dropLastCharacters(1).getDoubleValue() * 1000.0;
        return t.getDoubleValue();
    });
    b.freqSlider.onValueChanged = [this, bandIndex](double value) {
        bandFreqs_[bandIndex] = static_cast<float>(value);
        int paramIndex = bandIndex * kBandParamCount;  // freq is first in each band
        if (onParameterChanged)
            onParameterChanged(paramIndex, static_cast<float>(value));
    };
    addAndMakeVisible(b.freqSlider);
    setupLabelStatic(b.freqLabel, "FREQ", this);

    // Gain slider
    b.gainSlider.setRange(-20.0, 20.0, 0.1);
    b.gainSlider.onValueChanged = [this, bandIndex](double value) {
        bandGains_[bandIndex] = static_cast<float>(value);
        int paramIndex = bandIndex * kBandParamCount + 1;
        if (onParameterChanged)
            onParameterChanged(paramIndex, static_cast<float>(value));
    };
    addAndMakeVisible(b.gainSlider);
    setupLabelStatic(b.gainLabel, "GAIN", this);

    // Q slider
    b.qSlider.setRange(0.1, 4.0, 0.01);
    b.qSlider.onValueChanged = [this, bandIndex](double value) {
        int paramIndex = bandIndex * kBandParamCount + 2;
        if (onParameterChanged)
            onParameterChanged(paramIndex, static_cast<float>(value));
    };
    addAndMakeVisible(b.qSlider);
    setupLabelStatic(b.qLabel, "Q", this);
}

void EqualiserUI::setupLabel(juce::Label& label, const juce::String& text) {
    setupLabelStatic(label, text, this);
}

void EqualiserUI::updateFromParameters(const std::vector<magda::ParameterInfo>& params) {
    // Parameter layout: band0(freq,gain,Q), band1(freq,gain,Q), band2(freq,gain,Q),
    // band3(freq,gain,Q)
    for (int i = 0; i < kNumBands; ++i) {
        int base = i * kBandParamCount;
        if (base + 2 < static_cast<int>(params.size())) {
            float freq = params[static_cast<size_t>(base)].currentValue;
            float gain = params[static_cast<size_t>(base + 1)].currentValue;
            float q = params[static_cast<size_t>(base + 2)].currentValue;

            bands_[i].freqSlider.setValue(freq, juce::dontSendNotification);
            bands_[i].gainSlider.setValue(gain, juce::dontSendNotification);
            bands_[i].qSlider.setValue(q, juce::dontSendNotification);

            bandFreqs_[i] = freq;
            bandGains_[i] = gain;
        }
    }
    // Phase invert is at index 12 (after 4 bands x 3 params)
    if (params.size() > 12)
        phaseInvertSlider_.setValue(params[12].currentValue, juce::dontSendNotification);
}

void EqualiserUI::paint(juce::Graphics& g) {
    g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
    g.drawRect(getLocalBounds(), 1);
    g.setColour(DarkTheme::getColour(DarkTheme::BACKGROUND).brighter(0.05f));
    g.fillRect(getLocalBounds().reduced(1));
}

void EqualiserUI::resized() {
    auto area = getLocalBounds().reduced(4);

    // Top: curve display (~60% of height)
    int curveHeight = juce::jmax(60, static_cast<int>(area.getHeight() * 0.6f));
    curveDisplay_.setBounds(area.removeFromTop(curveHeight));

    area.removeFromTop(4);  // spacing

    // Bottom: 4 band columns + phase invert column
    int colWidth = area.getWidth() / (kNumBands + 1);
    int rowHeight = 16;

    for (int i = 0; i < kNumBands; ++i) {
        auto col = area.removeFromLeft(colWidth).reduced(2, 0);

        bands_[i].nameLabel.setBounds(col.removeFromTop(rowHeight));
        bands_[i].freqLabel.setBounds(col.removeFromTop(rowHeight - 4));
        bands_[i].freqSlider.setBounds(col.removeFromTop(rowHeight));
        bands_[i].gainLabel.setBounds(col.removeFromTop(rowHeight - 4));
        bands_[i].gainSlider.setBounds(col.removeFromTop(rowHeight));
        bands_[i].qLabel.setBounds(col.removeFromTop(rowHeight - 4));
        bands_[i].qSlider.setBounds(col.removeFromTop(rowHeight));
    }

    // Phase invert column
    auto phaseCol = area.removeFromLeft(colWidth).reduced(2, 0);
    phaseCol.removeFromTop(rowHeight);  // Skip name row to align with controls
    phaseInvertLabel_.setBounds(phaseCol.removeFromTop(rowHeight - 4));
    phaseInvertSlider_.setBounds(phaseCol.removeFromTop(rowHeight));
}

void EqualiserUI::timerCallback() {
    curveDisplay_.repaint();
}

std::vector<LinkableTextSlider*> EqualiserUI::getLinkableSliders() {
    std::vector<LinkableTextSlider*> sliders;
    // Parameter layout: band0(freq,gain,Q), band1(freq,gain,Q), ...
    for (int i = 0; i < kNumBands; ++i) {
        sliders.push_back(&bands_[i].freqSlider);
        sliders.push_back(&bands_[i].gainSlider);
        sliders.push_back(&bands_[i].qSlider);
    }
    // Phase invert at index 12
    sliders.push_back(&phaseInvertSlider_);
    return sliders;
}

// =============================================================================
// CurveDisplay
// =============================================================================

EqualiserUI::CurveDisplay::CurveDisplay(EqualiserUI& owner) : owner_(owner) {
    setMouseCursor(juce::MouseCursor::NormalCursor);
}

float EqualiserUI::CurveDisplay::freqToX(float freq, float width) const {
    // Log scale: 20Hz to 20kHz
    float logMin = std::log10(20.0f);
    float logMax = std::log10(20000.0f);
    float logFreq = std::log10(juce::jlimit(20.0f, 20000.0f, freq));
    return (logFreq - logMin) / (logMax - logMin) * width;
}

float EqualiserUI::CurveDisplay::dbToY(float db, float height) const {
    // -20dB at bottom, +20dB at top
    float normalized = (db + 20.0f) / 40.0f;  // 0..1
    return height * (1.0f - juce::jlimit(0.0f, 1.0f, normalized));
}

float EqualiserUI::CurveDisplay::xToFreq(float x, float width) const {
    float logMin = std::log10(20.0f);
    float logMax = std::log10(20000.0f);
    float logFreq = logMin + (x / width) * (logMax - logMin);
    return std::pow(10.0f, logFreq);
}

float EqualiserUI::CurveDisplay::yToDb(float y, float height) const {
    float normalized = 1.0f - (y / height);
    return normalized * 40.0f - 20.0f;  // map 0..1 to -20..+20
}

int EqualiserUI::CurveDisplay::findBandAt(float x, float y) const {
    float w = static_cast<float>(getWidth());
    float h = static_cast<float>(getHeight());
    constexpr float hitRadius = 12.0f;

    for (int i = 0; i < kNumBands; ++i) {
        float dotX = freqToX(owner_.bandFreqs_[i], w);
        float dotY = dbToY(owner_.bandGains_[i], h);
        float dx = x - dotX;
        float dy = y - dotY;
        if (dx * dx + dy * dy <= hitRadius * hitRadius)
            return i;
    }
    return -1;
}

void EqualiserUI::CurveDisplay::mouseDown(const juce::MouseEvent& e) {
    dragBand_ = findBandAt(static_cast<float>(e.x), static_cast<float>(e.y));
    if (dragBand_ >= 0)
        setMouseCursor(juce::MouseCursor::DraggingHandCursor);
}

void EqualiserUI::CurveDisplay::mouseDrag(const juce::MouseEvent& e) {
    if (dragBand_ < 0)
        return;

    float w = static_cast<float>(getWidth());
    float h = static_cast<float>(getHeight());
    float freq = juce::jlimit(20.0f, 20000.0f, xToFreq(static_cast<float>(e.x), w));
    float gain = juce::jlimit(-20.0f, 20.0f, yToDb(static_cast<float>(e.y), h));

    owner_.bandFreqs_[dragBand_] = freq;
    owner_.bandGains_[dragBand_] = gain;

    // Update the sliders (dontSendNotification to avoid double-firing)
    owner_.bands_[dragBand_].freqSlider.setValue(freq, juce::dontSendNotification);
    owner_.bands_[dragBand_].gainSlider.setValue(gain, juce::dontSendNotification);

    // Send parameter changes to the plugin
    if (owner_.onParameterChanged) {
        owner_.onParameterChanged(dragBand_ * kBandParamCount, freq);
        owner_.onParameterChanged(dragBand_ * kBandParamCount + 1, gain);
    }

    repaint();
}

void EqualiserUI::CurveDisplay::mouseUp(const juce::MouseEvent&) {
    dragBand_ = -1;
    setMouseCursor(juce::MouseCursor::NormalCursor);
}

void EqualiserUI::CurveDisplay::mouseMove(const juce::MouseEvent& e) {
    int band = findBandAt(static_cast<float>(e.x), static_cast<float>(e.y));
    if (band >= 0)
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
    else
        setMouseCursor(juce::MouseCursor::NormalCursor);
}

void EqualiserUI::CurveDisplay::paint(juce::Graphics& g) {
    auto bounds = getLocalBounds().toFloat();
    float w = bounds.getWidth();
    float h = bounds.getHeight();

    if (w < 1.0f || h < 1.0f)
        return;

    // Background
    g.setColour(DarkTheme::getColour(DarkTheme::BACKGROUND).darker(0.1f));
    g.fillRoundedRectangle(bounds, 2.0f);

    auto gridFont = FontManager::getInstance().getUIFont(7.0f);
    auto gridLineColour = DarkTheme::getColour(DarkTheme::BORDER);
    auto gridTextColour = DarkTheme::getSecondaryTextColour();

    // Frequency grid — major lines with labels, minor lines without
    struct FreqGrid {
        float freq;
        const char* label;  // nullptr = minor (no label)
    };
    const FreqGrid freqGridLines[] = {
        {30.0f, nullptr}, {50.0f, "50"},   {100.0f, "100"}, {200.0f, "200"},   {500.0f, "500"},
        {1000.0f, "1k"},  {2000.0f, "2k"}, {5000.0f, "5k"}, {10000.0f, "10k"}, {20000.0f, nullptr},
    };

    for (const auto& fg : freqGridLines) {
        float x = freqToX(fg.freq, w);
        bool isMajor = (fg.label != nullptr);
        g.setColour(gridLineColour.withAlpha(isMajor ? 0.3f : 0.12f));
        g.drawVerticalLine(static_cast<int>(x), 0.0f, h);
        if (fg.label) {
            g.setFont(gridFont);
            g.setColour(gridTextColour.withAlpha(0.5f));
            g.drawText(fg.label, static_cast<int>(x) - 15, static_cast<int>(h) - 11, 30, 11,
                       juce::Justification::centred);
        }
    }

    // dB grid — horizontal lines with labels
    const float gridDbs[] = {-20.0f, -10.0f, 0.0f, 10.0f, 20.0f};
    for (float db : gridDbs) {
        float y = dbToY(db, h);
        bool isZero = (db == 0.0f);
        g.setColour(gridLineColour.withAlpha(isZero ? 0.5f : 0.15f));
        g.drawHorizontalLine(static_cast<int>(y), 0.0f, w);
        // Label (skip ±20 to avoid edge clutter)
        if (db != -20.0f && db != 20.0f) {
            g.setFont(gridFont);
            g.setColour(gridTextColour.withAlpha(0.4f));
            juce::String dbText = (db > 0 ? "+" : "") + juce::String(static_cast<int>(db));
            g.drawText(dbText, 2, static_cast<int>(y) - 6, 22, 12,
                       juce::Justification::centredLeft);
        }
    }

    // Response curve
    if (owner_.getDBGainAtFrequency) {
        float zeroY = dbToY(0.0f, h);

        // Sample per pixel
        int numSamples = juce::jmax(100, static_cast<int>(w));
        std::vector<float> dbValues(static_cast<size_t>(numSamples));
        for (int i = 0; i < numSamples; ++i) {
            float x = (static_cast<float>(i) / static_cast<float>(numSamples - 1)) * w;
            float freq = xToFreq(x, w);
            dbValues[static_cast<size_t>(i)] = owner_.getDBGainAtFrequency(freq);
        }

        // Gaussian smoothing (3 passes of box blur ≈ Gaussian)
        // Use adaptive radius: wider at low frequencies (left) where log scale is compressed
        auto smoothed = dbValues;
        for (int pass = 0; pass < 3; ++pass) {
            auto prev = smoothed;
            for (int i = 0; i < numSamples; ++i) {
                float t = static_cast<float>(i) / static_cast<float>(numSamples - 1);
                int radius = static_cast<int>(3.0f + (1.0f - t) * 9.0f);  // 12 at left, 3 at right
                float sum = 0.0f;
                int count = 0;
                for (int k = juce::jmax(0, i - radius); k <= juce::jmin(numSamples - 1, i + radius);
                     ++k) {
                    sum += prev[static_cast<size_t>(k)];
                    ++count;
                }
                smoothed[static_cast<size_t>(i)] = sum / static_cast<float>(count);
            }
        }

        // Build path from smoothed data
        juce::Path curvePath;
        juce::Path fillPath;
        for (int i = 0; i < numSamples; ++i) {
            float x = (static_cast<float>(i) / static_cast<float>(numSamples - 1)) * w;
            float y = dbToY(smoothed[static_cast<size_t>(i)], h);
            if (i == 0) {
                curvePath.startNewSubPath(x, y);
                fillPath.startNewSubPath(x, zeroY);
                fillPath.lineTo(x, y);
            } else {
                curvePath.lineTo(x, y);
                fillPath.lineTo(x, y);
            }
        }

        // Close fill path
        fillPath.lineTo(w, zeroY);
        fillPath.closeSubPath();

        // Gradient fill under curve
        auto accentColour = DarkTheme::getColour(DarkTheme::ACCENT_BLUE);
        g.setGradientFill(juce::ColourGradient(accentColour.withAlpha(0.15f), 0.0f, 0.0f,
                                               accentColour.withAlpha(0.02f), 0.0f, h, false));
        g.fillPath(fillPath);

        // Curve stroke
        g.setColour(accentColour.withAlpha(0.85f));
        g.strokePath(curvePath, juce::PathStrokeType(2.0f));
    }

    // Band dots — glow ring + filled centre
    for (int i = 0; i < kNumBands; ++i) {
        float x = freqToX(owner_.bandFreqs_[i], w);
        float y = dbToY(owner_.bandGains_[i], h);

        // Outer glow
        g.setColour(owner_.bandColours_[i].withAlpha(0.18f));
        g.fillEllipse(x - 8.0f, y - 8.0f, 16.0f, 16.0f);

        // Ring
        g.setColour(owner_.bandColours_[i].withAlpha(0.6f));
        g.drawEllipse(x - 6.0f, y - 6.0f, 12.0f, 12.0f, 1.5f);

        // Filled centre
        g.setColour(owner_.bandColours_[i]);
        g.fillEllipse(x - 4.0f, y - 4.0f, 8.0f, 8.0f);
    }
}

}  // namespace magda::daw::ui
