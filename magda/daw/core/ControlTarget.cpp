#include "ControlTarget.hpp"

#include <array>

namespace magda {

namespace {

constexpr std::array<ControlTarget::Kind, 7> ALL_KINDS = {
    ControlTarget::Kind::PluginParam, ControlTarget::Kind::DeviceMacro,
    ControlTarget::Kind::ModParam,    ControlTarget::Kind::TrackVolume,
    ControlTarget::Kind::TrackPan,    ControlTarget::Kind::SendLevel,
    ControlTarget::Kind::Tempo,
};

}  // namespace

std::optional<ControlTarget::Kind> parseControlTargetKind(juce::StringRef kind) {
    const juce::String name(kind);
    for (auto candidate : ALL_KINDS) {
        if (name == toString(candidate))
            return candidate;
    }
    return std::nullopt;
}

juce::var toVar(const ControlTarget& target) {
    auto* obj = new juce::DynamicObject();
    obj->setProperty("kind", juce::String(toString(target.kind)));

    // Edit-scoped targets address a global value and carry no path; emitting a
    // placeholder one would round-trip into a bogus Track[-1] path.
    if (!target.isEditScoped())
        obj->setProperty("devicePath", toVar(target.devicePath));

    obj->setProperty("paramIndex", target.paramIndex);
    obj->setProperty("modId", target.modId);
    obj->setProperty("modParamIndex", target.modParamIndex);
    obj->setProperty("sendBusIndex", target.sendBusIndex);

    return {obj};
}

bool fromVar(const juce::var& v, ControlTarget& out) {
    if (!v.isObject())
        return false;

    auto* obj = v.getDynamicObject();
    if (obj == nullptr)
        return false;

    const auto kind = parseControlTargetKind(obj->getProperty("kind").toString());
    if (!kind.has_value())
        return false;

    ControlTarget target;
    target.kind = *kind;

    if (!target.isEditScoped()) {
        if (!fromVar(obj->getProperty("devicePath"), target.devicePath))
            return false;
    }

    if (obj->hasProperty("paramIndex"))
        target.paramIndex = static_cast<int>(obj->getProperty("paramIndex"));
    if (obj->hasProperty("modId"))
        target.modId = static_cast<int>(obj->getProperty("modId"));
    if (obj->hasProperty("modParamIndex"))
        target.modParamIndex = static_cast<int>(obj->getProperty("modParamIndex"));
    if (obj->hasProperty("sendBusIndex"))
        target.sendBusIndex = static_cast<int>(obj->getProperty("sendBusIndex"));

    out = std::move(target);
    return true;
}

}  // namespace magda
