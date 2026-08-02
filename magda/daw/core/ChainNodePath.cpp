#include "ChainNodePath.hpp"

namespace magda {

juce::var toVar(const ChainNodePath& path) {
    auto* obj = new juce::DynamicObject();
    obj->setProperty("trackId", path.trackId);
    obj->setProperty("topLevelDeviceId", path.topLevelDeviceId);
    obj->setProperty("isTrackLevel", path.isTrackLevel);

    juce::Array<juce::var> steps;
    for (const auto& step : path.steps) {
        auto* stepObj = new juce::DynamicObject();
        stepObj->setProperty("type", static_cast<int>(step.type));
        stepObj->setProperty("id", step.id);
        steps.add(juce::var(stepObj));
    }
    obj->setProperty("steps", juce::var(steps));

    return juce::var(obj);
}

bool fromVar(const juce::var& v, ChainNodePath& out) {
    if (!v.isObject())
        return false;

    auto* obj = v.getDynamicObject();
    if (obj == nullptr)
        return false;

    ChainNodePath path;
    path.trackId = static_cast<int>(obj->getProperty("trackId"));

    if (obj->hasProperty("topLevelDeviceId"))
        path.topLevelDeviceId = static_cast<int>(obj->getProperty("topLevelDeviceId"));
    if (obj->hasProperty("isTrackLevel"))
        path.isTrackLevel = static_cast<bool>(obj->getProperty("isTrackLevel"));

    auto stepsVar = obj->getProperty("steps");
    if (stepsVar.isArray()) {
        for (const auto& stepVar : *stepsVar.getArray()) {
            if (!stepVar.isObject())
                continue;
            auto* stepObj = stepVar.getDynamicObject();
            if (stepObj == nullptr)
                continue;

            const auto rawType = static_cast<int>(stepObj->getProperty("type"));
            if (rawType < static_cast<int>(ChainStepType::Rack) ||
                rawType > static_cast<int>(ChainStepType::Segment))
                continue;

            ChainPathStep step;
            step.type = static_cast<ChainStepType>(rawType);
            step.id = static_cast<int>(stepObj->getProperty("id"));
            path.steps.push_back(step);
        }
    }

    out = path;
    return true;
}

}  // namespace magda
