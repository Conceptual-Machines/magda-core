#pragma once

#include <juce_core/juce_core.h>

#include <vector>

#include "TypeIds.hpp"

namespace magda {

/**
 * @brief Which of a track's two FX lists a path descends into.
 *
 * Encoded as the id of a leading ChainStepType::Segment step. Fx is the
 * implicit default (paths with no Segment step address the main FX chain);
 * post-fx paths carry an explicit Segment(PostFx) step as their first step.
 */
enum class ChainSegment { Fx, PostFx, MixerAnalysis };

/**
 * @brief Type of element in a chain path step
 *
 * Segment is appended last so the persisted integer values of Rack/Chain/
 * Device (serialized in automation targets) stay stable.
 */
enum class ChainStepType { Rack, Chain, Device, Segment };

/**
 * @brief A single step in a chain node path
 */
struct ChainPathStep {
    ChainStepType type;
    int id;  // RackId, ChainId, or DeviceId depending on type

    bool operator==(const ChainPathStep& other) const {
        return type == other.type && id == other.id;
    }
};

/**
 * @brief Type of the selected chain node (derived from path)
 */
enum class ChainNodeType {
    None,            // No node selected
    Track,           // Track-level (mods/macros at track scope, can target any device)
    TopLevelDevice,  // Device directly on track (legacy, path empty + deviceId set)
    Rack,            // Rack at any depth (last step is Rack)
    Chain,           // Chain at any depth (last step is Chain)
    Device           // Device at any depth (last step is Device)
};

/**
 * @brief Unique identifier for any node in the chain hierarchy
 *
 * Supports arbitrary nesting depth. The path is a sequence of steps
 * representing the route through the hierarchy:
 *   Track → Rack → Chain → Rack → Chain → Device
 *          [step0] [step1] [step2] [step3] [step4]
 *
 * The last step determines what's selected.
 */
struct ChainNodePath {
    TrackId trackId = INVALID_TRACK_ID;
    std::vector<ChainPathStep> steps;

    // Legacy: top-level device (not in a rack/chain)
    DeviceId topLevelDeviceId = INVALID_DEVICE_ID;

    // Explicit flag for track-level paths (only set by trackLevel() factory)
    bool isTrackLevel = false;

    ChainNodeType getType() const {
        if (trackId == INVALID_TRACK_ID)
            return ChainNodeType::None;
        if (isTrackLevel)
            return ChainNodeType::Track;
        if (topLevelDeviceId != INVALID_DEVICE_ID)
            return ChainNodeType::TopLevelDevice;
        if (steps.empty())
            return ChainNodeType::None;

        switch (steps.back().type) {
            case ChainStepType::Rack:
                return ChainNodeType::Rack;
            case ChainStepType::Chain:
                return ChainNodeType::Chain;
            case ChainStepType::Device:
                return ChainNodeType::Device;
            case ChainStepType::Segment:
                return ChainNodeType::None;  // a bare segment is not a selectable node
        }
        return ChainNodeType::None;
    }

    bool isValid() const {
        return getType() != ChainNodeType::None;
    }

    // True when this path descends into the track's post-fader FX list. Post-fx
    // is flat, so such a path is always Segment(PostFx) followed by a Device step.
    bool isPostFx() const {
        return !steps.empty() && steps.front().type == ChainStepType::Segment &&
               steps.front().id == static_cast<int>(ChainSegment::PostFx);
    }

    bool isMixerAnalysis() const {
        return !steps.empty() && steps.front().type == ChainStepType::Segment &&
               steps.front().id == static_cast<int>(ChainSegment::MixerAnalysis);
    }

    bool operator==(const ChainNodePath& other) const {
        return trackId == other.trackId && steps == other.steps &&
               topLevelDeviceId == other.topLevelDeviceId && isTrackLevel == other.isTrackLevel;
    }

    bool operator!=(const ChainNodePath& other) const {
        return !(*this == other);
    }

    // Total order for use as a std::map / std::set key. The exact ordering
    // is an implementation detail (lexicographic on the tuple of fields);
    // callers should treat it as opaque.
    bool operator<(const ChainNodePath& other) const {
        if (trackId != other.trackId)
            return trackId < other.trackId;
        if (isTrackLevel != other.isTrackLevel)
            return !isTrackLevel && other.isTrackLevel;
        if (topLevelDeviceId != other.topLevelDeviceId)
            return topLevelDeviceId < other.topLevelDeviceId;
        if (steps.size() != other.steps.size())
            return steps.size() < other.steps.size();
        for (size_t i = 0; i < steps.size(); ++i) {
            if (steps[i].type != other.steps[i].type)
                return static_cast<int>(steps[i].type) < static_cast<int>(other.steps[i].type);
            if (steps[i].id != other.steps[i].id)
                return steps[i].id < other.steps[i].id;
        }
        return false;
    }

    // Get nesting depth (0 = top-level rack, 1 = chain in rack, 2 = nested rack, etc.)
    size_t depth() const {
        return steps.size();
    }

