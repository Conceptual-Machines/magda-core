#include "processors/CompiledFaustProcessor.hpp"

#include <cmath>
#include <utility>

#include "plugins/compiled/CompiledFaustInterface.hpp"
#include "plugins/compiled/tracktion/CompiledFaustTracktionAdapter.hpp"
#include "plugins/tracktion/TracktionMagdaDevicePlugin.hpp"

namespace magda {

namespace {

auto* compiledDevice(te::Plugin* plugin) {
    return daw::audio::tracktion_adapter::deviceFromPlugin<
        daw::audio::compiled::ICompiledFaustPlugin>(plugin);
}

}  // namespace

CompiledFaustProcessor::CompiledFaustProcessor(DeviceId deviceId, te::Plugin::Ptr plugin)
    : DeviceProcessor(deviceId, plugin) {}

int CompiledFaustProcessor::getParameterCount() const {
    const auto* host = compiledDevice(plugin_.get());
    return host != nullptr ? host->hostSlotCount() : 0;
}

ParameterInfo CompiledFaustProcessor::getParameterInfo(int index) const {
    const auto* host = compiledDevice(plugin_.get());
    if (host == nullptr || index < 0 || index >= host->hostSlotCount())
        return {};

    const auto& s = host->hostSlotInfo(index);
    ParameterInfo info;
    info.paramIndex = index;
    info.name = s.name;
    info.unit = s.unit;
    info.scale = s.scale;
    info.minValue = s.minValue;
    info.maxValue = s.maxValue;
    info.defaultValue = s.defaultValue;
    info.currentValue = s.defaultValue;
    if (std::isfinite(s.scaleAnchor))
        info.scaleAnchor = s.scaleAnchor;
    info.choices = s.choices;
    info.gateSlotIndex = s.gateSlotIndex;
    info.gateNegated = s.gateNegated;
    if (s.name.equalsIgnoreCase("Mix") && std::abs(s.minValue) < 1.0e-6f &&
        std::abs(s.maxValue - 1.0f) < 1.0e-6f)
        info.displayFormat = DisplayFormat::Percent;

    if (index == host->engineAwareModeSlot() && index >= 0) {
        info.choices = host->modeChoicesForActiveEngine();
        if (!info.choices.empty()) {
            info.minValue = 0.0f;
            info.maxValue = static_cast<float>(info.choices.size() - 1);
        }
    }
    info.hidden = host->isSlotHiddenForActiveEngine(index);

    info.teMinValue = 0.0f;
    info.teMaxValue = 1.0f;
    return info;
}

void CompiledFaustProcessor::populateParameters(DeviceInfo& info) const {
    info.parameters.clear();
    const auto* host = compiledDevice(plugin_.get());
    if (host == nullptr)
        return;

    for (int i = 0; i < host->hostSlotCount(); ++i) {
        auto paramInfo = getParameterInfo(i);
        // Base value, NOT getCurrentValue(): the current value includes live
        // modifier output, so repopulating while an LFO runs would snapshot a
        // random sweep sample into the model as if it were the knob position.
        if (const auto* param = daw::audio::compiled::tracktionParameterForSlot(plugin_.get(), i))
            paramInfo.currentValue = host->normalizedToDisplay(i, param->getCurrentBaseValue());
        info.parameters.push_back(std::move(paramInfo));
    }
}

void CompiledFaustProcessor::setParameterByIndex(int paramIndex, float value) {
    if (!plugin_)
        return;

    auto* host = compiledDevice(plugin_.get());
    if (host != nullptr) {
        if (auto* param =
                daw::audio::compiled::tracktionParameterForSlot(plugin_.get(), paramIndex)) {
            const float targetNative = host->displayToNormalized(paramIndex, value);
            param->setParameterFromHost(targetNative, juce::sendNotificationSync);
        }
    }
}

float CompiledFaustProcessor::getParameterByIndex(int paramIndex) const {
    if (!plugin_)
        return 0.0f;

    const auto* host = compiledDevice(plugin_.get());
    if (host != nullptr) {
        if (const auto* param =
                daw::audio::compiled::tracktionParameterForSlot(plugin_.get(), paramIndex))
            return host->normalizedToDisplay(paramIndex, param->getCurrentValue());
    }
    return 0.0f;
}

}  // namespace magda
