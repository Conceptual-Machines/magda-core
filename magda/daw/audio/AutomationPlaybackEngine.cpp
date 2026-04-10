#include "AutomationPlaybackEngine.hpp"

#include "../core/AutomationManager.hpp"
#include "../core/ParameterInfo.hpp"
#include "../core/ParameterUtils.hpp"
#include "AudioBridge.hpp"

namespace magda {

AutomationPlaybackEngine::AutomationPlaybackEngine(AudioBridge& bridge, te::Edit& edit)
    : bridge_(bridge), edit_(edit) {}

void AutomationPlaybackEngine::process() {
    bool playing = edit_.getTransport().isPlaying();

    // Detect stop transition — nothing special needed (hold last value)
    wasPlaying_ = playing;

    if (!playing)
        return;

    double position = edit_.getTransport().getPosition().inSeconds();

    auto& autoMgr = AutomationManager::getInstance();

    // Set feedback guard so AutomationManager::trackPropertyChanged() ignores
    // changes that originate from automation playback
    autoMgr.setPlaybackActive(true);

    for (const auto& lane : autoMgr.getLanes()) {
        if (!lane.hasData())
            continue;

        applyLane(lane, position);
    }

    autoMgr.setPlaybackActive(false);
}

void AutomationPlaybackEngine::applyLane(const AutomationLaneInfo& lane, double timeInSeconds) {
    auto& autoMgr = AutomationManager::getInstance();
    double normalizedValue = autoMgr.getValueAtTime(lane.id, timeInSeconds);

    switch (lane.target.type) {
        case AutomationTargetType::TrackVolume:
            applyTrackVolume(lane.target.trackId, normalizedValue);
            break;
        case AutomationTargetType::TrackPan:
            applyTrackPan(lane.target.trackId, normalizedValue);
            break;
        case AutomationTargetType::DeviceParameter:
            applyDeviceParameter(lane.target, normalizedValue);
            break;
        case AutomationTargetType::Macro:
            applyMacro(lane.target, normalizedValue);
            break;
        case AutomationTargetType::ModParameter:
            // Not yet implemented
            break;
    }
}

void AutomationPlaybackEngine::applyTrackVolume(TrackId trackId, double normalizedValue) {
    // Convert normalized 0-1 → dB via fader curve → linear gain
    auto paramInfo = ParameterPresets::faderVolume(-1, "Volume");
    float dB = ParameterUtils::normalizedToReal(static_cast<float>(normalizedValue), paramInfo);
    float gain = juce::Decibels::decibelsToGain(dB);
    bridge_.setTrackVolume(trackId, gain);
}

void AutomationPlaybackEngine::applyTrackPan(TrackId trackId, double normalizedValue) {
    // Pan ParameterInfo: linear -1.0 to +1.0, so normalizedToReal gives the TE value directly
    auto paramInfo = ParameterPresets::pan(-1, "Pan");
    float pan = ParameterUtils::normalizedToReal(static_cast<float>(normalizedValue), paramInfo);
    bridge_.setTrackPan(trackId, pan);
}

void AutomationPlaybackEngine::applyDeviceParameter(const AutomationTarget& target,
                                                    double normalizedValue) {
    DeviceId deviceId = target.devicePath.getDeviceId();
    if (deviceId == INVALID_DEVICE_ID)
        return;

    // Plugin parameters are already 0-1 normalized in TE
    bridge_.pushParameterChange(deviceId, target.paramIndex, static_cast<float>(normalizedValue));
}

void AutomationPlaybackEngine::applyMacro(const AutomationTarget& target, double normalizedValue) {
    // Determine if this is a rack macro based on the device path
    RackId rackId = target.devicePath.getRackId();
    bool isRack = (rackId != INVALID_RACK_ID);
    int id = isRack ? rackId : target.trackId;

    bridge_.macroValueChanged(target.trackId, isRack, id, target.macroIndex,
                              static_cast<float>(normalizedValue));
}

}  // namespace magda
