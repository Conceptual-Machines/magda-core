#include "processors/external/ExternalPluginProcessor.hpp"

#include <utility>

#include "core/TrackManager.hpp"
#include "processors/ParameterInfoBuilder.hpp"

namespace magda {

ExternalPluginProcessor::ExternalPluginProcessor(DeviceId deviceId, te::Plugin::Ptr plugin)
    : DeviceProcessor(deviceId, std::move(plugin)) {}

ExternalPluginProcessor::~ExternalPluginProcessor() {
    stopParameterListening();
}

te::ExternalPlugin* ExternalPluginProcessor::getExternalPlugin() const {
    return dynamic_cast<te::ExternalPlugin*>(plugin_.get());
}

void ExternalPluginProcessor::cacheParameterNames() const {
    if (parametersCached_)
        return;

    parameterNames_.clear();
    if (auto* ext = getExternalPlugin()) {
        auto params = ext->getAutomatableParameters();
        for (auto* param : params) {
            if (param) {
                parameterNames_.push_back(param->getParameterName());
            }
        }
    }
    parametersCached_ = true;
}

void ExternalPluginProcessor::setParameter(const juce::String& paramName, float value) {
    if (auto* ext = getExternalPlugin()) {
        for (auto params = ext->getAutomatableParameters(); auto* param : params) {
            if (param && param->getParameterName().equalsIgnoreCase(paramName)) {
                param->setParameterFromHost(value, juce::sendNotificationSync);
                return;
            }
        }
    }
}

float ExternalPluginProcessor::getParameter(const juce::String& paramName) const {
    if (auto* ext = getExternalPlugin()) {
        auto params = ext->getAutomatableParameters();
        for (auto* param : params) {
            if (param && param->getParameterName().equalsIgnoreCase(paramName)) {
                return param->getCurrentValue();
            }
        }
    }
    return 0.0f;
}

std::vector<juce::String> ExternalPluginProcessor::getParameterNames() const {
    cacheParameterNames();
    return parameterNames_;
}

int ExternalPluginProcessor::getParameterCount() const {
    if (auto* ext = getExternalPlugin()) {
        return static_cast<int>(ext->getAutomatableParameters().size());
    }
    return 0;
}

ParameterInfo ExternalPluginProcessor::getParameterInfo(int index) const {
    auto* ext = getExternalPlugin();
    if (!ext)
        return {};
    auto params = ext->getAutomatableParameters();
    if (index < 0 || index >= static_cast<int>(params.size()))
        return {};

    auto* param = params[static_cast<size_t>(index)];
    auto info = makeInfoFromTeParam(index, param);

    // Live display text provider: all display paths (param grid, automation
    // lane, curve tooltip) query the plugin's valueToString() at call time
    // through a safe TrackManager -> AudioBridge -> Processor lookup. No
    // dangling pointers, no stale sampled tables, exact values.
    if (info.valueTable.empty()) {
        auto provider = std::make_shared<ParameterInfo::DisplayTextProvider>();
        provider->deviceId = getDeviceId();
        provider->paramIndex = index;
        info.displayText = std::move(provider);
    }

    return info;
}

void ExternalPluginProcessor::populateParameters(DeviceInfo& info) const {
    info.parameters.clear();

    if (auto* ext = getExternalPlugin()) {
        auto params = ext->getAutomatableParameters();
        int maxParams = static_cast<int>(params.size());

        for (int i = 0; i < maxParams; ++i) {
            info.parameters.push_back(getParameterInfo(i));
        }
    }
}

void ExternalPluginProcessor::syncFromDeviceInfo(const DeviceInfo& info) {
    setGainDb(info.gainDb);
    setBypassed(info.bypassed);

    settingParameterFromUI_ = true;

    if (auto* ext = getExternalPlugin()) {
        auto params = ext->getAutomatableParameters();
        for (size_t i = 0; i < info.parameters.size() && i < static_cast<size_t>(params.size());
             ++i) {
            if (params[i]) {
                params[i]->setParameterFromHost(info.parameters[i].currentValue,
                                                juce::dontSendNotification);
            }
        }
    }

    settingParameterFromUI_ = false;
}

void ExternalPluginProcessor::setParameterByIndex(int paramIndex, float value) {
    settingParameterFromUI_ = true;

    if (auto* ext = getExternalPlugin()) {
        auto params = ext->getAutomatableParameters();
        if (paramIndex >= 0 && paramIndex < static_cast<int>(params.size())) {
            params[static_cast<size_t>(paramIndex)]->setParameterFromHost(
                value, juce::sendNotificationSync);
        }
    }

    settingParameterFromUI_ = false;
}

float ExternalPluginProcessor::getParameterByIndex(int paramIndex) const {
    if (auto* ext = getExternalPlugin()) {
        auto params = ext->getAutomatableParameters();
        if (paramIndex >= 0 && paramIndex < static_cast<int>(params.size())) {
            return params[static_cast<size_t>(paramIndex)]->getCurrentValue();
        }
    }
    return 0.0f;
}

void ExternalPluginProcessor::startParameterListening() {
    if (listeningForChanges_)
        return;

    if (auto* ext = getExternalPlugin()) {
        auto params = ext->getAutomatableParameters();
        for (auto* param : params) {
            if (param) {
                param->addListener(this);
            }
        }
        listeningForChanges_ = true;
        DBG("Started parameter listening for device " << deviceId_ << " with " << params.size()
                                                      << " parameters");
    }
}

void ExternalPluginProcessor::stopParameterListening() {
    if (!listeningForChanges_)
        return;

    if (auto* ext = getExternalPlugin()) {
        auto params = ext->getAutomatableParameters();
        for (auto* param : params) {
            if (param) {
                param->removeListener(this);
            }
        }
    }
    listeningForChanges_ = false;
}

void ExternalPluginProcessor::currentValueChanged(te::AutomatableParameter& param) {
    // This fires for ALL value changes including from the plugin's native UI.
    // parameterChanged() only fires for explicit setParameter() calls, so this
    // is the primary path for detecting plugin UI changes.
    propagateParameterChange(param);
}

void ExternalPluginProcessor::parameterChanged(te::AutomatableParameter& param,
                                               float /*newValue*/) {
    // This fires synchronously when setParameter() is called explicitly.
    // currentValueChanged handles all cases, so nothing needed here.
    juce::ignoreUnused(param);
}

void ExternalPluginProcessor::propagateParameterChange(te::AutomatableParameter& param) {
    if (settingParameterFromUI_)
        return;

    int parameterIndex = -1;
    if (auto* ext = getExternalPlugin()) {
        auto params = ext->getAutomatableParameters();
        for (size_t i = 0; i < static_cast<size_t>(params.size()); ++i) {
            if (params[i] == &param) {
                parameterIndex = static_cast<int>(i);
                break;
            }
        }
    }

    if (parameterIndex < 0)
        return;

    // When modifiers (macros) are active, the macro owns the base value - skip propagation
    // entirely. Otherwise, internal plugin modulation (e.g. Serum LFO) gets misinterpreted
    // as a base value change and overwrites the macro-controlled value.
    if (param.hasActiveModifierAssignments())
        return;

    float valueToStore = param.getCurrentValue();

    juce::MessageManager::callAsync([this, parameterIndex, valueToStore]() {
        auto& tm = TrackManager::getInstance();

        for (const auto& track : tm.getTracks()) {
            for (const auto& element : track.chainElements) {
                if (std::holds_alternative<DeviceInfo>(element)) {
                    const auto& device = std::get<DeviceInfo>(element);
                    if (device.id == deviceId_) {
                        ChainNodePath path;
                        path.trackId = track.id;
                        path.topLevelDeviceId = deviceId_;

                        tm.setDeviceParameterValueFromPlugin(path, parameterIndex, valueToStore);
                        return;
                    }
                }
            }

            // Also search in racks/chains (nested devices)
            // TODO: Implement nested device search if needed
        }
    });
}

}  // namespace magda
