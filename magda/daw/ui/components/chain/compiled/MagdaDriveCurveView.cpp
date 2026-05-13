#include "compiled/MagdaDriveCurveView.hpp"

#include <cmath>

#include "audio/plugins/FaustParamPool.hpp"
#include "audio/plugins/FaustParamSlot.hpp"
#include "audio/plugins/FaustPlugin.hpp"
#include "ui/themes/DarkTheme.hpp"

namespace magda::daw::ui {

namespace {

constexpr int kPollHz = 30;

// Inset around the plot so the axes aren't flush with the edges.
constexpr int kPlotPadding = 6;

float dbToLinear(float dB) {
    return std::pow(10.0f, dB / 20.0f);
}

}  // namespace

MagdaDriveCurveView::MagdaDriveCurveView(magda::daw::audio::FaustPlugin& plugin) : plugin_(plugin) {
    setInterceptsMouseClicks(false, false);

    // Seed the cached values from current state so the first paint
    // draws the correct curve without waiting for a timer tick.
    lastDrive_ = readSlotValue(findSlot("Drive"));
    lastGain_ = readSlotValue(findSlot("Gain"));

    startTimerHz(kPollHz);
}

MagdaDriveCurveView::~MagdaDriveCurveView() {
    stopTimer();
}

void MagdaDriveCurveView::timerCallback() {
    const float drive = readSlotValue(findSlot("Drive"));
    const float gain = readSlotValue(findSlot("Gain"));
    if (std::abs(drive - lastDrive_) > 0.0001f || std::abs(gain - lastGain_) > 0.0001f) {
        lastDrive_ = drive;
        lastGain_ = gain;
        repaint();
    }
}

const magda::daw::audio::FaustParamSlot* MagdaDriveCurveView::findSlot(
    const juce::String& label) const {
    const auto& pool = plugin_.getPool();
    for (int i = 0; i < magda::daw::audio::FaustParamPool::kSize; ++i) {
        const auto& s = pool.slot(i);
        if (s.active && s.label.equalsIgnoreCase(label))
            return &s;
    }
    return nullptr;
}

float MagdaDriveCurveView::readSlotValue(const magda::daw::audio::FaustParamSlot* slot) {
    if (slot == nullptr)
        return 0.0f;
    if (slot->zone != nullptr) {
        // Torn-read tolerated: the audio thread may be writing to this
        // zone concurrently. Worst case a single frame draws a curve
        // a few samples behind — fine for a cosmetic preview.
        return static_cast<float>(*slot->zone);
    }
    return slot->defaultValue;
}

void MagdaDriveCurveView::paint(juce::Graphics& g) {
    const auto bounds = getLocalBounds();

    // Background — slightly inset from the FaustUI body fill so the
    // header strip's separator stays visually distinct.
    g.setColour(DarkTheme::getColour(DarkTheme::BACKGROUND));
    g.fillRect(bounds);

    auto plot = bounds.reduced(kPlotPadding).toFloat();
    if (plot.getWidth() < 4.0f || plot.getHeight() < 4.0f)
        return;

    const float midX = plot.getCentreX();
    const float midY = plot.getCentreY();
    const float halfW = plot.getWidth() * 0.5f;
    const float halfH = plot.getHeight() * 0.5f;

    // Axis grid: midline crosshair plus a 0.5-amplitude rule on each side.
    g.setColour(DarkTheme::getColour(DarkTheme::BORDER).withAlpha(0.4f));
    g.drawHorizontalLine(static_cast<int>(std::round(midY)), plot.getX(), plot.getRight());
    g.drawVerticalLine(static_cast<int>(std::round(midX)), plot.getY(), plot.getBottom());

    g.setColour(DarkTheme::getColour(DarkTheme::BORDER).withAlpha(0.18f));
    for (float t : {-0.5f, 0.5f}) {
        const float x = midX + t * halfW;
        const float y = midY - t * halfH;
        g.drawVerticalLine(static_cast<int>(std::round(x)), plot.getY(), plot.getBottom());
        g.drawHorizontalLine(static_cast<int>(std::round(y)), plot.getX(), plot.getRight());
    }

    // Frame.
    g.setColour(DarkTheme::getColour(DarkTheme::BORDER).withAlpha(0.6f));
    g.drawRect(plot, 1.0f);

    // Sample the curve at one point per pixel.
    const int numSamples = juce::jmax(2, static_cast<int>(std::ceil(plot.getWidth())));
    juce::Path curve;
    const float driveLin = dbToLinear(lastDrive_);
    const float gainLin = dbToLinear(lastGain_);

    for (int i = 0; i < numSamples; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(numSamples - 1);
        const float xInput = -1.0f + 2.0f * t;
        const float yOutput = juce::jlimit(-1.0f, 1.0f, std::tanh(driveLin * xInput) * gainLin);

        const float px = plot.getX() + t * plot.getWidth();
        const float py = midY - yOutput * halfH;
        if (i == 0)
            curve.startNewSubPath(px, py);
        else
            curve.lineTo(px, py);
    }

    g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_GREEN));
    g.strokePath(curve, juce::PathStrokeType(1.6f, juce::PathStrokeType::curved,
                                             juce::PathStrokeType::rounded));
}

void registerBuiltInFaustCustomViews() {
    auto& registry = FaustCustomUIRegistry::getInstance();

    registry.registerView(FaustCustomViewKind::MagdaDrive,
                          [](magda::daw::audio::FaustPlugin& plugin) {
                              return std::make_unique<MagdaDriveCurveView>(plugin);
                          });
}

}  // namespace magda::daw::ui
