#include "compiled/CompiledReverbCurveView.hpp"

#include <algorithm>
#include <cmath>

#include "audio/plugins/compiled/MagdaReverbCompiledPlugin.hpp"
#include "ui/themes/DarkTheme.hpp"

namespace magda::daw::ui {

namespace {

constexpr float kPlotPadX = 8.0f;
constexpr float kPlotPadY = 8.0f;
constexpr int kPollMs = 50;
constexpr int kPathSamples = 160;
constexpr float kTimeAxisMaxSeconds = 8.0f;
constexpr float kVisibilityFloor = 0.005f;  // anything below this counts as silence

float valueForSlot(const magda::DeviceInfo& device, int slotIndex, float fallback) {
    for (const auto& param : device.parameters)
        if (param.paramIndex == slotIndex)
            return param.currentValue;
    return fallback;
}

const char* engineLabelFor(int engineIndex) {
    switch (engineIndex) {
        case 0:
            return "Plate";
        case 1:
            return "Hall";
        case 2:
            return "Room";
        default:
            return "";
    }
}

}  // namespace

CompiledReverbCurveView::CompiledReverbCurveView(juce::String /*pluginId*/) {
    setInterceptsMouseClicks(false, false);
    startTimer(kPollMs);
}

void CompiledReverbCurveView::setCompiledPlugin(
    magda::daw::audio::compiled::MagdaReverbCompiledPlugin* plugin) {
    compiledPlugin_ = plugin;
}

void CompiledReverbCurveView::bindPlugin(te::Plugin* plugin) {
    setCompiledPlugin(
        dynamic_cast<magda::daw::audio::compiled::MagdaReverbCompiledPlugin*>(plugin));
}

void CompiledReverbCurveView::updateFromDevice(const magda::DeviceInfo& device) {
    deviceSnapshot_ = device;
    resampleFromDevice();
    repaint();
}

float CompiledReverbCurveView::t60SecondsForEngine(int engineIndex, float decayDisplay) const {
    // Mirrors the time mappings inside each engine .dsp so the visual
    // matches what the user hears at a given Decay slot value.
    const float d01 = juce::jlimit(0.0f, 1.0f, decayDisplay / 100.0f);
    switch (engineIndex) {
        case 1:  // Hall — magda_reverb_hall.dsp: t60m = 0.5 + decay * 0.145
            return 0.5f + decayDisplay * 0.145f;
        case 2:  // Room — Freeverb fb1 → t60. Rough fit: 0.3..3.5s.
            return 0.3f + d01 * 3.2f;
        case 0:  // Plate — Dattorro decay coeff ~ d01*0.99 → rough fit to t60 0.3..6s.
        default:
            return 0.3f + d01 * 5.7f;
    }
}

void CompiledReverbCurveView::timerCallback() {
    using Rev = magda::daw::audio::compiled::MagdaReverbCompiledPlugin;

    if (compiledPlugin_ == nullptr) {
        // No live plugin — keep the cached deviceSnapshot_ values.
        repaint();
        return;
    }

    auto read = [this](int slot, float fallback) {
        if (auto* p = compiledPlugin_->getSlotParameter(slot))
            return compiledPlugin_->nativeValueToDisplayValue(slot, p->getCurrentValue());
        return fallback;
    };

    engine_ = juce::jlimit(0, Rev::kEngineCount - 1,
                           static_cast<int>(std::round(read(Rev::kEngineSlot, 0.0f))));
    decay_ = read(Rev::kDecaySlot, decay_);
    damping_ = read(Rev::kDampingSlot, damping_);
    predelayMs_ = read(Rev::kPredelaySlot, predelayMs_);

    repaint();
}

void CompiledReverbCurveView::resampleFromDevice() {
    using Rev = magda::daw::audio::compiled::MagdaReverbCompiledPlugin;
    engine_ = juce::jlimit(
        0, Rev::kEngineCount - 1,
        static_cast<int>(std::round(valueForSlot(deviceSnapshot_, Rev::kEngineSlot, 0.0f))));
    decay_ = valueForSlot(deviceSnapshot_, Rev::kDecaySlot, decay_);
    damping_ = valueForSlot(deviceSnapshot_, Rev::kDampingSlot, damping_);
    predelayMs_ = valueForSlot(deviceSnapshot_, Rev::kPredelaySlot, predelayMs_);
}

void CompiledReverbCurveView::paint(juce::Graphics& g) {
    auto bounds = getLocalBounds().toFloat();
    g.setColour(DarkTheme::getColour(DarkTheme::SURFACE));
    g.fillRoundedRectangle(bounds, 4.0f);

    plotArea_ = bounds.reduced(kPlotPadX, kPlotPadY);
    if (plotArea_.getWidth() < 8.0f || plotArea_.getHeight() < 8.0f)
        return;

    // Time mappings — both curves share the same axis. We auto-scale the
    // x-axis to the longer of the two t60s plus a margin, capped at
    // kTimeAxisMaxSeconds so very short tails still fill the plot.
    const float damping01 = juce::jlimit(0.0f, 1.0f, damping_ / 100.0f);
    const float t60Low = t60SecondsForEngine(engine_, decay_);
    const float t60High = std::max(0.05f, t60Low * (1.0f - damping01 * 0.75f));
    const float predelayS = juce::jlimit(0.0f, 1.0f, predelayMs_ * 0.001f);
    const float visibleSeconds = juce::jlimit(0.5f, kTimeAxisMaxSeconds, predelayS + t60Low * 1.4f);

    auto timeToX = [&](float t) {
        const float u = juce::jlimit(0.0f, 1.0f, t / visibleSeconds);
        return plotArea_.getX() + u * plotArea_.getWidth();
    };
    auto ampToY = [&](float amp) {
        const float u = juce::jlimit(0.0f, 1.0f, amp);
        return plotArea_.getBottom() - u * plotArea_.getHeight();
    };
    auto envelopeAt = [&](float t, float t60) {
        if (t <= predelayS)
            return 0.0f;
        const float dt = t - predelayS;
        // Standard t60 mapping: amplitude reaches -60 dB (10^-3) at t = t60.
        return std::exp(-3.0f * 2.302585f * dt / std::max(0.001f, t60));
    };

    // Build paths for the two envelopes; fill the low band, then the
    // high band on top so the user reads the gap between them as "Damping".
    juce::Path lowFill;
    juce::Path highFill;
    lowFill.startNewSubPath(timeToX(0.0f), ampToY(0.0f));
    highFill.startNewSubPath(timeToX(0.0f), ampToY(0.0f));

    for (int i = 0; i <= kPathSamples; ++i) {
        const float u = static_cast<float>(i) / static_cast<float>(kPathSamples);
        const float t = u * visibleSeconds;
        lowFill.lineTo(timeToX(t), ampToY(envelopeAt(t, t60Low)));
        highFill.lineTo(timeToX(t), ampToY(envelopeAt(t, t60High)));
    }
    lowFill.lineTo(timeToX(visibleSeconds), ampToY(0.0f));
    highFill.lineTo(timeToX(visibleSeconds), ampToY(0.0f));
    lowFill.closeSubPath();
    highFill.closeSubPath();

    const auto accentLow = DarkTheme::getColour(DarkTheme::ACCENT_CYAN).withAlpha(0.32f);
    const auto accentHigh = DarkTheme::getColour(DarkTheme::ACCENT_CYAN).withAlpha(0.7f);

    g.setColour(accentLow);
    g.fillPath(lowFill);
    g.setColour(accentHigh);
    g.fillPath(highFill);

    // Predelay strip — slightly muted, signals "silent pre-roll".
    if (predelayS > 0.0f) {
        const float x0 = timeToX(0.0f);
        const float x1 = timeToX(predelayS);
        g.setColour(DarkTheme::getColour(DarkTheme::TEXT_PRIMARY).withAlpha(0.08f));
        g.fillRect(juce::Rectangle<float>(x0, plotArea_.getY(), x1 - x0, plotArea_.getHeight()));
    }

    // Baseline + bounding rect.
    g.setColour(DarkTheme::getColour(DarkTheme::TEXT_PRIMARY).withAlpha(0.18f));
    g.drawLine(plotArea_.getX(), plotArea_.getBottom(), plotArea_.getRight(), plotArea_.getBottom(),
               1.0f);

    // t60 marker (low band) — vertical line at predelay + t60Low if visible.
    const float t60Time = predelayS + t60Low;
    if (t60Time < visibleSeconds) {
        const float xT60 = timeToX(t60Time);
        g.setColour(DarkTheme::getColour(DarkTheme::TEXT_PRIMARY).withAlpha(0.35f));
        const float dashes[] = {3.0f, 3.0f};
        juce::Line<float> line(xT60, plotArea_.getY() + 4.0f, xT60, plotArea_.getBottom());
        g.drawDashedLine(line, dashes, 2, 1.0f);
    }

    // Engine label, top-right.
    g.setColour(DarkTheme::getColour(DarkTheme::TEXT_PRIMARY).withAlpha(0.55f));
    g.drawFittedText(engineLabelFor(engine_),
                     juce::Rectangle<int>(static_cast<int>(plotArea_.getRight()) - 60,
                                          static_cast<int>(plotArea_.getY()) + 2, 56, 14),
                     juce::Justification::right, 1);

    // Decay readout, top-left.
    g.drawFittedText(juce::String(t60Low, t60Low >= 10.0f ? 1 : 2) + " s",
                     juce::Rectangle<int>(static_cast<int>(plotArea_.getX()) + 4,
                                          static_cast<int>(plotArea_.getY()) + 2, 60, 14),
                     juce::Justification::left, 1);

    juce::ignoreUnused(kVisibilityFloor);
}

const CompiledPresentationSpec& getMagdaReverbPresentation() {
    static const LegacyUiKind kSuppress[] = {LegacyUiKind::Reverb};
    static const CompiledPresentationSpec kSpec{
        .pluginId = magda::daw::audio::compiled::MagdaReverbCompiledPlugin::xmlTypeName,
        .layoutCellCount = 9,
        .layoutCellsPerRow = 9,
        .createPanel = [](juce::String pluginId) -> std::unique_ptr<CompiledDevicePanel> {
            return std::make_unique<CompiledReverbCurveView>(pluginId);
        },
        .suppressLegacyUis = kSuppress,
    };
    return kSpec;
}

}  // namespace magda::daw::ui
