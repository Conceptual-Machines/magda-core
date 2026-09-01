#include "processors/base/MagdaDeviceProcessor.hpp"

#include <utility>

#include "core/ParameterUtils.hpp"
#include "plugins/MagdaDevice.hpp"
#include "plugins/tracktion/TracktionMagdaDevicePlugin.hpp"

namespace magda {

namespace {

using daw::audio::tracktion_adapter::TracktionMagdaDevicePlugin;

const daw::audio::MagdaDevice* magdaDevice(const te::Plugin* plugin) {
    const auto* adapter = dynamic_cast<const TracktionMagdaDevicePlugin*>(plugin);
    return adapter != nullptr ? &adapter->device() : nullptr;
}

te::AutomatableParameter* slotParameter(te::Plugin* plugin, int slotIndex) {
    auto* adapter = dynamic_cast<TracktionMagdaDevicePlugin*>(plugin);
    return adapter != nullptr ? adapter->parameterForDeviceSlot(slotIndex) : nullptr;
}

}  // namespace

MagdaDeviceProcessor::MagdaDeviceProcessor(DeviceId deviceId, te::Plugin::Ptr plugin)
    : DeviceProcessor(deviceId, std::move(plugin)) {}

int MagdaDeviceProcessor::getParameterCount() const {
    const auto* device = magdaDevice(plugin_.get());
    return device != nullptr ? device->parameterCount() : 0;
}

ParameterInfo MagdaDeviceProcessor::getParameterInfo(int index) const {
    const auto* device = magdaDevice(plugin_.get());
    if (device == nullptr || index < 0 || index >= device->parameterCount())
        return {};

    auto info = device->parameterInfo(index);
    info.paramIndex = index;
    info.currentValue = info.defaultValue;

    // Base value, NOT getCurrentValue(): the current value includes live
    // modifier output, and this mirror is the model's idea of the knob
    // position (same rule as CompiledFaustProcessor::populateParameters).
    if (const auto* param = slotParameter(plugin_.get(), index))
        info.currentValue = ParameterUtils::normalizedToReal(param->getCurrentBaseValue(), info);

    info.teMinValue = 0.0f;
    info.teMaxValue = 1.0f;
    return info;
}

void MagdaDeviceProcessor::populateParameters(DeviceInfo& info) const {
    info.parameters.clear();
    const int count = getParameterCount();
    for (int index = 0; index < count; ++index)
        info.parameters.push_back(getParameterInfo(index));
}

void MagdaDeviceProcessor::setParameterByIndex(int paramIndex, float value) {
    const auto* device = magdaDevice(plugin_.get());
    auto* param = slotParameter(plugin_.get(), paramIndex);
    if (device == nullptr || param == nullptr)
        return;

    param->setParameterFromHost(
        ParameterUtils::realToNormalized(value, device->parameterInfo(paramIndex)),
        juce::sendNotificationSync);
}

float MagdaDeviceProcessor::getParameterByIndex(int paramIndex) const {
    const auto* device = magdaDevice(plugin_.get());
    const auto* param = slotParameter(plugin_.get(), paramIndex);
    if (device == nullptr || param == nullptr)
        return 0.0f;

    return ParameterUtils::normalizedToReal(param->getCurrentValue(),
                                            device->parameterInfo(paramIndex));
}

}  // namespace magda
