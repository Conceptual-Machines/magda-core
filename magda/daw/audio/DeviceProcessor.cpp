#include "DeviceProcessor.hpp"

#include <cmath>
#include <utility>

#include "../core/TrackManager.hpp"
#include "ArpeggiatorPlugin.hpp"
#include "StepSequencerPlugin.hpp"

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
        params[paramIndex]->setParameter(value, juce::sendNotificationSync);
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
// EqualiserProcessor
// =============================================================================

EqualiserProcessor::EqualiserProcessor(DeviceId deviceId, te::Plugin::Ptr plugin)
    : DeviceProcessor(deviceId, std::move(plugin)) {}

int EqualiserProcessor::getParameterCount() const {
    if (plugin_)
        return static_cast<int>(plugin_->getAutomatableParameters().size()) + 1;
    return 0;
}

ParameterInfo EqualiserProcessor::getParameterInfo(int index) const {
    ParameterInfo info;
    if (!plugin_)
        return info;

    auto params = plugin_->getAutomatableParameters();
    int autoCount = static_cast<int>(params.size());

    if (index >= 0 && index < autoCount) {
        auto* param = params[index];
        info.name = param->getParameterName();
        info.currentValue = param->getCurrentValue();
        auto range = param->getValueRange();
        info.minValue = range.getStart();
        info.maxValue = range.getEnd();
        info.defaultValue = param->getDefaultValue().value_or(range.getStart());
    } else if (index == autoCount) {
        info.name = "Phase Invert";
        info.minValue = 0.0f;
        info.maxValue = 1.0f;
        info.defaultValue = 0.0f;
        if (auto* eq = dynamic_cast<te::EqualiserPlugin*>(plugin_.get()))
            info.currentValue = eq->phaseInvert.get() ? 1.0f : 0.0f;
        else
            info.currentValue = 0.0f;
    }
    return info;
}

void EqualiserProcessor::populateParameters(DeviceInfo& info) const {
    info.parameters.clear();
    int count = getParameterCount();
    for (int i = 0; i < count; ++i) {
        info.parameters.push_back(getParameterInfo(i));
    }
}

void EqualiserProcessor::setParameterByIndex(int paramIndex, float value) {
    if (!plugin_)
        return;

    auto params = plugin_->getAutomatableParameters();
    int autoCount = static_cast<int>(params.size());

    if (paramIndex >= 0 && paramIndex < autoCount) {
        params[paramIndex]->setParameter(value, juce::sendNotificationSync);
    } else if (paramIndex == autoCount) {
        if (auto* eq = dynamic_cast<te::EqualiserPlugin*>(plugin_.get()))
            eq->phaseInvert = value >= 0.5f;
    }
}

float EqualiserProcessor::getParameterByIndex(int paramIndex) const {
    if (!plugin_)
        return 0.0f;

    auto params = plugin_->getAutomatableParameters();
    int autoCount = static_cast<int>(params.size());

    if (paramIndex >= 0 && paramIndex < autoCount)
        return params[paramIndex]->getCurrentValue();
    if (paramIndex == autoCount) {
        if (auto* eq = dynamic_cast<te::EqualiserPlugin*>(plugin_.get()))
            return eq->phaseInvert.get() ? 1.0f : 0.0f;
    }
    return 0.0f;
}

// =============================================================================
// CompressorProcessor
// =============================================================================

CompressorProcessor::CompressorProcessor(DeviceId deviceId, te::Plugin::Ptr plugin)
    : DeviceProcessor(deviceId, std::move(plugin)) {}

int CompressorProcessor::getParameterCount() const {
    if (plugin_)
        return static_cast<int>(plugin_->getAutomatableParameters().size()) + 1;
    return 0;
}

ParameterInfo CompressorProcessor::getParameterInfo(int index) const {
    ParameterInfo info;
    if (!plugin_)
        return info;

    auto params = plugin_->getAutomatableParameters();
    int autoCount = static_cast<int>(params.size());

    if (index >= 0 && index < autoCount) {
        auto* param = params[index];
        info.name = param->getParameterName();
        info.currentValue = param->getCurrentValue();
        auto range = param->getValueRange();
        info.minValue = range.getStart();
        info.maxValue = range.getEnd();
        info.defaultValue = param->getDefaultValue().value_or(range.getStart());
    } else if (index == autoCount) {
        info.name = "Sidechain Trigger";
        info.minValue = 0.0f;
        info.maxValue = 1.0f;
        info.defaultValue = 0.0f;
        if (auto* comp = dynamic_cast<te::CompressorPlugin*>(plugin_.get()))
            info.currentValue = comp->useSidechainTrigger.get() ? 1.0f : 0.0f;
        else
            info.currentValue = 0.0f;
    }
    return info;
}

