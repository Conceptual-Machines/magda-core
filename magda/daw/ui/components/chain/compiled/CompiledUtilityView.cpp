#include "compiled/CompiledUtilityView.hpp"

#include "audio/plugins/compiled/MagdaUtilityCompiledPlugin.hpp"
#include "ui/themes/DarkTheme.hpp"

namespace magda::daw::ui {

namespace {

constexpr int kPollMs = 100;
constexpr int kFirstToggleSlot = 4;  // Mono slot index

float valueForSlot(const magda::DeviceInfo& device, int slotIndex, float fallback) {
    for (const auto& param : device.parameters)
        if (param.paramIndex == slotIndex)
            return param.currentValue;
    return fallback;
}

}  // namespace

CompiledUtilityView::CompiledUtilityView(juce::String /*pluginId*/) {
    const auto accent = DarkTheme::getColour(DarkTheme::ACCENT_ORANGE);
    const auto accentBg = accent.withAlpha(0.25f);
    const auto inactive = DarkTheme::getSecondaryTextColour();
    const auto bg = DarkTheme::getColour(DarkTheme::BACKGROUND);

    for (int i = 0; i < 4; ++i) {
        auto& btn = btns_[static_cast<size_t>(i)];
        btn.setButtonText(kLabels[static_cast<size_t>(i)]);
        btn.setClickingTogglesState(true);
        btn.setColour(juce::TextButton::buttonColourId, bg);
        btn.setColour(juce::TextButton::buttonOnColourId, accentBg);
        btn.setColour(juce::TextButton::textColourOffId, inactive);
        btn.setColour(juce::TextButton::textColourOnId, accent);
        const int slot = kFirstToggleSlot + i;
        btn.onClick = [this, slot, i]() {
            const bool on = btns_[static_cast<size_t>(i)].getToggleState();
            if (onParameterChanged)
                onParameterChanged(slot, on ? 1.0f : 0.0f);
        };
        addAndMakeVisible(btn);
    }

    startTimer(kPollMs);
}

void CompiledUtilityView::setCompiledPlugin(
    magda::daw::audio::compiled::MagdaUtilityCompiledPlugin* plugin) {
    compiledPlugin_ = plugin;
}

void CompiledUtilityView::updateFromDevice(const magda::DeviceInfo& device) {
    deviceSnapshot_ = device;
    resampleFromDevice();
}

void CompiledUtilityView::timerCallback() {
    using Util = magda::daw::audio::compiled::MagdaUtilityCompiledPlugin;

    auto read = [this](int slot) -> bool {
        if (compiledPlugin_ != nullptr) {
            if (auto* p = compiledPlugin_->getSlotParameter(slot))
                return compiledPlugin_->nativeValueToDisplayValue(slot, p->getCurrentValue()) >=
                       0.5f;
        }
        return valueForSlot(deviceSnapshot_, slot, 0.0f) >= 0.5f;
    };

    for (int i = 0; i < 4; ++i) {
        const bool on = read(Util::kMonoSlot + i);
        auto& btn = btns_[static_cast<size_t>(i)];
        if (btn.getToggleState() != on)
            btn.setToggleState(on, juce::dontSendNotification);
    }
}

void CompiledUtilityView::resampleFromDevice() {
    using Util = magda::daw::audio::compiled::MagdaUtilityCompiledPlugin;
    for (int i = 0; i < 4; ++i) {
        const bool on = valueForSlot(deviceSnapshot_, Util::kMonoSlot + i, 0.0f) >= 0.5f;
        btns_[static_cast<size_t>(i)].setToggleState(on, juce::dontSendNotification);
    }
}

void CompiledUtilityView::updateButtonState(int btnIdx, bool on) {
    if (btnIdx >= 0 && btnIdx < 4)
        btns_[static_cast<size_t>(btnIdx)].setToggleState(on, juce::dontSendNotification);
}

void CompiledUtilityView::paint(juce::Graphics& g) {
    g.fillAll(DarkTheme::getColour(DarkTheme::BACKGROUND));
}

void CompiledUtilityView::resized() {
    auto area = getLocalBounds().reduced(6, 6);
    const int btnW = area.getWidth() / 4;
    for (int i = 0; i < 4; ++i)
        btns_[static_cast<size_t>(i)].setBounds(
            area.removeFromLeft(i < 3 ? btnW : area.getWidth()).reduced(2, 0));
}

void CompiledUtilityView::bindPlugin(te::Plugin* plugin) {
    setCompiledPlugin(
        dynamic_cast<magda::daw::audio::compiled::MagdaUtilityCompiledPlugin*>(plugin));
}

const CompiledPresentationSpec& getMagdaUtilityPresentation() {
    static const CompiledPresentationSpec kSpec{
        .pluginId = magda::daw::audio::compiled::MagdaUtilityCompiledPlugin::xmlTypeName,
        .layoutCellCount = 4,
        .layoutCellsPerRow = 2,
        .createPanel = [](juce::String pluginId) -> std::unique_ptr<CompiledDevicePanel> {
            return std::make_unique<CompiledUtilityView>(pluginId);
        },
        .suppressLegacyUis = {},
        .visualMinFractionNumerator = 1,
        .visualMinFractionDenominator = 3,
    };
    return kSpec;
}

}  // namespace magda::daw::ui
