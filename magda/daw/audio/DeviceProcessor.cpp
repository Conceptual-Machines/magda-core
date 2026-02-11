#include "DeviceProcessor.hpp"

#include <cmath>
#include <utility>

#include "../core/TrackManager.hpp"

namespace magda {

// =============================================================================
// DeviceProcessor Base Class
// =============================================================================

DeviceProcessor::DeviceProcessor(DeviceId deviceId, te::Plugin::Ptr plugin)
    : deviceId_(deviceId), plugin_(std::move(plugin)) {}

void DeviceProcessor::setParameter(const juce::String& /*paramName*/, float /*value*/) {
    // Base implementation does nothing - override in subclasses
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

void DeviceProcessor::populateParameters(DeviceInfo& info) const {
    info.parameters.clear();
    int count = getParameterCount();
    for (int i = 0; i < count; ++i) {
        info.parameters.push_back(getParameterInfo(i));
    }
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
    DBG("syncFromDeviceInfo: deviceId=" << deviceId_ << " gainDb=" << info.gainDb
                                        << " params.size=" << info.parameters.size());

    setGainDb(info.gainDb);
    setBypassed(info.bypassed);

    // Sync parameter values (ParameterInfo stores actual values in real units)
    auto names = getParameterNames();
    for (size_t i = 0; i < info.parameters.size(); ++i) {
        const auto& param = info.parameters[i];
        if (i < names.size()) {
            setParameter(names[i], param.currentValue);
        }
    }
}

void DeviceProcessor::syncToDeviceInfo(DeviceInfo& info) const {
    info.gainDb = gainDb_;
    info.gainValue = gainLinear_;
    info.bypassed = isBypassed();
}

void DeviceProcessor::applyGain() {
    // Base implementation does nothing - subclasses override to apply gain
    // to the appropriate parameter (e.g., level for tone generator, volume for mixer)
}

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
    setFrequency(440.0f);
    setLevel(0.25f);
    setOscType(0);  // Sine wave

    initialized_ = true;
}

te::ToneGeneratorPlugin* ToneGeneratorProcessor::getTonePlugin() const {
    return dynamic_cast<te::ToneGeneratorPlugin*>(plugin_.get());
}

void ToneGeneratorProcessor::setParameter(const juce::String& paramName, float value) {
    if (paramName.equalsIgnoreCase("frequency") || paramName.equalsIgnoreCase("freq")) {
        // Value is actual Hz (20-20000)
        setFrequency(value);
    } else if (paramName.equalsIgnoreCase("level") || paramName.equalsIgnoreCase("gain") ||
               paramName.equalsIgnoreCase("volume")) {
        // Value is actual dB (-60 to +6)
        float level = juce::Decibels::decibelsToGain(value, -60.0f);
        setLevel(level);
    } else if (paramName.equalsIgnoreCase("oscType") || paramName.equalsIgnoreCase("type") ||
               paramName.equalsIgnoreCase("waveform")) {
        // Value is actual choice index (0 or 1)
        int type = static_cast<int>(std::round(value));
        setOscType(type);
    }
}

float ToneGeneratorProcessor::getParameter(const juce::String& paramName) const {
    if (paramName.equalsIgnoreCase("frequency") || paramName.equalsIgnoreCase("freq")) {
        // Return actual Hz (20-20000)
        return getFrequency();
    } else if (paramName.equalsIgnoreCase("level") || paramName.equalsIgnoreCase("gain") ||
               paramName.equalsIgnoreCase("volume")) {
        // Return actual dB (-60 to 0)
        float level = getLevel();
        return juce::Decibels::gainToDecibels(level, -60.0f);
    } else if (paramName.equalsIgnoreCase("oscType") || paramName.equalsIgnoreCase("type") ||
               paramName.equalsIgnoreCase("waveform")) {
        // Return actual choice index (0 or 1)
        return static_cast<float>(getOscType());
    }
    return 0.0f;
}

std::vector<juce::String> ToneGeneratorProcessor::getParameterNames() const {
    return {"frequency", "level", "oscType"};
}

int ToneGeneratorProcessor::getParameterCount() const {
    return 3;  // frequency, level, oscType
}

ParameterInfo ToneGeneratorProcessor::getParameterInfo(int index) const {
    ParameterInfo info;
    info.paramIndex = index;

    switch (index) {
        case 0: {  // Frequency
            info.name = "Frequency";
            info.unit = "Hz";
            info.minValue = 20.0f;
            info.maxValue = 20000.0f;
            info.defaultValue = 440.0f;
            info.scale = ParameterScale::Logarithmic;
            // Store actual value in Hz
            info.currentValue = juce::jlimit(20.0f, 20000.0f, getFrequency());
            break;
        }

        case 1: {  // Level - display as dB
            info.name = "Level";
            info.unit = "dB";
            info.minValue = -60.0f;
            info.maxValue = 0.0f;
            info.defaultValue = -12.0f;  // 0.25 linear ≈ -12 dB
            info.scale = ParameterScale::Linear;
            // Store actual value in dB
            float level = getLevel();
            float db = level > 0.0f ? juce::Decibels::gainToDecibels(level, -60.0f) : -60.0f;
            info.currentValue = juce::jlimit(-60.0f, 0.0f, db);
            break;
        }

        case 2:  // Oscillator Type
            info.name = "Waveform";
            info.unit = "";
            info.minValue = 0.0f;
            info.maxValue = 1.0f;
            info.defaultValue = 0.0f;
            // Store actual value (choice index)
            info.currentValue = static_cast<float>(getOscType());  // 0 or 1
            info.scale = ParameterScale::Discrete;
            info.choices = {"Sine", "Noise"};
            break;

        default:
            break;
    }

    return info;
}

void ToneGeneratorProcessor::setFrequency(float hz) {
    if (auto* tone = getTonePlugin()) {
        // Clamp to valid range
        hz = juce::jlimit(20.0f, 20000.0f, hz);

        // Set via AutomatableParameter - this is the proper Tracktion Engine way
        // The parameter will automatically sync to the CachedValue
        if (tone->frequencyParam) {
            tone->frequencyParam->setParameter(hz, juce::dontSendNotification);
        }
    }
}

float ToneGeneratorProcessor::getFrequency() const {
    if (auto* tone = getTonePlugin()) {
        return tone->frequency;
    }
    return 440.0f;
}

void ToneGeneratorProcessor::setLevel(float level) {
    if (auto* tone = getTonePlugin()) {
        // Set via AutomatableParameter - proper Tracktion Engine way
        if (tone->levelParam) {
            tone->levelParam->setParameter(level, juce::dontSendNotification);
        }
    }
}

float ToneGeneratorProcessor::getLevel() const {
    if (auto* tone = getTonePlugin()) {
        return tone->level;
    }
    return 0.25f;
}

void ToneGeneratorProcessor::setOscType(int type) {
    if (auto* tone = getTonePlugin()) {
        // Map our 0/1 (sine/noise) to TE's 0/5 (sin/noise)
        // TE enum: 0=sin, 1=triangle, 2=sawUp, 3=sawDown, 4=square, 5=noise
        float teType = (type == 0) ? 0.0f : 5.0f;  // 0→sin, 1→noise

        // Set via AutomatableParameter - proper Tracktion Engine way
        if (tone->oscTypeParam) {
            tone->oscTypeParam->setParameter(teType, juce::dontSendNotification);
        }
    }
}

int ToneGeneratorProcessor::getOscType() const {
    if (auto* tone = getTonePlugin()) {
        // Map TE's 0/5 back to our 0/1
        int teType = static_cast<int>(tone->oscType);
        return (teType == 5) ? 1 : 0;  // noise→1, everything else→0 (sine)
    }
    return 0;  // Sine
}

void ToneGeneratorProcessor::applyGain() {
    // For tone generator, the Level parameter controls output directly.
    // The device gain stage is separate (would need a VolumeAndPan plugin after).
    // For now, don't apply gain here - let Level param control output.
    // TODO: Implement proper per-device gain stage via plugin chain
}

// =============================================================================
// VolumeProcessor
// =============================================================================

VolumeProcessor::VolumeProcessor(DeviceId deviceId, te::Plugin::Ptr plugin)
    : DeviceProcessor(deviceId, std::move(plugin)) {}

te::VolumeAndPanPlugin* VolumeProcessor::getVolPanPlugin() const {
    return dynamic_cast<te::VolumeAndPanPlugin*>(plugin_.get());
}

void VolumeProcessor::setParameter(const juce::String& paramName, float value) {
    if (paramName.equalsIgnoreCase("volume") || paramName.equalsIgnoreCase("gain") ||
        paramName.equalsIgnoreCase("level")) {
        // Value is actual dB
        setVolume(value);
    } else if (paramName.equalsIgnoreCase("pan")) {
        // Value is actual pan (-1 to 1)
        setPan(value);
    }
}

float VolumeProcessor::getParameter(const juce::String& paramName) const {
    if (paramName.equalsIgnoreCase("volume") || paramName.equalsIgnoreCase("gain") ||
        paramName.equalsIgnoreCase("level")) {
        // Return actual dB
        return getVolume();
    } else if (paramName.equalsIgnoreCase("pan")) {
        // Return actual pan (-1 to 1)
        return getPan();
    }
    return 0.0f;
}

std::vector<juce::String> VolumeProcessor::getParameterNames() const {
    return {"volume", "pan"};
}

void VolumeProcessor::setVolume(float db) {
    if (auto* volPan = getVolPanPlugin()) {
        if (volPan->volParam) {
            volPan->volParam->setParameter(db, juce::sendNotificationSync);
        }
    }
}

float VolumeProcessor::getVolume() const {
    if (auto* volPan = getVolPanPlugin()) {
        if (volPan->volParam) {
            return volPan->volParam->getCurrentValue();
        }
    }
    return 0.0f;
}

void VolumeProcessor::setPan(float pan) {
    if (auto* volPan = getVolPanPlugin()) {
        if (volPan->panParam) {
            volPan->panParam->setParameter(pan, juce::sendNotificationSync);
        }
    }
}

float VolumeProcessor::getPan() const {
    if (auto* volPan = getVolPanPlugin()) {
        if (volPan->panParam) {
            return volPan->panParam->getCurrentValue();
        }
    }
    return 0.0f;
}

void VolumeProcessor::applyGain() {
    // For volume plugin, the gain stage is the volume parameter itself
    setVolume(gainDb_);
}

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
    ParameterInfo info;
    if (!plugin_)
        return info;

