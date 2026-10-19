#include "compiled/StrumOnsetsPanel.hpp"

#include "audio/plugins/MidiStrumPlugin.hpp"
#include "ui/themes/ActiveTheme.hpp"
#include "ui/themes/FontManager.hpp"

namespace magda::daw::ui {

namespace {

using Strum = magda::daw::audio::MidiStrumPlugin;

// Enough ticks to read the timing shape without pretending to be an exact chord size.
constexpr int kPreviewTicks = 12;
constexpr int kHeaderHeight = 24;

int choiceOf(const magda::DeviceInfo& device, int slot, int fallback) {
    const auto* param = device.findParameterByIndex(slot);
    return param != nullptr ? juce::roundToInt(param->currentValue) : fallback;
}

}  // namespace

void StrumOnsetsPanel::updateFromDevice(const magda::DeviceInfo& device) {
    const int shape = choiceOf(device, Strum::kShape, 1);
    const int cycles = choiceOf(device, Strum::kCycles, 0);
    if (shape == shape_ && cycles == cycles_)
        return;
    shape_ = shape;
    cycles_ = cycles;
    onsets_ = Strum::curveOnsetPreview(shape_, cycles_, kPreviewTicks);
    repaint();
}

void StrumOnsetsPanel::paint(juce::Graphics& g) {
    auto area = getLocalBounds();
    g.setFont(FontManager::getInstance().getMonoFont(10.0f).withExtraKerningFactor(0.12f));
    g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_DIM));
    g.drawText("ONSETS", area.removeFromTop(kHeaderHeight), juce::Justification::centredLeft,
               false);
    area.removeFromTop(4);

    const auto plot = area.toFloat();
    g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_GRAPH_BG));
    g.fillRoundedRectangle(plot, 4.0f);

    const auto inner = plot.reduced(14.0f, 12.0f);
    g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_GRAPH_GRID));
    for (const float t : {0.25f, 0.5f, 0.75f})
        g.drawVerticalLine(juce::roundToInt(inner.getX() + t * inner.getWidth()), inner.getY(),
                           inner.getBottom());
    g.drawHorizontalLine(juce::roundToInt(inner.getBottom()), inner.getX(), inner.getRight());

    g.setColour(ActiveTheme::getColour(ActiveTheme::ACCENT_POSITIVE).withAlpha(0.85f));
    for (const float onset : onsets_) {
        const float x = inner.getX() + juce::jlimit(0.0f, 1.0f, onset) * inner.getWidth();
        g.fillRect(juce::Rectangle<float>(x - 1.0f, inner.getY(), 2.0f, inner.getHeight()));
    }
}

const CompiledPresentationSpec& getStrumPresentation() {
    // Trigger, order and shape head the faceplate; the timing and loop controls stand beside it.
    static constexpr int kKnobSlots[] = {Strum::kTrigger,  Strum::kOrder,       Strum::kShape,
                                         Strum::kCycles,   Strum::kStrumLength, Strum::kLoopSync,
                                         Strum::kLoopRate, Strum::kSyncInterval};
    static constexpr int kFaceplateSlots[] = {Strum::kTrigger, Strum::kOrder, Strum::kShape};
    static constexpr std::pair<int, const char*> kKnobLabels[] = {
        {Strum::kStrumLength, "Length"},
        {Strum::kLoopSync, "Loop sync"},
        {Strum::kLoopRate, "Loop rate"},
        {Strum::kSyncInterval, "Interval"}};
    static const CompiledPresentationSpec kSpec{
        .pluginId = Strum::xmlTypeName,
        .layoutCellCount = Strum::kNumParams,
        .layoutCellsPerRow = Strum::kNumParams,
        .createPanel = [](juce::String) -> std::unique_ptr<CompiledDevicePanel> {
            return std::make_unique<StrumOnsetsPanel>();
        },
        // The loop controls act in Loop mode; its rate in Beat sync, its interval in Time.
        .isParameterEnabled =
            [](const magda::DeviceInfo& device, int slot) {
                const bool loop =
                    choiceOf(device, Strum::kTrigger, 0) == static_cast<int>(Strum::Trigger::Loop);
                const bool beat = choiceOf(device, Strum::kLoopSync, 0) ==
                                  static_cast<int>(Strum::LoopSync::Beat);
                switch (slot) {
                    case Strum::kLoopSync:
                        return loop;
                    case Strum::kLoopRate:
                        return loop && beat;
                    case Strum::kSyncInterval:
                        return loop && !beat;
                    default:
                        return true;
                }
            },
        .knobSlots = kKnobSlots,
        .knobLabels = kKnobLabels,
        .faceplateWidth = 420,
        .faceplateSlots = kFaceplateSlots,
    };
    return kSpec;
}

}  // namespace magda::daw::ui
