#include "Target.hpp"

namespace magda {

// ============================================================================
// Debug string
// ============================================================================

juce::String toDebugString(const Target& target) {
    return std::visit(
        [](const auto& t) -> juce::String {
            using T = std::decay_t<decltype(t)>;

            if constexpr (std::is_same_v<T, StaticTarget>) {
                return "StaticTarget{path=" + t.devicePath.toString() +
                       ", paramIndex=" + juce::String(t.paramIndex) + "}";
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

            if constexpr (std::is_same_v<T, StaticTarget>) {
                obj->setProperty("kind", juce::String("static"));
                obj->setProperty("paramIndex", t.paramIndex);

                // Encode ChainNodePath inline
                auto* pathObj = new juce::DynamicObject();
                pathObj->setProperty("trackId", t.devicePath.trackId);
                pathObj->setProperty("topLevelDeviceId", t.devicePath.topLevelDeviceId);
                pathObj->setProperty("isTrackLevel", t.devicePath.isTrackLevel);

                juce::Array<juce::var> stepsArray;
                for (const auto& step : t.devicePath.steps) {
                    auto* stepObj = new juce::DynamicObject();
                    stepObj->setProperty("type", static_cast<int>(step.type));
                    stepObj->setProperty("id", step.id);
                    stepsArray.add(juce::var(stepObj));
                }
                pathObj->setProperty("steps", juce::var(stepsArray));
                obj->setProperty("path", juce::var(pathObj));

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
        StaticTarget t;
        t.paramIndex = static_cast<int>(obj->getProperty("paramIndex"));

        auto pathVar = obj->getProperty("path");
        if (!pathVar.isObject())
            return std::nullopt;

        auto* pathObj = pathVar.getDynamicObject();
        t.devicePath.trackId = static_cast<int>(pathObj->getProperty("trackId"));
        t.devicePath.topLevelDeviceId = static_cast<int>(pathObj->getProperty("topLevelDeviceId"));
        if (pathObj->hasProperty("isTrackLevel"))
            t.devicePath.isTrackLevel = static_cast<bool>(pathObj->getProperty("isTrackLevel"));

        auto stepsVar = pathObj->getProperty("steps");
        if (stepsVar.isArray()) {
            for (const auto& stepVar : *stepsVar.getArray()) {
                if (!stepVar.isObject())
                    continue;
                auto* stepObj = stepVar.getDynamicObject();
                ChainPathStep step;
                step.type =
                    static_cast<ChainStepType>(static_cast<int>(stepObj->getProperty("type")));
                step.id = static_cast<int>(stepObj->getProperty("id"));
                t.devicePath.steps.push_back(step);
            }
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