    auto params = plugin_->getAutomatableParameters();
    if (index < 0 || index >= params.size())
        return info;

    auto* param = params[index];
    info.name = param->getParameterName();
    info.currentValue = param->getCurrentValue();
    auto range = param->getValueRange();
    info.minValue = range.getStart();
    info.maxValue = range.getEnd();
    info.defaultValue = param->getDefaultValue().value_or(range.getStart());
    return info;
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
        params[paramIndex]->setParameter(value, juce::sendNotificationSync);
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
// ExternalPluginProcessor
// =============================================================================

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
                param->setParameter(value, juce::sendNotificationSync);
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
    ParameterInfo info;
    info.paramIndex = index;

    if (auto* ext = getExternalPlugin()) {
        auto params = ext->getAutomatableParameters();
        if (index >= 0 && index < static_cast<int>(params.size())) {
            auto* param = params[static_cast<size_t>(index)];
            if (param) {
                info.name = param->getParameterName();
                info.unit = param->getLabel();

                // Get range from parameter
                auto range = param->getValueRange();
                info.minValue = range.getStart();
                info.maxValue = range.getEnd();

                // getDefaultValue returns optional<float>
                auto defaultVal = param->getDefaultValue();
                info.defaultValue = defaultVal.has_value() ? *defaultVal : info.minValue;
                info.currentValue = param->getCurrentValue();

                // Determine scale type
                // Default to linear scale (could be enhanced to detect logarithmic ranges)
                info.scale = ParameterScale::Linear;

                // Check if parameter has discrete states
                int numStates = param->getNumberOfStates();
                if (numStates > 0 && numStates <= 10) {
                    info.scale = ParameterScale::Discrete;
                    // Could populate choices from parameter if available
                }
            }
        }
    }

