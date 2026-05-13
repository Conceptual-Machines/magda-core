#include "processors/base/DeviceProcessor.hpp"

#include <utility>

namespace magda {

DeviceProcessor::DeviceProcessor(DeviceId deviceId, te::Plugin::Ptr plugin)
    : deviceId_(deviceId), plugin_(std::move(plugin)) {}

void DeviceProcessor::setParameter(const juce::String& /*paramName*/, float /*value*/) {
    // Base implementation does nothing - override in subclasses.
}

float DeviceProcessor::getParameter(const juce::String& /*paramName*/) const {
    return 0.0f;
}

std::vector<juce::String> DeviceProcessor::getParameterNames() const {
    return {};
}

int DeviceProcessor::getParameterCount() const {
    return 0;
}

ParameterInfo DeviceProcessor::getParameterInfo(int /*index*/) const {
    return {};
}

juce::String DeviceProcessor::formatParameterValue(int index, float normalizedValue) const {
    if (!plugin_)
        return {};
    // This is a display hot path - lane repaints and automation echoes can
    // call it thousands of times per second. TE's getAutomatableParameters()
    // allocates and copies a juce::Array every call, which is roughly O(N)
    // with the plugin's parameter count; for Vital (~777 params) that alone
    // beach-balls the UI during playback. Cache the array and refresh it
    // only when the underlying plugin pointer changes.
    if (cachedParamsPlugin_ != plugin_.get()) {
        cachedParamsPlugin_ = plugin_.get();
        cachedParams_ = plugin_->getAutomatableParameters();
    }
    if (index < 0 || index >= cachedParams_.size())
        return {};
    return cachedParams_[index]->valueToString(normalizedValue);
}

void DeviceProcessor::populateParameters(DeviceInfo& info) const {
    info.parameters.clear();
    int count = getParameterCount();
    for (int i = 0; i < count; ++i) {
        info.parameters.push_back(getParameterInfo(i));
    }
}

void DeviceProcessor::setParameterByIndex(int paramIndex, float value) {
    const auto names = getParameterNames();
    if (paramIndex >= 0 && paramIndex < static_cast<int>(names.size()))
        setParameter(names[static_cast<size_t>(paramIndex)], value);
}

void DeviceProcessor::setParameterByIndex(int paramIndex, ParameterModelValue value) {
    setParameterByIndex(paramIndex, value.value);
}

void DeviceProcessor::setGainDb(float gainDb) {
    gainDb_ = gainDb;
    gainLinear_ = juce::Decibels::decibelsToGain(gainDb);
    applyGain();
}

void DeviceProcessor::setGainLinear(float gainLinear) {
    gainLinear_ = gainLinear;
    gainDb_ = juce::Decibels::gainToDecibels(gainLinear);
    applyGain();
}

void DeviceProcessor::setBypassed(bool bypassed) {
    if (plugin_) {
        plugin_->setEnabled(!bypassed);
    }
}

bool DeviceProcessor::isBypassed() const {
    return plugin_ ? !plugin_->isEnabled() : true;
}

void DeviceProcessor::syncFromDeviceInfo(const DeviceInfo& info) {
    setGainDb(info.gainDb);
    setBypassed(info.bypassed);

    // ParameterInfo::paramIndex is the stable plugin/slot index. This keeps
    // restore semantics aligned with live UI writes and supports processors
    // whose display order is not the same as their native parameter index.
    for (size_t i = 0; i < info.parameters.size(); ++i) {
        const auto& param = info.parameters[i];
        const int paramIndex = param.paramIndex >= 0 ? param.paramIndex : static_cast<int>(i);
        setParameterByIndex(paramIndex, param.currentValue);
    }
}

void DeviceProcessor::syncToDeviceInfo(DeviceInfo& info) const {
    info.gainDb = gainDb_;
    info.gainValue = gainLinear_;
    info.bypassed = isBypassed();
}

void DeviceProcessor::applyGain() {
    // Base implementation does nothing - subclasses override to apply gain
    // to the appropriate parameter.
}

}  // namespace magda
