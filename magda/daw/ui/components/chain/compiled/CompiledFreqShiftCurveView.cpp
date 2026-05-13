#include "compiled/CompiledFreqShiftCurveView.hpp"

#include <algorithm>
#include <cmath>

#include "audio/plugins/compiled/MagdaFreqShiftCompiledPlugin.hpp"
#include "ui/themes/DarkTheme.hpp"

namespace magda::daw::ui {

namespace {

constexpr float kPlotPadX = 8.0f;
constexpr float kPlotPadY = 8.0f;
constexpr int kPollMs = 33;
constexpr float kAxisMaxHz = 2000.0f;
constexpr float kReferenceHz = 500.0f;
constexpr float kSpreadHalfHz = 25.0f;  // mirror SPREAD_HALF_HZ in the DSP

float valueForSlot(const magda::DeviceInfo& device, int slotIndex, float fallback) {
    for (const auto& param : device.parameters)
        if (param.paramIndex == slotIndex)
            return param.currentValue;
    return fallback;
}

float hzToX(float hz, const juce::Rectangle<float>& plot) {
    const float t = juce::jlimit(0.0f, 1.0f, hz / kAxisMaxHz);
    return plot.getX() + t * plot.getWidth();
}

}  // namespace

CompiledFreqShiftCurveView::CompiledFreqShiftCurveView(juce::String /*pluginId*/) {
    setInterceptsMouseClicks(false, false);
    startTimer(kPollMs);
}

void CompiledFreqShiftCurveView::setCompiledPlugin(
    magda::daw::audio::compiled::MagdaFreqShiftCompiledPlugin* plugin) {
    compiledPlugin_ = plugin;
}

void CompiledFreqShiftCurveView::bindPlugin(te::Plugin* plugin) {
    setCompiledPlugin(
        dynamic_cast<magda::daw::audio::compiled::MagdaFreqShiftCompiledPlugin*>(plugin));
}

void CompiledFreqShiftCurveView::updateFromDevice(const magda::DeviceInfo& device) {
    deviceSnapshot_ = device;
    resampleFromPlugin();
    repaint();
}

void CompiledFreqShiftCurveView::timerCallback() {
    using FS = magda::daw::audio::compiled::MagdaFreqShiftCompiledPlugin;

    auto readPluginSlot = [this](int slot, float fallback) {
        if (compiledPlugin_ == nullptr)
            return fallback;
        if (auto* p = compiledPlugin_->getSlotParameter(slot))
            return compiledPlugin_->nativeValueToDisplayValue(slot, p->getCurrentValue());
        return fallback;
    };

    float shift = shiftHz_;
    float fb = feedback_;
    float mix = mix_;
    float spread = spread_;

    if (compiledPlugin_ != nullptr) {
        shift = readPluginSlot(FS::kShiftSlot, shift);
        fb = readPluginSlot(FS::kFeedbackSlot, fb);
        mix = readPluginSlot(FS::kMixSlot, mix);
        spread = readPluginSlot(FS::kSpreadSlot, spread);
    } else {
        shift = valueForSlot(deviceSnapshot_, FS::kShiftSlot, shift);
        fb = valueForSlot(deviceSnapshot_, FS::kFeedbackSlot, fb);
        mix = valueForSlot(deviceSnapshot_, FS::kMixSlot, mix);
        spread = valueForSlot(deviceSnapshot_, FS::kSpreadSlot, spread);
    }

    const bool moved = std::fabs(shift - shiftHz_) > 0.05f || std::fabs(fb - feedback_) > 0.001f ||
                       std::fabs(mix - mix_) > 0.001f || std::fabs(spread - spread_) > 0.001f;
    if (moved) {
        shiftHz_ = shift;
        feedback_ = fb;
        mix_ = juce::jlimit(0.0f, 1.0f, mix);
        spread_ = juce::jlimit(0.0f, 1.0f, spread);
        repaint();
    }
}

void CompiledFreqShiftCurveView::resampleFromPlugin() {
    using FS = magda::daw::audio::compiled::MagdaFreqShiftCompiledPlugin;
    shiftHz_ = valueForSlot(deviceSnapshot_, FS::kShiftSlot, shiftHz_);
    feedback_ = valueForSlot(deviceSnapshot_, FS::kFeedbackSlot, feedback_);
    mix_ = juce::jlimit(0.0f, 1.0f, valueForSlot(deviceSnapshot_, FS::kMixSlot, mix_));
    spread_ = juce::jlimit(0.0f, 1.0f, valueForSlot(deviceSnapshot_, FS::kSpreadSlot, spread_));
}

void CompiledFreqShiftCurveView::paint(juce::Graphics& g) {
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

    const auto accent = DarkTheme::getColour(DarkTheme::ACCENT_PURPLE);
    const auto dimText = DarkTheme::getColour(DarkTheme::TEXT_PRIMARY).withAlpha(0.55f);

    // Linear frequency axis grid at 500 Hz / 1 kHz / 1.5 kHz.
    g.setColour(DarkTheme::getColour(DarkTheme::BORDER).withAlpha(0.22f));
    for (float gridHz : {500.0f, 1000.0f, 1500.0f}) {
        const float x = hzToX(gridHz, plot);
        g.drawVerticalLine(static_cast<int>(std::round(x)), plot.getY(), plot.getBottom());
    }

    // Big shift readout, top centre.
    const juce::String sign = shiftHz_ > 0.0f ? "+" : "";
    const juce::String shiftLabel = sign + juce::String(shiftHz_, 1) + " Hz";
    g.setColour(DarkTheme::getColour(DarkTheme::TEXT_PRIMARY).withAlpha(0.9f));
    g.setFont(18.0f);
    g.drawText(shiftLabel,
               juce::Rectangle<float>(plot.getX(), plot.getY() + 4.0f, plot.getWidth(), 22.0f)
                   .toNearestInt(),
               juce::Justification::centred);

    // Direction hint under the big number.
    const juce::String dirLabel = std::fabs(shiftHz_) < 0.5f ? juce::String("BYPASS")
                                  : shiftHz_ > 0.0f ? juce::String::fromUTF8("\xe2\x96\xb2 UP")
                                                    : juce::String::fromUTF8("\xe2\x96\xbc DOWN");
    g.setColour(dimText);
    g.setFont(10.0f);
    g.drawText(dirLabel,
               juce::Rectangle<float>(plot.getX(), plot.getY() + 26.0f, plot.getWidth(), 12.0f)
                   .toNearestInt(),
               juce::Justification::centred);

    // Arrow strip — sits in the bottom third of the panel.
    const float stripY = plot.getBottom() - 36.0f;
    const float stripH = 28.0f;
    juce::Rectangle<float> strip(plot.getX(), stripY, plot.getWidth(), stripH);

    // Axis line.
    const float axisY = strip.getCentreY();
    g.setColour(DarkTheme::getColour(DarkTheme::BORDER).withAlpha(0.45f));
    g.drawHorizontalLine(static_cast<int>(std::round(axisY)), strip.getX(), strip.getRight());

    // Decade tick labels.
    g.setFont(9.0f);
    g.setColour(dimText);
    for (float labelHz : {500.0f, 1000.0f, 1500.0f}) {
        const float x = hzToX(labelHz, strip);
        const juce::String t = labelHz >= 1000.0f ? juce::String(labelHz / 1000.0f, 1) + "k"
                                                  : juce::String(labelHz, 0);
        g.drawText(t,
                   juce::Rectangle<float>(x - 20.0f, strip.getBottom() - 12.0f, 40.0f, 12.0f)
                       .toNearestInt(),
                   juce::Justification::centred);
    }

    // Input marker at reference Hz.
    const float inX = hzToX(kReferenceHz, strip);
    g.setColour(DarkTheme::getColour(DarkTheme::TEXT_PRIMARY).withAlpha(0.55f));
    g.fillEllipse(inX - 4.0f, axisY - 4.0f, 8.0f, 8.0f);

    // Output markers — L and R diverge with spread.
    const float outHzL =
        juce::jlimit(0.0f, kAxisMaxHz, kReferenceHz + shiftHz_ - spread_ * kSpreadHalfHz);
    const float outHzR =
        juce::jlimit(0.0f, kAxisMaxHz, kReferenceHz + shiftHz_ + spread_ * kSpreadHalfHz);
    const float outXL = hzToX(outHzL, strip);
    const float outXR = hzToX(outHzR, strip);

    // Arrow from input to (midpoint of) output, accent colour.
    const float outXMid = (outXL + outXR) * 0.5f;
    g.setColour(accent.withAlpha(0.85f));
    g.drawLine(inX, axisY, outXMid, axisY, 1.8f);

    // Arrowhead at output midpoint.
    if (std::fabs(shiftHz_) > 0.5f) {
        const float dir = outXMid > inX ? 1.0f : -1.0f;
        juce::Path head;
        head.startNewSubPath(outXMid, axisY);
        head.lineTo(outXMid - dir * 6.0f, axisY - 4.0f);
        head.lineTo(outXMid - dir * 6.0f, axisY + 4.0f);
        head.closeSubPath();
        g.fillPath(head);
    }

    // Output dots — separate L/R when spread > 0.
    g.setColour(accent);
    g.fillEllipse(outXL - 3.5f, axisY - 3.5f, 7.0f, 7.0f);
    if (spread_ > 0.001f && std::abs(outXR - outXL) > 1.5f)
        g.fillEllipse(outXR - 3.5f, axisY - 3.5f, 7.0f, 7.0f);
}

const CompiledPresentationSpec& getMagdaFreqShiftPresentation() {
    static const CompiledPresentationSpec kSpec{
        .pluginId = magda::daw::audio::compiled::MagdaFreqShiftCompiledPlugin::xmlTypeName,
        .layoutCellCount = 4,
        .layoutCellsPerRow = 4,
        .createPanel = [](juce::String pluginId) -> std::unique_ptr<CompiledDevicePanel> {
            return std::make_unique<CompiledFreqShiftCurveView>(pluginId);
        },
        .suppressLegacyUis = {},
    };
    return kSpec;
}

}  // namespace magda::daw::ui
