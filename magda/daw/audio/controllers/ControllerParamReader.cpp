#include "controllers/ControllerParamReader.hpp"

#include <cmath>

#include "../../core/AutomationInfo.hpp"
#include "../../core/ParameterUtils.hpp"
#include "../../core/TrackManager.hpp"
#include "AudioBridge.hpp"

namespace magda {

namespace {

/// Linear gain back to the normalized position that produced it: the inverse of
/// `DefaultControllerParamWriter::writeTrackLevel`'s `pow(10, dB/20)`.
///
/// Silence has no dB, so it answers the bottom of the fader's range rather than
/// negative infinity. That is where a fader at zero gain sits.
float normalizedFromGain(float gain, const ParameterInfo& info) {
    const float dB = gain > 0.0f ? 20.0f * std::log10(gain) : info.minValue;
    return ParameterUtils::realToNormalized(dB, info);
}

}  // namespace

std::optional<float> DefaultControllerParamReader::read(const ResolveResult& resolved) {
    if (!resolved.ok())
        return std::nullopt;

    switch (resolved.target.kind) {
        case ControlTarget::Kind::PluginParam:
            return readPluginParam(resolved.target);
        case ControlTarget::Kind::DeviceMacro:
            return readMacro(resolved.target);
        case ControlTarget::Kind::TrackVolume:
        case ControlTarget::Kind::TrackPan:
            return readTrackLevel(resolved.target);
        case ControlTarget::Kind::SendLevel:
            return readSendLevel(resolved.target);
        case ControlTarget::Kind::ModParam:
        case ControlTarget::Kind::Tempo:
            // See the header: neither has a reading, for reasons that are about
            // the target rather than about this being unfinished.
            return std::nullopt;
    }
    return std::nullopt;
}

// Read from TrackInfo, which is what the writer wrote. The te::AutomatableParameter
// behind a track fader holds a TE fader *position* rather than the linear gain
// TrackManager keeps, so inverting through it would convert across two different
// curves and the round trip would not close.
std::optional<float> DefaultControllerParamReader::readTrackLevel(const ControlTarget& target) {
    const auto trackId = target.devicePath.trackId;
    if (trackId == INVALID_TRACK_ID)
        return std::nullopt;

    const auto* track = TrackManager::getInstance().getTrack(trackId);
    if (track == nullptr)
        return std::nullopt;

    const ParameterInfo info = getParameterInfoForTarget(target);
    if (target.kind == ControlTarget::Kind::TrackVolume)
        return normalizedFromGain(track->volume, info);
    return ParameterUtils::realToNormalized(track->pan, info);
}

std::optional<float> DefaultControllerParamReader::readSendLevel(const ControlTarget& target) {
    const auto trackId = target.devicePath.trackId;
    if (trackId == INVALID_TRACK_ID || target.sendBusIndex < 0)
        return std::nullopt;

    const auto* track = TrackManager::getInstance().getTrack(trackId);
    if (track == nullptr)
        return std::nullopt;

    for (const auto& send : track->sends)
        if (send.busIndex == target.sendBusIndex)
            return normalizedFromGain(send.level, getParameterInfoForTarget(target));

    return std::nullopt;
}

std::optional<float> DefaultControllerParamReader::readMacro(const ControlTarget& target) {
    if (!target.devicePath.isValid() || target.paramIndex < 0)
        return std::nullopt;

    // The same path-driven view the unified setter writes through, so track,
    // rack and device macros all read the one way. Const-qualified deliberately:
    // the mutable overload is private, and reading has no business with it.
    const TrackManager& trackMgr = TrackManager::getInstance();
    const auto node = trackMgr.resolveChainNode(target.devicePath);
    if (!node.valid() || target.paramIndex >= static_cast<int>(node.macros->size()))
        return std::nullopt;

    return (*node.macros)[static_cast<size_t>(target.paramIndex)].value;
}

std::optional<float> DefaultControllerParamReader::readPluginParam(const ControlTarget& target) {
    auto& trackMgr = TrackManager::getInstance();

    // The writer's own branch, taken on the same condition: a display-mapped
    // internal parameter never reached TE's value range on the way in, so it
    // must not be read back through it either.
    if (const auto* device = trackMgr.getDeviceInChainByPath(target.devicePath)) {
        if (const auto* info = device->findParameterByIndex(target.paramIndex);
            info != nullptr && device->format == PluginFormat::Internal &&
            ParameterUtils::isDisplayMappedInternalValue(*info)) {
            return ParameterUtils::modelToNormalizedValue(ParameterModelValue{info->currentValue},
                                                          *info)
                .value;
        }
    }

    auto* param = bridge_.resolveControlTarget(target);
    if (param == nullptr)
        return std::nullopt;

    const auto range = param->getValueRange();
    const float span = static_cast<float>(range.getLength());
    if (span <= 0.0f)
        return std::nullopt;

    // The base value rather than the current one. A parameter with an LFO or a
    // macro on it has a value that moves every block, and echoing that would
    // both flood the surface and put its fader somewhere the user cannot have
    // put it. What a control surface owns is the value underneath the
    // modulation, which is what it wrote.
    const auto base = static_cast<float>(param->getCurrentBaseValue());
    return juce::jlimit(0.0f, 1.0f, (base - static_cast<float>(range.getStart())) / span);
}

}  // namespace magda
