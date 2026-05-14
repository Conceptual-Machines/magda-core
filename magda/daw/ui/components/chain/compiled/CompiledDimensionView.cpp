#include "compiled/CompiledDimensionView.hpp"

#include <cmath>

#include "audio/plugins/compiled/MagdaDimensionCompiledPlugin.hpp"
#include "ui/themes/DarkTheme.hpp"

namespace magda::daw::ui {

namespace {

constexpr int kPollMs = 50;
constexpr float kPadX = 8.0f;
constexpr float kPadY = 8.0f;

float valueForSlot(const magda::DeviceInfo& device, int slotIndex, float fallback) {
    for (const auto& param : device.parameters)
        if (param.paramIndex == slotIndex)
            return param.currentValue;
    return fallback;
}

const char* engineLabel(int engineIndex) {
    switch (engineIndex) {
        case 0:
            return "Dimension";
        case 1:
            return "Haas";
        case 2:
            return "M/S";
        default:
            return "";
    }
}

}  // namespace

CompiledDimensionView::CompiledDimensionView(juce::String /*pluginId*/) {
    setInterceptsMouseClicks(false, false);
    startTimer(kPollMs);
}

void CompiledDimensionView::setCompiledPlugin(
    magda::daw::audio::compiled::MagdaDimensionCompiledPlugin* plugin) {
    compiledPlugin_ = plugin;
}

void CompiledDimensionView::bindPlugin(te::Plugin* plugin) {
    setCompiledPlugin(
        dynamic_cast<magda::daw::audio::compiled::MagdaDimensionCompiledPlugin*>(plugin));
}

void CompiledDimensionView::updateFromDevice(const magda::DeviceInfo& device) {
    deviceSnapshot_ = device;
    resampleFromDevice();
    repaint();
}

void CompiledDimensionView::resampleFromDevice() {
    engine_ = juce::jlimit(
        0, Plugin::kEngineCount - 1,
        static_cast<int>(std::round(valueForSlot(deviceSnapshot_, Plugin::kEngineSlot, 0.0f))));
    amount_ = valueForSlot(deviceSnapshot_, Plugin::kAmountSlot, amount_);
    width_ = valueForSlot(deviceSnapshot_, Plugin::kWidthSlot, width_);
}

void CompiledDimensionView::timerCallback() {
    if (compiledPlugin_ != nullptr) {
        auto read = [this](int slot, float fallback) {
            if (auto* p = compiledPlugin_->getSlotParameter(slot))
                return compiledPlugin_->nativeValueToDisplayValue(slot, p->getCurrentValue());
            return fallback;
        };
        engine_ = juce::jlimit(0, Plugin::kEngineCount - 1,
                               static_cast<int>(std::round(read(Plugin::kEngineSlot, 0.0f))));
        amount_ = read(Plugin::kAmountSlot, amount_);
        width_ = read(Plugin::kWidthSlot, width_);
    }
    repaint();
}

void CompiledDimensionView::paint(juce::Graphics& g) {
    auto bounds = getLocalBounds().toFloat();
    g.setColour(DarkTheme::getColour(DarkTheme::SURFACE));
    g.fillRoundedRectangle(bounds, 4.0f);

    auto plot = bounds.reduced(kPadX, kPadY);
    if (plot.getWidth() < 16.0f || plot.getHeight() < 16.0f)
        return;

    // Stereo image indicator: two dots on a horizontal axis, distance
    // between them scales with Amount × (Width/100). When both are 0 the
    // dots collapse to the centre = mono; at max they sit at the edges.
    const float spread = juce::jlimit(0.0f, 1.0f, amount_ * (width_ / 100.0f) * 0.5f);
    const float centreY = plot.getCentreY();
    const float centreX = plot.getCentreX();
    const float reach = plot.getWidth() * 0.4f * spread;

    g.setColour(DarkTheme::getColour(DarkTheme::TEXT_PRIMARY).withAlpha(0.18f));
    g.drawLine(plot.getX() + 4.0f, centreY, plot.getRight() - 4.0f, centreY, 1.0f);

    const auto accent = DarkTheme::getColour(DarkTheme::ACCENT_CYAN);
    g.setColour(accent.withAlpha(0.95f));
    g.fillEllipse(centreX - reach - 4.0f, centreY - 4.0f, 8.0f, 8.0f);
    g.fillEllipse(centreX + reach - 4.0f, centreY - 4.0f, 8.0f, 8.0f);

    // Engine label, top-right corner — same convention as the Reverb view.
    g.setColour(DarkTheme::getColour(DarkTheme::TEXT_PRIMARY).withAlpha(0.55f));
    g.drawFittedText(engineLabel(engine_),
                     juce::Rectangle<int>(static_cast<int>(plot.getRight()) - 80,
                                          static_cast<int>(plot.getY()) + 2, 76, 14),
                     juce::Justification::right, 1);
}

const CompiledPresentationSpec& getMagdaDimensionPresentation() {
    static const CompiledPresentationSpec kSpec{
        .pluginId = magda::daw::audio::compiled::MagdaDimensionCompiledPlugin::xmlTypeName,
        .layoutCellCount =
            magda::daw::audio::compiled::MagdaDimensionCompiledPlugin::kHostSlotCount,
        .layoutCellsPerRow = 6,
        .createPanel = [](juce::String pluginId) -> std::unique_ptr<CompiledDevicePanel> {
            return std::make_unique<CompiledDimensionView>(pluginId);
        },
    };
    return kSpec;
}

}  // namespace magda::daw::ui
