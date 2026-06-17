#include "Target.hpp"

namespace magda {

namespace {

juce::String kindToJsonString(ControlTarget::Kind k) {
    switch (k) {
        case ControlTarget::Kind::PluginParam:
            return "plugin_param";
        case ControlTarget::Kind::DeviceMacro:
            return "device_macro";
        case ControlTarget::Kind::ModParam:
            return "mod_param";
        case ControlTarget::Kind::TrackVolume:
            return "track_volume";
        case ControlTarget::Kind::TrackPan:
            return "track_pan";
        case ControlTarget::Kind::SendLevel:
            return "send_level";
        case ControlTarget::Kind::Tempo:
            return "tempo";
    }
    return "plugin_param";
}

std::optional<ControlTarget::Kind> kindFromJsonString(const juce::String& s) {
    if (s == "plugin_param" || s.isEmpty())
        return ControlTarget::Kind::PluginParam;
    if (s == "device_macro")
        return ControlTarget::Kind::DeviceMacro;
    if (s == "mod_param")
        return ControlTarget::Kind::ModParam;
    if (s == "track_volume")
        return ControlTarget::Kind::TrackVolume;
    if (s == "track_pan")
        return ControlTarget::Kind::TrackPan;
    if (s == "send_level")
        return ControlTarget::Kind::SendLevel;
    if (s == "tempo")
        return ControlTarget::Kind::Tempo;
    return std::nullopt;
}

void encodePath(juce::DynamicObject* obj, const ChainNodePath& path) {
    auto* pathObj = new juce::DynamicObject();
    pathObj->setProperty("trackId", path.trackId);
    pathObj->setProperty("topLevelDeviceId", path.topLevelDeviceId);
    pathObj->setProperty("isTrackLevel", path.isTrackLevel);

    juce::Array<juce::var> stepsArray;
    for (const auto& step : path.steps) {
        auto* stepObj = new juce::DynamicObject();
        stepObj->setProperty("type", static_cast<int>(step.type));
        stepObj->setProperty("id", step.id);
        stepsArray.add(juce::var(stepObj));
    }
    pathObj->setProperty("steps", juce::var(stepsArray));
    obj->setProperty("path", juce::var(pathObj));
}

bool decodePath(juce::DynamicObject* obj, ChainNodePath& out) {
    auto pathVar = obj->getProperty("path");
    if (!pathVar.isObject())
        return false;
    auto* pathObj = pathVar.getDynamicObject();
    out.trackId = static_cast<int>(pathObj->getProperty("trackId"));
    out.topLevelDeviceId = static_cast<int>(pathObj->getProperty("topLevelDeviceId"));
    if (pathObj->hasProperty("isTrackLevel"))
        out.isTrackLevel = static_cast<bool>(pathObj->getProperty("isTrackLevel"));

    auto stepsVar = pathObj->getProperty("steps");
    if (stepsVar.isArray()) {
        for (const auto& stepVar : *stepsVar.getArray()) {
            if (!stepVar.isObject())
                continue;
            auto* stepObj = stepVar.getDynamicObject();
            ChainPathStep step;
            step.type = static_cast<ChainStepType>(static_cast<int>(stepObj->getProperty("type")));
            step.id = static_cast<int>(stepObj->getProperty("id"));
            out.steps.push_back(step);
        }
    }
    return true;
}

}  // namespace

// ============================================================================
// Debug string
// ============================================================================

juce::String toDebugString(const Target& target) {
    return std::visit(
        [](const auto& t) -> juce::String {
            using T = std::decay_t<decltype(t)>;

            if constexpr (std::is_same_v<T, ControlTarget>) {
                juce::String s = "ControlTarget{path=" + t.devicePath.toString() +
                                 ", kind=" + juce::String(toString(t.kind));
                switch (t.kind) {
                    case ControlTarget::Kind::PluginParam:
                    case ControlTarget::Kind::DeviceMacro:
                        s += ", paramIndex=" + juce::String(t.paramIndex);
                        break;
                    case ControlTarget::Kind::ModParam:
                        s += ", modId=" + juce::String(t.modId) +
                             ", modParamIndex=" + juce::String(t.modParamIndex);
                        break;
                    case ControlTarget::Kind::TrackVolume:
                    case ControlTarget::Kind::TrackPan:
                        break;
                    case ControlTarget::Kind::SendLevel:
                        s += ", sendBusIndex=" + juce::String(t.sendBusIndex);
                        break;
                    case ControlTarget::Kind::Tempo:
                        break;
                }
                s += "}";
                return s;
            } else if constexpr (std::is_same_v<T, AliasRef>) {
                juce::String s = "AliasRef{name=" + t.name;
                if (t.pluginType.isNotEmpty())
                    s += ", pluginType=" + t.pluginType;
                s += "}";
                return s;
            } else if constexpr (std::is_same_v<T, ResolverRef>) {
                juce::String s = "ResolverRef{kind=" + t.kind;
                for (int i = 0; i < t.args.size(); ++i)
                    s += ", " + t.args.getAllKeys()[i] + "=" + t.args.getAllValues()[i];
                s += "}";
                return s;
            }
        },
        target);
}

// ============================================================================
// JSON encoding
// ============================================================================

juce::String encodeTarget(const Target& target) {
    auto* obj = new juce::DynamicObject();

    std::visit(
        [&obj](const auto& t) {
            using T = std::decay_t<decltype(t)>;

            if constexpr (std::is_same_v<T, ControlTarget>) {
                obj->setProperty("kind", juce::String("static"));
                obj->setProperty("controlKind", kindToJsonString(t.kind));
                encodePath(obj, t.devicePath);

                switch (t.kind) {
                    case ControlTarget::Kind::PluginParam:
                    case ControlTarget::Kind::DeviceMacro:
                        obj->setProperty("paramIndex", t.paramIndex);
                        break;
                    case ControlTarget::Kind::ModParam:
                        obj->setProperty("modId", t.modId);
                        obj->setProperty("modParamIndex", t.modParamIndex);
                        break;
                    case ControlTarget::Kind::TrackVolume:
                    case ControlTarget::Kind::TrackPan:
                        break;
                    case ControlTarget::Kind::SendLevel:
                        obj->setProperty("sendBusIndex", t.sendBusIndex);
                        break;
                    case ControlTarget::Kind::Tempo:
                        break;
                }

            } else if constexpr (std::is_same_v<T, AliasRef>) {
                obj->setProperty("kind", juce::String("alias"));
                obj->setProperty("name", t.name);
                obj->setProperty("pluginType", t.pluginType);

            } else if constexpr (std::is_same_v<T, ResolverRef>) {
                obj->setProperty("kind", juce::String("resolver"));
                obj->setProperty("resolverKind", t.kind);

                auto* argsObj = new juce::DynamicObject();
                for (int i = 0; i < t.args.size(); ++i) {
                    argsObj->setProperty(t.args.getAllKeys()[i], t.args.getAllValues()[i]);
                }
                obj->setProperty("args", juce::var(argsObj));
            }
        },
        target);

    return juce::JSON::toString(juce::var(obj));
}

// ============================================================================
// JSON decoding
// ============================================================================

std::optional<Target> decodeTarget(const juce::String& json) {
    juce::var parsed;
    if (juce::JSON::parse(json, parsed).failed())
        return std::nullopt;

    auto* obj = parsed.getDynamicObject();
    if (obj == nullptr)
        return std::nullopt;

    auto kind = obj->getProperty("kind").toString();

    if (kind == "static") {
        ControlTarget t;

        auto controlKindStr = obj->getProperty("controlKind").toString();
        auto parsedKind = kindFromJsonString(controlKindStr);
        if (!parsedKind.has_value()) {
            DBG("decodeTarget: unknown controlKind '" + controlKindStr +
                "', defaulting to PluginParam");
            t.kind = ControlTarget::Kind::PluginParam;
        } else {
            t.kind = *parsedKind;
        }

        if (!decodePath(obj, t.devicePath))
            return std::nullopt;

        switch (t.kind) {
            case ControlTarget::Kind::PluginParam:
            case ControlTarget::Kind::DeviceMacro:
                t.paramIndex = static_cast<int>(obj->getProperty("paramIndex"));
                break;
            case ControlTarget::Kind::ModParam:
                t.modId = static_cast<ModId>(static_cast<int>(obj->getProperty("modId")));
                t.modParamIndex = static_cast<int>(obj->getProperty("modParamIndex"));
                break;
            case ControlTarget::Kind::TrackVolume:
            case ControlTarget::Kind::TrackPan:
                break;
            case ControlTarget::Kind::SendLevel:
                t.sendBusIndex = static_cast<int>(obj->getProperty("sendBusIndex"));
                break;
            case ControlTarget::Kind::Tempo:
                break;
        }

        return Target{t};

    } else if (kind == "alias") {
        AliasRef t;
        t.name = obj->getProperty("name").toString();
        t.pluginType = obj->getProperty("pluginType").toString();
        return Target{t};

    } else if (kind == "resolver") {
        ResolverRef t;
        t.kind = obj->getProperty("resolverKind").toString();

        auto argsVar = obj->getProperty("args");
        if (argsVar.isObject()) {
            if (auto* argsObj = argsVar.getDynamicObject()) {
                for (const auto& prop : argsObj->getProperties()) {
                    t.args.set(prop.name.toString(), prop.value.toString());
                }
            }
        }

        return Target{t};
    }

    return std::nullopt;
}

}  // namespace magda
