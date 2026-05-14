#include "compiled/CompiledModCurveView.hpp"

#include <algorithm>
#include <cmath>

#include "audio/plugins/compiled/MagdaModCompiledPlugin.hpp"
#include "ui/themes/DarkTheme.hpp"

namespace magda::daw::ui {

namespace {

constexpr float kPlotPadX = 8.0f;
constexpr float kPlotPadY = 8.0f;
constexpr int kPollMs = 33;
constexpr int kCyclesVisible = 2;
constexpr int kPathSamples = 200;

float valueForSlot(const magda::DeviceInfo& device, int slotIndex, float fallback) {
    for (const auto& param : device.parameters)
        if (param.paramIndex == slotIndex)
            return param.currentValue;
    return fallback;
}

float triangleWave(double phase) {
    const double w = phase - std::floor(phase);
    return static_cast<float>(w < 0.5 ? 4.0 * w - 1.0 : 3.0 - 4.0 * w);
}

float squareWave(double phase) {
    const double w = phase - std::floor(phase);
    return w < 0.5 ? 1.0f : -1.0f;
}

}  // namespace

CompiledModCurveView::CompiledModCurveView(juce::String /*pluginId*/) {
    setInterceptsMouseClicks(false, false);
    lastTickMs_ = juce::Time::getMillisecondCounter();
    startTimer(kPollMs);
}

void CompiledModCurveView::setCompiledPlugin(
    magda::daw::audio::compiled::MagdaModCompiledPlugin* plugin) {
    compiledPlugin_ = plugin;
}

void CompiledModCurveView::bindPlugin(te::Plugin* plugin) {
    setCompiledPlugin(dynamic_cast<magda::daw::audio::compiled::MagdaModCompiledPlugin*>(plugin));
}

void CompiledModCurveView::updateFromDevice(const magda::DeviceInfo& device) {
    deviceSnapshot_ = device;
    resampleFromPlugin();
    repaint();
}

float CompiledModCurveView::effectiveRateHz() const {
    if (sync_) {
        const float divisor = std::max(0.001f, division_);
        return std::max(0.01f, bpm_ / (60.0f * divisor));
    }
    return std::max(0.01f, rateHz_);
}

float CompiledModCurveView::lfoSample(double phase) const {
    switch (shape_) {
        case 1:
            return triangleWave(phase);
        case 2:
            return squareWave(phase);
        case 3: {
            // Deterministic noise per integer phase bucket; same input phase
            // always yields the same value so the trace doesn't shimmer.
            const auto bucket = static_cast<int>(std::floor(phase));
            juce::Random local(static_cast<juce::int64>(bucket) + 0x5a17u);
            return local.nextFloat() * 2.0f - 1.0f;
        }
        default:
            return static_cast<float>(std::sin(phase * juce::MathConstants<double>::twoPi));
    }
}

void CompiledModCurveView::timerCallback() {
    using Mod = magda::daw::audio::compiled::MagdaModCompiledPlugin;

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

    int mode = mode_;
    int shape = shape_;
    bool sync = sync_;
    float rateHz = rateHz_;
    float division = division_;
    float depth = depth_;
    float bpm = bpm_;

    if (compiledPlugin_ != nullptr) {
        mode = static_cast<int>(std::round(readPluginSlot(Mod::kModeSlot, mode)));
        shape = static_cast<int>(std::round(readPluginSlot(Mod::kShapeSlot, shape)));
        sync = readPluginSlot(Mod::kSyncSlot, sync ? 1.0f : 0.0f) >= 0.5f;
        rateHz = readPluginSlot(Mod::kRateSlot, rateHz);
        depth = readPluginSlot(Mod::kDepthSlot, depth);
        // Division — the host slot stores an index into divisionChoiceValues_;
        // ask the plugin for the underlying Faust value.
        if (auto* p = compiledPlugin_->getSlotParameter(Mod::kDivisionSlot)) {
            const float norm = p->getCurrentValue();
            const auto& info = compiledPlugin_->getSlotInfo(Mod::kDivisionSlot);
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
        mode = static_cast<int>(std::round(valueForSlot(deviceSnapshot_, Mod::kModeSlot, mode)));
        shape = static_cast<int>(std::round(valueForSlot(deviceSnapshot_, Mod::kShapeSlot, shape)));
        sync = valueForSlot(deviceSnapshot_, Mod::kSyncSlot, sync ? 1.0f : 0.0f) >= 0.5f;
        rateHz = valueForSlot(deviceSnapshot_, Mod::kRateSlot, rateHz);
        depth = valueForSlot(deviceSnapshot_, Mod::kDepthSlot, depth);
    }

    mode_ = juce::jlimit(0, 2, mode);
    shape_ = juce::jlimit(0, 3, shape);
    sync_ = sync;
    rateHz_ = rateHz;
    division_ = std::max(0.001f, division);
    depth_ = juce::jlimit(0.0f, 1.0f, depth);
    bpm_ = std::max(20.0f, bpm);

    const double dt = static_cast<double>(elapsedMs) * 0.001;
    lfoPhase_ += dt * static_cast<double>(effectiveRateHz());
    lfoPhase_ -= std::floor(lfoPhase_);

    repaint();
}

void CompiledModCurveView::resampleFromPlugin() {
    using Mod = magda::daw::audio::compiled::MagdaModCompiledPlugin;
    mode_ = juce::jlimit(
        0, 2, static_cast<int>(std::round(valueForSlot(deviceSnapshot_, Mod::kModeSlot, mode_))));
    shape_ = juce::jlimit(
        0, 3, static_cast<int>(std::round(valueForSlot(deviceSnapshot_, Mod::kShapeSlot, shape_))));
    sync_ = valueForSlot(deviceSnapshot_, Mod::kSyncSlot, sync_ ? 1.0f : 0.0f) >= 0.5f;
    rateHz_ = valueForSlot(deviceSnapshot_, Mod::kRateSlot, rateHz_);
    depth_ = juce::jlimit(0.0f, 1.0f, valueForSlot(deviceSnapshot_, Mod::kDepthSlot, depth_));
}

void CompiledModCurveView::paint(juce::Graphics& g) {
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

    // Zero-crossing midline.
    g.setColour(DarkTheme::getColour(DarkTheme::BORDER).withAlpha(0.30f));
    const float midY = plot.getCentreY();
    g.drawHorizontalLine(static_cast<int>(std::round(midY)), plot.getX(), plot.getRight());

    // Beat grid: one vertical per LFO period when synced (the period IS one
    // musical division). When free, no grid.
    if (sync_) {
        g.setColour(DarkTheme::getColour(DarkTheme::BORDER).withAlpha(0.22f));
        for (int i = 1; i < kCyclesVisible; ++i) {
            const float frac = static_cast<float>(i) / static_cast<float>(kCyclesVisible);
            const float x = plot.getX() + frac * plot.getWidth();
            g.drawVerticalLine(static_cast<int>(std::round(x)), plot.getY(), plot.getBottom());
        }
    }

    // LFO trace. Phase scrolls so the playhead stays at the right edge —
    // the part visible is "what already happened" for the LFO.
    const auto accent = mode_ == 1   ? DarkTheme::getColour(DarkTheme::ACCENT_GREEN)
                        : mode_ == 2 ? DarkTheme::getColour(DarkTheme::ACCENT_PURPLE)
                                     : DarkTheme::getColour(DarkTheme::ACCENT_BLUE_LIGHT);

    const float depthScale = 0.10f + 0.85f * depth_;
    const float halfHeight = plot.getHeight() * 0.45f * depthScale;

    juce::Path curve;
    for (int i = 0; i <= kPathSamples; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(kPathSamples);
        // t=0 at left edge → that much earlier in time. Show kCyclesVisible
        // periods total.
        const double tracePhase =
            lfoPhase_ - static_cast<double>(kCyclesVisible) * (1.0 - static_cast<double>(t));
        const float v = lfoSample(tracePhase);
        const float x = plot.getX() + t * plot.getWidth();
        const float y = midY - v * halfHeight;
        if (i == 0)
            curve.startNewSubPath(x, y);
        else
            curve.lineTo(x, y);
    }
    g.setColour(accent.withAlpha(0.85f));
    g.strokePath(curve, juce::PathStrokeType(1.8f));

    // Playhead at the right edge.
    g.setColour(accent.withAlpha(0.55f));
    g.drawVerticalLine(static_cast<int>(std::round(plot.getRight() - 1.0f)), plot.getY(),
                       plot.getBottom());

    // Mode badge.
    const char* modeLabel = mode_ == 1 ? "VIBRATO" : mode_ == 2 ? "AUTOPAN" : "TREMOLO";
    g.setFont(11.0f);
    g.setColour(DarkTheme::getColour(DarkTheme::TEXT_PRIMARY).withAlpha(0.75f));
    g.drawText(
        modeLabel,
        juce::Rectangle<float>(plot.getX() + 6.0f, plot.getY() + 4.0f, 80.0f, 14.0f).toNearestInt(),
        juce::Justification::centredLeft);

    // Rate readout — Hz when free, division label when synced.
    juce::String rateLabel;
    if (sync_) {
        if (compiledPlugin_ != nullptr) {
            const auto& info = compiledPlugin_->getSlotInfo(
                magda::daw::audio::compiled::MagdaModCompiledPlugin::kDivisionSlot);
            if (auto* p = compiledPlugin_->getSlotParameter(
                    magda::daw::audio::compiled::MagdaModCompiledPlugin::kDivisionSlot)) {
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

const CompiledPresentationSpec& getMagdaModPresentation() {
    static const CompiledPresentationSpec kSpec{
        .pluginId = magda::daw::audio::compiled::MagdaModCompiledPlugin::xmlTypeName,
        .layoutCellCount = 6,
        .layoutCellsPerRow = 6,
        .createPanel = [](juce::String pluginId) -> std::unique_ptr<CompiledDevicePanel> {
            return std::make_unique<CompiledModCurveView>(pluginId);
        },
        .suppressLegacyUis = {},
    };
    return kSpec;
}

}  // namespace magda::daw::ui