    // Get the ID of a specific step type at the given index
    // Returns INVALID_*_ID if not found or wrong type
    RackId getRackIdAt(size_t index) const {
        if (index < steps.size() && steps[index].type == ChainStepType::Rack)
            return steps[index].id;
        return INVALID_RACK_ID;
    }

    ChainId getChainIdAt(size_t index) const {
        if (index < steps.size() && steps[index].type == ChainStepType::Chain)
            return steps[index].id;
        return INVALID_CHAIN_ID;
    }

    DeviceId getDeviceId() const {
        if (topLevelDeviceId != INVALID_DEVICE_ID)
            return topLevelDeviceId;
        if (!steps.empty() && steps.back().type == ChainStepType::Device)
            return steps.back().id;
        return INVALID_DEVICE_ID;
    }

    // Convenience: get the first rack ID (for backward compatibility)
    RackId getRackId() const {
        return getRackIdAt(0);
    }

    // Convenience: get the first chain ID (for backward compatibility)
    ChainId getChainId() const {
        return getChainIdAt(1);
    }

    // Factory methods for creating paths
    static ChainNodePath trackLevel(TrackId track) {
        ChainNodePath p;
        p.trackId = track;
        p.isTrackLevel = true;
        return p;
    }

    static ChainNodePath topLevelDevice(TrackId track, DeviceId device) {
        ChainNodePath p;
        p.trackId = track;
        p.topLevelDeviceId = device;
        return p;
    }

    static ChainNodePath rack(TrackId track, RackId r) {
        ChainNodePath p;
        p.trackId = track;
        p.steps.push_back({ChainStepType::Rack, r});
        return p;
    }

    static ChainNodePath chain(TrackId track, RackId r, ChainId c) {
        ChainNodePath p;
        p.trackId = track;
        p.steps.push_back({ChainStepType::Rack, r});
        p.steps.push_back({ChainStepType::Chain, c});
        return p;
    }

    static ChainNodePath chainDevice(TrackId track, RackId r, ChainId c, DeviceId device) {
        ChainNodePath p;
        p.trackId = track;
        p.steps.push_back({ChainStepType::Rack, r});
        p.steps.push_back({ChainStepType::Chain, c});
        p.steps.push_back({ChainStepType::Device, device});
        return p;
    }

    // A device in the track's post-fader FX list. Flat by construction:
    //   Track > Segment(PostFx) > Device
    static ChainNodePath postFxDevice(TrackId track, DeviceId device) {
        ChainNodePath p;
        p.trackId = track;
        p.steps.push_back({ChainStepType::Segment, static_cast<int>(ChainSegment::PostFx)});
        p.steps.push_back({ChainStepType::Device, device});
        return p;
    }

    // A device in the rail-managed mixer-analysis section (mini scope/spec).
    // Flat by construction: Track > Segment(MixerAnalysis) > Device.
    static ChainNodePath mixerAnalysisDevice(TrackId track, DeviceId device) {
        ChainNodePath p;
        p.trackId = track;
        p.steps.push_back({ChainStepType::Segment, static_cast<int>(ChainSegment::MixerAnalysis)});
        p.steps.push_back({ChainStepType::Device, device});
        return p;
    }

    // Create a path by extending an existing path
    ChainNodePath withRack(RackId r) const {
        ChainNodePath p = *this;
        p.steps.push_back({ChainStepType::Rack, r});
        return p;
    }

    ChainNodePath withChain(ChainId c) const {
        ChainNodePath p = *this;
        p.steps.push_back({ChainStepType::Chain, c});
        return p;
    }

    ChainNodePath withDevice(DeviceId d) const {
        ChainNodePath p = *this;
        p.steps.push_back({ChainStepType::Device, d});
        return p;
    }

    // Get the parent path (without the last step)
    ChainNodePath parent() const {
        ChainNodePath p = *this;
        if (!p.steps.empty()) {
            p.steps.pop_back();
        }
        return p;
    }

    // Build a human-readable path string (for debugging/display)
    juce::String toString() const {
        juce::String result = "Track[" + juce::String(trackId) + "]";
        for (const auto& step : steps) {
            switch (step.type) {
                case ChainStepType::Rack:
                    result += " > Rack[" + juce::String(step.id) + "]";
                    break;
                case ChainStepType::Chain:
                    result += " > Chain[" + juce::String(step.id) + "]";
                    break;
                case ChainStepType::Device:
                    result += " > Device[" + juce::String(step.id) + "]";
                    break;
                case ChainStepType::Segment: {
                    auto seg = static_cast<ChainSegment>(step.id);
                    result += seg == ChainSegment::PostFx          ? " > Segment[PostFx]"
                              : seg == ChainSegment::MixerAnalysis ? " > Segment[MixerAnalysis]"
                                                                   : " > Segment[Fx]";
                    break;
                }
            }
        }
        if (topLevelDeviceId != INVALID_DEVICE_ID) {
            result += " > Device[" + juce::String(topLevelDeviceId) + "]";
        }
        return result;
    }
};

}  // namespace magda
