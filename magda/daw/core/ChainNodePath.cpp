#include "ChainNodePath.hpp"

namespace magda {

namespace {

// A missing property reads back as a void var, which converts to 0. Silently
// accepting that turns an empty object into a valid address, so every numeric
// field a path depends on must be present and actually numeric.
bool readRequiredInt(const juce::DynamicObject& obj, const char* name, int& out) {
    if (!obj.hasProperty(name))
        return false;
    const auto value = obj.getProperty(name);
    if (!value.isInt() && !value.isInt64())
        return false;
    out = static_cast<int>(value);
    return true;
}

}  // namespace

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
    // Required: without it an empty object would parse as a path to track 0.
    if (!readRequiredInt(*obj, "trackId", path.trackId))
        return false;

    if (obj->hasProperty("topLevelDeviceId") &&
        !readRequiredInt(*obj, "topLevelDeviceId", path.topLevelDeviceId))
        return false;

    // Parsing fails closed. Skipping an unreadable step would shorten the
    // address rather than reject it, and a shorter path still resolves — just
    // to a different device. Dropping a leading Segment step in particular
    // re-points a post-fx path at the main FX chain, because isPostFx() tests
    // steps.front().
    if (obj->hasProperty("steps")) {
        const auto stepsVar = obj->getProperty("steps");
        if (!stepsVar.isArray())
            return false;

        const auto& steps = *stepsVar.getArray();
        for (int i = 0; i < steps.size(); ++i) {
            const auto& stepVar = steps.getReference(i);
            if (!stepVar.isObject())
                return false;
            auto* stepObj = stepVar.getDynamicObject();
            if (stepObj == nullptr)
                return false;

            int rawType = 0;
            if (!readRequiredInt(*stepObj, "type", rawType))
                return false;
            if (rawType < static_cast<int>(ChainStepType::Rack) ||
                rawType > static_cast<int>(ChainStepType::Segment))
                return false;

            ChainPathStep step;
            step.type = static_cast<ChainStepType>(rawType);
            if (!readRequiredInt(*stepObj, "id", step.id))
                return false;

            // A Segment step names one of the track's flat sections. It is only
            // ever leading, and its id must be a real ChainSegment.
            if (step.type == ChainStepType::Segment) {
                if (i != 0)
                    return false;
                if (step.id < static_cast<int>(ChainSegment::Fx) ||
                    step.id > static_cast<int>(ChainSegment::MixerAnalysis))
                    return false;
            }

            path.steps.push_back(step);
        }
    }

    if (obj->hasProperty("isTrackLevel")) {
        const auto flag = obj->getProperty("isTrackLevel");
        if (!flag.isBool())
            return false;
        path.isTrackLevel = static_cast<bool>(flag);
    } else if (path.trackId != INVALID_TRACK_ID && path.steps.empty() &&
               path.topLevelDeviceId == INVALID_DEVICE_ID) {
        // Legacy producers omitted the flag entirely. A path naming a track but
        // no steps and no device addresses nothing, and a track-level path is
        // the only thing that serializes to that shape, so recover it rather
        // than hand back an unusable address. Scoped to the absent-property
        // case: an explicit `false` is left alone.
        path.isTrackLevel = true;
    }

    out = path;
    return true;
}

}  // namespace magda
