#include "compiled/CompiledSaturatorCurveView.hpp"

#include <algorithm>
#include <cmath>

#include "audio/plugins/compiled/MagdaSaturatorCompiledPlugin.hpp"
#include "ui/themes/DarkTheme.hpp"
#include "ui/themes/FontManager.hpp"

namespace magda::daw::ui {

namespace {

constexpr float kPlotPadX = 8.0f;
constexpr float kPlotPadY = 6.0f;
constexpr int kPollMs = 33;  // ~30 Hz, plenty for a static-when-still transfer curve

float dbToLinear(float dB) {
    return std::pow(10.0f, dB / 20.0f);
}

// One-knee soft limit — must match the DSP's `soft_limit` so the rendered
// curve agrees with what the audio path does.
constexpr float kSoftKnee = 0.85f;
float softLimit(float x) {
    const float ax = std::fabs(x);
    if (ax < kSoftKnee)
        return x;
    const float sign = (x < 0.0f) ? -1.0f : 1.0f;
    return sign *
           (kSoftKnee + (1.0f - kSoftKnee) * std::tanh((ax - kSoftKnee) / (1.0f - kSoftKnee)));
}

float valueForSlot(const magda::DeviceInfo& device, int slotIndex, float fallback) {
    for (const auto& param : device.parameters)
        if (param.paramIndex == slotIndex)
            return param.currentValue;
    return fallback;
}

}  // namespace

CompiledSaturatorCurveView::CompiledSaturatorCurveView(juce::String /*pluginId*/) {
    setInterceptsMouseClicks(false, false);
    startTimer(kPollMs);
}

void CompiledSaturatorCurveView::setCompiledPlugin(
    magda::daw::audio::compiled::MagdaSaturatorCompiledPlugin* plugin) {
    compiledPlugin_ = plugin;
}

void CompiledSaturatorCurveView::updateFromDevice(const magda::DeviceInfo& device) {
    deviceSnapshot_ = device;
    resampleFromPlugin();
    repaint();
}

void CompiledSaturatorCurveView::timerCallback() {
    using Sat = magda::daw::audio::compiled::MagdaSaturatorCompiledPlugin;

    // Snapshot the current values straight off the plugin's host params
    // when we have one (live audio-thread state); fall back to the device
    // snapshot otherwise (e.g. before initial wiring).
    float drive = driveDb_, bias = bias_, output = outputDb_, mix = mix_;
    int mode = modeIndex_;

    if (compiledPlugin_ != nullptr) {
        if (auto* p = compiledPlugin_->getSlotParameter(Sat::kDriveSlot))
            drive =
                compiledPlugin_->nativeValueToDisplayValue(Sat::kDriveSlot, p->getCurrentValue());
        if (auto* p = compiledPlugin_->getSlotParameter(Sat::kBiasSlot))
            bias = compiledPlugin_->nativeValueToDisplayValue(Sat::kBiasSlot, p->getCurrentValue());
        if (auto* p = compiledPlugin_->getSlotParameter(Sat::kOutputSlot))
            output =
                compiledPlugin_->nativeValueToDisplayValue(Sat::kOutputSlot, p->getCurrentValue());
        if (auto* p = compiledPlugin_->getSlotParameter(Sat::kMixSlot))
            mix = compiledPlugin_->nativeValueToDisplayValue(Sat::kMixSlot, p->getCurrentValue());
        if (auto* p = compiledPlugin_->getSlotParameter(Sat::kModeSlot))
            mode = static_cast<int>(std::round(
                compiledPlugin_->nativeValueToDisplayValue(Sat::kModeSlot, p->getCurrentValue())));
    } else {
        drive = valueForSlot(deviceSnapshot_, Sat::kDriveSlot, drive);
        bias = valueForSlot(deviceSnapshot_, Sat::kBiasSlot, bias);
        output = valueForSlot(deviceSnapshot_, Sat::kOutputSlot, output);
        mix = valueForSlot(deviceSnapshot_, Sat::kMixSlot, mix);
        mode = static_cast<int>(
            std::round(valueForSlot(deviceSnapshot_, Sat::kModeSlot, static_cast<float>(mode))));
    }

    const bool driveMoved = std::fabs(drive - driveDb_) > 0.001f;
    const bool biasMoved = std::fabs(bias - bias_) > 0.001f;
    const bool outputMoved = std::fabs(output - outputDb_) > 0.001f;
    const bool mixMoved = std::fabs(mix - mix_) > 0.001f;
    const bool modeMoved = mode != modeIndex_;

    if (driveMoved || biasMoved || outputMoved || mixMoved || modeMoved) {
        driveDb_ = drive;
        bias_ = bias;
        outputDb_ = output;
        mix_ = mix;
        modeIndex_ = mode;
        repaint();
    }
}

void CompiledSaturatorCurveView::resampleFromPlugin() {
    // First-paint seed: pull whatever's in the device snapshot so the
    // initial draw isn't a frame behind the timer.
    using Sat = magda::daw::audio::compiled::MagdaSaturatorCompiledPlugin;
    driveDb_ = valueForSlot(deviceSnapshot_, Sat::kDriveSlot, driveDb_);
    bias_ = valueForSlot(deviceSnapshot_, Sat::kBiasSlot, bias_);
    outputDb_ = valueForSlot(deviceSnapshot_, Sat::kOutputSlot, outputDb_);
    mix_ = valueForSlot(deviceSnapshot_, Sat::kMixSlot, mix_);
    modeIndex_ = static_cast<int>(
        std::round(valueForSlot(deviceSnapshot_, Sat::kModeSlot, static_cast<float>(modeIndex_))));
}

float CompiledSaturatorCurveView::shapeSample(Mode mode, float x) {
    // Mirrors magda_saturator.dsp's nonlinearity branches. Keep the same
    // formulas so the on-screen curve always matches what the audio runs.
    switch (mode) {
        case Mode::Tanh:
            return std::tanh(x);
        case Mode::Soft: {
            const float ax = std::fabs(x);
            if (ax < 1.0f) {
                const float sign = (x < 0.0f) ? -1.0f : 1.0f;
                return sign * (1.0f - std::exp(-ax));
            }
            return x - x * x * x / 3.0f;
        }
        case Mode::Hard:
            return std::max(-1.0f, std::min(1.0f, x));
        case Mode::Fold:
            return std::sin(x * juce::MathConstants<float>::pi * 0.5f);
        case Mode::Tube:
            return x > 0.0f ? std::tanh(x * 1.4f) : std::tanh(x * 1.0f);
        case Mode::Tape:
            return std::tanh(x) * (1.0f - 0.15f * x * x);
    }
    return std::tanh(x);
}

void CompiledSaturatorCurveView::paint(juce::Graphics& g) {
    const auto bounds = getLocalBounds();
    g.setColour(DarkTheme::getColour(DarkTheme::BACKGROUND).darker(0.06f));
    g.fillRect(bounds);

    auto plot = bounds.toFloat().reduced(kPlotPadX, kPlotPadY);
    if (plot.getWidth() < 8.0f || plot.getHeight() < 8.0f)
        return;

    g.setColour(DarkTheme::getColour(DarkTheme::BORDER).withAlpha(0.55f));
    g.drawRect(plot, 1.0f);

    const float midX = plot.getCentreX();
    const float midY = plot.getCentreY();
    const float halfW = plot.getWidth() * 0.5f;
    const float halfH = plot.getHeight() * 0.5f;

    // Axes — slightly brighter centre crosshair plus quarter rules. Same
    // grid the legacy MagdaDriveCurveView used so the visual reads
    // familiar.
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

    const float driveLin = dbToLinear(driveDb_);
    const float outputLin = dbToLinear(outputDb_);
    const Mode mode = static_cast<Mode>(juce::jlimit(0, 5, modeIndex_));

    // Sample the chain at one point per pixel. Mirror order matches
    // magda_saturator.dsp::sat_chain — drive → +bias → nl → output trim.
    // DC-block + tone tilt are left out: dcblock is by definition transparent
    // for the static transfer plot, and tone is frequency-dependent so it
    // can't be visualised on a single x→y curve. Soft-limit IS applied so
    // the curve shows the actual ceiling the bus output sees.
    const int numSamples = juce::jmax(64, static_cast<int>(std::ceil(plot.getWidth())));
    juce::Path curvePath;
    juce::Path fillPath;

    for (int i = 0; i < numSamples; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(numSamples - 1);
        const float xInput = -1.0f + 2.0f * t;
        const float wet = softLimit(shapeSample(mode, xInput * driveLin + bias_) * outputLin);
        const float yOutput = juce::jlimit(-1.0f, 1.0f, xInput * (1.0f - mix_) + wet * mix_);

        const float px = plot.getX() + t * plot.getWidth();
        const float py = midY - yOutput * halfH;

        if (i == 0) {
            curvePath.startNewSubPath(px, py);
            fillPath.startNewSubPath(px, midY);
            fillPath.lineTo(px, py);
        } else {
            curvePath.lineTo(px, py);
            fillPath.lineTo(px, py);
        }
    }
    fillPath.lineTo(plot.getRight(), midY);
    fillPath.closeSubPath();

    const auto accent = DarkTheme::getColour(DarkTheme::ACCENT_GREEN);
    g.setColour(accent.withAlpha(0.13f));
    g.fillPath(fillPath);
    g.setColour(accent.withAlpha(0.9f));
    g.strokePath(curvePath, juce::PathStrokeType(1.7f, juce::PathStrokeType::curved,
                                                 juce::PathStrokeType::rounded));
}

const CompiledPresentationSpec& getMagdaSaturatorPresentation() {
    static const CompiledPresentationSpec kSpec{
        .pluginId = magda::daw::audio::compiled::MagdaSaturatorCompiledPlugin::xmlTypeName,
        .layoutCellCount = 6,
        .layoutCellsPerRow = 6,
        .createPanel = [](juce::String pluginId) -> std::unique_ptr<CompiledDevicePanel> {
            return std::make_unique<CompiledSaturatorCurveView>(pluginId);
        },
        .suppressLegacyUis = {},
    };
    return kSpec;
}

void CompiledSaturatorCurveView::bindPlugin(te::Plugin* plugin) {
    setCompiledPlugin(
        dynamic_cast<magda::daw::audio::compiled::MagdaSaturatorCompiledPlugin*>(plugin));
}

}  // namespace magda::daw::ui
