#include "DeviceProcessor.hpp"

#include <cmath>

namespace magda {

// =============================================================================
// DeviceProcessor Base Class
// =============================================================================

DeviceProcessor::DeviceProcessor(DeviceId deviceId, te::Plugin::Ptr plugin)
    : deviceId_(deviceId), plugin_(plugin) {}

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
            DBG("  Syncing param " << i << " (" << names[i] << ") = " << param.currentValue << " "
                                   << param.unit);
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
    : DeviceProcessor(deviceId, plugin) {
    // Note: Don't set defaults here - the plugin may not be fully ready
    // Call initializeDefaults() after the processor is stored and plugin is initialized
    if (auto* tone = getTonePlugin()) {
        DBG("ToneGeneratorProcessor created: plugin freq="
            << tone->frequency.get() << " Hz, level=" << tone->level.get()
            << " frequencyParam=" << (tone->frequencyParam ? "yes" : "no"));
    } else {
        DBG("ToneGeneratorProcessor created but getTonePlugin() returned nullptr!");
    }
}

void ToneGeneratorProcessor::initializeDefaults() {
    if (initialized_)
        return;

    if (auto* tone = getTonePlugin()) {
        // Set default values using the proper setters
        setFrequency(440.0f);
        setLevel(0.25f);
        setOscType(0);  // Sine wave

        initialized_ = true;
        DBG("ToneGeneratorProcessor::initializeDefaults: freq=" << getFrequency()
                                                                << " Hz, level=" << getLevel());
    }
}

te::ToneGeneratorPlugin* ToneGeneratorProcessor::getTonePlugin() const {
    return dynamic_cast<te::ToneGeneratorPlugin*>(plugin_.get());
}

void ToneGeneratorProcessor::setParameter(const juce::String& paramName, float value) {
    if (paramName.equalsIgnoreCase("frequency") || paramName.equalsIgnoreCase("freq")) {
        // Value is actual Hz (20-20000)
        DBG("ToneGen::setParameter freq=" << value << " Hz");
        setFrequency(value);
    } else if (paramName.equalsIgnoreCase("level") || paramName.equalsIgnoreCase("gain") ||
               paramName.equalsIgnoreCase("volume")) {
        // Value is actual dB (-60 to 0)
        float level = juce::Decibels::decibelsToGain(value, -60.0f);
        DBG("ToneGen::setParameter level=" << value << " dB (linear=" << level << ")");
        setLevel(level);
    } else if (paramName.equalsIgnoreCase("oscType") || paramName.equalsIgnoreCase("type") ||
               paramName.equalsIgnoreCase("waveform")) {
        // Value is actual choice index (0 or 1)
        int type = static_cast<int>(std::round(value));
        DBG("ToneGen::setParameter oscType=" << type);
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

        // Update parameter first with NORMALIZED value (0-1)
        if (tone->frequencyParam) {
            // Frequency is logarithmic: 20-20000 Hz → 0-1
            float normalized = std::log(hz / 20.0f) / std::log(20000.0f / 20.0f);
            tone->frequencyParam->setNormalisedParameter(normalized, juce::dontSendNotification);
        }

        // CRITICAL: Overwrite CachedValue with precise value (bypasses quantization)
        tone->frequency = hz;

        DBG("ToneGen::setFrequency " << hz << " Hz, actual=" << tone->frequency.get());
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
        // Update parameter first with normalized value
        // Level is already 0-1 linear, so it's already normalized
        if (tone->levelParam) {
            tone->levelParam->setNormalisedParameter(level, juce::dontSendNotification);
        }

        // Overwrite CachedValue with precise value
        tone->level = level;

        DBG("ToneGen::setLevel " << level << " (linear), actual=" << tone->level.get());
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

        // Update parameter first with normalized value (0-1 for discrete 0-5)
        if (tone->oscTypeParam) {
            float normalized = teType / 5.0f;  // 0→0.0, 5→1.0
            tone->oscTypeParam->setNormalisedParameter(normalized, juce::dontSendNotification);
        }

        // Set CachedValue
        tone->oscType = teType;
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
    : DeviceProcessor(deviceId, plugin) {}

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

}  // namespace magda
