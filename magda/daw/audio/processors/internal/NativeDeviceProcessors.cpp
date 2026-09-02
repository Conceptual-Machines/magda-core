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
    : AutomatablePluginProcessor(deviceId, std::move(plugin)) {}

MutableElementsProcessor::MutableElementsProcessor(DeviceId deviceId, te::Plugin::Ptr plugin)
    : MagdaDeviceProcessor(deviceId, std::move(plugin)) {}

MutableRingsProcessor::MutableRingsProcessor(DeviceId deviceId, te::Plugin::Ptr plugin)
    : MagdaDeviceProcessor(deviceId, std::move(plugin)) {}

MutableCloudsProcessor::MutableCloudsProcessor(DeviceId deviceId, te::Plugin::Ptr plugin)
    : MagdaDeviceProcessor(deviceId, std::move(plugin)) {}

MagdaConvolutionProcessor::MagdaConvolutionProcessor(DeviceId deviceId, te::Plugin::Ptr plugin)
    : MagdaDeviceProcessor(deviceId, std::move(plugin)) {}

SidechainProcessor::SidechainProcessor(DeviceId deviceId, te::Plugin::Ptr plugin)
    : AutomatablePluginProcessor(deviceId, std::move(plugin)) {}

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
    // filterFreq stores a MIDI note in 0..135.076 that TE turns into Hz via
    // valueToString. The custom UI pins A4 (note 69, 440 Hz) to the visual
    // centre with setSkewForCentre(69.0); mirror that on the shared
    // ParameterInfo so the automation lane, generic slot slider, and curve
    // playback all agree with the plugin UI's skew. Without this, visual
    // centre lands on note 67.5 / ~404 Hz, which doesn't match what a user
    // dragging the FREQ knob sees.
    if (auto params = getAutomatableParameters(); index >= 0 && index < params.size() &&
                                                  params[index] &&
                                                  params[index]->paramID == "filterFreq")
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
        info.displayText = makeDeviceParameterDisplayTextProvider({}, getDeviceId(), index);
    }
}

// =============================================================================
// FaustProcessor
// =============================================================================

namespace {

// Shared by the effect and the instrument: both pool the same way, and both
// filter `[hidden:1]` here rather than in meterInfoFromOutput, so "which
// outputs get a cell" stays a display decision.
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
    info.canSidechain = faust->properties().canSidechain;
    // Only push active, non-hidden slots so the standard ParamGridComponent
    // shows populated cells only. Each ParameterInfo carries its real slot
    // index in `paramIndex`, so links / automation / MIDI Learn still
    // bind to the stable pool slot; display order is not slot identity.
    //
    // `[hidden:1]` slots are active - the host still writes their zone every
    // block (that is the whole point of [role:projecttempo]) - they just get
    // no cell. Filtering here rather than in paramInfoFromSlot keeps "which
    // params are displayed" a display decision.
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
    auto* faust = dynamic_cast<daw::audio::FaustInstrumentPlugin*>(plugin_.get());
    if (faust == nullptr)
        return 0;
    return faust->getPool().activeCount();
}

ParameterInfo FaustInstrumentProcessor::getParameterInfo(int index) const {
    auto* faust = dynamic_cast<daw::audio::FaustInstrumentPlugin*>(plugin_.get());
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
    auto* faust = dynamic_cast<daw::audio::FaustInstrumentPlugin*>(plugin_.get());
    if (faust == nullptr)
        return;
    // Only push active, non-hidden slots; each ParameterInfo carries its real
    // slot index in `paramIndex` so links / automation / MIDI Learn bind to
    // the stable slot. See the effect processor above for why `[hidden:1]`
    // is filtered here rather than in paramInfoFromSlot.
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

    // Host-owned voice allocation, appended after the patch's own controls so
    // they read as device settings rather than as part of the patch. Present
    // for every runtime Faust instrument, whatever the .dsp declares.
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