void CompressorProcessor::populateParameters(DeviceInfo& info) const {
    info.parameters.clear();
    int count = getParameterCount();
    for (int i = 0; i < count; ++i) {
        info.parameters.push_back(getParameterInfo(i));
    }
}

void CompressorProcessor::setParameterByIndex(int paramIndex, float value) {
    if (!plugin_)
        return;

    auto params = plugin_->getAutomatableParameters();
    int autoCount = static_cast<int>(params.size());

    if (paramIndex >= 0 && paramIndex < autoCount) {
        params[paramIndex]->setParameter(value, juce::sendNotificationSync);
    } else if (paramIndex == autoCount) {
        if (auto* comp = dynamic_cast<te::CompressorPlugin*>(plugin_.get()))
            comp->useSidechainTrigger = value >= 0.5f;
    }
}

float CompressorProcessor::getParameterByIndex(int paramIndex) const {
    if (!plugin_)
        return 0.0f;

    auto params = plugin_->getAutomatableParameters();
    int autoCount = static_cast<int>(params.size());

    if (paramIndex >= 0 && paramIndex < autoCount)
        return params[paramIndex]->getCurrentValue();
    if (paramIndex == autoCount) {
        if (auto* comp = dynamic_cast<te::CompressorPlugin*>(plugin_.get()))
            return comp->useSidechainTrigger.get() ? 1.0f : 0.0f;
    }
    return 0.0f;
}

// =============================================================================
// DelayProcessor
// =============================================================================

DelayProcessor::DelayProcessor(DeviceId deviceId, te::Plugin::Ptr plugin)
    : DeviceProcessor(deviceId, std::move(plugin)) {}

int DelayProcessor::getParameterCount() const {
    // 2 automatable params (feedback, mix) + 1 virtual param (lengthMs)
    if (plugin_)
        return static_cast<int>(plugin_->getAutomatableParameters().size()) + 1;
    return 0;
}

ParameterInfo DelayProcessor::getParameterInfo(int index) const {
    ParameterInfo info;
    if (!plugin_)
        return info;

    auto params = plugin_->getAutomatableParameters();
    int autoCount = static_cast<int>(params.size());

    if (index >= 0 && index < autoCount) {
        auto* param = params[index];
        info.name = param->getParameterName();
        info.currentValue = param->getCurrentValue();
        auto range = param->getValueRange();
        info.minValue = range.getStart();
        info.maxValue = range.getEnd();
        info.defaultValue = param->getDefaultValue().value_or(range.getStart());
    } else if (index == autoCount) {
        // Virtual parameter: delay length in ms
        info.name = "Length";
        info.unit = "ms";
        info.minValue = 1.0f;
        info.maxValue = 2000.0f;
        info.defaultValue = 150.0f;
        if (auto* delay = dynamic_cast<te::DelayPlugin*>(plugin_.get()))
            info.currentValue = static_cast<float>(delay->lengthMs.get());
        else
            info.currentValue = 150.0f;
    }
    return info;
}

void DelayProcessor::populateParameters(DeviceInfo& info) const {
    info.parameters.clear();
    int count = getParameterCount();
    for (int i = 0; i < count; ++i) {
        info.parameters.push_back(getParameterInfo(i));
    }
}

void DelayProcessor::setParameterByIndex(int paramIndex, float value) {
    if (!plugin_)
        return;

    auto params = plugin_->getAutomatableParameters();
    int autoCount = static_cast<int>(params.size());

    if (paramIndex >= 0 && paramIndex < autoCount) {
        params[paramIndex]->setParameter(value, juce::sendNotificationSync);
    } else if (paramIndex == autoCount) {
        // Virtual parameter: delay length in ms
        if (auto* delay = dynamic_cast<te::DelayPlugin*>(plugin_.get()))
            delay->lengthMs = static_cast<int>(value);
    }
}

