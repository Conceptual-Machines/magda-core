#include "compiled/CompiledPitchEditorView.hpp"

#include <cmath>

#include "audio/plugins/compiled/MagdaPitchCompiledPlugin.hpp"
#include "ui/themes/DarkTheme.hpp"

namespace magda::daw::ui {

namespace {

constexpr int kPollMs = 50;
constexpr float kPadX = 10.0f;
constexpr float kPadY = 8.0f;
constexpr float kRange = 24.0f;  // semitones each side of zero

float valueForSlot(const magda::DeviceInfo& device, int slotIndex, float fallback) {
    for (const auto& param : device.parameters)
        if (param.paramIndex == slotIndex)
            return param.currentValue;
    return fallback;
}

const char* engineLabel(int engineIndex) {
    switch (engineIndex) {
        case 0:
            return "Shifter";
        case 1:
            return "Detuner";
        case 2:
            return "Harmonizer";
        default:
            return "";
    }
}

float semisToX(float semis, juce::Rectangle<float> plot) {
    const float clamped = juce::jlimit(-kRange, kRange, semis);
    const float t = (clamped + kRange) / (2.0f * kRange);
    return plot.getX() + t * plot.getWidth();
}

}  // namespace

CompiledPitchEditorView::CompiledPitchEditorView(juce::String /*pluginId*/) {
    setInterceptsMouseClicks(false, false);
    startTimer(kPollMs);
}

void CompiledPitchEditorView::setCompiledPlugin(
    magda::daw::audio::compiled::MagdaPitchCompiledPlugin* plugin) {
    compiledPlugin_ = plugin;
}

void CompiledPitchEditorView::bindPlugin(te::Plugin* plugin) {
    setCompiledPlugin(dynamic_cast<magda::daw::audio::compiled::MagdaPitchCompiledPlugin*>(plugin));
}

void CompiledPitchEditorView::updateFromDevice(const magda::DeviceInfo& device) {
    deviceSnapshot_ = device;
    resampleFromDevice();
    repaint();
}

void CompiledPitchEditorView::resampleFromDevice() {
    engine_ = juce::jlimit(
        0, Plugin::kEngineCount - 1,
        static_cast<int>(std::round(valueForSlot(deviceSnapshot_, Plugin::kEngineSlot, 0.0f))));
    pitchSemis_ = valueForSlot(deviceSnapshot_, Plugin::kPitchSlot, pitchSemis_);
    fineCents_ = valueForSlot(deviceSnapshot_, Plugin::kFineSlot, fineCents_);
}

void CompiledPitchEditorView::timerCallback() {
    if (compiledPlugin_ != nullptr) {
        auto read = [this](int slot, float fallback) {
            if (auto* p = compiledPlugin_->getSlotParameter(slot))
                return compiledPlugin_->nativeValueToDisplayValue(slot, p->getCurrentValue());
            return fallback;
        };
        engine_ = juce::jlimit(0, Plugin::kEngineCount - 1,
                               static_cast<int>(std::round(read(Plugin::kEngineSlot, 0.0f))));
        pitchSemis_ = read(Plugin::kPitchSlot, pitchSemis_);
        fineCents_ = read(Plugin::kFineSlot, fineCents_);
    }
    repaint();
}

void CompiledPitchEditorView::paint(juce::Graphics& g) {
    auto bounds = getLocalBounds().toFloat();
    g.setColour(DarkTheme::getColour(DarkTheme::SURFACE));
    g.fillRoundedRectangle(bounds, 4.0f);

    auto plot = bounds.reduced(kPadX, kPadY);
    if (plot.getWidth() < 32.0f || plot.getHeight() < 16.0f)
        return;

    const float baselineY = plot.getCentreY() + 4.0f;
    const auto textColour = DarkTheme::getColour(DarkTheme::TEXT_PRIMARY);
    const auto accent = DarkTheme::getColour(DarkTheme::ACCENT_CYAN);

    // Ruler baseline.
    g.setColour(textColour.withAlpha(0.18f));
    g.drawLine(plot.getX(), baselineY, plot.getRight(), baselineY, 1.0f);

    // Octave ticks at -24, -12, 0, +12, +24. Zero is taller.
    const int ticks[] = {-24, -12, 0, 12, 24};
    for (int t : ticks) {
        const float x = semisToX(static_cast<float>(t), plot);
        const float h = (t == 0) ? 7.0f : 4.0f;
        g.setColour(textColour.withAlpha(t == 0 ? 0.45f : 0.22f));
        g.drawLine(x, baselineY - h, x, baselineY + h, 1.0f);
    }

    const float shift = pitchSemis_ + fineCents_ / 100.0f;
    auto drawDot = [&](float semis, float alpha, const juce::String& tag) {
        const float x = semisToX(semis, plot);
        g.setColour(accent.withAlpha(alpha));
        g.fillEllipse(x - 4.0f, baselineY - 4.0f, 8.0f, 8.0f);
        if (tag.isNotEmpty()) {
            g.setColour(textColour.withAlpha(0.55f));
            g.drawFittedText(tag,
                             juce::Rectangle<int>(static_cast<int>(x) - 12,
                                                  static_cast<int>(baselineY) + 6, 24, 12),
                             juce::Justification::centred, 1);
        }
    };

    switch (static_cast<Plugin::PitchEngine>(engine_)) {
        case Plugin::PitchEngine::Shifter:
            drawDot(shift, 0.95f, {});
            break;
        case Plugin::PitchEngine::Detuner:
            drawDot(+shift, 0.95f, "L");
            drawDot(-shift, 0.95f, "R");
            break;
        case Plugin::PitchEngine::Harmonizer:
            drawDot(0.0f, 0.45f, "dry");
            drawDot(shift, 0.95f, {});
            break;
    }

    // Engine label, top-right corner - matches Dimension / Reverb.
    g.setColour(textColour.withAlpha(0.55f));
    g.drawFittedText(engineLabel(engine_),
                     juce::Rectangle<int>(static_cast<int>(plot.getRight()) - 96,
                                          static_cast<int>(plot.getY()) + 2, 92, 14),
                     juce::Justification::right, 1);
}

const CompiledPresentationSpec& getMagdaPitchPresentation() {
    static const CompiledPresentationSpec kSpec{
        .pluginId = magda::daw::audio::compiled::MagdaPitchCompiledPlugin::xmlTypeName,
        .layoutCellCount = magda::daw::audio::compiled::MagdaPitchCompiledPlugin::kHostSlotCount,
        .layoutCellsPerRow = 6,
        .createPanel = [](juce::String pluginId) -> std::unique_ptr<CompiledDevicePanel> {
            return std::make_unique<CompiledPitchEditorView>(pluginId);
        },
    };
    return kSpec;
}

}  // namespace magda::daw::ui
