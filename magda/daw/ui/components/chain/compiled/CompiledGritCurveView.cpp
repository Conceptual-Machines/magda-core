#include "compiled/CompiledGritCurveView.hpp"

#include <algorithm>
#include <cmath>

#include "audio/plugins/compiled/MagdaGritCompiledPlugin.hpp"
#include "ui/themes/DarkTheme.hpp"

namespace magda::daw::ui {

namespace {

constexpr float kPlotPadX = 8.0f;
constexpr float kPlotPadY = 8.0f;
constexpr int kPollMs = 33;  // ~30 Hz cosmetic update
constexpr float kMinFreq = 20.0f;
constexpr float kMaxFreq = 20000.0f;

float valueForSlot(const magda::DeviceInfo& device, int slotIndex, float fallback) {
    for (const auto& param : device.parameters)
        if (param.paramIndex == slotIndex)
            return param.currentValue;
    return fallback;
}

// Log-frequency mapping: kMinFreq → 0, kMaxFreq → 1. Symmetrical inverse
// is used to step across the plot when sampling the response curve.
float freqToNorm(float hz) {
    const float clamped = juce::jlimit(kMinFreq, kMaxFreq, hz);
    return std::log(clamped / kMinFreq) / std::log(kMaxFreq / kMinFreq);
}

float normToFreq(float n) {
    const float clamped = juce::jlimit(0.0f, 1.0f, n);
    return kMinFreq * std::exp(clamped * std::log(kMaxFreq / kMinFreq));
}

// 2nd-order resonant bandpass magnitude: H(f) = 1 / sqrt(1 + Q² · ((f/fc) − (fc/f))²).
// Peak = 1 at f = fc; falls off symmetrically in log frequency, sharper as Q rises.
float bpfMagnitude(float f, float fc, float q) {
    const float fSafe = std::max(f, 1.0e-3f);
    const float fcSafe = std::max(fc, 1.0e-3f);
    const float ratio = fSafe / fcSafe - fcSafe / fSafe;
    const float denom = std::sqrt(1.0f + q * q * ratio * ratio);
    return 1.0f / denom;
}

}  // namespace

CompiledGritCurveView::CompiledGritCurveView(juce::String /*pluginId*/) {
    setInterceptsMouseClicks(false, false);
    startTimer(kPollMs);
}

void CompiledGritCurveView::setCompiledPlugin(
    magda::daw::audio::compiled::MagdaGritCompiledPlugin* plugin) {
    compiledPlugin_ = plugin;
}

void CompiledGritCurveView::updateFromDevice(const magda::DeviceInfo& device) {
    deviceSnapshot_ = device;
    resampleFromPlugin();
    repaint();
}

void CompiledGritCurveView::timerCallback() {
    using Grit = magda::daw::audio::compiled::MagdaGritCompiledPlugin;

    float freq = frequencyHz_;
    float width = width_;
    float amount = amount_;
    int mode = modeIndex_;

    if (compiledPlugin_ != nullptr) {
        if (auto* p = compiledPlugin_->getSlotParameter(Grit::kFrequencySlot))
            freq = compiledPlugin_->nativeValueToDisplayValue(Grit::kFrequencySlot,
                                                              p->getCurrentValue());
        if (auto* p = compiledPlugin_->getSlotParameter(Grit::kWidthSlot))
            width =
                compiledPlugin_->nativeValueToDisplayValue(Grit::kWidthSlot, p->getCurrentValue());
        if (auto* p = compiledPlugin_->getSlotParameter(Grit::kAmountSlot))
            amount =
                compiledPlugin_->nativeValueToDisplayValue(Grit::kAmountSlot, p->getCurrentValue());
        if (auto* p = compiledPlugin_->getSlotParameter(Grit::kModeSlot)) {
            mode = static_cast<int>(std::round(
                compiledPlugin_->nativeValueToDisplayValue(Grit::kModeSlot, p->getCurrentValue())));
        }
    } else {
        freq = valueForSlot(deviceSnapshot_, Grit::kFrequencySlot, freq);
        width = valueForSlot(deviceSnapshot_, Grit::kWidthSlot, width);
        amount = valueForSlot(deviceSnapshot_, Grit::kAmountSlot, amount);
        mode = static_cast<int>(
            std::round(valueForSlot(deviceSnapshot_, Grit::kModeSlot, static_cast<float>(mode))));
    }

    const bool moved = std::fabs(freq - frequencyHz_) > 0.5f ||
                       std::fabs(width - width_) > 0.001f || std::fabs(amount - amount_) > 0.001f ||
                       mode != modeIndex_;
    if (moved) {
        frequencyHz_ = freq;
        width_ = width;
        amount_ = amount;
        modeIndex_ = mode;
        repaint();
    }
}

void CompiledGritCurveView::resampleFromPlugin() {
    using Grit = magda::daw::audio::compiled::MagdaGritCompiledPlugin;
    frequencyHz_ = valueForSlot(deviceSnapshot_, Grit::kFrequencySlot, frequencyHz_);
    width_ = valueForSlot(deviceSnapshot_, Grit::kWidthSlot, width_);
    amount_ = valueForSlot(deviceSnapshot_, Grit::kAmountSlot, amount_);
    modeIndex_ = static_cast<int>(
        std::round(valueForSlot(deviceSnapshot_, Grit::kModeSlot, static_cast<float>(modeIndex_))));
}

void CompiledGritCurveView::paint(juce::Graphics& g) {
    const auto bounds = getLocalBounds();
    g.setColour(DarkTheme::getColour(DarkTheme::BACKGROUND).darker(0.06f));
    g.fillRect(bounds);

    auto plot = bounds.toFloat().reduced(kPlotPadX, kPlotPadY);
    if (plot.getWidth() < 8.0f || plot.getHeight() < 8.0f)
        return;

    g.setColour(DarkTheme::getColour(DarkTheme::BORDER).withAlpha(0.55f));
    g.drawRect(plot, 1.0f);

    juce::Graphics::ScopedSaveState clipGuard(g);
    g.reduceClipRegion(plot.toNearestInt());

    // Decade grid lines (100 Hz / 1 kHz / 10 kHz) — keeps the log axis
    // legible without a full ruler.
    g.setColour(DarkTheme::getColour(DarkTheme::BORDER).withAlpha(0.22f));
    for (float decade : {100.0f, 1000.0f, 10000.0f}) {
        const float n = freqToNorm(decade);
        const float x = plot.getX() + n * plot.getWidth();
        g.drawVerticalLine(static_cast<int>(std::round(x)), plot.getY(), plot.getBottom());
    }

    const auto carrierColour = DarkTheme::getColour(DarkTheme::ACCENT_BLUE);
    const float amount01 = juce::jlimit(0.0f, 1.0f, amount_);
    const float plotBottom = plot.getBottom();
    const float plotTop = plot.getY();
    const float maxBarH = plot.getHeight() * 0.92f;

    // Sine mode = single tonal carrier. Draw a tall spike at the freq;
    // Width has no audible effect here so we don't draw a halo around it.
    if (modeIndex_ == 2) {
        const float n = freqToNorm(frequencyHz_);
        const float x = plot.getX() + n * plot.getWidth();
        const float h = maxBarH * std::max(0.05f, amount01);
        constexpr float kSpikeW = 3.0f;
        g.setColour(carrierColour);
        g.fillRect(juce::Rectangle<float>(x - kSpikeW * 0.5f, plotBottom - h, kSpikeW, h));
        return;
    }

    // Noise / Wide Noise: sample the BPF magnitude response across the
    // log-frequency axis and trace a closed path under the curve.
    // Width 0..1 → Q 0.5..20 (matches the DSP).
    const float q = 0.5f + juce::jlimit(0.0f, 1.0f, width_) * 19.5f;
    const float scale = maxBarH * std::max(0.05f, amount01);

    constexpr int kPoints = 96;
    juce::Path curve;
    curve.startNewSubPath(plot.getX(), plotBottom);
    for (int i = 0; i <= kPoints; ++i) {
        const float n = static_cast<float>(i) / static_cast<float>(kPoints);
        const float f = normToFreq(n);
        const float mag = bpfMagnitude(f, frequencyHz_, q);
        const float x = plot.getX() + n * plot.getWidth();
        const float y = juce::jmax(plotTop, plotBottom - mag * scale);
        curve.lineTo(x, y);
    }
    curve.lineTo(plot.getRight(), plotBottom);
    curve.closeSubPath();

    g.setColour(carrierColour);
    g.fillPath(curve);
}

const CompiledPresentationSpec& getMagdaGritPresentation() {
    static const CompiledPresentationSpec kSpec{
        .pluginId = magda::daw::audio::compiled::MagdaGritCompiledPlugin::xmlTypeName,
        .layoutCellCount = 4,
        .layoutCellsPerRow = 4,
        .createPanel = [](juce::String pluginId) -> std::unique_ptr<CompiledDevicePanel> {
            return std::make_unique<CompiledGritCurveView>(pluginId);
        },
        .suppressLegacyUis = {},
    };
    return kSpec;
}

void CompiledGritCurveView::bindPlugin(te::Plugin* plugin) {
    setCompiledPlugin(dynamic_cast<magda::daw::audio::compiled::MagdaGritCompiledPlugin*>(plugin));
}

}  // namespace magda::daw::ui
