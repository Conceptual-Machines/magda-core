#include "custom_ui/ToneGeneratorUI.hpp"

namespace magda::daw::ui {

using Tone = devices::ToneGenerator;

ToneGeneratorUI::ToneGeneratorUI() {
    waveform_.setOptions({"Sine", "Triangle", "Saw Up", "Saw Down", "Square", "Noise"});
    waveform_.setSelectedIndex(0);
    waveform_.onChange = [this](int index) {
        writeParameter(Tone::kWaveformParamIndex, static_cast<float>(index));
        refreshNoiseColor();
    };
    addAndMakeVisible(waveform_);

    noiseColor_.setOptions({"White", "Pink", "Brown"});
    noiseColor_.setSelectedIndex(0);
    noiseColor_.onChange = [this](int index) {
        writeParameter(Tone::kNoiseColorParamIndex, static_cast<float>(index));
    };
    addAndMakeVisible(noiseColor_);

    transportOnly_.setToggle("Transport only");
    transportOnly_.setSelectedIndex(0);
    transportOnly_.onChange = [this](int index) {
        writeParameter(Tone::kTransportOnlyParamIndex, static_cast<float>(index));
    };
    addAndMakeVisible(transportOnly_);
    style_ = resolveControlStyle({});
    magda::Config::getInstance().addListener(this);
}

ToneGeneratorUI::~ToneGeneratorUI() {
    magda::Config::getInstance().removeListener(this);
}

void ToneGeneratorUI::configChanged() {
    if (const auto style = resolveControlStyle(snapshot_.controlStyle); style != style_) {
        style_ = style;
        resized();
    }
}

void ToneGeneratorUI::updateFromDevice(const magda::DeviceInfo& device) {
    snapshot_ = device;
    if (const auto* param = device.findParameterByIndex(Tone::kWaveformParamIndex))
        waveform_.setSelectedIndex(
            juce::jlimit(0, Tone::kWaveformCount - 1, juce::roundToInt(param->currentValue)));
    if (const auto* param = device.findParameterByIndex(Tone::kNoiseColorParamIndex))
        noiseColor_.setSelectedIndex(
            juce::jlimit(0, Tone::kNoiseColorCount - 1, juce::roundToInt(param->currentValue)));
    if (const auto* param = device.findParameterByIndex(Tone::kTransportOnlyParamIndex))
        transportOnly_.setSelectedIndex(param->currentValue >= 0.5f ? 1 : 0);
    refreshNoiseColor();
    configChanged();
}

void ToneGeneratorUI::writeParameter(int slot, float value) {
    if (auto* param = snapshot_.findParameterByIndex(slot))
        param->currentValue = value;
    if (onParameterChanged_)
        onParameterChanged_(slot, value);
}

void ToneGeneratorUI::refreshNoiseColor() {
    // Only noise has a colour, so it dims for the rest, the way Frequency dims for noise.
    const bool noise = waveform_.getSelectedIndex() == static_cast<int>(Tone::Waveform::Noise);
    noiseColor_.setEnabled(noise);
    noiseColor_.setAlpha(noise ? 1.0f : 0.4f);
}

void ToneGeneratorUI::resized() {
    const auto geometry = ToneGeneratorGeometry::of(getLocalBounds(), style_);
    waveform_.setBounds(geometry.waveform);
    noiseColor_.setBounds(geometry.noiseColor);
    transportOnly_.setBounds(geometry.transportOnly);
}

const CompiledPresentationSpec& getToneGeneratorPresentation() {
    static const CompiledPresentationSpec kSpec{
        .pluginId = Tone::kDeviceType,
        .layoutCellCount = 0,
        .layoutCellsPerRow = 0,
        .createPanel = [](juce::String) -> std::unique_ptr<CompiledDevicePanel> {
            return std::make_unique<ToneGeneratorUI>();
        },
        .preferredSlotWidth = 260,
        .createLayout = []() -> std::unique_ptr<DeviceParamLayout> {
            return std::make_unique<ToneGeneratorLayout>();
        },
    };
    return kSpec;
}

}  // namespace magda::daw::ui
