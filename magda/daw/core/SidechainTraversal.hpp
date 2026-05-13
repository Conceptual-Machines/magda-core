#pragma once

#include <optional>
#include <vector>

#include "RackInfo.hpp"

namespace magda::sidechain {

inline bool typeMatches(const SidechainConfig& config,
                        std::optional<SidechainConfig::Type> requiredType) {
    return !requiredType.has_value() || config.type == *requiredType;
}

inline bool deviceUsesSource(const DeviceInfo& device, TrackId sourceTrackId,
                             std::optional<SidechainConfig::Type> requiredType = std::nullopt) {
    return sourceTrackId != INVALID_TRACK_ID && device.sidechain.sourceTrackId == sourceTrackId &&
           typeMatches(device.sidechain, requiredType);
}

inline bool rackUsesSource(const RackInfo& rack, TrackId sourceTrackId,
                           std::optional<SidechainConfig::Type> requiredType = std::nullopt) {
    return sourceTrackId != INVALID_TRACK_ID && rack.sidechain.sourceTrackId == sourceTrackId &&
           typeMatches(rack.sidechain, requiredType);
}

inline bool elementsUseSource(const std::vector<ChainElement>& elements, TrackId sourceTrackId,
                              std::optional<SidechainConfig::Type> requiredType = std::nullopt);

inline bool rackContainsSource(const RackInfo& rack, TrackId sourceTrackId,
                               std::optional<SidechainConfig::Type> requiredType = std::nullopt) {
    if (rackUsesSource(rack, sourceTrackId, requiredType))
        return true;

    for (const auto& chain : rack.chains)
        if (elementsUseSource(chain.elements, sourceTrackId, requiredType))
            return true;

    return false;
}

inline bool elementsUseSource(const std::vector<ChainElement>& elements, TrackId sourceTrackId,
                              std::optional<SidechainConfig::Type> requiredType) {
    for (const auto& element : elements) {
        if (isDevice(element)) {
            if (deviceUsesSource(getDevice(element), sourceTrackId, requiredType))
                return true;
        } else if (isRack(element)) {
            if (rackContainsSource(getRack(element), sourceTrackId, requiredType))
                return true;
        }
    }

    return false;
}

inline bool elementsContainExternalSource(const std::vector<ChainElement>& elements) {
    for (const auto& element : elements) {
        if (isDevice(element)) {
            if (getDevice(element).sidechain.sourceTrackId != INVALID_TRACK_ID)
                return true;
        } else if (isRack(element)) {
            const auto& rack = getRack(element);
            if (rack.sidechain.sourceTrackId != INVALID_TRACK_ID)
                return true;

            for (const auto& chain : rack.chains)
                if (elementsContainExternalSource(chain.elements))
                    return true;
        }
    }

    return false;
}

inline bool elementsHaveMidiTriggeredMod(const std::vector<ChainElement>& elements) {
    for (const auto& element : elements) {
        if (isDevice(element)) {
            for (const auto& mod : getDevice(element).mods)
                if (mod.triggerMode == LFOTriggerMode::MIDI)
                    return true;
        } else if (isRack(element)) {
            const auto& rack = getRack(element);
            for (const auto& mod : rack.mods)
                if (mod.triggerMode == LFOTriggerMode::MIDI)
                    return true;

            for (const auto& chain : rack.chains)
                if (elementsHaveMidiTriggeredMod(chain.elements))
                    return true;
        }
    }

    return false;
}

inline bool rackHasAudioTriggeredModForSource(const RackInfo& rack, TrackId sourceTrackId) {
    if (rackUsesSource(rack, sourceTrackId)) {
        for (const auto& mod : rack.mods)
            if (mod.triggerMode == LFOTriggerMode::Audio)
                return true;
    }

    for (const auto& chain : rack.chains) {
        for (const auto& element : chain.elements) {
            if (isDevice(element)) {
                const auto& device = getDevice(element);
                if (!deviceUsesSource(device, sourceTrackId))
                    continue;

                for (const auto& mod : device.mods)
                    if (mod.triggerMode == LFOTriggerMode::Audio)
                        return true;
            } else if (isRack(element)) {
                if (rackHasAudioTriggeredModForSource(getRack(element), sourceTrackId))
                    return true;
            }
        }
    }

    return false;
}

inline std::optional<TrackId> findFirstSource(const RackInfo& rack) {
    if (rack.sidechain.sourceTrackId != INVALID_TRACK_ID)
        return rack.sidechain.sourceTrackId;

    for (const auto& chain : rack.chains) {
        for (const auto& element : chain.elements) {
            if (isDevice(element)) {
                const auto sourceTrackId = getDevice(element).sidechain.sourceTrackId;
                if (sourceTrackId != INVALID_TRACK_ID)
                    return sourceTrackId;
            } else if (isRack(element)) {
                if (auto nestedSource = findFirstSource(getRack(element)))
                    return nestedSource;
            }
        }
    }

    return std::nullopt;
}

}  // namespace magda::sidechain
