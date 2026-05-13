#include "compiled/CompiledFlangerCurveView.hpp"

#include <algorithm>
#include <cmath>

#include "audio/plugins/compiled/MagdaFlangerCompiledPlugin.hpp"
#include "ui/themes/DarkTheme.hpp"

namespace magda::daw::ui {

namespace {

constexpr float kPlotPadX = 8.0f;
constexpr float kPlotPadY = 8.0f;
constexpr int kPollMs = 33;
constexpr float kMinFreqHz = 50.0f;
constexpr float kMaxFreqHz = 18000.0f;
constexpr float kCenterMs = 3.0f;
constexpr float kSwingMs = 2.5f;

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

// |H(f)| of a single recirculating delay line:
//   H(f) = 1 / (1 - g · e^{-j 2π f τ})  →  |H(f)|² = 1 / (1 + g² - 2g cos(2π f τ))
// Returns the unnormalised magnitude (mag = 1 at feedback = 0, no boost or
// cut). The renderer turns it into dB centred at 0 so peaks/dips read above
// and below the midline of the plot.
float magnitudeAt(float hz, float delaySec, float feedback) {
    const float g = juce::jlimit(-0.99f, 0.99f, feedback);
    const float w = 2.0f * juce::MathConstants<float>::pi * hz * delaySec;
    const float denomSq = 1.0f + g * g - 2.0f * g * std::cos(w);
    if (denomSq <= 1.0e-9f)
        return 1.0f;
    return 1.0f / std::sqrt(denomSq);
}

}  // namespace

CompiledFlangerCurveView::CompiledFlangerCurveView(juce::String /*pluginId*/) {
    setInterceptsMouseClicks(false, false);
    lastTickMs_ = juce::Time::getMillisecondCounter();
    startTimer(kPollMs);
}

void CompiledFlangerCurveView::setCompiledPlugin(
    magda::daw::audio::compiled::MagdaFlangerCompiledPlugin* plugin) {
    compiledPlugin_ = plugin;
}

void CompiledFlangerCurveView::bindPlugin(te::Plugin* plugin) {
    setCompiledPlugin(
        dynamic_cast<magda::daw::audio::compiled::MagdaFlangerCompiledPlugin*>(plugin));
}

void CompiledFlangerCurveView::updateFromDevice(const magda::DeviceInfo& device) {
    deviceSnapshot_ = device;
    resampleFromPlugin();
    repaint();
}

float CompiledFlangerCurveView::effectiveRateHz() const {
    if (sync_) {
        const float divisor = std::max(0.001f, division_);
        return std::max(0.01f, bpm_ / (60.0f * divisor));
    }
    return std::max(0.01f, rateHz_);
}

void CompiledFlangerCurveView::timerCallback() {
    using Flanger = magda::daw::audio::compiled::MagdaFlangerCompiledPlugin;

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
    float rateHz = rateHz_;
    float division = division_;
    float depth = depth_;
    float feedback = feedback_;
    float mix = mix_;
    float width = width_;
    float bpm = bpm_;

    if (compiledPlugin_ != nullptr) {
        sync = readPluginSlot(Flanger::kSyncSlot, sync ? 1.0f : 0.0f) >= 0.5f;
        rateHz = readPluginSlot(Flanger::kRateSlot, rateHz);
        depth = readPluginSlot(Flanger::kDepthSlot, depth);
        feedback = readPluginSlot(Flanger::kFeedbackSlot, feedback);
        mix = readPluginSlot(Flanger::kMixSlot, mix);
        width = readPluginSlot(Flanger::kWidthSlot, width);
        if (auto* p = compiledPlugin_->getSlotParameter(Flanger::kDivisionSlot)) {
            const float norm = p->getCurrentValue();
            const auto& info = compiledPlugin_->getSlotInfo(Flanger::kDivisionSlot);
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
        sync = valueForSlot(deviceSnapshot_, Flanger::kSyncSlot, sync ? 1.0f : 0.0f) >= 0.5f;
        rateHz = valueForSlot(deviceSnapshot_, Flanger::kRateSlot, rateHz);
        depth = valueForSlot(deviceSnapshot_, Flanger::kDepthSlot, depth);
        feedback = valueForSlot(deviceSnapshot_, Flanger::kFeedbackSlot, feedback);
        mix = valueForSlot(deviceSnapshot_, Flanger::kMixSlot, mix);
        width = valueForSlot(deviceSnapshot_, Flanger::kWidthSlot, width);
    }

    sync_ = sync;
    rateHz_ = rateHz;
    division_ = std::max(0.001f, division);
    depth_ = juce::jlimit(0.0f, 1.0f, depth);
    feedback_ = juce::jlimit(-0.95f, 0.95f, feedback);
    mix_ = juce::jlimit(0.0f, 1.0f, mix);
    width_ = juce::jlimit(0.0f, 1.0f, width);
    bpm_ = std::max(20.0f, bpm);

    const double dt = static_cast<double>(elapsedMs) * 0.001;
    lfoPhase_ += dt * static_cast<double>(effectiveRateHz());
    lfoPhase_ -= std::floor(lfoPhase_);

    repaint();
}

void CompiledFlangerCurveView::resampleFromPlugin() {
    using Flanger = magda::daw::audio::compiled::MagdaFlangerCompiledPlugin;
    sync_ = valueForSlot(deviceSnapshot_, Flanger::kSyncSlot, sync_ ? 1.0f : 0.0f) >= 0.5f;
    rateHz_ = valueForSlot(deviceSnapshot_, Flanger::kRateSlot, rateHz_);
    depth_ = juce::jlimit(0.0f, 1.0f, valueForSlot(deviceSnapshot_, Flanger::kDepthSlot, depth_));
    feedback_ = juce::jlimit(-0.95f, 0.95f,
                             valueForSlot(deviceSnapshot_, Flanger::kFeedbackSlot, feedback_));
    mix_ = juce::jlimit(0.0f, 1.0f, valueForSlot(deviceSnapshot_, Flanger::kMixSlot, mix_));
    width_ = juce::jlimit(0.0f, 1.0f, valueForSlot(deviceSnapshot_, Flanger::kWidthSlot, width_));
}

void CompiledFlangerCurveView::paint(juce::Graphics& g) {
    const auto bounds = getLocalBounds();
    g.setColour(DarkTheme::getColour(DarkTheme::BACKGROUND).darker(0.06f));
    g.fillRect(bounds);

    auto plot = bounds.toFloat().reduced(kPlotPadX, kPlotPadY);
    plotArea_ = plot;
    if (plot.getWidth() < 8.0f || plot.getHeight() < 8.0f)
        return;

    g.setColour(DarkTheme::getColour(DarkTheme::BORDER).withAlpha(0.55f));
    g.drawRect(plot, 1.0f);

    juce::Graphics::ScopedSaveState clipGuard(g);
    g.reduceClipRegion(plot.toNearestInt());

    // Log-frequency grid at decades.
    g.setColour(DarkTheme::getColour(DarkTheme::BORDER).withAlpha(0.18f));
    for (float decade : {100.0f, 1000.0f, 10000.0f}) {
        const float x = freqToX(decade, plot);
        g.drawVerticalLine(static_cast<int>(std::round(x)), plot.getY(), plot.getBottom());
    }

    // Current delay time from LFO position.
    const float lfoBi =
        static_cast<float>(std::sin(lfoPhase_ * juce::MathConstants<double>::twoPi));
    const float delayMs = kCenterMs + lfoBi * depth_ * kSwingMs;
    const float delaySec = delayMs / 1000.0f;

    // 0 dB midline — flat response sits on it; peaks rise above, notches dip
    // below. Symmetric reading uses the whole plot height rather than the
    // top half only.
    const float midY = plot.getCentreY();
    g.setColour(DarkTheme::getColour(DarkTheme::BORDER).withAlpha(0.30f));
    g.drawHorizontalLine(static_cast<int>(std::round(midY)), plot.getX(), plot.getRight());

    // Plot the magnitude response in dB. Comb notches at high feedback can
    // be narrower than a single pixel, so we oversample each output column
    // and average — kills the per-frame aliasing flicker that comes from
    // sample positions snapping past razor-thin notches.
    const auto accent = DarkTheme::getColour(DarkTheme::ACCENT_PURPLE);
    const int pixelCount = std::max(64, static_cast<int>(std::round(plot.getWidth())));
    constexpr int kSubSamples = 8;
    constexpr float kDisplayRangeDb = 24.0f;  // ±24 dB fills the plot
    const float logMinHz = std::log(kMinFreqHz);
    const float logSpan = std::log(kMaxFreqHz / kMinFreqHz);
    const float halfHeight = plot.getHeight() * 0.46f;
    juce::Path curve;
    for (int px = 0; px <= pixelCount; ++px) {
        float magSum = 0.0f;
        for (int s = 0; s < kSubSamples; ++s) {
            const float t = (static_cast<float>(px) +
                             (static_cast<float>(s) + 0.5f) / static_cast<float>(kSubSamples)) /
                            static_cast<float>(pixelCount);
            const float hz = std::exp(logMinHz + t * logSpan);
            magSum += magnitudeAt(hz, delaySec, feedback_);
        }
        const float mag = magSum / static_cast<float>(kSubSamples);
        const float magDb = 20.0f * std::log10(std::max(0.001f, mag));
        // Scale deviation by mix — dry-only collapses toward the midline.
        const float scaledDb = magDb * mix_;
        const float normalised = juce::jlimit(-1.0f, 1.0f, scaledDb / kDisplayRangeDb);
        const float x = plot.getX() +
                        (static_cast<float>(px) / static_cast<float>(pixelCount)) * plot.getWidth();
        const float y = midY - normalised * halfHeight;
        if (px == 0)
            curve.startNewSubPath(x, y);
        else
            curve.lineTo(x, y);
    }
    g.setColour(accent.withAlpha(0.85f));
    g.strokePath(curve, juce::PathStrokeType(1.8f));

    // Delay readout (current LFO position translated to ms).
    const juce::String delayLabel = juce::String(delayMs, 2) + " ms";
    g.setFont(11.0f);
    g.setColour(DarkTheme::getColour(DarkTheme::TEXT_PRIMARY).withAlpha(0.75f));
    g.drawText(
        delayLabel,
        juce::Rectangle<float>(plot.getX() + 6.0f, plot.getY() + 4.0f, 80.0f, 14.0f).toNearestInt(),
        juce::Justification::centredLeft);

    // Rate readout (Hz / division).
    juce::String rateLabel;
    if (sync_) {
        if (compiledPlugin_ != nullptr) {
            const auto& info = compiledPlugin_->getSlotInfo(
                magda::daw::audio::compiled::MagdaFlangerCompiledPlugin::kDivisionSlot);
            if (auto* p = compiledPlugin_->getSlotParameter(
                    magda::daw::audio::compiled::MagdaFlangerCompiledPlugin::kDivisionSlot)) {
                const float norm = p->getCurrentValue();
                const int count = static_cast<int>(info.choices.size());
                if (count > 0) {
                    const int idx = juce::jlimit(
                        0, count - 1,
                        static_cast<int>(std::round(norm * static_cast<float>(count - 1))));
                    rateLabel = info.choices[static_cast<size_t>(idx)];
                }
            }
        }
        if (rateLabel.isEmpty())
            rateLabel = "Sync";
    } else {
        rateLabel =
            rateHz_ >= 10.0f ? juce::String(rateHz_, 1) + " Hz" : juce::String(rateHz_, 2) + " Hz";
    }
    g.setColour(DarkTheme::getColour(DarkTheme::TEXT_PRIMARY).withAlpha(0.6f));
    g.drawText(
        rateLabel,
        juce::Rectangle<float>(plot.getRight() - 80.0f - 6.0f, plot.getY() + 4.0f, 80.0f, 14.0f)
            .toNearestInt(),
        juce::Justification::centredRight);
}

const CompiledPresentationSpec& getMagdaFlangerPresentation() {
    static const CompiledPresentationSpec kSpec{
        .pluginId = magda::daw::audio::compiled::MagdaFlangerCompiledPlugin::xmlTypeName,
        .layoutCellCount = 7,
        .layoutCellsPerRow = 7,
        .createPanel = [](juce::String pluginId) -> std::unique_ptr<CompiledDevicePanel> {
            return std::make_unique<CompiledFlangerCurveView>(pluginId);
        },
        .suppressLegacyUis = {},  // there is no legacy "Flanger" UI
    };
    return kSpec;
}

}  // namespace magda::daw::ui
