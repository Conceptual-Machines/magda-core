#include "compiled/CompiledChorusCurveView.hpp"

#include <algorithm>
#include <cmath>

#include "audio/plugins/compiled/MagdaChorusCompiledPlugin.hpp"
#include "ui/themes/DarkTheme.hpp"

namespace magda::daw::ui {

namespace {

constexpr float kPlotPadX = 8.0f;
constexpr float kPlotPadY = 8.0f;
constexpr int kPollMs = 33;
constexpr int kCyclesVisible = 2;
constexpr int kPathSamples = 200;
constexpr float kCenterMs = 18.0f;
constexpr float kSwingMs = 12.0f;
constexpr float kAxisMinMs = 4.0f;
constexpr float kAxisMaxMs = 32.0f;

float valueForSlot(const magda::DeviceInfo& device, int slotIndex, float fallback) {
    for (const auto& param : device.parameters)
        if (param.paramIndex == slotIndex)
            return param.currentValue;
    return fallback;
}

float msToY(float ms, const juce::Rectangle<float>& plot) {
    const float t = juce::jlimit(0.0f, 1.0f, (ms - kAxisMinMs) / (kAxisMaxMs - kAxisMinMs));
    return plot.getY() + t * plot.getHeight();
}

}  // namespace

CompiledChorusCurveView::CompiledChorusCurveView(juce::String /*pluginId*/) {
    setInterceptsMouseClicks(false, false);
    lastTickMs_ = juce::Time::getMillisecondCounter();
    startTimer(kPollMs);
}

void CompiledChorusCurveView::setCompiledPlugin(
    magda::daw::audio::compiled::MagdaChorusCompiledPlugin* plugin) {
    compiledPlugin_ = plugin;
}

void CompiledChorusCurveView::bindPlugin(te::Plugin* plugin) {
    setCompiledPlugin(
        dynamic_cast<magda::daw::audio::compiled::MagdaChorusCompiledPlugin*>(plugin));
}

void CompiledChorusCurveView::updateFromDevice(const magda::DeviceInfo& device) {
    deviceSnapshot_ = device;
    resampleFromPlugin();
    repaint();
}

float CompiledChorusCurveView::effectiveRateHz() const {
    if (sync_) {
        const float divisor = std::max(0.001f, division_);
        return std::max(0.01f, bpm_ / (60.0f * divisor));
    }
    return std::max(0.01f, rateHz_);
}

void CompiledChorusCurveView::timerCallback() {
    using Chorus = magda::daw::audio::compiled::MagdaChorusCompiledPlugin;

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

    int voicesIdx = voices_ - 1;
    bool sync = sync_;
    float rateHz = rateHz_;
    float division = division_;
    float depth = depth_;
    float width = width_;
    float bpm = bpm_;

    if (compiledPlugin_ != nullptr) {
        voicesIdx = static_cast<int>(
            std::round(readPluginSlot(Chorus::kVoicesSlot, static_cast<float>(voicesIdx))));
        sync = readPluginSlot(Chorus::kSyncSlot, sync ? 1.0f : 0.0f) >= 0.5f;
        rateHz = readPluginSlot(Chorus::kRateSlot, rateHz);
        depth = readPluginSlot(Chorus::kDepthSlot, depth);
        width = readPluginSlot(Chorus::kWidthSlot, width);
        if (auto* p = compiledPlugin_->getSlotParameter(Chorus::kDivisionSlot)) {
            const float norm = p->getCurrentValue();
            const auto& info = compiledPlugin_->getSlotInfo(Chorus::kDivisionSlot);
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
        voicesIdx = static_cast<int>(std::round(
            valueForSlot(deviceSnapshot_, Chorus::kVoicesSlot, static_cast<float>(voicesIdx))));
        sync = valueForSlot(deviceSnapshot_, Chorus::kSyncSlot, sync ? 1.0f : 0.0f) >= 0.5f;
        rateHz = valueForSlot(deviceSnapshot_, Chorus::kRateSlot, rateHz);
        depth = valueForSlot(deviceSnapshot_, Chorus::kDepthSlot, depth);
        width = valueForSlot(deviceSnapshot_, Chorus::kWidthSlot, width);
    }

    voices_ = juce::jlimit(1, 3, voicesIdx + 1);
    sync_ = sync;
    rateHz_ = rateHz;
    division_ = std::max(0.001f, division);
    depth_ = juce::jlimit(0.0f, 1.0f, depth);
    width_ = juce::jlimit(0.0f, 1.0f, width);
    bpm_ = std::max(20.0f, bpm);

    const double dt = static_cast<double>(elapsedMs) * 0.001;
    lfoPhase_ += dt * static_cast<double>(effectiveRateHz());
    lfoPhase_ -= std::floor(lfoPhase_);

    repaint();
}

void CompiledChorusCurveView::resampleFromPlugin() {
    using Chorus = magda::daw::audio::compiled::MagdaChorusCompiledPlugin;
    const int voicesIdx = static_cast<int>(std::round(
        valueForSlot(deviceSnapshot_, Chorus::kVoicesSlot, static_cast<float>(voices_ - 1))));
    voices_ = juce::jlimit(1, 3, voicesIdx + 1);
    sync_ = valueForSlot(deviceSnapshot_, Chorus::kSyncSlot, sync_ ? 1.0f : 0.0f) >= 0.5f;
    rateHz_ = valueForSlot(deviceSnapshot_, Chorus::kRateSlot, rateHz_);
    depth_ = juce::jlimit(0.0f, 1.0f, valueForSlot(deviceSnapshot_, Chorus::kDepthSlot, depth_));
    width_ = juce::jlimit(0.0f, 1.0f, valueForSlot(deviceSnapshot_, Chorus::kWidthSlot, width_));
}

void CompiledChorusCurveView::paint(juce::Graphics& g) {
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

    // Centre delay line at 18 ms — gives the eye a reference for "how
    // much the voices are swinging".
    g.setColour(DarkTheme::getColour(DarkTheme::BORDER).withAlpha(0.30f));
    const float centerY = msToY(kCenterMs, plot);
    g.drawHorizontalLine(static_cast<int>(std::round(centerY)), plot.getX(), plot.getRight());

    // Per-voice trajectories. Each voice's phase offset is i/3 × width;
    // depth scales the swing magnitude. Mirrors the DSP's lfoAt(...)
    // formula in magda_chorus.dsp.
    const juce::Colour voiceColours[3] = {
        DarkTheme::getColour(DarkTheme::ACCENT_BLUE_LIGHT),
        DarkTheme::getColour(DarkTheme::ACCENT_GREEN),
        DarkTheme::getColour(DarkTheme::ACCENT_PURPLE),
    };

    for (int v = 0; v < voices_; ++v) {
        const double phaseOffset = (static_cast<double>(v) / 3.0) * static_cast<double>(width_);
        juce::Path curve;
        for (int i = 0; i <= kPathSamples; ++i) {
            const float t = static_cast<float>(i) / static_cast<float>(kPathSamples);
            const double tracePhase =
                lfoPhase_ - static_cast<double>(kCyclesVisible) * (1.0 - static_cast<double>(t));
            const double lfo =
                std::sin((tracePhase + phaseOffset) * juce::MathConstants<double>::twoPi);
            const float ms = kCenterMs + static_cast<float>(lfo) * depth_ * kSwingMs * 0.9f;
            const float x = plot.getX() + t * plot.getWidth();
            const float y = msToY(ms, plot);
            if (i == 0)
                curve.startNewSubPath(x, y);
            else
                curve.lineTo(x, y);
        }
        g.setColour(voiceColours[v].withAlpha(0.85f));
        g.strokePath(curve, juce::PathStrokeType(1.8f));
    }

    // Playhead at right edge.
    g.setColour(voiceColours[0].withAlpha(0.55f));
    g.drawVerticalLine(static_cast<int>(std::round(plot.getRight() - 1.0f)), plot.getY(),
                       plot.getBottom());

    // Voice-count badge.
    const juce::String voiceLabel = juce::String(voices_) + (voices_ == 1 ? " VOICE" : " VOICES");
    g.setFont(11.0f);
    g.setColour(DarkTheme::getColour(DarkTheme::TEXT_PRIMARY).withAlpha(0.75f));
    g.drawText(
        voiceLabel,
        juce::Rectangle<float>(plot.getX() + 6.0f, plot.getY() + 4.0f, 80.0f, 14.0f).toNearestInt(),
        juce::Justification::centredLeft);

    // Rate readout (Hz for free, musical division for sync).
    juce::String rateLabel;
    if (sync_) {
        if (compiledPlugin_ != nullptr) {
            const auto& info = compiledPlugin_->getSlotInfo(
                magda::daw::audio::compiled::MagdaChorusCompiledPlugin::kDivisionSlot);
            if (auto* p = compiledPlugin_->getSlotParameter(
                    magda::daw::audio::compiled::MagdaChorusCompiledPlugin::kDivisionSlot)) {
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

const CompiledPresentationSpec& getMagdaChorusPresentation() {
    static const LegacyUiKind kSuppress[] = {LegacyUiKind::Chorus};
    static const CompiledPresentationSpec kSpec{
        .pluginId = magda::daw::audio::compiled::MagdaChorusCompiledPlugin::xmlTypeName,
        .layoutCellCount = 8,
        .layoutCellsPerRow = 8,
        .createPanel = [](juce::String pluginId) -> std::unique_ptr<CompiledDevicePanel> {
            return std::make_unique<CompiledChorusCurveView>(pluginId);
        },
        .suppressLegacyUis = kSuppress,
    };
    return kSpec;
}

}  // namespace magda::daw::ui
