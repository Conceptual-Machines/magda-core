#include "param/ParamKey.hpp"

#include <tuple>

namespace magda::engine {

namespace {

/// The section a path descends into. Post-fx and mixer-analysis paths say so in
/// their first step; everything else is the main FX chain.
ChainSegment segmentOf(const magda::ChainNodePath& path) {
    if (path.isPostFx())
        return ChainSegment::PostFx;
    if (path.isMixerAnalysis())
        return ChainSegment::MixerAnalysis;
    return ChainSegment::Fx;
}

/// The rack a path ends in or passes through last, which is what owns a macro
/// at rack scope.
RackId lastRackOf(const magda::ChainNodePath& path) {
    for (auto step = path.steps.rbegin(); step != path.steps.rend(); ++step)
        if (step->type == magda::ChainStepType::Rack)
            return step->id;
    return INVALID_RACK_ID;
}

/// The scope a path names, filled into @p key. False when the path names
/// something that owns neither macros nor modifiers, which a chain does not:
/// macros and mods live on tracks, racks and devices, and a chain is the thing
/// between two of them.
bool fillScope(const magda::ChainNodePath& path, ParamKey& key) {
    key.trackId = path.trackId;

    switch (path.getType()) {
        case magda::ChainNodeType::Track:
            key.scope = ParamKey::Scope::Track;
            return true;

        case magda::ChainNodeType::Rack:
            key.scope = ParamKey::Scope::Rack;
            key.rackId = lastRackOf(path);
            return key.rackId != INVALID_RACK_ID;

        case magda::ChainNodeType::TopLevelDevice:
        case magda::ChainNodeType::Device:
            key.scope = ParamKey::Scope::Device;
            key.rackId = lastRackOf(path);
            key.device = DeviceKey{segmentOf(path), path.getDeviceId()};
            return key.device.deviceId != INVALID_DEVICE_ID;

        case magda::ChainNodeType::Chain:
        case magda::ChainNodeType::None:
            return false;
    }
    return false;
}

}  // namespace

bool ParamKey::operator==(const ParamKey& o) const {
    return kind == o.kind && scope == o.scope && trackId == o.trackId && rackId == o.rackId &&
           device == o.device && modId == o.modId && index == o.index;
}

bool ParamKey::operator<(const ParamKey& o) const {
    return std::tie(trackId, rackId, device, kind, scope, modId, index) <
           std::tie(o.trackId, o.rackId, o.device, o.kind, o.scope, o.modId, o.index);
}

std::size_t ParamKeyHash::operator()(const ParamKey& key) const noexcept {
    auto mix = [](std::size_t seed, std::size_t value) {
        return seed ^ (value + static_cast<std::size_t>(0x9e3779b9U) + (seed << 6U) + (seed >> 2U));
    };

    std::size_t seed = DeviceKeyHash{}(key.device);
    seed = mix(seed, static_cast<std::size_t>(key.kind));
    seed = mix(seed, static_cast<std::size_t>(key.scope));
    seed = mix(seed, static_cast<std::size_t>(key.trackId));
    seed = mix(seed, static_cast<std::size_t>(key.rackId));
    seed = mix(seed, static_cast<std::size_t>(key.modId));
    seed = mix(seed, static_cast<std::size_t>(key.index));
    return seed;
}

std::optional<ParamKey> paramKeyFor(const magda::ControlTarget& target) {
    ParamKey key;

    switch (target.kind) {
        case magda::ControlTarget::Kind::PluginParam:
            key.kind = ParamKey::Kind::DeviceParam;
            if (!fillScope(target.devicePath, key) || key.scope != ParamKey::Scope::Device)
                return std::nullopt;
            if (target.paramIndex < 0)
                return std::nullopt;
            key.index = target.paramIndex;
            return key;

        case magda::ControlTarget::Kind::DeviceMacro:
            key.kind = ParamKey::Kind::Macro;
            if (!fillScope(target.devicePath, key))
                return std::nullopt;
            if (target.paramIndex < 0)
                return std::nullopt;
            key.index = target.paramIndex;
            return key;

        case magda::ControlTarget::Kind::ModParam:
            key.kind = ParamKey::Kind::ModParam;
            if (!fillScope(target.devicePath, key))
                return std::nullopt;
            if (target.modId == INVALID_MOD_ID || target.modParamIndex < 0)
                return std::nullopt;
            key.modId = target.modId;
            key.index = target.modParamIndex;
            return key;

        case magda::ControlTarget::Kind::TrackVolume:
        case magda::ControlTarget::Kind::TrackPan:
        case magda::ControlTarget::Kind::SendLevel: {
            // Track-scoped and nothing else: a mixer value belongs to a track,
            // and the path a target carries for one is the track's own.
            if (target.devicePath.trackId == INVALID_TRACK_ID)
                return std::nullopt;

            key.scope = ParamKey::Scope::Track;
            key.trackId = target.devicePath.trackId;

            if (target.kind == magda::ControlTarget::Kind::TrackVolume) {
                key.kind = ParamKey::Kind::TrackVolume;
                return key;
            }
            if (target.kind == magda::ControlTarget::Kind::TrackPan) {
                key.kind = ParamKey::Kind::TrackPan;
                return key;
            }

            if (target.sendBusIndex < 0)
                return std::nullopt;
            key.kind = ParamKey::Kind::SendLevel;
            key.index = target.sendBusIndex;
            return key;
        }

        // The tempo is a real target that belongs to the transport rather than
        // to any track, and nothing here can carry one.
        case magda::ControlTarget::Kind::Tempo:
            return std::nullopt;
    }

    return std::nullopt;
}

std::optional<ParamKey> modifierKeyFor(const magda::ControlTarget& target) {
    if (target.kind != magda::ControlTarget::Kind::ModParam)
        return std::nullopt;

    auto key = paramKeyFor(target);
    if (!key.has_value())
        return std::nullopt;

    key->index = -1;
    return key;
}

std::string toString(const ParamKey& key) {
    std::string text = "T" + std::to_string(key.trackId);

    switch (key.scope) {
        case ParamKey::Scope::Track:
            break;
        case ParamKey::Scope::Rack:
            text += "/R" + std::to_string(key.rackId);
            break;
        case ParamKey::Scope::Device:
            // The rack it is in, where it is in one. A device id is unique
            // inside its section, so the rack is not what tells two devices
            // apart; it is what tells two keys apart. A link whose path
            // remembers a rack the device has since left resolves to a key
            // that differs from the live one only here, and without it the
            // report of that miss would name the parameter the project has.
            if (key.rackId != INVALID_RACK_ID)
                text += "/R" + std::to_string(key.rackId);
            text += "/" + toString(key.device);
            break;
    }

    switch (key.kind) {
        case ParamKey::Kind::DeviceParam:
            text += ":param" + std::to_string(key.index);
            break;
        case ParamKey::Kind::Macro:
            text += ":macro" + std::to_string(key.index);
            break;
        case ParamKey::Kind::ModParam:
            text += ":mod" + std::to_string(key.modId);
            if (key.index >= 0)
                text += ".param" + std::to_string(key.index);
            break;
        case ParamKey::Kind::TrackVolume:
            text += ":volume";
            break;
        case ParamKey::Kind::TrackPan:
            text += ":pan";
            break;
        case ParamKey::Kind::SendLevel:
            text += ":send" + std::to_string(key.index);
            break;
    }

    return text;
}

}  // namespace magda::engine
