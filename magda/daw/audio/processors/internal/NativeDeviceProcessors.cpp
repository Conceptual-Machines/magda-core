#include "processors/internal/NativeDeviceProcessors.hpp"

#include <utility>

#include "core/ParameterUtils.hpp"
#include "plugins/FaustInstrumentPlugin.hpp"
#include "plugins/FaustParamInfo.hpp"
#include "plugins/FaustParamPool.hpp"
#include "plugins/FaustPlugin.hpp"
#include "plugins/tracktion/TracktionMagdaDevicePlugin.hpp"
#include "processors/ParameterDisplayTextProvider.hpp"

namespace magda {

// =============================================================================
// MagdaSamplerProcessor
// =============================================================================

MagdaSamplerProcessor::MagdaSamplerProcessor(DeviceId deviceId, te::Plugin::Ptr plugin)
    : MagdaDeviceProcessor(deviceId, std::move(plugin)) {}

MutableElementsProcessor::MutableElementsProcessor(DeviceId deviceId, te::Plugin::Ptr plugin)
    : MagdaDeviceProcessor(deviceId, std::move(plugin)) {}

MutableRingsProcessor::MutableRingsProcessor(DeviceId deviceId, te::Plugin::Ptr plugin)
    : MagdaDeviceProcessor(deviceId, std::move(plugin)) {}

MutableCloudsProcessor::MutableCloudsProcessor(DeviceId deviceId, te::Plugin::Ptr plugin)
    : MagdaDeviceProcessor(deviceId, std::move(plugin)) {}

MagdaConvolutionProcessor::MagdaConvolutionProcessor(DeviceId deviceId, te::Plugin::Ptr plugin)
    : MagdaDeviceProcessor(deviceId, std::move(plugin)) {}

SidechainProcessor::SidechainProcessor(DeviceId deviceId, te::Plugin::Ptr plugin)
    : MagdaDeviceProcessor(deviceId, std::move(plugin)) {}

// =============================================================================
// FourOscProcessor
// =============================================================================

FourOscProcessor::FourOscProcessor(DeviceId deviceId, te::Plugin::Ptr plugin)
    : AutomatablePluginProcessor(deviceId, std::move(plugin)) {}

std::optional<FourOscPluginState> FourOscProcessor::capturePluginState(te::Plugin* plugin) {
    auto* fourOsc = dynamic_cast<te::FourOscPlugin*>(plugin);
    if (fourOsc == nullptr)
        return std::nullopt;

    FourOscPluginState state;
    for (int i = 0; i < 4; ++i) {
        state.oscWaveShape[i] = fourOsc->oscParams[i]->waveShapeValue.get();
        state.oscVoices[i] = fourOsc->oscParams[i]->voicesValue.get();
    }
    state.filterType = fourOsc->filterTypeValue.get();
    state.filterSlope = fourOsc->filterSlopeValue.get();
    state.ampAnalog = fourOsc->ampAnalogValue.get();
    for (int i = 0; i < 2; ++i) {
        state.lfoWaveShape[i] = fourOsc->lfoParams[i]->waveShapeValue.get();
        state.lfoSync[i] = fourOsc->lfoParams[i]->syncValue.get();
    }
    state.distortionOn = fourOsc->distortionOnValue.get();
    state.reverbOn = fourOsc->reverbOnValue.get();
    state.delayOn = fourOsc->delayOnValue.get();
    state.chorusOn = fourOsc->chorusOnValue.get();
    state.voiceMode = fourOsc->voiceModeValue.get();
    state.globalVoices = fourOsc->voicesValue.get();
    return state;
}

void FourOscProcessor::customiseParameterInfo(int index, ParameterInfo& info) const {
    // filterFreq is a MIDI note in 0..135.076; the custom UI skews it with
    // setSkewForCentre(69.0), and the shared ParameterInfo has to match.
    if (auto params = getAutomatableParameters(); index >= 0 && index < params.size() &&
                                                  params[index] &&
                                                  params[index]->paramID == "filterFreq")
        info.scaleAnchor = 69.0f;

    // 4OSC exposes raw values and relies on TE's valueToString for display
    // text, so the generic formatter would print a note number instead of Hz.
    if (info.scale != ParameterScale::Boolean && info.scale != ParameterScale::Discrete &&
        info.valueTable.empty()) {
        info.displayText = makeDeviceParameterDisplayTextProvider({}, getDeviceId(), index);
    }
}

// =============================================================================
// FaustProcessor
// =============================================================================

namespace {

// Shared by the effect and the instrument. `[hidden:1]` is filtered here so
// which outputs get a cell stays a display decision.
void populateFaustMeters(const daw::audio::FaustParamPool& pool, DeviceInfo& info) {
    info.meters.clear();
    for (int i = 0; i < daw::audio::FaustParamPool::kOutputSize; ++i) {
        const auto& output = pool.output(i);
        if (output.active && !output.hidden)
            info.meters.push_back(daw::audio::meterInfoFromOutput(output));
    }
}

}  // namespace

FaustProcessor::FaustProcessor(DeviceId deviceId, te::Plugin::Ptr plugin)
    : DeviceProcessor(deviceId, std::move(plugin)) {}

int FaustProcessor::getParameterCount() const {
    auto* faust =
        daw::audio::tracktion_adapter::deviceFromPlugin<daw::audio::FaustPlugin>(plugin_.get());
    if (faust == nullptr) {
        DBG("[FaustProcessor] getParameterCount: plugin cast NULL");
        return 0;
    }
    const int count = faust->getPool().activeCount();
    DBG("[FaustProcessor] getParameterCount -> " << count);
    return count;
}

ParameterInfo FaustProcessor::getParameterInfo(int index) const {
    auto* faust =
        daw::audio::tracktion_adapter::deviceFromPlugin<daw::audio::FaustPlugin>(plugin_.get());
    if (faust == nullptr || index < 0 || index >= daw::audio::FaustParamPool::kSize)
        return {};
    return daw::audio::paramInfoFromSlot(faust->getPool().slot(index));
}

void FaustProcessor::populateParametersFromEngine(DeviceInfo& info) const {
    info.parameters.clear();
    auto* faust =
        daw::audio::tracktion_adapter::deviceFromPlugin<daw::audio::FaustPlugin>(plugin_.get());
    if (faust == nullptr) {
        DBG("[FaustProcessor] populateParameters: plugin cast NULL");
        return;
    }
    info.sidechainPort = faust->properties().sidechain;
    // Active, non-hidden slots only; `paramIndex` keeps the real slot index so
    // links and automation bind to the stable slot. A `[hidden:1]` slot is still
    // written every block, it just gets no cell.
    int active = 0;
    auto params = plugin_->getAutomatableParameters();
    for (int i = 0; i < daw::audio::FaustParamPool::kSize; ++i) {
        const auto& slot = faust->getPool().slot(i);
        if (slot.active && !slot.hidden) {
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
    populateFaustMeters(faust->getPool(), info);
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

// =============================================================================
// FaustInstrumentProcessor
// =============================================================================

FaustInstrumentProcessor::FaustInstrumentProcessor(DeviceId deviceId, te::Plugin::Ptr plugin)
    : DeviceProcessor(deviceId, std::move(plugin)) {}

int FaustInstrumentProcessor::getParameterCount() const {
    auto* faust =
        daw::audio::tracktion_adapter::deviceFromPlugin<daw::audio::FaustInstrumentPlugin>(
            plugin_.get());
    if (faust == nullptr)
        return 0;
    return faust->getPool().activeCount();
}

ParameterInfo FaustInstrumentProcessor::getParameterInfo(int index) const {
    auto* faust =
        daw::audio::tracktion_adapter::deviceFromPlugin<daw::audio::FaustInstrumentPlugin>(
            plugin_.get());
    if (faust == nullptr || index < 0)
        return {};
    // Voice Mode and Glide sit past the pool: they belong to the host, not to
    // whatever patch happens to be loaded.
    if (index >= daw::audio::FaustParamPool::kSize) {
        const int hostIndex = index - daw::audio::FaustParamPool::kSize;
        if (hostIndex >= daw::audio::FaustInstrumentPlugin::kHostParamCount)
            return {};
        return daw::audio::faustInstrumentHostParamInfo(hostIndex);
    }
    return daw::audio::paramInfoFromSlot(faust->getPool().slot(index));
}

void FaustInstrumentProcessor::populateParametersFromEngine(DeviceInfo& info) const {
    info.parameters.clear();
    auto* faust =
        daw::audio::tracktion_adapter::deviceFromPlugin<daw::audio::FaustInstrumentPlugin>(
            plugin_.get());
    if (faust == nullptr)
        return;
    // Same slot rule as FaustProcessor::populateParametersFromEngine.
    auto params = plugin_->getAutomatableParameters();
    for (int i = 0; i < daw::audio::FaustParamPool::kSize; ++i) {
        const auto& slot = faust->getPool().slot(i);
        if (slot.active && !slot.hidden) {
            auto paramInfo = daw::audio::paramInfoFromSlot(slot);
            if (i >= 0 && i < params.size() && params[i]) {
                paramInfo.currentValue =
                    ParameterUtils::normalizedToReal(params[i]->getCurrentValue(), paramInfo);
            }
            info.parameters.push_back(std::move(paramInfo));
        }
    }

    // Host-owned voice settings, appended after the patch's own controls.
    for (int hostIndex = 0; hostIndex < daw::audio::FaustInstrumentPlugin::kHostParamCount;
         ++hostIndex) {
        auto hostInfo = daw::audio::faustInstrumentHostParamInfo(hostIndex);
        const int paramIndex = daw::audio::FaustParamPool::kSize + hostIndex;
        if (paramIndex < params.size() && params[paramIndex]) {
            hostInfo.currentValue =
                ParameterUtils::normalizedToReal(params[paramIndex]->getCurrentValue(), hostInfo);
        }
        info.parameters.push_back(std::move(hostInfo));
    }

    populateFaustMeters(faust->getPool(), info);
}

void FaustInstrumentProcessor::setParameterByIndex(int paramIndex, float value) {
    if (!plugin_)
        return;
    auto params = plugin_->getAutomatableParameters();
    if (paramIndex >= 0 && paramIndex < params.size()) {
        const auto info = getParameterInfo(paramIndex);
        const float normalised = ParameterUtils::realToNormalized(value, info);
        params[paramIndex]->setParameterFromHost(normalised, juce::sendNotificationSync);
    }
}

float FaustInstrumentProcessor::getParameterByIndex(int paramIndex) const {
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
