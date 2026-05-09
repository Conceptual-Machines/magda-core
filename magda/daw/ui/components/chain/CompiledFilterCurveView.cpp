#include "CompiledFilterCurveView.hpp"

#include <algorithm>
#include <cmath>

#include "audio/plugins/compiled/CompiledFaustPluginBase.hpp"
#include "ui/themes/DarkTheme.hpp"
#include "ui/themes/FontManager.hpp"

namespace magda::daw::ui {

namespace {

constexpr float kMinFreq = 20.0f;
constexpr float kMaxFreq = 20000.0f;
constexpr float kMinDb = -30.0f;
constexpr float kMaxDb = 18.0f;
constexpr float kPlotPadX = 8.0f;
constexpr float kPlotPadY = 6.0f;

float valueForSlot(const magda::DeviceInfo& device, int slotIndex, float fallback) {
    for (const auto& param : device.parameters) {
        if (param.paramIndex == slotIndex)
            return param.currentValue;
    }
    return fallback;
}

float freqToX(float freq, float width) {
    const float norm = std::log(freq / kMinFreq) / std::log(kMaxFreq / kMinFreq);
    return juce::jlimit(0.0f, 1.0f, norm) * width;
}

float dbToY(float db, float height) {
    const float norm = (juce::jlimit(kMinDb, kMaxDb, db) - kMinDb) / (kMaxDb - kMinDb);
    return height * (1.0f - norm);
}

float xToFreq(float x, float width) {
    const float norm = width > 0.0f ? juce::jlimit(0.0f, 1.0f, x / width) : 0.0f;
    return kMinFreq * std::pow(kMaxFreq / kMinFreq, norm);
}

float linearToDb(float linear) {
    return 20.0f * std::log10(std::max(linear, 1.0e-5f));
}

}  // namespace

CompiledFilterCurveView::CompiledFilterCurveView(juce::String pluginId) {
    using namespace magda::daw::audio::compiled;

    if (pluginId.equalsIgnoreCase(MagdaLadderCompiledPlugin::xmlTypeName))
        family_ = FilterFamily::Ladder;
    else if (pluginId.equalsIgnoreCase(MagdaKorg35CompiledPlugin::xmlTypeName))
        family_ = FilterFamily::Korg35;
    else if (pluginId.equalsIgnoreCase(MagdaOberheimCompiledPlugin::xmlTypeName))
        family_ = FilterFamily::Oberheim;
    else if (pluginId.equalsIgnoreCase(MagdaSallenKeyCompiledPlugin::xmlTypeName))
        family_ = FilterFamily::SallenKey;
    else
        family_ = FilterFamily::SVF;

    setInterceptsMouseClicks(false, false);
}

void CompiledFilterCurveView::updateFromDevice(const magda::DeviceInfo& device) {
    const float cutoff = valueForSlot(device, 0, cutoffHz_);
    const float resonance = valueForSlot(device, 1, resonance_);
    const float drive = valueForSlot(device, 2, drive_);
    const int mode =
        static_cast<int>(std::round(valueForSlot(device, 3, static_cast<float>(modeIndex_))));

    if (std::abs(cutoff - cutoffHz_) > 0.01f || std::abs(resonance - resonance_) > 0.0001f ||
        std::abs(drive - drive_) > 0.0001f || mode != modeIndex_) {
        cutoffHz_ = juce::jlimit(kMinFreq, kMaxFreq, cutoff);
        resonance_ = juce::jlimit(0.0f, 1.0f, resonance);
        drive_ = juce::jlimit(0.0f, 1.0f, drive);
        modeIndex_ = mode;
        repaint();
    }
}

CompiledFilterCurveView::FilterMode CompiledFilterCurveView::modeForIndex() const {
    if (family_ == FilterFamily::Ladder)
        return FilterMode::LowPass;
    if (family_ == FilterFamily::Korg35)
        return modeIndex_ == 1 ? FilterMode::HighPass : FilterMode::LowPass;
    if (family_ == FilterFamily::SallenKey) {
        if (modeIndex_ == 1)
            return FilterMode::BandPass;
        if (modeIndex_ == 2)
            return FilterMode::HighPass;
        return FilterMode::LowPass;
    }
    if (modeIndex_ == 1)
        return FilterMode::BandPass;
    if (modeIndex_ == 2)
        return FilterMode::HighPass;
    if (modeIndex_ == 3)
        return FilterMode::Notch;
    return FilterMode::LowPass;
}

float CompiledFilterCurveView::qValue() const {
    switch (family_) {
        case FilterFamily::SVF:
            return 0.5f + resonance_ * 11.5f;
        case FilterFamily::Ladder:
            return 0.6f + resonance_ * 9.4f;
        case FilterFamily::Korg35:
        case FilterFamily::Oberheim:
        case FilterFamily::SallenKey:
            return 0.7f + resonance_ * 9.3f;
    }
    return 1.0f;
}

float CompiledFilterCurveView::responseDbAt(float frequencyHz) const {
    const float r = juce::jlimit(0.001f, 1000.0f, frequencyHz / cutoffHz_);
    const float q = qValue();
    const float denom = std::sqrt(std::pow(1.0f - r * r, 2.0f) + std::pow(r / q, 2.0f));

    float magnitude = 1.0f;
    switch (modeForIndex()) {
        case FilterMode::LowPass:
            magnitude = 1.0f / denom;
            if (family_ == FilterFamily::Ladder)
                magnitude *= magnitude;
            break;
        case FilterMode::BandPass:
            magnitude = (r / q) / denom;
            break;
        case FilterMode::HighPass:
            magnitude = (r * r) / denom;
            break;
        case FilterMode::Notch:
            magnitude = std::abs(1.0f - r * r) / denom;
            break;
    }

    const float driveTrimDb = -drive_ * 2.5f;
    return juce::jlimit(kMinDb, kMaxDb, linearToDb(magnitude) + driveTrimDb);
}

void CompiledFilterCurveView::paint(juce::Graphics& g) {
    const auto bounds = getLocalBounds();
    g.setColour(DarkTheme::getColour(DarkTheme::BACKGROUND).darker(0.06f));
    g.fillRect(bounds);

    auto plot = bounds.toFloat().reduced(kPlotPadX, kPlotPadY);
    if (plot.getWidth() < 8.0f || plot.getHeight() < 8.0f)
        return;

    g.setColour(DarkTheme::getColour(DarkTheme::BORDER).withAlpha(0.55f));
    g.drawRect(plot, 1.0f);

    auto font = FontManager::getInstance().getUIFont(7.0f);
    g.setFont(font);

    struct FreqLine {
        float freq;
        const char* label;
    };
    const FreqLine freqLines[] = {{50.0f, "50"},   {100.0f, "100"},    {500.0f, nullptr},
                                  {1000.0f, "1k"}, {5000.0f, nullptr}, {10000.0f, "10k"}};

    for (const auto& line : freqLines) {
        const float x = plot.getX() + freqToX(line.freq, plot.getWidth());
        g.setColour(DarkTheme::getColour(DarkTheme::BORDER).withAlpha(line.label ? 0.27f : 0.14f));
        g.drawVerticalLine(static_cast<int>(std::round(x)), plot.getY(), plot.getBottom());
        if (line.label != nullptr) {
            g.setColour(DarkTheme::getSecondaryTextColour().withAlpha(0.45f));
            g.drawText(line.label, static_cast<int>(x) - 14,
                       static_cast<int>(plot.getBottom()) - 11, 28, 10,
                       juce::Justification::centred);
        }
    }

    for (float db : {-24.0f, -12.0f, 0.0f, 12.0f}) {
        const float y = plot.getY() + dbToY(db, plot.getHeight());
        g.setColour(DarkTheme::getColour(DarkTheme::BORDER).withAlpha(db == 0.0f ? 0.46f : 0.16f));
        g.drawHorizontalLine(static_cast<int>(std::round(y)), plot.getX(), plot.getRight());
    }

    const int samples = juce::jmax(64, static_cast<int>(std::ceil(plot.getWidth())));
    juce::Path fillPath;
    juce::Path curvePath;

    const float zeroY = plot.getY() + dbToY(0.0f, plot.getHeight());
    for (int i = 0; i < samples; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(samples - 1);
        const float x = plot.getX() + t * plot.getWidth();
        const float freq = xToFreq(t * plot.getWidth(), plot.getWidth());
        const float y = plot.getY() + dbToY(responseDbAt(freq), plot.getHeight());

        if (i == 0) {
            curvePath.startNewSubPath(x, y);
            fillPath.startNewSubPath(x, zeroY);
            fillPath.lineTo(x, y);
        } else {
            curvePath.lineTo(x, y);
            fillPath.lineTo(x, y);
        }
    }
    fillPath.lineTo(plot.getRight(), zeroY);
    fillPath.closeSubPath();

    const auto accent = DarkTheme::getColour(DarkTheme::ACCENT_GREEN);
    g.setColour(accent.withAlpha(0.13f + drive_ * 0.08f));
    g.fillPath(fillPath);
    g.setColour(accent.withAlpha(0.9f));
    g.strokePath(curvePath, juce::PathStrokeType(1.7f, juce::PathStrokeType::curved,
                                                 juce::PathStrokeType::rounded));

    const float cutoffX = plot.getX() + freqToX(cutoffHz_, plot.getWidth());
    g.setColour(accent.withAlpha(0.45f));
    g.drawVerticalLine(static_cast<int>(std::round(cutoffX)), plot.getY(), plot.getBottom());
}

}  // namespace magda::daw::ui
