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

    // Parsing fails closed. Skipping an unreadable step would shorten the
    // address rather than reject it, and a shorter path still resolves — just
    // to a different device. Dropping a leading Segment step in particular
    // re-points a post-fx path at the main FX chain, because isPostFx() tests
    // steps.front().
    auto stepsVar = obj->getProperty("steps");
    if (stepsVar.isArray()) {
        const auto& steps = *stepsVar.getArray();
        for (int i = 0; i < steps.size(); ++i) {
            const auto& stepVar = steps.getReference(i);
            if (!stepVar.isObject())
                return false;
            auto* stepObj = stepVar.getDynamicObject();
            if (stepObj == nullptr)
                return false;

            const auto rawType = static_cast<int>(stepObj->getProperty("type"));
            if (rawType < static_cast<int>(ChainStepType::Rack) ||
                rawType > static_cast<int>(ChainStepType::Segment))
                return false;

            ChainPathStep step;
            step.type = static_cast<ChainStepType>(rawType);
            step.id = static_cast<int>(stepObj->getProperty("id"));

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
        path.isTrackLevel = static_cast<bool>(obj->getProperty("isTrackLevel"));
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
