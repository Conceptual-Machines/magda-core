#include "ChainNodePath.hpp"

#include <limits>

namespace magda {

namespace {

// A missing property reads back as a void var, which converts to 0. Silently
// accepting that turns an empty object into a valid address, so every numeric
// field a path depends on must be present and actually numeric.
//
// The range check matters as much as the type check: narrowing an out-of-range
// int64 wraps it into a different valid id, so 4294967297 would address track 1.
bool readRequiredInt(const juce::DynamicObject& obj, const char* name, int& out) {
    if (!obj.hasProperty(name))
        return false;
    const auto value = obj.getProperty(name);
    if (!value.isInt() && !value.isInt64())
        return false;

    const auto wide = static_cast<juce::int64>(value);
    if (wide < static_cast<juce::int64>(std::numeric_limits<int>::lowest()) ||
        wide > static_cast<juce::int64>(std::numeric_limits<int>::max()))
        return false;

    out = static_cast<int>(wide);
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
                rawType > static_cast<int>(ChainStepType::PadChain))
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

            // The pad steps come as a leading pair. A pad path names its owning
            // grid by DeviceId rather than by a route to it, so PadRack is always
            // step 0 and PadChain always step 1; accepting either anywhere else
            // would readmit exactly the ambiguity the types exist to remove.
            if (step.type == ChainStepType::PadRack && i != 0)
                return false;
            if (step.type == ChainStepType::PadChain &&
                (i != 1 || path.steps.front().type != ChainStepType::PadRack))
                return false;
            if (i == 1 && path.steps.front().type == ChainStepType::PadRack &&
                step.type != ChainStepType::PadChain)
                return false;

            path.steps.push_back(step);
        }
    }

    // A PadRack with nothing after it is not an address. The pair is the whole
    // point: alone, the step names the pad rack, which nothing resolves, and
    // getType() would report the path as an ordinary Rack whose id is a
    // DeviceId, which is the ambiguity the types exist to remove. The in-loop
    // checks cannot catch it, because there is no second step to check.
    if (!path.steps.empty() && path.steps.back().type == ChainStepType::PadRack)
        return false;

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

    // Track-level, legacy top-level-device, and stepped routes are three
    // different ways to say where a path points, and getType() picks between
    // them by precedence. A path carrying more than one passes isValid() while
    // its accessors disagree — getDeviceId() prefers topLevelDeviceId, but
    // isPostFx() reads steps.front() — so the same path resolves to different
    // devices depending on which accessor the caller reaches for. No factory
    // builds that combination; only corrupt data does.
    const int representations = (path.isTrackLevel ? 1 : 0) +
                                (path.topLevelDeviceId != INVALID_DEVICE_ID ? 1 : 0) +
                                (path.steps.empty() ? 0 : 1);
    if (representations > 1)
        return false;

    out = path;
    return true;
}

}  // namespace magda
