#include "processors/internal/StockDeviceProcessors.hpp"

#include <cmath>
#include <utility>

#include "core/ParameterUtils.hpp"
#include "plugins/ToneGeneratorPlugin.hpp"
#include "plugins/compiled/tracktion/CompiledFaustTracktionAdapter.hpp"
#include "plugins/tracktion/TracktionMagdaDevicePlugin.hpp"
#include "processors/ParameterInfoBuilder.hpp"

namespace magda {

// =============================================================================
// ToneGeneratorProcessor
// =============================================================================

ToneGeneratorProcessor::ToneGeneratorProcessor(DeviceId deviceId, te::Plugin::Ptr plugin)
    : DeviceProcessor(deviceId, std::move(plugin)) {
    // Note: Don't set defaults here - the plugin may not be fully ready
    // Call initializeDefaults() after the processor is stored and plugin is initialized
}

void ToneGeneratorProcessor::initializeDefaults() {
    if (initialized_)
        return;

    // Set default values using the proper setters (they handle null checks internally)
    setOscType(0);  // TE sine
    setBandLimit(false);
    setFrequency(440.0f);
    setLevel(0.25f);

    initialized_ = true;
}

// The device is MAGDA's now and the chain holds the host's wrapper around it,
// so the parameters are reached by slot rather than by the retired plugin's
// named members. The accessors below keep their old units -- Hz, linear level,
// the waveform enum -- because their callers are written against those; the
// conversion to the slot's normalized position happens here.

daw::audio::ToneGeneratorPlugin* ToneGeneratorProcessor::getToneDevice() const {
    return daw::audio::tracktion_adapter::deviceFromPlugin<daw::audio::ToneGeneratorPlugin>(
        plugin_.get());
}

void ToneGeneratorProcessor::setSlotDisplayValue(int slot, float displayValue) const {
    auto* device = getToneDevice();
    auto* param = daw::audio::compiled::tracktionParameterForSlot(plugin_.get(), slot);
    if (device == nullptr || param == nullptr)
        return;

    param->setParameterFromHost(
        ParameterUtils::realToNormalized(displayValue, device->parameterInfo(slot)),
        juce::dontSendNotification);
}

float ToneGeneratorProcessor::slotDisplayValue(int slot, float fallback) const {
    auto* device = getToneDevice();
    auto* param = daw::audio::compiled::tracktionParameterForSlot(plugin_.get(), slot);
    if (device == nullptr || param == nullptr)
        return fallback;

    return ParameterUtils::normalizedToReal(param->getCurrentValue(), device->parameterInfo(slot));
}

void ToneGeneratorProcessor::setParameter(const juce::String& paramName, float value) {
    if (paramName.equalsIgnoreCase("oscType") || paramName.equalsIgnoreCase("type") ||
        paramName.equalsIgnoreCase("waveform")) {
        setOscType(static_cast<int>(std::round(value)));
    } else if (paramName.equalsIgnoreCase("bandLimit")) {
        setBandLimit(value >= 0.5f);
    } else if (paramName.equalsIgnoreCase("frequency") || paramName.equalsIgnoreCase("freq")) {
        setFrequency(value);
    } else if (paramName.equalsIgnoreCase("level") || paramName.equalsIgnoreCase("gain") ||
               paramName.equalsIgnoreCase("volume")) {
        // Value is dB; convert to linear for the plugin
        setLevel(juce::Decibels::decibelsToGain(value, -60.0f));
    }
}

float ToneGeneratorProcessor::getParameter(const juce::String& paramName) const {
    if (paramName.equalsIgnoreCase("oscType") || paramName.equalsIgnoreCase("type") ||
        paramName.equalsIgnoreCase("waveform")) {
        return static_cast<float>(getOscType());
    } else if (paramName.equalsIgnoreCase("bandLimit")) {
        return getBandLimit() ? 1.0f : 0.0f;
    } else if (paramName.equalsIgnoreCase("frequency") || paramName.equalsIgnoreCase("freq")) {
        return getFrequency();
    } else if (paramName.equalsIgnoreCase("level") || paramName.equalsIgnoreCase("gain") ||
               paramName.equalsIgnoreCase("volume")) {
        float level = getLevel();
        return juce::Decibels::gainToDecibels(level, -60.0f);
    }
    return 0.0f;
}

std::vector<juce::String> ToneGeneratorProcessor::getParameterNames() const {
    // Order matches te::ToneGeneratorPlugin::getAutomatableParameters() so
    // mod/macro links resolve to the correct TE parameter.
    return {"oscType", "bandLimit", "frequency", "level"};
}

int ToneGeneratorProcessor::getParameterCount() const {
    return 4;
}

ParameterInfo ToneGeneratorProcessor::getParameterInfo(int index) const {
    ParameterInfo info;
    info.paramIndex = index;

    switch (index) {
        case 0:  // Oscillator Type (TE enum 0-5)
            info.name = "Waveform";
            info.unit = "";
            info.minValue = 0.0f;
            info.maxValue = 5.0f;
            info.teMinValue = 0.0f;
            info.teMaxValue = 5.0f;
            info.defaultValue = 0.0f;
            info.currentValue = static_cast<float>(getOscType());
            info.scale = ParameterScale::Discrete;
            info.choices = {"Sine", "Triangle", "Saw Up", "Saw Down", "Square", "Noise"};
            break;

        case 1:  // Band Limit
            info.name = "Band Limit";
            info.unit = "";
            info.minValue = 0.0f;
            info.maxValue = 1.0f;
            info.teMinValue = 0.0f;
            info.teMaxValue = 1.0f;
            info.defaultValue = 0.0f;
            info.currentValue = getBandLimit() ? 1.0f : 0.0f;
            info.scale = ParameterScale::Boolean;
            info.modulatable = false;
            info.choices = {"Aliased", "Band Limited"};
            break;

        case 2: {  // Frequency (log sweep, 1 kHz at visual midpoint)
            info.name = "Frequency";
            info.unit = magda::technicalText(magda::TechnicalTextToken::Hertz);
            info.minValue = 20.0f;
            info.maxValue = 20000.0f;
            info.teMinValue = 20.0f;
            info.teMaxValue = 20000.0f;
            info.defaultValue = 440.0f;
            info.scale = ParameterScale::Logarithmic;
            info.scaleAnchor = 1000.0f;
            info.currentValue = juce::jlimit(20.0f, 20000.0f, getFrequency());
            break;
        }

        case 3: {  // Level — UI is in dB; plugin-native is linear, converted at the bridge
            info.name = "Level";
            info.unit = magda::technicalText(magda::TechnicalTextToken::Decibels);
            info.minValue = -60.0f;
            info.maxValue = 0.0f;
            info.teMinValue = -60.0f;
            info.teMaxValue = 0.0f;
            info.defaultValue = -12.0f;  // 0.25 linear ≈ -12 dB
            info.scale = ParameterScale::Linear;
            info.displayFormat = DisplayFormat::Decibels;
            float level = getLevel();
            float db = level > 0.0f ? juce::Decibels::gainToDecibels(level, -60.0f) : -60.0f;
            info.currentValue = juce::jlimit(-60.0f, 0.0f, db);
            break;
        }

        default:
            break;
    }

    return info;
}

void ToneGeneratorProcessor::setParameterByIndex(int paramIndex, float value) {
    switch (paramIndex) {
        case 0:  // Oscillator type (TE enum 0-5)
            setOscType(static_cast<int>(std::round(value)));
            break;
        case 1:  // Band limit
            setBandLimit(value >= 0.5f);
            break;
        case 2:  // Frequency (Hz)
            setFrequency(value);
            break;
        case 3:  // Level (dB from UI, convert to linear for plugin)
            setLevel(juce::Decibels::decibelsToGain(value, -60.0f));
            break;
        default:
            break;
    }
}

void ToneGeneratorProcessor::setFrequency(float hz) {
    setSlotDisplayValue(daw::audio::ToneGeneratorPlugin::kFrequencyParamIndex,
                        juce::jlimit(20.0f, 20000.0f, hz));
}

float ToneGeneratorProcessor::getFrequency() const {
    return slotDisplayValue(daw::audio::ToneGeneratorPlugin::kFrequencyParamIndex, 440.0f);
}

void ToneGeneratorProcessor::setLevel(float level) {
    // The caller's level is linear; the device's is dB.
    setSlotDisplayValue(daw::audio::ToneGeneratorPlugin::kLevelParamIndex,
                        juce::Decibels::gainToDecibels(level, -60.0f));
}

float ToneGeneratorProcessor::getLevel() const {
    const float db = slotDisplayValue(daw::audio::ToneGeneratorPlugin::kLevelParamIndex, -12.0f);
    return db <= -60.0f ? 0.0f : juce::Decibels::decibelsToGain(db, -60.0f);
}

void ToneGeneratorProcessor::setOscType(int teOscType) {
    setSlotDisplayValue(daw::audio::ToneGeneratorPlugin::kWaveformParamIndex,
                        static_cast<float>(juce::jlimit(0, 5, teOscType)));
}

int ToneGeneratorProcessor::getOscType() const {
    return juce::jlimit(0, 5,
                        static_cast<int>(std::lround(slotDisplayValue(
                            daw::audio::ToneGeneratorPlugin::kWaveformParamIndex, 0.0f))));
}

void ToneGeneratorProcessor::setBandLimit(bool bandLimited) {
    setSlotDisplayValue(daw::audio::ToneGeneratorPlugin::kBandLimitParamIndex,
                        bandLimited ? 1.0f : 0.0f);
}

bool ToneGeneratorProcessor::getBandLimit() const {
    return slotDisplayValue(daw::audio::ToneGeneratorPlugin::kBandLimitParamIndex, 0.0f) >= 0.5f;
}

void ToneGeneratorProcessor::applyGain() {
    // For tone generator, the Level parameter controls output directly.
    // The device gain stage is separate (would need a VolumeAndPan plugin after).
    // For now, don't apply gain here - let Level param control output.
    // TODO: Implement proper per-device gain stage via plugin chain
}

// =============================================================================
// UtilityProcessor
// =============================================================================

UtilityProcessor::UtilityProcessor(DeviceId deviceId, te::Plugin::Ptr plugin)
    : DeviceProcessor(deviceId, std::move(plugin)) {}

te::VolumeAndPanPlugin* UtilityProcessor::getVolPanPlugin() const {
    return dynamic_cast<te::VolumeAndPanPlugin*>(plugin_.get());
}

int UtilityProcessor::getParameterCount() const {
    // Volume (automatable), Pan (automatable), Polarity (virtual bool)
    return 3;
}

ParameterInfo UtilityProcessor::getParameterInfo(int index) const {
    ParameterInfo info;
    auto* volPan = getVolPanPlugin();
    if (!volPan)
        return info;

    switch (index) {
        case 0: {
            // Volume — slider position 0..1 (fader-position, not dB)
            info.name = "Volume";
            info.minValue = 0.0f;
            info.maxValue = 1.0f;
            info.defaultValue = te::decibelsToVolumeFaderPosition(0.0f);
            if (volPan->volParam)
                info.currentValue = volPan->volParam->getCurrentValue();
            break;
        }
        case 1: {
            // Pan — -1..1
            info = ParameterPresets::pan(1, "Pan");
            if (volPan->panParam)
                info.currentValue = volPan->panParam->getCurrentValue();
            break;
        }
        case 2: {
            // Polarity — CachedValue<bool>
            info = ParameterPresets::boolean(2, "Polarity");
            info.currentValue = volPan->polarity.get() ? 1.0f : 0.0f;
            break;
        }
        default:
            break;
    }
    return info;
}

void UtilityProcessor::populateParameters(DeviceInfo& info) const {
    info.parameters.clear();
    for (int i = 0; i < getParameterCount(); ++i) {
        info.parameters.push_back(getParameterInfo(i));
    }
}

void UtilityProcessor::setParameterByIndex(int paramIndex, float value) {
    auto* volPan = getVolPanPlugin();
    if (!volPan)
        return;

    switch (paramIndex) {
        case 0:
            if (volPan->volParam)
                volPan->volParam->setParameterFromHost(value, juce::sendNotificationSync);
            break;
        case 1:
            if (volPan->panParam)
                volPan->panParam->setParameterFromHost(value, juce::sendNotificationSync);
            break;
        case 2:
            volPan->polarity = value >= 0.5f;
            break;
        default:
            break;
    }
}

float UtilityProcessor::getParameterByIndex(int paramIndex) const {
    auto* volPan = getVolPanPlugin();
    if (!volPan)
        return 0.0f;

    switch (paramIndex) {
        case 0:
            return volPan->volParam ? volPan->volParam->getCurrentValue() : 0.0f;
        case 1:
            return volPan->panParam ? volPan->panParam->getCurrentValue() : 0.0f;
        case 2:
            return volPan->polarity.get() ? 1.0f : 0.0f;
        default:
            return 0.0f;
    }
}

}  // namespace magda
