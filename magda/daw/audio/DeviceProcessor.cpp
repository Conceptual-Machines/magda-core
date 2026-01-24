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
    setGainDb(info.gainDb);
    setBypassed(info.bypassed);
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
    // Set default level to -12dB (0.25 linear) so meters show green
    setLevel(0.25f);
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
            setFrequency(hz);
        } else {
            setFrequency(value);
        }
    } else if (paramName.equalsIgnoreCase("level") || paramName.equalsIgnoreCase("gain") ||
               paramName.equalsIgnoreCase("volume")) {
        setLevel(isNormalized ? value : juce::Decibels::decibelsToGain(value));
    } else if (paramName.equalsIgnoreCase("oscType") || paramName.equalsIgnoreCase("type") ||
               paramName.equalsIgnoreCase("waveform")) {
        setOscType(static_cast<int>(value * 3.0f));  // 0-3 range
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

void ToneGeneratorProcessor::setFrequency(float hz) {
    if (auto* tone = getTonePlugin()) {
        tone->frequency = hz;
        if (tone->frequencyParam) {
            // Normalize to 0-1 for the automatable parameter
            float normalized = std::log(hz / 20.0f) / std::log(1000.0f);
            tone->frequencyParam->setParameter(juce::jlimit(0.0f, 1.0f, normalized),
                                               juce::sendNotificationSync);
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
    // For tone generator, gain is applied via the level parameter
    // Map gainDb to level: 0dB = 1.0, -12dB = 0.25, etc.
    // We apply gainLinear_ directly as the level (0-1 range)
    setLevel(gainLinear_);
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
