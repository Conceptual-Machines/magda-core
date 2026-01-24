#include "DeviceProcessor.hpp"

#include <cmath>

namespace magda {

// =============================================================================
// DeviceProcessor Base Class
// =============================================================================

DeviceProcessor::DeviceProcessor(DeviceId deviceId, te::Plugin::Ptr plugin)
    : deviceId_(deviceId), plugin_(plugin) {}

void DeviceProcessor::setParameter(const juce::String& /*paramName*/, float /*value*/,
                                   bool /*isNormalized*/) {
    // Base implementation does nothing - override in subclasses
}

float DeviceProcessor::getParameter(const juce::String& /*paramName*/, bool /*normalized*/) const {
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

    // Sync parameter values
    auto names = getParameterNames();
    for (size_t i = 0; i < info.parameters.size(); ++i) {
        const auto& param = info.parameters[i];
        // Use setParameter with normalized=true since ParameterInfo stores normalized values
        if (i < names.size()) {
            DBG("  Syncing param " << i << " (" << names[i] << ") = " << param.currentValue);
            setParameter(names[i], param.currentValue, true);
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
        // Set default values - the plugin should be fully ready now
        // IMPORTANT: setParameter() expects ACTUAL values (Hz, not normalized!)
        // Use setNormalisedParameter() for 0-1 values
        if (tone->frequencyParam) {
            // setParameter expects actual Hz value
            tone->frequencyParam->setParameter(440.0f, juce::sendNotificationSync);
            DBG("initializeDefaults: set frequencyParam to 440 Hz");
        } else {
            tone->frequency = 440.0f;
            DBG("initializeDefaults: set frequency CachedValue to 440 Hz");
        }

        if (tone->levelParam) {
            // Level is 0-1, so setParameter and setNormalisedParameter are equivalent
            tone->levelParam->setParameter(0.25f, juce::sendNotificationSync);
        } else {
            tone->level = 0.25f;
        }

        if (tone->oscTypeParam) {
            // OscType is discrete 0-5
            tone->oscTypeParam->setParameter(0.0f, juce::sendNotificationSync);
        } else {
            tone->oscType = 0.0f;
        }

        initialized_ = true;
        DBG("ToneGeneratorProcessor::initializeDefaults: freq=" << getFrequency()
                                                                << " Hz, level=" << getLevel());
    }
}

te::ToneGeneratorPlugin* ToneGeneratorProcessor::getTonePlugin() const {
    return dynamic_cast<te::ToneGeneratorPlugin*>(plugin_.get());
}

void ToneGeneratorProcessor::setParameter(const juce::String& paramName, float value,
                                          bool isNormalized) {
    if (paramName.equalsIgnoreCase("frequency") || paramName.equalsIgnoreCase("freq")) {
        if (isNormalized) {
            // Map 0-1 to 20-20000 Hz (logarithmic)
            float hz = 20.0f * std::pow(1000.0f, value);
            DBG("ToneGen::setParameter freq normalized=" << value << " -> " << hz << " Hz");
            setFrequency(hz);
        } else {
            DBG("ToneGen::setParameter freq raw=" << value << " Hz");
            setFrequency(value);
        }
    } else if (paramName.equalsIgnoreCase("level") || paramName.equalsIgnoreCase("gain") ||
               paramName.equalsIgnoreCase("volume")) {
        float level;
        if (isNormalized) {
            // Normalized value is position in -60 to 0 dB range, convert to linear
            float db = -60.0f + value * 60.0f;
            level = juce::Decibels::decibelsToGain(db, -60.0f);
        } else {
            level = juce::Decibels::decibelsToGain(value);
        }
        DBG("ToneGen::setParameter level=" << level << " (normalized=" << value << ")");
        setLevel(level);
    } else if (paramName.equalsIgnoreCase("oscType") || paramName.equalsIgnoreCase("type") ||
               paramName.equalsIgnoreCase("waveform")) {
        int type = static_cast<int>(value * 3.0f);
        DBG("ToneGen::setParameter oscType=" << type);
        setOscType(type);  // 0-3 range
    }
}

float ToneGeneratorProcessor::getParameter(const juce::String& paramName, bool normalized) const {
    if (paramName.equalsIgnoreCase("frequency") || paramName.equalsIgnoreCase("freq")) {
        float hz = getFrequency();
        if (normalized) {
            // Map 20-20000 Hz to 0-1 (logarithmic)
            return std::log(hz / 20.0f) / std::log(1000.0f);
        }
        return hz;
    } else if (paramName.equalsIgnoreCase("level") || paramName.equalsIgnoreCase("gain") ||
               paramName.equalsIgnoreCase("volume")) {
        float level = getLevel();
        return normalized ? level : juce::Decibels::gainToDecibels(level);
    } else if (paramName.equalsIgnoreCase("oscType") || paramName.equalsIgnoreCase("type") ||
               paramName.equalsIgnoreCase("waveform")) {
        return static_cast<float>(getOscType()) / 3.0f;
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
            // Store normalized value (0-1) for UI slider
            // Clamp frequency to valid range to prevent negative normalized values
            float hz = std::max(20.0f, std::min(20000.0f, getFrequency()));
            info.currentValue = juce::jlimit(
                0.0f, 1.0f, static_cast<float>(std::log(hz / 20.0f) / std::log(1000.0f)));
            break;
        }

        case 1: {  // Level - display as dB
            info.name = "Level";
            info.unit = "dB";
            info.minValue = -60.0f;
            info.maxValue = 0.0f;
            info.defaultValue = -12.0f;  // 0.25 linear ≈ -12 dB
            info.scale = ParameterScale::Linear;
            // Convert linear level to normalized position in dB range
            float level = getLevel();
            float db = level > 0.0f ? juce::Decibels::gainToDecibels(level, -60.0f) : -60.0f;
            info.currentValue = juce::jlimit(0.0f, 1.0f, (db + 60.0f) / 60.0f);
            break;
        }

        case 2:  // Oscillator Type
            info.name = "Waveform";
            info.unit = "";
            info.minValue = 0.0f;
            info.maxValue = 3.0f;
            info.defaultValue = 0.0f;
            info.currentValue = static_cast<float>(getOscType()) / 3.0f;  // Normalize to 0-1
            info.scale = ParameterScale::Discrete;
            info.choices = {"Sine", "Square", "Saw", "Noise"};
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

        // Set via automatable parameter if available (preferred - proper sync)
        // IMPORTANT: setParameter expects actual Hz value, not normalized!
        if (tone->frequencyParam) {
            tone->frequencyParam->setParameter(hz, juce::sendNotificationSync);
            DBG("ToneGen::setFrequency " << hz
                                         << " Hz via param, actual=" << tone->frequency.get());
        } else {
            // Fallback to CachedValue directly
            tone->frequency = hz;
            DBG("ToneGen::setFrequency " << hz << " Hz direct, actual=" << tone->frequency.get());
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
        tone->level = level;
        if (tone->levelParam) {
            tone->levelParam->setParameter(level, juce::sendNotificationSync);
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
        tone->oscType = static_cast<float>(type);
        if (tone->oscTypeParam) {
            tone->oscTypeParam->setParameter(static_cast<float>(type) / 3.0f,
                                             juce::sendNotificationSync);
        }
    }
}

int ToneGeneratorProcessor::getOscType() const {
    if (auto* tone = getTonePlugin()) {
        return static_cast<int>(tone->oscType);
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

void VolumeProcessor::setParameter(const juce::String& paramName, float value, bool isNormalized) {
    if (paramName.equalsIgnoreCase("volume") || paramName.equalsIgnoreCase("gain") ||
        paramName.equalsIgnoreCase("level")) {
        if (isNormalized) {
            // Map 0-1 to -inf to +6dB
            float db = value > 0.0f ? juce::Decibels::gainToDecibels(value * 2.0f) : -100.0f;
            setVolume(db);
        } else {
            setVolume(value);  // Already in dB
        }
    } else if (paramName.equalsIgnoreCase("pan")) {
        setPan(isNormalized ? (value * 2.0f - 1.0f) : value);  // 0-1 to -1 to 1
    }
}

float VolumeProcessor::getParameter(const juce::String& paramName, bool normalized) const {
    if (paramName.equalsIgnoreCase("volume") || paramName.equalsIgnoreCase("gain") ||
        paramName.equalsIgnoreCase("level")) {
        float db = getVolume();
        if (normalized) {
            return juce::Decibels::decibelsToGain(db) / 2.0f;
        }
        return db;
    } else if (paramName.equalsIgnoreCase("pan")) {
        float pan = getPan();
        return normalized ? (pan + 1.0f) / 2.0f : pan;
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
