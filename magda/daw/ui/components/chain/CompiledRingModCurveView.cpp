#include "CompiledRingModCurveView.hpp"

#include <algorithm>
#include <cmath>

#include "audio/plugins/compiled/MagdaRingModCompiledPlugin.hpp"
#include "ui/themes/DarkTheme.hpp"

namespace magda::daw::ui {

namespace {

constexpr float kPlotPadX = 8.0f;
constexpr float kPlotPadY = 8.0f;
constexpr int kPollMs = 33;
constexpr float kMinFreqHz = 10.0f;
constexpr float kMaxFreqHz = 20000.0f;
constexpr int kInsetCycles = 2;
constexpr int kInsetSamples = 96;

float valueForSlot(const magda::DeviceInfo& device, int slotIndex, float fallback) {
    for (const auto& param : device.parameters)
        if (param.paramIndex == slotIndex)
            return param.currentValue;
    return fallback;
}

float freqToX(float hz, const juce::Rectangle<float>& plot) {
    const float ratio = std::log(hz / kMinFreqHz) / std::log(kMaxFreqHz / kMinFreqHz);
    return plot.getX() + juce::jlimit(0.0f, 1.0f, ratio) * plot.getWidth();
}

// Bipolar carrier sample given a phase in [0,1). Mirrors the DSP's
// carrierAt(off) selectn over Sine / Triangle / Square.
float carrierSample(int shape, double phase) {
    const float p = static_cast<float>(phase - std::floor(phase));
    switch (shape) {
        case 1:
            return 4.0f * std::abs(p - 0.5f) - 1.0f;
        case 2:
            return p < 0.5f ? 1.0f : -1.0f;
        default:
            return std::sin(static_cast<double>(p) * juce::MathConstants<double>::twoPi);
    }
}

}  // namespace

CompiledRingModCurveView::CompiledRingModCurveView(juce::String /*pluginId*/) {
    setInterceptsMouseClicks(false, false);
    lastTickMs_ = juce::Time::getMillisecondCounter();
    startTimer(kPollMs);
}

void CompiledRingModCurveView::setCompiledPlugin(
    magda::daw::audio::compiled::MagdaRingModCompiledPlugin* plugin) {
    compiledPlugin_ = plugin;
}

void CompiledRingModCurveView::bindPlugin(te::Plugin* plugin) {
    setCompiledPlugin(
        dynamic_cast<magda::daw::audio::compiled::MagdaRingModCompiledPlugin*>(plugin));
}

void CompiledRingModCurveView::updateFromDevice(const magda::DeviceInfo& device) {
    deviceSnapshot_ = device;
    resampleFromPlugin();
    repaint();
}

float CompiledRingModCurveView::effectiveFreqHz() const {
    if (sync_) {
        const float divisor = std::max(0.001f, division_);
        return std::max(0.01f, bpm_ / (60.0f * divisor));
    }
    return std::max(0.01f, freqHz_);
}

void CompiledRingModCurveView::timerCallback() {
    using RM = magda::daw::audio::compiled::MagdaRingModCompiledPlugin;

    const auto now = juce::Time::getMillisecondCounter();
    const auto elapsedMs = now - lastTickMs_;
    lastTickMs_ = now;

    auto readPluginSlot = [this](int slot, float fallback) {
        if (compiledPlugin_ == nullptr)
            return fallback;
        if (auto* p = compiledPlugin_->getSlotParameter(slot))
            return compiledPlugin_->nativeValueToDisplayValue(slot, p->getCurrentValue());
        return fallback;
    };

    bool sync = sync_;
    float freqHz = freqHz_;
    float division = division_;
    int shape = shape_;
    float mix = mix_;
    float width = width_;
    float bpm = bpm_;

    if (compiledPlugin_ != nullptr) {
        sync = readPluginSlot(RM::kSyncSlot, sync ? 1.0f : 0.0f) >= 0.5f;
        freqHz = readPluginSlot(RM::kFrequencySlot, freqHz);
        shape = static_cast<int>(std::round(readPluginSlot(RM::kShapeSlot, shape)));
        mix = readPluginSlot(RM::kMixSlot, mix);
        width = readPluginSlot(RM::kWidthSlot, width);
        if (auto* p = compiledPlugin_->getSlotParameter(RM::kDivisionSlot)) {
            const float norm = p->getCurrentValue();
            const auto& info = compiledPlugin_->getSlotInfo(RM::kDivisionSlot);
            const int count = static_cast<int>(info.choices.size());
            const int idx =
                count > 1 ? juce::jlimit(
                                0, count - 1,
                                static_cast<int>(std::round(norm * static_cast<float>(count - 1))))
                          : 0;
            division = compiledPlugin_->divisionFaustValueForIndex(idx);
        }
        bpm = compiledPlugin_->currentBpm();
    } else {
        sync = valueForSlot(deviceSnapshot_, RM::kSyncSlot, sync ? 1.0f : 0.0f) >= 0.5f;
        freqHz = valueForSlot(deviceSnapshot_, RM::kFrequencySlot, freqHz);
        shape = static_cast<int>(
            std::round(valueForSlot(deviceSnapshot_, RM::kShapeSlot, static_cast<float>(shape))));
        mix = valueForSlot(deviceSnapshot_, RM::kMixSlot, mix);
        width = valueForSlot(deviceSnapshot_, RM::kWidthSlot, width);
    }

    sync_ = sync;
    freqHz_ = freqHz;
    division_ = std::max(0.001f, division);
    shape_ = juce::jlimit(0, 2, shape);
    mix_ = juce::jlimit(0.0f, 1.0f, mix);
    width_ = juce::jlimit(0.0f, 1.0f, width);
    bpm_ = std::max(20.0f, bpm);

    // Inset phase advances with real time but is clamped so high-rate
    // carriers don't strobe — at any rate above ~20 Hz we cap the visible
    // sweep, otherwise the inset waveform just blurs.
    const float visualHz = std::min(20.0f, effectiveFreqHz());
    const double dt = static_cast<double>(elapsedMs) * 0.001;
    phase_ += dt * static_cast<double>(visualHz);
    phase_ -= std::floor(phase_);

    repaint();
}

void CompiledRingModCurveView::resampleFromPlugin() {
    using RM = magda::daw::audio::compiled::MagdaRingModCompiledPlugin;
    sync_ = valueForSlot(deviceSnapshot_, RM::kSyncSlot, sync_ ? 1.0f : 0.0f) >= 0.5f;
    freqHz_ = valueForSlot(deviceSnapshot_, RM::kFrequencySlot, freqHz_);
    shape_ = juce::jlimit(0, 2,
                          static_cast<int>(std::round(valueForSlot(deviceSnapshot_, RM::kShapeSlot,
                                                                   static_cast<float>(shape_)))));
    mix_ = juce::jlimit(0.0f, 1.0f, valueForSlot(deviceSnapshot_, RM::kMixSlot, mix_));
    width_ = juce::jlimit(0.0f, 1.0f, valueForSlot(deviceSnapshot_, RM::kWidthSlot, width_));
}

void CompiledRingModCurveView::paint(juce::Graphics& g) {
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

    // Audible-band shading (~20 Hz – 20 kHz). Outside this range the carrier
    // produces sub-audio tremolo / inaudible top-octave content.
    const auto accent = DarkTheme::getColour(DarkTheme::ACCENT_PURPLE);
    {
        const float audibleLeft = freqToX(20.0f, plot);
        const float audibleRight = freqToX(20000.0f, plot);
        g.setColour(accent.withAlpha(0.05f));
        g.fillRect(juce::Rectangle<float>(audibleLeft, plot.getY(), audibleRight - audibleLeft,
                                          plot.getHeight()));
    }

    // Decade grid lines.
    g.setColour(DarkTheme::getColour(DarkTheme::BORDER).withAlpha(0.20f));
    for (float decade : {100.0f, 1000.0f, 10000.0f}) {
        const float x = freqToX(decade, plot);
        g.drawVerticalLine(static_cast<int>(std::round(x)), plot.getY(), plot.getBottom());
    }

    // Carrier marker — thick vertical line at the live frequency.
    const float carrierX = freqToX(effectiveFreqHz(), plot);
    g.setColour(accent.withAlpha(0.85f));
    g.fillRect(juce::Rectangle<float>(carrierX - 1.0f, plot.getY(), 2.0f, plot.getHeight()));

    // Top-left frequency readout.
    const float freqDisp = effectiveFreqHz();
    const juce::String freqLabel = freqDisp >= 1000.0f
                                       ? juce::String(freqDisp / 1000.0f, 2) + " kHz"
                                       : juce::String(freqDisp, freqDisp >= 100.0f ? 0 : 1) + " Hz";
    g.setFont(11.0f);
    g.setColour(DarkTheme::getColour(DarkTheme::TEXT_PRIMARY).withAlpha(0.85f));
    g.drawText(freqLabel,
               juce::Rectangle<float>(plot.getX() + 6.0f, plot.getY() + 4.0f, 100.0f, 14.0f)
                   .toNearestInt(),
               juce::Justification::centredLeft);

    // Top-right shape label.
    const char* shapeLabel = shape_ == 1 ? "TRI" : shape_ == 2 ? "SQR" : "SIN";
    g.setColour(DarkTheme::getColour(DarkTheme::TEXT_PRIMARY).withAlpha(0.6f));
    g.drawText(
        shapeLabel,
        juce::Rectangle<float>(plot.getRight() - 80.0f - 6.0f, plot.getY() + 4.0f, 80.0f, 14.0f)
            .toNearestInt(),
        juce::Justification::centredRight);

    // Carrier-shape inset — bottom-right, ~30% width × 50% height. Shows two
    // cycles of the active shape. At audio rates the phase is clamped in the
    // timer so the glyph reads as a stable preview of the chosen waveform.
    const float insetW = std::min(140.0f, plot.getWidth() * 0.35f);
    const float insetH = std::min(50.0f, plot.getHeight() * 0.45f);
    const float insetMargin = 6.0f;
    juce::Rectangle<float> inset(plot.getRight() - insetW - insetMargin,
                                 plot.getBottom() - insetH - insetMargin, insetW, insetH);
    g.setColour(DarkTheme::getColour(DarkTheme::BACKGROUND).darker(0.20f));
    g.fillRect(inset);
    g.setColour(DarkTheme::getColour(DarkTheme::BORDER).withAlpha(0.45f));
    g.drawRect(inset, 1.0f);

    const float midY = inset.getCentreY();
    g.setColour(DarkTheme::getColour(DarkTheme::BORDER).withAlpha(0.30f));
    g.drawHorizontalLine(static_cast<int>(std::round(midY)), inset.getX(), inset.getRight());

    juce::Path wave;
    const float halfH = inset.getHeight() * 0.42f;
    for (int i = 0; i <= kInsetSamples; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(kInsetSamples);
        const double tracePhase =
            phase_ - static_cast<double>(kInsetCycles) * (1.0 - static_cast<double>(t));
        const float v = carrierSample(shape_, tracePhase);
        const float x = inset.getX() + t * inset.getWidth();
        const float y = midY - v * halfH;
        if (i == 0)
            wave.startNewSubPath(x, y);
        else
            wave.lineTo(x, y);
    }
    g.setColour(accent.withAlpha(0.9f));
    g.strokePath(wave, juce::PathStrokeType(1.6f));
}

const CompiledPresentationSpec& getMagdaRingModPresentation() {
    static const CompiledPresentationSpec kSpec{
        .pluginId = magda::daw::audio::compiled::MagdaRingModCompiledPlugin::xmlTypeName,
        .layoutCellCount = 6,
        .layoutCellsPerRow = 6,
        .createPanel = [](juce::String pluginId) -> std::unique_ptr<CompiledDevicePanel> {
            return std::make_unique<CompiledRingModCurveView>(pluginId);
        },
        .suppressLegacyUis = {},
    };
    return kSpec;
}

}  // namespace magda::daw::ui
