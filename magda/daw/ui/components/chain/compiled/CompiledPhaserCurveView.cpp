#include "compiled/CompiledPhaserCurveView.hpp"

#include <algorithm>
#include <cmath>

#include "audio/plugins/compiled/MagdaPhaserCompiledPlugin.hpp"
#include "ui/themes/DarkTheme.hpp"

namespace magda::daw::ui {

namespace {

constexpr float kPlotPadX = 8.0f;
constexpr float kPlotPadY = 8.0f;
constexpr int kPollMs = 33;
constexpr float kMinFreq = 20.0f;
constexpr float kMaxFreq = 20000.0f;
constexpr float kHandlePickPx = 8.0f;
constexpr float kMinSweepHz = 30.0f;
constexpr float kMaxSweepHz = 8000.0f;
constexpr float kMinSweepGapHz = 5.0f;

float valueForSlot(const magda::DeviceInfo& device, int slotIndex, float fallback) {
    for (const auto& param : device.parameters)
        if (param.paramIndex == slotIndex)
            return param.currentValue;
    return fallback;
}

float logLerp(float t, float lo, float hi) {
    const float clamped = juce::jlimit(0.0f, 1.0f, t);
    return lo * std::exp(clamped * std::log(hi / lo));
}

float invLogLerp(float v, float lo, float hi) {
    const float clamped = juce::jlimit(lo, hi, v);
    return std::log(clamped / lo) / std::log(hi / lo);
}

float triangularLfo(double phase) {
    const double wrapped = phase - std::floor(phase);
    return static_cast<float>(wrapped < 0.5 ? wrapped * 2.0 : 2.0 - wrapped * 2.0);
}

}  // namespace

CompiledPhaserCurveView::CompiledPhaserCurveView(juce::String /*pluginId*/) {
    setInterceptsMouseClicks(true, false);
    setMouseCursor(juce::MouseCursor::NormalCursor);
    lastTickMs_ = juce::Time::getMillisecondCounter();
    startTimer(kPollMs);
}

void CompiledPhaserCurveView::setCompiledPlugin(
    magda::daw::audio::compiled::MagdaPhaserCompiledPlugin* plugin) {
    compiledPlugin_ = plugin;
}

void CompiledPhaserCurveView::bindPlugin(te::Plugin* plugin) {
    setCompiledPlugin(
        dynamic_cast<magda::daw::audio::compiled::MagdaPhaserCompiledPlugin*>(plugin));
}

void CompiledPhaserCurveView::updateFromDevice(const magda::DeviceInfo& device) {
    deviceSnapshot_ = device;
    resampleFromPlugin();
    repaint();
}

void CompiledPhaserCurveView::timerCallback() {
    using Phaser = magda::daw::audio::compiled::MagdaPhaserCompiledPlugin;

    const auto now = juce::Time::getMillisecondCounter();
    const auto elapsedMs = now - lastTickMs_;
    lastTickMs_ = now;
    lfoPhase_ += static_cast<double>(juce::jlimit(0.0f, 10.0f, rateHz_)) *
                 static_cast<double>(elapsedMs) * 0.001;
    lfoPhase_ -= std::floor(lfoPhase_);

    float rate = rateHz_;
    float depth = depth_;
    float feedback = feedback_;
    int stages = stagesIndex_;
    float minHz = minHz_;
    float maxHz = maxHz_;
    float mix = mix_;

    auto readPluginSlot = [this](int slot, float fallback) {
        if (compiledPlugin_ == nullptr)
            return fallback;
        if (auto* p = compiledPlugin_->getSlotParameter(slot))
            return compiledPlugin_->nativeValueToDisplayValue(slot, p->getCurrentValue());
        return fallback;
    };

    if (compiledPlugin_ != nullptr) {
        rate = readPluginSlot(Phaser::kRateSlot, rate);
        depth = readPluginSlot(Phaser::kDepthSlot, depth);
        feedback = readPluginSlot(Phaser::kFeedbackSlot, feedback);
        stages = static_cast<int>(std::round(readPluginSlot(Phaser::kStagesSlot, stages)));
        minHz = readPluginSlot(Phaser::kMinHzSlot, minHz);
        maxHz = readPluginSlot(Phaser::kMaxHzSlot, maxHz);
        mix = readPluginSlot(Phaser::kMixSlot, mix);
    } else {
        rate = valueForSlot(deviceSnapshot_, Phaser::kRateSlot, rate);
        depth = valueForSlot(deviceSnapshot_, Phaser::kDepthSlot, depth);
        feedback = valueForSlot(deviceSnapshot_, Phaser::kFeedbackSlot, feedback);
        stages = static_cast<int>(
            std::round(valueForSlot(deviceSnapshot_, Phaser::kStagesSlot, stages)));
        minHz = valueForSlot(deviceSnapshot_, Phaser::kMinHzSlot, minHz);
        maxHz = valueForSlot(deviceSnapshot_, Phaser::kMaxHzSlot, maxHz);
        mix = valueForSlot(deviceSnapshot_, Phaser::kMixSlot, mix);
    }

    const bool moved = std::fabs(rate - rateHz_) > 0.001f || std::fabs(depth - depth_) > 0.001f ||
                       std::fabs(feedback - feedback_) > 0.001f || stages != stagesIndex_ ||
                       std::fabs(minHz - minHz_) > 0.5f || std::fabs(maxHz - maxHz_) > 0.5f ||
                       std::fabs(mix - mix_) > 0.001f;
    if (moved) {
        rateHz_ = rate;
        depth_ = depth;
        feedback_ = feedback;
        stagesIndex_ = stages;
        minHz_ = minHz;
        maxHz_ = maxHz;
        mix_ = mix;
        normalizeSweepRange();
    }

    repaint();
}

void CompiledPhaserCurveView::resampleFromPlugin() {
    using Phaser = magda::daw::audio::compiled::MagdaPhaserCompiledPlugin;
    rateHz_ = valueForSlot(deviceSnapshot_, Phaser::kRateSlot, rateHz_);
    depth_ = valueForSlot(deviceSnapshot_, Phaser::kDepthSlot, depth_);
    feedback_ = valueForSlot(deviceSnapshot_, Phaser::kFeedbackSlot, feedback_);
    stagesIndex_ = static_cast<int>(
        std::round(valueForSlot(deviceSnapshot_, Phaser::kStagesSlot, stagesIndex_)));
    minHz_ = valueForSlot(deviceSnapshot_, Phaser::kMinHzSlot, minHz_);
    maxHz_ = valueForSlot(deviceSnapshot_, Phaser::kMaxHzSlot, maxHz_);
    mix_ = valueForSlot(deviceSnapshot_, Phaser::kMixSlot, mix_);
    normalizeSweepRange();
}

void CompiledPhaserCurveView::normalizeSweepRange() {
    minHz_ = juce::jlimit(kMinSweepHz, 1000.0f, minHz_);
    maxHz_ = juce::jlimit(500.0f, kMaxSweepHz, maxHz_);

    if (minHz_ >= maxHz_ - kMinSweepGapHz) {
        const float preferredMin = juce::jlimit(kMinSweepHz, 1000.0f, maxHz_ * 0.5f);
        minHz_ = std::min(preferredMin, maxHz_ - kMinSweepGapHz);
        minHz_ = std::max(kMinSweepHz, minHz_);
    }
}

float CompiledPhaserCurveView::xToFreq(float x) const {
    if (plotArea_.getWidth() <= 0.0f)
        return kMinFreq;
    const float t = (x - plotArea_.getX()) / plotArea_.getWidth();
    return logLerp(t, kMinFreq, kMaxFreq);
}

float CompiledPhaserCurveView::freqToX(float hz) const {
    const float t = invLogLerp(hz, kMinFreq, kMaxFreq);
    return plotArea_.getX() + t * plotArea_.getWidth();
}

CompiledPhaserCurveView::Handle CompiledPhaserCurveView::pickHandle(float x) const {
    const float minX = freqToX(minHz_);
    const float maxX = freqToX(maxHz_);
    const float dMin = std::fabs(x - minX);
    const float dMax = std::fabs(x - maxX);
    if (dMin > kHandlePickPx && dMax > kHandlePickPx)
        return Handle::None;
    return dMin <= dMax ? Handle::MinHz : Handle::MaxHz;
}

void CompiledPhaserCurveView::mouseMove(const juce::MouseEvent& e) {
    if (draggedHandle_ != Handle::None)
        return;
    const auto picked = pickHandle(static_cast<float>(e.x));
    if (picked != hoveredHandle_) {
        hoveredHandle_ = picked;
        setMouseCursor(picked == Handle::None ? juce::MouseCursor::NormalCursor
                                              : juce::MouseCursor::LeftRightResizeCursor);
        repaint();
    }
}

void CompiledPhaserCurveView::mouseExit(const juce::MouseEvent&) {
    if (hoveredHandle_ != Handle::None && draggedHandle_ == Handle::None) {
        hoveredHandle_ = Handle::None;
        setMouseCursor(juce::MouseCursor::NormalCursor);
        repaint();
    }
}

void CompiledPhaserCurveView::mouseDown(const juce::MouseEvent& e) {
    const auto picked = pickHandle(static_cast<float>(e.x));
    if (picked == Handle::None)
        return;
    draggedHandle_ = picked;
    hoveredHandle_ = picked;
    setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
}

void CompiledPhaserCurveView::mouseDrag(const juce::MouseEvent& e) {
    using Phaser = magda::daw::audio::compiled::MagdaPhaserCompiledPlugin;
    if (draggedHandle_ == Handle::None)
        return;

    const float rawHz = xToFreq(static_cast<float>(e.x));
    if (draggedHandle_ == Handle::MinHz) {
        const float ceiling = std::max(kMinSweepHz, std::min(1000.0f, maxHz_ - kMinSweepGapHz));
        const float clamped = juce::jlimit(kMinSweepHz, ceiling, rawHz);
        if (std::fabs(clamped - minHz_) > 0.5f) {
            minHz_ = clamped;
            if (onParameterChanged)
                onParameterChanged(Phaser::kMinHzSlot, clamped);
            repaint();
        }
    } else {
        const float floor = std::min(kMaxSweepHz, std::max(500.0f, minHz_ + kMinSweepGapHz));
        const float clamped = juce::jlimit(floor, kMaxSweepHz, rawHz);
        if (std::fabs(clamped - maxHz_) > 0.5f) {
            maxHz_ = clamped;
            if (onParameterChanged)
                onParameterChanged(Phaser::kMaxHzSlot, clamped);
            repaint();
        }
    }
}

void CompiledPhaserCurveView::mouseUp(const juce::MouseEvent& e) {
    draggedHandle_ = Handle::None;
    hoveredHandle_ = pickHandle(static_cast<float>(e.x));
    setMouseCursor(hoveredHandle_ == Handle::None ? juce::MouseCursor::NormalCursor
                                                  : juce::MouseCursor::LeftRightResizeCursor);
    repaint();
}

void CompiledPhaserCurveView::paint(juce::Graphics& g) {
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

    g.setColour(DarkTheme::getColour(DarkTheme::BORDER).withAlpha(0.18f));
    for (float decade : {100.0f, 1000.0f, 10000.0f}) {
        const float x = freqToX(decade);
        g.drawVerticalLine(static_cast<int>(std::round(x)), plot.getY(), plot.getBottom());
    }

    const auto accent = DarkTheme::getColour(DarkTheme::ACCENT_BLUE_LIGHT);
    const auto sweepColour = DarkTheme::getColour(DarkTheme::ACCENT_PURPLE);
    const float minX = freqToX(minHz_);
    const float maxX = freqToX(maxHz_);
    const float sweepLeft = std::min(minX, maxX);
    const float sweepRight = std::max(minX, maxX);
    const float mix01 = juce::jlimit(0.0f, 1.0f, mix_);
    const float depth01 = juce::jlimit(0.0f, 1.0f, depth_ * 0.5f);
    const float fb01 = juce::jlimit(0.0f, 1.0f, std::fabs(feedback_) / 0.95f);

    g.setColour(sweepColour.withAlpha(0.08f + mix01 * 0.12f));
    g.fillRect(
        juce::Rectangle<float>(sweepLeft, plot.getY(), sweepRight - sweepLeft, plot.getHeight()));

    const int notchCount = (juce::jlimit(0, 3, stagesIndex_) + 1) * 2;
    const float sweep01 = triangularLfo(lfoPhase_);
    const float sweepMinNorm = invLogLerp(std::max(kMinSweepHz, minHz_), kMinFreq, kMaxFreq);
    const float sweepMaxNorm =
        invLogLerp(std::max(minHz_ + kMinSweepGapHz, maxHz_), kMinFreq, kMaxFreq);
    const float centreNorm = invLogLerp(
        logLerp(sweep01, std::max(kMinSweepHz, minHz_), std::max(minHz_ + kMinSweepGapHz, maxHz_)),
        kMinFreq, kMaxFreq);
    const float sweepWidthNorm = std::max(0.015f, sweepMaxNorm - sweepMinNorm);
    const float requestedHalfSpread = sweepWidthNorm * (0.18f + depth01 * 0.32f);
    const float edgeLimitedHalfSpread =
        std::max(0.0f, std::min(centreNorm - sweepMinNorm, sweepMaxNorm - centreNorm));
    const float halfSpreadNorm = std::min(requestedHalfSpread, edgeLimitedHalfSpread);
    const float notchDepth = plot.getHeight() * (0.22f + depth01 * 0.48f + fb01 * 0.12f);
    const float notchWidth = 0.010f + (1.0f - fb01) * 0.018f;

    juce::Path curve;
    constexpr int kPoints = 160;
    for (int i = 0; i <= kPoints; ++i) {
        const float n = static_cast<float>(i) / static_cast<float>(kPoints);
        float response = 1.0f;
        for (int notch = 0; notch < notchCount; ++notch) {
            const float lane = (static_cast<float>(notch) + 0.5f) / static_cast<float>(notchCount);
            const float notchNorm = centreNorm + (lane - 0.5f) * halfSpreadNorm * 2.0f;
            const float d = std::fabs(n - notchNorm) / notchWidth;
            const float dip = (0.22f + fb01 * 0.45f + depth01 * 0.18f) / (1.0f + d * d * 4.0f);
            response = std::min(response, juce::jlimit(0.02f, 1.0f, 1.0f - dip));
        }

        const float x = plot.getX() + n * plot.getWidth();
        const float y = plot.getY() + (1.0f - response) * notchDepth + plot.getHeight() * 0.18f;
        if (i == 0)
            curve.startNewSubPath(x, y);
        else
            curve.lineTo(x, y);
    }

    g.setColour(accent.withAlpha(0.40f + mix01 * 0.45f));
    g.strokePath(curve, juce::PathStrokeType(2.0f));

    for (int notch = 0; notch < notchCount; ++notch) {
        const float lane = (static_cast<float>(notch) + 0.5f) / static_cast<float>(notchCount);
        const float notchNorm = centreNorm + (lane - 0.5f) * halfSpreadNorm * 2.0f;
        const float x = plot.getX() + notchNorm * plot.getWidth();
        g.setColour(accent.withAlpha(0.20f + mix01 * 0.18f));
        g.drawVerticalLine(static_cast<int>(std::round(x)), plot.getY(), plot.getBottom());
    }

    auto drawHandle = [&](float x, Handle handle) {
        const bool active = handle == hoveredHandle_ || handle == draggedHandle_;
        g.setColour(
            DarkTheme::getColour(DarkTheme::TEXT_PRIMARY).withAlpha(active ? 0.95f : 0.62f));
        const float thickness = active ? 2.0f : 1.0f;
        g.fillRect(
            juce::Rectangle<float>(x - thickness * 0.5f, plot.getY(), thickness, plot.getHeight()));
    };
    drawHandle(minX, Handle::MinHz);
    drawHandle(maxX, Handle::MaxHz);

    auto drawLabel = [&](float x, float hz, Handle handle) {
        if (handle != hoveredHandle_ && handle != draggedHandle_)
            return;
        const auto text = hz >= 1000.0f ? juce::String(hz / 1000.0f, 2) + " kHz"
                                        : juce::String(static_cast<int>(std::round(hz))) + " Hz";
        g.setColour(DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
        g.setFont(11.0f);
        const int textW = 64;
        const int textH = 14;
        const float lx =
            juce::jlimit(plot.getX() + 2.0f, plot.getRight() - textW - 2.0f, x - textW * 0.5f);
        g.drawText(text,
                   juce::Rectangle<float>(lx, plot.getY() + 2.0f, textW, textH).toNearestInt(),
                   juce::Justification::centred);
    };
    drawLabel(minX, minHz_, Handle::MinHz);
    drawLabel(maxX, maxHz_, Handle::MaxHz);
}

const CompiledPresentationSpec& getMagdaPhaserPresentation() {
    static const CompiledPresentationSpec kSpec{
        .pluginId = magda::daw::audio::compiled::MagdaPhaserCompiledPlugin::xmlTypeName,
        .layoutCellCount = 7,
        .layoutCellsPerRow = 7,
        .createPanel = [](juce::String pluginId) -> std::unique_ptr<CompiledDevicePanel> {
            return std::make_unique<CompiledPhaserCurveView>(pluginId);
        },
        .suppressLegacyUis = {},
    };
    return kSpec;
}

}  // namespace magda::daw::ui