    return info;
}

void ExternalPluginProcessor::populateParameters(DeviceInfo& info) const {
    info.parameters.clear();

    if (auto* ext = getExternalPlugin()) {
        auto params = ext->getAutomatableParameters();
        // Load all parameters - UI uses user-selectable visibility and pagination
        int maxParams = static_cast<int>(params.size());

        for (int i = 0; i < maxParams; ++i) {
            info.parameters.push_back(getParameterInfo(i));
        }
    }
}

void ExternalPluginProcessor::syncFromDeviceInfo(const DeviceInfo& info) {
    // Call base class for gain and bypass
    DeviceProcessor::syncFromDeviceInfo(info);

    // Set flag to prevent our listener from triggering a feedback loop
    settingParameterFromUI_ = true;

    // Sync parameter values
    if (auto* ext = getExternalPlugin()) {
        auto params = ext->getAutomatableParameters();
        for (size_t i = 0; i < info.parameters.size() && i < static_cast<size_t>(params.size());
             ++i) {
            if (params[i]) {
                params[i]->setParameter(info.parameters[i].currentValue,
                                        juce::dontSendNotification);
            }
        }
    }

    settingParameterFromUI_ = false;
}

void ExternalPluginProcessor::setParameterByIndex(int paramIndex, float value) {
    // Set flag to prevent our listener from triggering a feedback loop
    settingParameterFromUI_ = true;

    if (auto* ext = getExternalPlugin()) {
        auto params = ext->getAutomatableParameters();
        if (paramIndex >= 0 && paramIndex < static_cast<int>(params.size())) {
            params[static_cast<size_t>(paramIndex)]->setParameter(value,
                                                                  juce::sendNotificationSync);
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
    // This is called asynchronously when the parameter value changes from any source.
    // We use parameterChanged instead for synchronous notification.
    juce::ignoreUnused(param);
}

void ExternalPluginProcessor::parameterChanged(te::AutomatableParameter& param, float newValue) {
    // Prevent feedback loop: don't propagate if we're setting the parameter ourselves
    if (settingParameterFromUI_)
        return;

    // Find the parameter index
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

    // When modifiers are active, use the base value (without modulation) to prevent
    // modulated values from overwriting the base parameter value in the data model.
    float valueToStore =
        param.hasActiveModifierAssignments() ? param.getCurrentBaseValue() : newValue;

    // Update TrackManager on the message thread to avoid threading issues
    // Use callAsync to ensure we're on the message thread
    juce::MessageManager::callAsync([this, parameterIndex, valueToStore]() {
        // Find this device in TrackManager and update its parameter
        // Use a special method that doesn't trigger AudioBridge notification
        auto& tm = TrackManager::getInstance();

        // Search through all tracks to find this device
        for (const auto& track : tm.getTracks()) {
            for (const auto& element : track.chainElements) {
                if (std::holds_alternative<DeviceInfo>(element)) {
                    const auto& device = std::get<DeviceInfo>(element);
                    if (device.id == deviceId_) {
                        // Build a path to this device
                        ChainNodePath path;
                        path.trackId = track.id;
                        path.topLevelDeviceId = deviceId_;
                        // Note: For top-level devices, no steps needed

                        // Update parameter without triggering audio bridge notification
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