float DelayProcessor::getParameterByIndex(int paramIndex) const {
    if (!plugin_)
        return 0.0f;

    auto params = plugin_->getAutomatableParameters();
    int autoCount = static_cast<int>(params.size());

    if (paramIndex >= 0 && paramIndex < autoCount)
        return params[paramIndex]->getCurrentValue();
    if (paramIndex == autoCount) {
        if (auto* delay = dynamic_cast<te::DelayPlugin*>(plugin_.get()))
            return static_cast<float>(delay->lengthMs.get());
    }
    return 0.0f;
}

// =============================================================================
// ReverbProcessor
// =============================================================================

ReverbProcessor::ReverbProcessor(DeviceId deviceId, te::Plugin::Ptr plugin)
    : DeviceProcessor(deviceId, std::move(plugin)) {}

int ReverbProcessor::getParameterCount() const {
    if (plugin_)
        return plugin_->getAutomatableParameters().size();
    return 0;
}

ParameterInfo ReverbProcessor::getParameterInfo(int index) const {
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

void ReverbProcessor::populateParameters(DeviceInfo& info) const {
    info.parameters.clear();
    int count = getParameterCount();
    for (int i = 0; i < count; ++i) {
        info.parameters.push_back(getParameterInfo(i));
    }
}

void ReverbProcessor::setParameterByIndex(int paramIndex, float value) {
    if (!plugin_)
        return;

    auto params = plugin_->getAutomatableParameters();
    if (paramIndex >= 0 && paramIndex < params.size()) {
        params[paramIndex]->setParameter(value, juce::sendNotificationSync);
    }
}

float ReverbProcessor::getParameterByIndex(int paramIndex) const {
    if (!plugin_)
        return 0.0f;

    auto params = plugin_->getAutomatableParameters();
    if (paramIndex >= 0 && paramIndex < params.size())
        return params[paramIndex]->getCurrentValue();
    return 0.0f;
}

// =============================================================================
// ChorusProcessor
// =============================================================================

ChorusProcessor::ChorusProcessor(DeviceId deviceId, te::Plugin::Ptr plugin)
    : DeviceProcessor(deviceId, std::move(plugin)) {}

int ChorusProcessor::getParameterCount() const {
    return 4;  // All virtual: depthMs, speedHz, width, mixProportion
}

ParameterInfo ChorusProcessor::getParameterInfo(int index) const {
    ParameterInfo info;
    auto* chorus = plugin_ ? dynamic_cast<te::ChorusPlugin*>(plugin_.get()) : nullptr;

    switch (index) {
        case 0:
            info.name = "Depth";
            info.unit = "ms";
            info.minValue = 0.1f;
            info.maxValue = 20.0f;
            info.defaultValue = 3.0f;
            info.currentValue = chorus ? chorus->depthMs.get() : 3.0f;
            break;
        case 1:
            info.name = "Speed";
            info.unit = "Hz";
            info.minValue = 0.1f;
            info.maxValue = 10.0f;
            info.defaultValue = 1.0f;
            info.currentValue = chorus ? chorus->speedHz.get() : 1.0f;
            break;
        case 2:
            info.name = "Width";
            info.minValue = 0.0f;
            info.maxValue = 1.0f;
            info.defaultValue = 0.5f;
            info.currentValue = chorus ? chorus->width.get() : 0.5f;
            break;
        case 3:
            info.name = "Mix";
            info.minValue = 0.0f;
            info.maxValue = 1.0f;
            info.defaultValue = 0.5f;
            info.currentValue = chorus ? chorus->mixProportion.get() : 0.5f;
            break;
        default:
            break;
    }
    return info;
}

void ChorusProcessor::populateParameters(DeviceInfo& info) const {
    info.parameters.clear();
    for (int i = 0; i < getParameterCount(); ++i)
        info.parameters.push_back(getParameterInfo(i));
}

void ChorusProcessor::setParameterByIndex(int paramIndex, float value) {
    auto* chorus = plugin_ ? dynamic_cast<te::ChorusPlugin*>(plugin_.get()) : nullptr;
    if (!chorus)
        return;

    switch (paramIndex) {
        case 0:
            chorus->depthMs = value;
            break;
        case 1:
            chorus->speedHz = value;
            break;
        case 2:
            chorus->width = value;
            break;
        case 3:
            chorus->mixProportion = value;
            break;
        default:
            break;
    }
}

float ChorusProcessor::getParameterByIndex(int paramIndex) const {
    auto* chorus = plugin_ ? dynamic_cast<te::ChorusPlugin*>(plugin_.get()) : nullptr;
    if (!chorus)
        return 0.0f;

    switch (paramIndex) {
        case 0:
            return chorus->depthMs.get();
        case 1:
            return chorus->speedHz.get();
        case 2:
            return chorus->width.get();
        case 3:
            return chorus->mixProportion.get();
        default:
            return 0.0f;
    }
}

// =============================================================================
// PhaserProcessor
// =============================================================================

PhaserProcessor::PhaserProcessor(DeviceId deviceId, te::Plugin::Ptr plugin)
    : DeviceProcessor(deviceId, std::move(plugin)) {}

int PhaserProcessor::getParameterCount() const {
    return 3;  // All virtual: depth, rate, feedbackGain
}

ParameterInfo PhaserProcessor::getParameterInfo(int index) const {
    ParameterInfo info;
    auto* phaser = plugin_ ? dynamic_cast<te::PhaserPlugin*>(plugin_.get()) : nullptr;

    switch (index) {
        case 0:
            info.name = "Depth";
            info.minValue = 0.0f;
            info.maxValue = 12.0f;
            info.defaultValue = 5.0f;
            info.currentValue = phaser ? phaser->depth.get() : 5.0f;
            break;
        case 1:
            info.name = "Rate";
            info.minValue = 0.0f;
            info.maxValue = 2.0f;
            info.defaultValue = 0.4f;
            info.currentValue = phaser ? phaser->rate.get() : 0.4f;
            break;
        case 2:
            info.name = "Feedback";
            info.minValue = 0.0f;
            info.maxValue = 0.99f;
            info.defaultValue = 0.7f;
            info.currentValue = phaser ? phaser->feedbackGain.get() : 0.7f;
            break;
        default:
            break;
    }
    return info;
}

void PhaserProcessor::populateParameters(DeviceInfo& info) const {
    info.parameters.clear();
    for (int i = 0; i < getParameterCount(); ++i)
        info.parameters.push_back(getParameterInfo(i));
}

void PhaserProcessor::setParameterByIndex(int paramIndex, float value) {
    auto* phaser = plugin_ ? dynamic_cast<te::PhaserPlugin*>(plugin_.get()) : nullptr;
    if (!phaser)
        return;

    switch (paramIndex) {
        case 0:
            phaser->depth = value;
            break;
        case 1:
            phaser->rate = value;
            break;
        case 2:
            phaser->feedbackGain = value;
            break;
        default:
            break;
    }
}

float PhaserProcessor::getParameterByIndex(int paramIndex) const {
    auto* phaser = plugin_ ? dynamic_cast<te::PhaserPlugin*>(plugin_.get()) : nullptr;
    if (!phaser)
        return 0.0f;

    switch (paramIndex) {
        case 0:
            return phaser->depth.get();
        case 1:
            return phaser->rate.get();
        case 2:
            return phaser->feedbackGain.get();
        default:
            return 0.0f;
    }
}

// =============================================================================
// FilterProcessor
// =============================================================================

FilterProcessor::FilterProcessor(DeviceId deviceId, te::Plugin::Ptr plugin)
    : DeviceProcessor(deviceId, std::move(plugin)) {}

int FilterProcessor::getParameterCount() const {
    // 1 automatable (frequency) + 1 virtual (mode)
    int autoCount = 0;
    if (plugin_) {
        autoCount = static_cast<int>(plugin_->getAutomatableParameters().size());
    }
    return autoCount + 1;
}

ParameterInfo FilterProcessor::getParameterInfo(int index) const {
    ParameterInfo info;
    if (!plugin_)
        return info;

    auto params = plugin_->getAutomatableParameters();
    int autoCount = static_cast<int>(params.size());

    if (index >= 0 && index < autoCount) {
        auto* param = params[index];
        info.name = param->getParameterName();
        info.currentValue = param->getCurrentValue();
        auto range = param->getValueRange();
        info.minValue = range.getStart();
        info.maxValue = range.getEnd();
        info.defaultValue = param->getDefaultValue().value_or(range.getStart());
    } else if (index == autoCount) {
        // Virtual param: mode (0 = lowpass, 1 = highpass)
        info.name = "Mode";
        info.minValue = 0.0f;
        info.maxValue = 1.0f;
        info.defaultValue = 0.0f;
        if (auto* lp = dynamic_cast<te::LowPassPlugin*>(plugin_.get()))
            info.currentValue = lp->isLowPass() ? 0.0f : 1.0f;
        else
            info.currentValue = 0.0f;
    }
    return info;
}

void FilterProcessor::populateParameters(DeviceInfo& info) const {
    info.parameters.clear();
    for (int i = 0; i < getParameterCount(); ++i)
        info.parameters.push_back(getParameterInfo(i));
}

void FilterProcessor::setParameterByIndex(int paramIndex, float value) {
    if (!plugin_)
        return;

    auto params = plugin_->getAutomatableParameters();
    int autoCount = static_cast<int>(params.size());

    if (paramIndex < autoCount && params[paramIndex]) {
        params[paramIndex]->setParameter(value, juce::sendNotificationSync);
        return;
    }

    // Virtual param: mode
    if (paramIndex == autoCount) {
        if (auto* lp = dynamic_cast<te::LowPassPlugin*>(plugin_.get()))
            lp->mode = value >= 0.5f ? "highpass" : "lowpass";
    }
}

float FilterProcessor::getParameterByIndex(int paramIndex) const {
    if (!plugin_)
        return 0.0f;

    auto params = plugin_->getAutomatableParameters();
    int autoCount = static_cast<int>(params.size());

    if (paramIndex < autoCount && params[paramIndex])
        return params[paramIndex]->getCurrentValue();

    // Virtual param: mode
    if (paramIndex == autoCount) {
        if (auto* lp = dynamic_cast<te::LowPassPlugin*>(plugin_.get()))
            return lp->isLowPass() ? 0.0f : 1.0f;
    }
    return 0.0f;
}

// =============================================================================
// PitchShiftProcessor
// =============================================================================

PitchShiftProcessor::PitchShiftProcessor(DeviceId deviceId, te::Plugin::Ptr plugin)
    : DeviceProcessor(deviceId, std::move(plugin)) {}

int PitchShiftProcessor::getParameterCount() const {
    if (!plugin_)
        return 0;
    return static_cast<int>(plugin_->getAutomatableParameters().size());
}

ParameterInfo PitchShiftProcessor::getParameterInfo(int index) const {
    ParameterInfo info;
    if (!plugin_)
        return info;

    auto params = plugin_->getAutomatableParameters();
    if (index >= 0 && index < static_cast<int>(params.size())) {
        auto* param = params[index];
        info.name = param->getParameterName();
        info.currentValue = param->getCurrentValue();
        auto range = param->getValueRange();
        info.minValue = range.getStart();
        info.maxValue = range.getEnd();
        info.defaultValue = param->getDefaultValue().value_or(range.getStart());
    }
    return info;
}

void PitchShiftProcessor::populateParameters(DeviceInfo& info) const {
    info.parameters.clear();
    for (int i = 0; i < getParameterCount(); ++i)
        info.parameters.push_back(getParameterInfo(i));
}

void PitchShiftProcessor::setParameterByIndex(int paramIndex, float value) {
    if (!plugin_)
        return;
    auto params = plugin_->getAutomatableParameters();
    if (paramIndex < static_cast<int>(params.size()) && params[paramIndex])
        params[paramIndex]->setParameter(value, juce::sendNotificationSync);
}

float PitchShiftProcessor::getParameterByIndex(int paramIndex) const {
    if (!plugin_)
        return 0.0f;
    auto params = plugin_->getAutomatableParameters();
    if (paramIndex < static_cast<int>(params.size()) && params[paramIndex])
        return params[paramIndex]->getCurrentValue();
    return 0.0f;
}

// =============================================================================
// ImpulseResponseProcessor
// =============================================================================

ImpulseResponseProcessor::ImpulseResponseProcessor(DeviceId deviceId, te::Plugin::Ptr plugin)
    : DeviceProcessor(deviceId, std::move(plugin)) {}

int ImpulseResponseProcessor::getParameterCount() const {
    if (!plugin_)
        return 0;
    return static_cast<int>(plugin_->getAutomatableParameters().size());
}

ParameterInfo ImpulseResponseProcessor::getParameterInfo(int index) const {
    ParameterInfo info;
    if (!plugin_)
        return info;

    auto params = plugin_->getAutomatableParameters();
    if (index >= 0 && index < static_cast<int>(params.size())) {
        auto* param = params[index];
        info.name = param->getParameterName();
        info.currentValue = param->getCurrentValue();
        auto range = param->getValueRange();
        info.minValue = range.getStart();
        info.maxValue = range.getEnd();
        info.defaultValue = param->getDefaultValue().value_or(range.getStart());
    }
    return info;
}

void ImpulseResponseProcessor::populateParameters(DeviceInfo& info) const {
    info.parameters.clear();
    for (int i = 0; i < getParameterCount(); ++i)
        info.parameters.push_back(getParameterInfo(i));
}

void ImpulseResponseProcessor::setParameterByIndex(int paramIndex, float value) {
    if (!plugin_)
        return;
    auto params = plugin_->getAutomatableParameters();
    if (paramIndex < static_cast<int>(params.size()) && params[paramIndex])
        params[paramIndex]->setParameter(value, juce::sendNotificationSync);
}

float ImpulseResponseProcessor::getParameterByIndex(int paramIndex) const {
    if (!plugin_)
        return 0.0f;
    auto params = plugin_->getAutomatableParameters();
    if (paramIndex < static_cast<int>(params.size()) && params[paramIndex])
        return params[paramIndex]->getCurrentValue();
    return 0.0f;
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
            // Volume — slider position 0..1
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
            info.name = "Pan";
            info.minValue = -1.0f;
            info.maxValue = 1.0f;
            info.defaultValue = 0.0f;
            if (volPan->panParam)
                info.currentValue = volPan->panParam->getCurrentValue();
            break;
        }
        case 2: {
            // Polarity — CachedValue<bool>
            info.name = "Polarity";
            info.minValue = 0.0f;
            info.maxValue = 1.0f;
            info.defaultValue = 0.0f;
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
                volPan->volParam->setParameter(value, juce::sendNotificationSync);
            break;
        case 1:
            if (volPan->panParam)
                volPan->panParam->setParameter(value, juce::sendNotificationSync);
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

// =============================================================================
// ArpeggiatorProcessor
// =============================================================================

ArpeggiatorProcessor::ArpeggiatorProcessor(DeviceId deviceId, te::Plugin::Ptr plugin)
    : DeviceProcessor(deviceId, std::move(plugin)) {}

daw::audio::ArpeggiatorPlugin* ArpeggiatorProcessor::getArpPlugin() const {
    return dynamic_cast<daw::audio::ArpeggiatorPlugin*>(plugin_.get());
}

int ArpeggiatorProcessor::getParameterCount() const {
    if (plugin_)
        return static_cast<int>(plugin_->getAutomatableParameters().size());
    return 0;
}

ParameterInfo ArpeggiatorProcessor::getParameterInfo(int index) const {
    ParameterInfo info;
    if (!plugin_)
        return info;

    auto params = plugin_->getAutomatableParameters();
    int autoCount = static_cast<int>(params.size());

    if (index >= 0 && index < autoCount) {
        auto* param = params[index];
        info.name = param->getParameterName();
        info.currentValue = param->getCurrentValue();
        auto range = param->getValueRange();
        info.minValue = range.getStart();
        info.maxValue = range.getEnd();
        info.defaultValue = param->getDefaultValue().value_or(range.getStart());
        // Depth (5) and skew (6) default to bipolar; all others unipolar
        info.bipolarModulation = (index == 5 || index == 6);
    }
    return info;
}

void ArpeggiatorProcessor::populateParameters(DeviceInfo& info) const {
    info.parameters.clear();
    for (int i = 0; i < getParameterCount(); ++i) {
        info.parameters.push_back(getParameterInfo(i));
    }
}

void ArpeggiatorProcessor::setParameterByIndex(int paramIndex, float value) {
    if (!plugin_)
        return;

    auto params = plugin_->getAutomatableParameters();
    int autoCount = static_cast<int>(params.size());

    if (paramIndex >= 0 && paramIndex < autoCount) {
        params[paramIndex]->setParameter(value, juce::sendNotificationSync);
    }
}

float ArpeggiatorProcessor::getParameterByIndex(int paramIndex) const {
    if (!plugin_)
        return 0.0f;

    auto params = plugin_->getAutomatableParameters();
    int autoCount = static_cast<int>(params.size());

    if (paramIndex >= 0 && paramIndex < autoCount)
        return params[paramIndex]->getCurrentValue();
    return 0.0f;
}

// =============================================================================
// StepSequencerProcessor
// =============================================================================

StepSequencerProcessor::StepSequencerProcessor(DeviceId deviceId, te::Plugin::Ptr plugin)
    : DeviceProcessor(deviceId, std::move(plugin)) {}

daw::audio::StepSequencerPlugin* StepSequencerProcessor::getSeqPlugin() const {
    return dynamic_cast<daw::audio::StepSequencerPlugin*>(plugin_.get());
}

int StepSequencerProcessor::getParameterCount() const {
    if (plugin_)
        return static_cast<int>(plugin_->getAutomatableParameters().size());
    return 0;
}

ParameterInfo StepSequencerProcessor::getParameterInfo(int index) const {
    ParameterInfo info;
    if (!plugin_)
        return info;

    auto params = plugin_->getAutomatableParameters();
    int autoCount = static_cast<int>(params.size());

    if (index >= 0 && index < autoCount) {
        auto* param = params[index];
        info.name = param->getParameterName();
        info.currentValue = param->getCurrentValue();
        auto range = param->getValueRange();
        info.minValue = range.getStart();
        info.maxValue = range.getEnd();
        info.defaultValue = param->getDefaultValue().value_or(range.getStart());
        // Timing Depth (6) and Timing Skew (7) are bipolar
        info.bipolarModulation = (index == 6 || index == 7);
    }
    return info;
}

void StepSequencerProcessor::populateParameters(DeviceInfo& info) const {
    info.parameters.clear();
    for (int i = 0; i < getParameterCount(); ++i) {
        info.parameters.push_back(getParameterInfo(i));
    }
}

void StepSequencerProcessor::setParameterByIndex(int paramIndex, float value) {
    if (!plugin_)
        return;

    auto params = plugin_->getAutomatableParameters();
    int autoCount = static_cast<int>(params.size());

    if (paramIndex >= 0 && paramIndex < autoCount) {
        params[paramIndex]->setParameter(value, juce::sendNotificationSync);
    }
}

float StepSequencerProcessor::getParameterByIndex(int paramIndex) const {
    if (!plugin_)
        return 0.0f;

    auto params = plugin_->getAutomatableParameters();
    int autoCount = static_cast<int>(params.size());

    if (paramIndex >= 0 && paramIndex < autoCount)
        return params[paramIndex]->getCurrentValue();
    return 0.0f;
}

// =============================================================================
// DrumGridProcessor
// =============================================================================

DrumGridProcessor::DrumGridProcessor(DeviceId deviceId, te::Plugin::Ptr plugin)
    : DeviceProcessor(deviceId, std::move(plugin)) {}

int DrumGridProcessor::getParameterCount() const {
    if (plugin_)
        return static_cast<int>(plugin_->getAutomatableParameters().size());
    return 0;
}

ParameterInfo DrumGridProcessor::getParameterInfo(int index) const {
    ParameterInfo info;
    if (!plugin_)
        return info;

    auto params = plugin_->getAutomatableParameters();
    if (index >= 0 && index < static_cast<int>(params.size())) {
        auto* param = params[index];
        info.name = param->getParameterName();
        info.currentValue = param->getCurrentValue();
        auto range = param->getValueRange();
        info.minValue = range.getStart();
        info.maxValue = range.getEnd();
        info.defaultValue = param->getDefaultValue().value_or(range.getStart());
        // Pan params (odd indices) are bipolar
        info.bipolarModulation = (index % 2 == 1);
    }
    return info;
}

void DrumGridProcessor::populateParameters(DeviceInfo& info) const {
    info.parameters.clear();
    for (int i = 0; i < getParameterCount(); ++i)
        info.parameters.push_back(getParameterInfo(i));
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

    // When modifiers (macros) are active, the macro owns the base value — skip propagation
    // entirely. Otherwise, internal plugin modulation (e.g. Serum LFO) gets misinterpreted
    // as a base value change and overwrites the macro-controlled value.
    if (param.hasActiveModifierAssignments())
        return;

    float valueToStore = param.getCurrentValue();

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
