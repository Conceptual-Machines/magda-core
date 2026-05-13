#include "processors/internal/NativeDeviceProcessors.hpp"

#include <utility>

#include "core/ParameterUtils.hpp"
#include "plugins/FaustParamInfo.hpp"
#include "plugins/FaustParamPool.hpp"
#include "plugins/FaustPlugin.hpp"
#include "processors/ParameterInfoBuilder.hpp"

namespace magda {

// =============================================================================
// MagdaSamplerProcessor
// =============================================================================

MagdaSamplerProcessor::MagdaSamplerProcessor(DeviceId deviceId, te::Plugin::Ptr plugin)
    : DeviceProcessor(deviceId, std::move(plugin)) {}

int MagdaSamplerProcessor::getParameterCount() const {
    if (plugin_)
        return plugin_->getAutomatableParameters().size();
    return 0;
}

ParameterInfo MagdaSamplerProcessor::getParameterInfo(int index) const {
    if (!plugin_)
        return {};
    auto params = plugin_->getAutomatableParameters();
    if (index < 0 || index >= params.size())
        return {};
    return makeInfoFromTeParam(index, params[index]);
}

void MagdaSamplerProcessor::populateParameters(DeviceInfo& info) const {
    info.parameters.clear();
    int count = getParameterCount();
    for (int i = 0; i < count; ++i) {
        info.parameters.push_back(getParameterInfo(i));
    }
}

void MagdaSamplerProcessor::setParameterByIndex(int paramIndex, float value) {
    if (!plugin_)
        return;

    auto params = plugin_->getAutomatableParameters();
    if (paramIndex >= 0 && paramIndex < params.size()) {
        params[paramIndex]->setParameterFromHost(value, juce::sendNotificationSync);
    }
}

float MagdaSamplerProcessor::getParameterByIndex(int paramIndex) const {
    if (!plugin_)
        return 0.0f;

    auto params = plugin_->getAutomatableParameters();
    if (paramIndex >= 0 && paramIndex < params.size())
        return params[paramIndex]->getCurrentValue();
    return 0.0f;
}

// =============================================================================
// FourOscProcessor
// =============================================================================

FourOscProcessor::FourOscProcessor(DeviceId deviceId, te::Plugin::Ptr plugin)
    : DeviceProcessor(deviceId, std::move(plugin)) {}

int FourOscProcessor::getParameterCount() const {
    if (plugin_)
        return plugin_->getAutomatableParameters().size();
    return 0;
}

ParameterInfo FourOscProcessor::getParameterInfo(int index) const {
    if (!plugin_)
        return {};
    auto params = plugin_->getAutomatableParameters();
    if (index < 0 || index >= params.size())
        return {};
    auto* param = params[index];
    ParameterInfo info = makeInfoFromTeParam(index, param);

    // filterFreq stores a MIDI note in 0..135.076 that TE turns into Hz via
    // valueToString. The custom UI pins A4 (note 69, 440 Hz) to the visual
    // centre with setSkewForCentre(69.0); mirror that on the shared
    // ParameterInfo so the automation lane, generic slot slider, and curve
    // playback all agree with the plugin UI's skew. Without this, visual
    // centre lands on note 67.5 / ~404 Hz, which doesn't match what a user
    // dragging the FREQ knob sees.
    if (param && param->paramID == "filterFreq")
        info.scaleAnchor = 69.0f;

    // 4OSC exposes raw values (note number for filter freq, 0..100 for
    // percentage-shaped params, etc.) and relies on TE's valueToString to
    // convert them to the correct display text (e.g. "440 Hz" for note 69).
    // Without a DisplayTextProvider the custom-UI sliders fall through
    // DeviceSlotComponent::updateSliders -> TextSlider::setParameterInfo,
    // which replaces the hand-written Hz formatter with the generic
    // ParameterUtils::formatValue one - and for a linear-scale, empty-unit
    // parameter that just prints the raw note number. Routing the formatter
    // through TE keeps the custom UI label correct and matches what users
    // see in the plugin's native UI.
    if (info.scale != ParameterScale::Boolean && info.scale != ParameterScale::Discrete &&
        info.valueTable.empty()) {
        auto provider = std::make_shared<ParameterInfo::DisplayTextProvider>();
        provider->deviceId = getDeviceId();
        provider->paramIndex = index;
        info.displayText = std::move(provider);
    }
    return info;
}

void FourOscProcessor::populateParameters(DeviceInfo& info) const {
    info.parameters.clear();
    int count = getParameterCount();
    for (int i = 0; i < count; ++i) {
        info.parameters.push_back(getParameterInfo(i));
    }
}

void FourOscProcessor::setParameterByIndex(int paramIndex, float value) {
    if (!plugin_)
        return;

    auto params = plugin_->getAutomatableParameters();
    if (paramIndex >= 0 && paramIndex < params.size()) {
        params[paramIndex]->setParameterFromHost(value, juce::sendNotificationSync);
    }
}

float FourOscProcessor::getParameterByIndex(int paramIndex) const {
    if (!plugin_)
        return 0.0f;

    auto params = plugin_->getAutomatableParameters();
    if (paramIndex >= 0 && paramIndex < params.size())
        return params[paramIndex]->getCurrentValue();
    return 0.0f;
}

// =============================================================================
// FaustProcessor
// =============================================================================

FaustProcessor::FaustProcessor(DeviceId deviceId, te::Plugin::Ptr plugin)
    : DeviceProcessor(deviceId, std::move(plugin)) {}

int FaustProcessor::getParameterCount() const {
    auto* faust = dynamic_cast<daw::audio::FaustPlugin*>(plugin_.get());
    if (faust == nullptr) {
        DBG("[FaustProcessor] getParameterCount: plugin cast NULL");
        return 0;
    }
    const int count = faust->getPool().activeCount();
    DBG("[FaustProcessor] getParameterCount -> " << count);
    return count;
}

ParameterInfo FaustProcessor::getParameterInfo(int index) const {
    auto* faust = dynamic_cast<daw::audio::FaustPlugin*>(plugin_.get());
    if (faust == nullptr || index < 0 || index >= daw::audio::FaustParamPool::kSize)
        return {};
    return daw::audio::paramInfoFromSlot(faust->getPool().slot(index));
}

void FaustProcessor::populateParameters(DeviceInfo& info) const {
    info.parameters.clear();
    auto* faust = dynamic_cast<daw::audio::FaustPlugin*>(plugin_.get());
    if (faust == nullptr) {
        DBG("[FaustProcessor] populateParameters: plugin cast NULL");
        return;
    }
    // Only push active slots so the standard ParamGridComponent shows
    // populated cells only. Each ParameterInfo carries its real slot
    // index in `paramIndex`, so links / automation / MIDI Learn still
    // bind to the stable pool slot; display order is not slot identity.
    int active = 0;
    auto params = plugin_->getAutomatableParameters();
    for (int i = 0; i < daw::audio::FaustParamPool::kSize; ++i) {
        const auto& slot = faust->getPool().slot(i);
        if (slot.active) {
            auto paramInfo = daw::audio::paramInfoFromSlot(slot);
            if (i >= 0 && i < params.size() && params[i]) {
                paramInfo.currentValue =
                    ParameterUtils::normalizedToReal(params[i]->getCurrentValue(), paramInfo);
            }
            info.parameters.push_back(std::move(paramInfo));
            DBG("[FaustProcessor] populateParameters: slot " << i << " '" << slot.label
                                                             << "' kind=" << (int)slot.kind);
            ++active;
        }
    }
    DBG("[FaustProcessor] populateParameters: pushed " << active << " active params");
}

void FaustProcessor::setParameterByIndex(int paramIndex, float value) {
    if (!plugin_)
        return;
    auto params = plugin_->getAutomatableParameters();
    if (paramIndex >= 0 && paramIndex < params.size()) {
        const auto info = getParameterInfo(paramIndex);
        const float normalised = ParameterUtils::realToNormalized(value, info);
        params[paramIndex]->setParameterFromHost(normalised, juce::sendNotificationSync);
    }
}

float FaustProcessor::getParameterByIndex(int paramIndex) const {
    if (!plugin_)
        return 0.0f;
    auto params = plugin_->getAutomatableParameters();
    if (paramIndex >= 0 && paramIndex < params.size()) {
        const auto info = getParameterInfo(paramIndex);
        return ParameterUtils::normalizedToReal(params[paramIndex]->getCurrentValue(), info);
    }
    return 0.0f;
}

}  // namespace magda
