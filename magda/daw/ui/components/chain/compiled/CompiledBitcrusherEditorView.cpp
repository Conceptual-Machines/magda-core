#include "compiled/CompiledBitcrusherEditorView.hpp"

#include <cmath>

#include "audio/plugins/compiled/MagdaBitcrusherCompiledPlugin.hpp"
#include "ui/themes/DarkTheme.hpp"

namespace magda::daw::ui {

namespace {

constexpr int kPollMs = 50;
constexpr float kPadX = 10.0f;
constexpr float kPadY = 8.0f;

float valueForSlot(const magda::DeviceInfo& device, int slotIndex, float fallback) {
    for (const auto& param : device.parameters)
        if (param.paramIndex == slotIndex)
            return param.currentValue;
    return fallback;
}

juce::String formatRate(float hz) {
    if (hz >= 1000.0f)
        return juce::String(hz / 1000.0f, hz >= 10000.0f ? 0 : 1) + " kHz";
    return juce::String(static_cast<int>(std::round(hz))) + " Hz";
}

}  // namespace

CompiledBitcrusherEditorView::CompiledBitcrusherEditorView(juce::String /*pluginId*/) {
    setInterceptsMouseClicks(false, false);
    startTimer(kPollMs);
}

void CompiledBitcrusherEditorView::setCompiledPlugin(
    magda::daw::audio::compiled::MagdaBitcrusherCompiledPlugin* plugin) {
    compiledPlugin_ = plugin;
}

void CompiledBitcrusherEditorView::bindPlugin(te::Plugin* plugin) {
    setCompiledPlugin(
        dynamic_cast<magda::daw::audio::compiled::MagdaBitcrusherCompiledPlugin*>(plugin));
}

void CompiledBitcrusherEditorView::updateFromDevice(const magda::DeviceInfo& device) {
    deviceSnapshot_ = device;
    resampleFromDevice();
    repaint();
}

void CompiledBitcrusherEditorView::resampleFromDevice() {
    bits_ = valueForSlot(deviceSnapshot_, Plugin::kBitsSlot, bits_);
    rateHz_ = valueForSlot(deviceSnapshot_, Plugin::kRateSlot, rateHz_);
}

void CompiledBitcrusherEditorView::timerCallback() {
    if (compiledPlugin_ != nullptr) {
        auto read = [this](int slot, float fallback) {
            if (auto* p = compiledPlugin_->getSlotParameter(slot))
                return compiledPlugin_->nativeValueToDisplayValue(slot, p->getCurrentValue());
            return fallback;
        };
        bits_ = read(Plugin::kBitsSlot, bits_);
        rateHz_ = read(Plugin::kRateSlot, rateHz_);
    }
    repaint();
}

void CompiledBitcrusherEditorView::paint(juce::Graphics& g) {
    auto bounds = getLocalBounds().toFloat();
    g.setColour(DarkTheme::getColour(DarkTheme::SURFACE));
    g.fillRoundedRectangle(bounds, 4.0f);

    auto plot = bounds.reduced(kPadX, kPadY);
    if (plot.getWidth() < 32.0f || plot.getHeight() < 16.0f)
        return;

    const auto textColour = DarkTheme::getColour(DarkTheme::TEXT_PRIMARY);
    const auto accent = DarkTheme::getColour(DarkTheme::ACCENT_CYAN);

    // Centre axis.
    g.setColour(textColour.withAlpha(0.15f));
    g.drawLine(plot.getX(), plot.getCentreY(), plot.getRight(), plot.getCentreY(), 1.0f);
    g.drawLine(plot.getCentreX(), plot.getY(), plot.getCentreX(), plot.getBottom(), 1.0f);

    // Quantization staircase: y = floor(x * L + 0.5) / L for input x in [-1, 1].
    // Cap visible step count so 16-bit doesn't melt the renderer.
    const int bitsInt = juce::jlimit(1, 16, static_cast<int>(std::round(bits_)));
    const float levels = std::pow(2.0f, static_cast<float>(bitsInt - 1));

    auto inputToY = [&](float yNorm) {
        return juce::jmap(yNorm, -1.0f, 1.0f, plot.getBottom(), plot.getY());
    };
    auto inputToX = [&](float xNorm) {
        return juce::jmap(xNorm, -1.0f, 1.0f, plot.getX(), plot.getRight());
    };

    juce::Path stair;
    const int samples = juce::jmin(256, static_cast<int>(plot.getWidth()) * 2);
    for (int i = 0; i <= samples; ++i) {
        const float x =
            juce::jmap(static_cast<float>(i), 0.0f, static_cast<float>(samples), -1.0f, 1.0f);
        const float q = std::floor(x * levels + 0.5f) / levels;
        const juce::Point<float> pt(inputToX(x), inputToY(q));
        if (i == 0)
            stair.startNewSubPath(pt);
        else
            stair.lineTo(pt);
    }

    g.setColour(accent.withAlpha(0.95f));
    g.strokePath(stair, juce::PathStrokeType(1.5f));

    // Rate readout top-right.
    g.setColour(textColour.withAlpha(0.55f));
    g.drawFittedText(formatRate(rateHz_),
                     juce::Rectangle<int>(static_cast<int>(plot.getRight()) - 80,
                                          static_cast<int>(plot.getY()) + 2, 76, 14),
                     juce::Justification::right, 1);
    g.drawFittedText(juce::String(bitsInt) + " bits",
                     juce::Rectangle<int>(static_cast<int>(plot.getX()),
                                          static_cast<int>(plot.getY()) + 2, 76, 14),
                     juce::Justification::left, 1);
}

const CompiledPresentationSpec& getMagdaBitcrusherPresentation() {
    static const CompiledPresentationSpec kSpec{
        .pluginId = magda::daw::audio::compiled::MagdaBitcrusherCompiledPlugin::xmlTypeName,
        .layoutCellCount =
            magda::daw::audio::compiled::MagdaBitcrusherCompiledPlugin::kHostSlotCount,
        .layoutCellsPerRow = 6,
        .createPanel = [](juce::String pluginId) -> std::unique_ptr<CompiledDevicePanel> {
            return std::make_unique<CompiledBitcrusherEditorView>(pluginId);
        },
    };
    return kSpec;
}

}  // namespace magda::daw::ui
