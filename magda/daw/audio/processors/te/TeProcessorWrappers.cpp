#include "processors/te/TeProcessorWrappers.hpp"

#include <cmath>
#include <utility>

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

te::ToneGeneratorPlugin* ToneGeneratorProcessor::getTonePlugin() const {
    return dynamic_cast<te::ToneGeneratorPlugin*>(plugin_.get());
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
    if (auto* tone = getTonePlugin()) {
        // Clamp to valid range
        hz = juce::jlimit(20.0f, 20000.0f, hz);

        // Set via AutomatableParameter - this is the proper Tracktion Engine way
        // The parameter will automatically sync to the CachedValue
        if (tone->frequencyParam) {
            tone->frequencyParam->setParameterFromHost(hz, juce::dontSendNotification);
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
            tone->levelParam->setParameterFromHost(level, juce::dontSendNotification);
        }
    }
}

float ToneGeneratorProcessor::getLevel() const {
    if (auto* tone = getTonePlugin()) {
        return tone->level;
    }
    return 0.25f;
}

void ToneGeneratorProcessor::setOscType(int teOscType) {
    if (auto* tone = getTonePlugin()) {
        // TE enum: 0=sin, 1=triangle, 2=sawUp, 3=sawDown, 4=square, 5=noise
        float teType = static_cast<float>(juce::jlimit(0, 5, teOscType));
        if (tone->oscTypeParam) {
            tone->oscTypeParam->setParameterFromHost(teType, juce::dontSendNotification);
        }
    }
}

int ToneGeneratorProcessor::getOscType() const {
    if (auto* tone = getTonePlugin()) {
        return juce::jlimit(0, 5, static_cast<int>(tone->oscType));
    }
    return 0;
}

void ToneGeneratorProcessor::setBandLimit(bool bandLimited) {
    if (auto* tone = getTonePlugin()) {
        if (tone->bandLimitParam) {
            tone->bandLimitParam->setParameterFromHost(bandLimited ? 1.0f : 0.0f,
                                                       juce::dontSendNotification);
        }
    }
}

bool ToneGeneratorProcessor::getBandLimit() const {
    if (auto* tone = getTonePlugin()) {
        return static_cast<float>(tone->bandLimit) >= 0.5f;
    }
    return false;
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
            volPan->volParam->setParameterFromHost(db, juce::sendNotificationSync);
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
            volPan->panParam->setParameterFromHost(pan, juce::sendNotificationSync);
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
        ParameterInfo eqInfo = makeInfoFromTeParam(index, params[index]);
        // TE's EQAutomatableParameter doesn't override getLabel(), so the
        // helper leaves unit empty — patch units/scale based on the parameter
        // name so the automation lane shows dB/Hz instead of falling back to %.
        juce::String lowerName = eqInfo.name.toLowerCase();
        if (lowerName.contains("gain")) {
            eqInfo.unit = magda::technicalText(magda::TechnicalTextToken::Decibels);
            eqInfo.scale = ParameterScale::Linear;
            eqInfo.displayFormat = DisplayFormat::Decibels;
            eqInfo.bipolarModulation = true;  // gain is symmetric around 0 dB
        } else if (lowerName.contains("freq")) {
            eqInfo.unit = magda::technicalText(magda::TechnicalTextToken::Hertz);
            eqInfo.scale = ParameterScale::Logarithmic;  // frequency sweeps log
            // Place 1 kHz at the slider's visual midpoint — the EQ convention.
            // ParameterUtils now applies a log-space skew so the lane's labels
            // and conversion match this anchor.
            eqInfo.scaleAnchor = 1000.0f;
        } else if (lowerName.endsWithIgnoreCase("q") || lowerName.contains(" q ") ||
                   lowerName.endsWith(" q")) {
            eqInfo.unit = "";
            eqInfo.scale = ParameterScale::Logarithmic;  // Q is perceptually log
        }
        return eqInfo;
    }

    if (index == autoCount) {
        info.name = "Phase Invert";
        info.minValue = 0.0f;
        info.maxValue = 1.0f;
        info.defaultValue = 0.0f;
        info.scale = ParameterScale::Boolean;
        info.modulatable = false;
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
        params[paramIndex]->setParameterFromHost(value, juce::sendNotificationSync);
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

    if (index >= 0 && index < autoCount)
        return makeInfoFromTeParam(index, params[index]);

    if (index == autoCount) {
        info.name = "Sidechain Trigger";
        info.minValue = 0.0f;
        info.maxValue = 1.0f;
        info.defaultValue = 0.0f;
        info.scale = ParameterScale::Boolean;
        info.modulatable = false;
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
        params[paramIndex]->setParameterFromHost(value, juce::sendNotificationSync);
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

    if (index >= 0 && index < autoCount)
        return makeInfoFromTeParam(index, params[index]);

    if (index == autoCount) {
        // Virtual parameter: delay length in ms
        info.name = "Length";
        info.unit = magda::technicalText(magda::TechnicalTextToken::Milliseconds);
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
        params[paramIndex]->setParameterFromHost(value, juce::sendNotificationSync);
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
    : AutomatablePluginProcessor(deviceId, std::move(plugin)) {}

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
            info.unit = magda::technicalText(magda::TechnicalTextToken::Milliseconds);
            info.minValue = 0.1f;
            info.maxValue = 20.0f;
            info.defaultValue = 3.0f;
            info.currentValue = chorus ? chorus->depthMs.get() : 3.0f;
            break;
        case 1:
            info.name = "Speed";
            info.unit = magda::technicalText(magda::TechnicalTextToken::Hertz);
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

    if (index >= 0 && index < autoCount)
        return makeInfoFromTeParam(index, params[index]);

    if (index == autoCount) {
        // Virtual param: mode (0 = lowpass, 1 = highpass)
        info.name = "Mode";
        info.minValue = 0.0f;
        info.maxValue = 1.0f;
        info.defaultValue = 0.0f;
        info.scale = ParameterScale::Boolean;
        info.modulatable = false;
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
        params[paramIndex]->setParameterFromHost(value, juce::sendNotificationSync);
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
    : AutomatablePluginProcessor(deviceId, std::move(plugin)) {}

// =============================================================================
// ImpulseResponseProcessor
// =============================================================================

ImpulseResponseProcessor::ImpulseResponseProcessor(DeviceId deviceId, te::Plugin::Ptr plugin)
    : AutomatablePluginProcessor(deviceId, std::move(plugin)) {}

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
