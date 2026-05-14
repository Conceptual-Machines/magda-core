#include "processors/base/AutomatablePluginProcessor.hpp"

#include "processors/ParameterInfoBuilder.hpp"

namespace magda {

juce::Array<te::AutomatableParameter*> AutomatablePluginProcessor::getAutomatableParameters()
    const {
    return plugin_ ? plugin_->getAutomatableParameters() : juce::Array<te::AutomatableParameter*>{};
}

int AutomatablePluginProcessor::getParameterCount() const {
    return getAutomatableParameters().size();
}

ParameterInfo AutomatablePluginProcessor::getParameterInfo(int index) const {
    auto params = getAutomatableParameters();
    if (index < 0 || index >= params.size() || params[index] == nullptr)
        return {};

    auto info = makeInfoFromTeParam(index, params[index]);
    customiseParameterInfo(index, info);
    return info;
}

void AutomatablePluginProcessor::setParameterByIndex(int paramIndex, float value) {
    auto params = getAutomatableParameters();
    if (paramIndex >= 0 && paramIndex < params.size() && params[paramIndex] != nullptr)
        params[paramIndex]->setParameterFromHost(value, juce::sendNotificationSync);
}

float AutomatablePluginProcessor::getParameterByIndex(int paramIndex) const {
    auto params = getAutomatableParameters();
    if (paramIndex >= 0 && paramIndex < params.size() && params[paramIndex] != nullptr)
        return params[paramIndex]->getCurrentValue();
    return 0.0f;
}

void AutomatablePluginProcessor::customiseParameterInfo(int, ParameterInfo&) const {}

}  // namespace magda
