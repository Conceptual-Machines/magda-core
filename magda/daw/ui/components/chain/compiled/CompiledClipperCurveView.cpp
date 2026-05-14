#include "compiled/CompiledClipperCurveView.hpp"

#include <algorithm>
#include <cmath>

#include "audio/plugins/compiled/MagdaClipperCompiledPlugin.hpp"
#include "ui/themes/DarkTheme.hpp"
#include "ui/themes/FontManager.hpp"

namespace magda::daw::ui {

namespace {

constexpr float kPlotPad = 8.0f;
constexpr int kPollMs = 33;
// Plot input axis from -1.5 to +1.5 — wide enough to show clipping
// behaviour past unity, narrow enough to keep the curve readable.
constexpr float kInputRange = 1.5f;
constexpr int kCurveSamples = 96;

float valueForSlot(const magda::DeviceInfo& device, int slotIndex, float fallback) {
    for (const auto& param : device.parameters)
        if (param.paramIndex == slotIndex)
            return param.currentValue;
    return fallback;
}

// Plain (non-ADAA) versions of each clipper's static formula. ADAA is
// only meaningful for audio antialiasing; the visualisation is a static
// snapshot of the curve shape, where the underlying f(x) is what
// matters. These match aa.hardclip/softclipQuadratic1/etc.
float clipHard(float x) {
    return juce::jlimit(-1.0f, 1.0f, x);
}

float clipSoftQuadratic(float x) {
    // 3-piece quadratic from aanl.lib softclipEnv.softclip.
    const float a = std::fabs(x);
    const float s = x >= 0.0f ? 1.0f : -1.0f;
    if (a < 1.0f / 3.0f)
        return 2.0f * x;
    if (a < 2.0f / 3.0f) {
        const float t = 2.0f - a * 3.0f;
        return s * (3.0f - t * t) / 3.0f;
    }
    return s;
}

float clipTanh(float x) {
    return std::tanh(x);
}

float clipHyperbolic(float x) {
    return x / (1.0f + std::fabs(x));
}

float clipSinArctan(float x) {
    return x / std::sqrt(1.0f + x * x);
}

float clipForMode(int mode, float x) {
    switch (mode) {
        case 0:
            return clipHard(x);
        case 1:
            return clipSoftQuadratic(x);
        case 2:
            return clipTanh(x);
        case 3:
            return clipHyperbolic(x);
        case 4:
            return clipSinArctan(x);
        default:
            return clipHard(x);
    }
}

}  // namespace

CompiledClipperCurveView::CompiledClipperCurveView(juce::String /*pluginId*/) {
    setInterceptsMouseClicks(false, false);
    startTimer(kPollMs);
}

void CompiledClipperCurveView::setCompiledPlugin(
    magda::daw::audio::compiled::MagdaClipperCompiledPlugin* plugin) {
    compiledPlugin_ = plugin;
}

void CompiledClipperCurveView::bindPlugin(te::Plugin* plugin) {
    setCompiledPlugin(
        dynamic_cast<magda::daw::audio::compiled::MagdaClipperCompiledPlugin*>(plugin));
}

void CompiledClipperCurveView::updateFromDevice(const magda::DeviceInfo& device) {
    deviceSnapshot_ = device;
    resampleFromDevice();
    repaint();
}

void CompiledClipperCurveView::timerCallback() {
    using Clip = magda::daw::audio::compiled::MagdaClipperCompiledPlugin;

    auto readPluginSlot = [this](int slot, float fallback) {
        if (compiledPlugin_ == nullptr)
            return fallback;
        if (auto* p = compiledPlugin_->getSlotParameter(slot))
            return compiledPlugin_->nativeValueToDisplayValue(slot, p->getCurrentValue());
        return fallback;
    };

    float drive = driveDb_;
    float modeF = static_cast<float>(mode_);

    if (compiledPlugin_ != nullptr) {
        drive = readPluginSlot(Clip::kDriveSlot, drive);
        modeF = readPluginSlot(Clip::kModeSlot, modeF);
        inputPeakDb_ = compiledPlugin_->getInputPeakDb();
    } else {
        drive = valueForSlot(deviceSnapshot_, Clip::kDriveSlot, drive);
        modeF = valueForSlot(deviceSnapshot_, Clip::kModeSlot, modeF);
    }

    driveDb_ = drive;
    mode_ = juce::jlimit(0, Clip::kModeCount - 1, static_cast<int>(std::round(modeF)));

    // Smooth the input amplitude shown by the riding dot so it doesn't
    // jitter every block. Fast attack, slow release — same envelope shape
    // as the compressor curve view.
    const float inAmp = std::pow(10.0f, inputPeakDb_ / 20.0f);
    const float coeff = inAmp > smoothedInputAmp_ ? 0.45f : 0.88f;
    smoothedInputAmp_ = smoothedInputAmp_ * coeff + inAmp * (1.0f - coeff);

    repaint();
}

void CompiledClipperCurveView::resampleFromDevice() {
    using Clip = magda::daw::audio::compiled::MagdaClipperCompiledPlugin;
    driveDb_ = valueForSlot(deviceSnapshot_, Clip::kDriveSlot, driveDb_);
    const float m = valueForSlot(deviceSnapshot_, Clip::kModeSlot, static_cast<float>(mode_));
    mode_ = juce::jlimit(0, Clip::kModeCount - 1, static_cast<int>(std::round(m)));
}

void CompiledClipperCurveView::paint(juce::Graphics& g) {
    const auto bounds = getLocalBounds();
    g.setColour(DarkTheme::getColour(DarkTheme::BACKGROUND).darker(0.06f));
    g.fillRect(bounds);

    auto plot = bounds.toFloat().reduced(kPlotPad, kPlotPad);
    if (plot.getWidth() < 32.0f || plot.getHeight() < 32.0f)
        return;

    const auto border = DarkTheme::getColour(DarkTheme::BORDER);
    const auto accent = DarkTheme::getColour(DarkTheme::ACCENT_ORANGE);

    g.setColour(border.withAlpha(0.55f));
    g.drawRect(plot, 1.0f);

    juce::Graphics::ScopedSaveState clipGuard(g);
    g.reduceClipRegion(plot.toNearestInt());

    auto xToScreen = [&](float x) {
        const float t = (x + kInputRange) / (2.0f * kInputRange);
        return plot.getX() + juce::jlimit(0.0f, 1.0f, t) * plot.getWidth();
    };
    auto yToScreen = [&](float y) {
        const float t = (y + kInputRange) / (2.0f * kInputRange);
        return plot.getBottom() - juce::jlimit(0.0f, 1.0f, t) * plot.getHeight();
    };

    // Centre crosshair (0 dB axes).
    g.setColour(border.withAlpha(0.18f));
    const float zeroX = xToScreen(0.0f);
    const float zeroY = yToScreen(0.0f);
    g.drawLine(zeroX, plot.getY(), zeroX, plot.getBottom(), 1.0f);
    g.drawLine(plot.getX(), zeroY, plot.getRight(), zeroY, 1.0f);

    // ±1 grid lines (the natural clip ceiling for bounded curves).
    g.setColour(border.withAlpha(0.28f));
    for (float v : {-1.0f, 1.0f}) {
        const float xs = xToScreen(v);
        const float ys = yToScreen(v);
        g.drawLine(xs, plot.getY(), xs, plot.getBottom(), 1.0f);
        g.drawLine(plot.getX(), ys, plot.getRight(), ys, 1.0f);
    }

    // Reference diagonal — what y = x looks like (no clipping).
    g.setColour(border.withAlpha(0.25f));
    g.drawLine(xToScreen(-kInputRange), yToScreen(-kInputRange), xToScreen(kInputRange),
               yToScreen(kInputRange), 1.0f);

    // Active mode's transfer curve.
    juce::Path curve;
    bool started = false;
    for (int i = 0; i < kCurveSamples; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(kCurveSamples - 1);
        const float xIn = juce::jmap(t, -kInputRange, kInputRange);
        const float yOut = clipForMode(mode_, xIn);
        const float sx = xToScreen(xIn);
        const float sy = yToScreen(yOut);
        if (!started) {
            curve.startNewSubPath(sx, sy);
            started = true;
        } else {
            curve.lineTo(sx, sy);
        }
    }
    g.setColour(accent.withAlpha(0.9f));
    g.strokePath(curve, juce::PathStrokeType(1.8f, juce::PathStrokeType::curved,
                                             juce::PathStrokeType::rounded));

    // Riding dot at (driven input peak, curve(driven input peak)).
    const float driveLin = std::pow(10.0f, driveDb_ / 20.0f);
    const float drivenAmp = smoothedInputAmp_ * driveLin;
    const float dotX = xToScreen(drivenAmp);
    const float dotY = yToScreen(clipForMode(mode_, drivenAmp));
    g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_GREEN).withAlpha(0.95f));
    g.fillEllipse(dotX - 3.5f, dotY - 3.5f, 7.0f, 7.0f);
}

const CompiledPresentationSpec& getMagdaClipperPresentation() {
    static const CompiledPresentationSpec kSpec{
        .pluginId = magda::daw::audio::compiled::MagdaClipperCompiledPlugin::xmlTypeName,
        .layoutCellCount = 3,
        .layoutCellsPerRow = 3,
        .createPanel = [](juce::String pluginId) -> std::unique_ptr<CompiledDevicePanel> {
            return std::make_unique<CompiledClipperCurveView>(pluginId);
        },
        .suppressLegacyUis = {},
    };
    return kSpec;
}

}  // namespace magda::daw::ui
