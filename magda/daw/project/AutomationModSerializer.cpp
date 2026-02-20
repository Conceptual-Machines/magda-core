#include "ProjectSerializer.hpp"
#include "SerializationHelpers.hpp"

namespace magda {

// ============================================================================
// Automation serialization helpers
// ============================================================================

juce::var ProjectSerializer::serializeAutomationLaneInfo(const AutomationLaneInfo& lane) {
    auto* obj = new juce::DynamicObject();

    obj->setProperty("id", lane.id);
    obj->setProperty("target", serializeAutomationTarget(lane.target));
    obj->setProperty("type", static_cast<int>(lane.type));
    obj->setProperty("name", lane.name);
    obj->setProperty("visible", lane.visible);
    obj->setProperty("expanded", lane.expanded);
    obj->setProperty("armed", lane.armed);
    obj->setProperty("height", lane.height);

    // Absolute points
    juce::Array<juce::var> pointsArray;
    for (const auto& point : lane.absolutePoints) {
        pointsArray.add(serializeAutomationPoint(point));
    }
    obj->setProperty("absolutePoints", juce::var(pointsArray));

    // Clip IDs
    juce::Array<juce::var> clipIdsArray;
    for (auto clipId : lane.clipIds) {
        clipIdsArray.add(clipId);
    }
    obj->setProperty("clipIds", juce::var(clipIdsArray));

    return juce::var(obj);
}

bool ProjectSerializer::deserializeAutomationLaneInfo(const juce::var& json,
                                                      AutomationLaneInfo& outLane) {
    if (!json.isObject()) {
        lastError_ = "Automation lane is not an object";
        return false;
    }

    auto* obj = json.getDynamicObject();

    outLane.id = obj->getProperty("id");
    if (!deserializeAutomationTarget(obj->getProperty("target"), outLane.target)) {
        return false;
    }
    outLane.type = static_cast<AutomationLaneType>(static_cast<int>(obj->getProperty("type")));
    outLane.name = obj->getProperty("name").toString();
    outLane.visible = obj->getProperty("visible");
    outLane.expanded = obj->getProperty("expanded");
    outLane.armed = obj->getProperty("armed");
    outLane.height = obj->getProperty("height");

    // Absolute points
    auto pointsVar = obj->getProperty("absolutePoints");
    if (pointsVar.isArray()) {
        auto* arr = pointsVar.getArray();
        for (const auto& pointVar : *arr) {
            AutomationPoint point;
            if (!deserializeAutomationPoint(pointVar, point)) {
                return false;
            }
            outLane.absolutePoints.push_back(point);
        }
    }

    // Clip IDs
    auto clipIdsVar = obj->getProperty("clipIds");
    if (clipIdsVar.isArray()) {
        auto* arr = clipIdsVar.getArray();
        for (const auto& idVar : *arr) {
            outLane.clipIds.push_back(static_cast<int>(idVar));
        }
    }

    return true;
}

juce::var ProjectSerializer::serializeAutomationClipInfo(const AutomationClipInfo& clip) {
    auto* obj = new juce::DynamicObject();

    obj->setProperty("id", clip.id);
    obj->setProperty("laneId", clip.laneId);
    obj->setProperty("name", clip.name);
    obj->setProperty("colour", colourToString(clip.colour));
    obj->setProperty("startTime", clip.startTime);
    obj->setProperty("length", clip.length);
    obj->setProperty("looping", clip.looping);
    obj->setProperty("loopLength", clip.loopLength);

    // Points
    juce::Array<juce::var> pointsArray;
    for (const auto& point : clip.points) {
        pointsArray.add(serializeAutomationPoint(point));
    }
    obj->setProperty("points", juce::var(pointsArray));

    return juce::var(obj);
}

bool ProjectSerializer::deserializeAutomationClipInfo(const juce::var& json,
                                                      AutomationClipInfo& outClip) {
    if (!json.isObject()) {
        lastError_ = "Automation clip is not an object";
        return false;
    }

    auto* obj = json.getDynamicObject();

    outClip.id = obj->getProperty("id");
    outClip.laneId = obj->getProperty("laneId");
    outClip.name = obj->getProperty("name").toString();
    outClip.colour = stringToColour(obj->getProperty("colour").toString());
    outClip.startTime = obj->getProperty("startTime");
    outClip.length = obj->getProperty("length");
    outClip.looping = obj->getProperty("looping");
    outClip.loopLength = obj->getProperty("loopLength");

    // Points
    auto pointsVar = obj->getProperty("points");
    if (pointsVar.isArray()) {
        auto* arr = pointsVar.getArray();
        for (const auto& pointVar : *arr) {
            AutomationPoint point;
            if (!deserializeAutomationPoint(pointVar, point)) {
                return false;
            }
            outClip.points.push_back(point);
        }
    }

    return true;
}

juce::var ProjectSerializer::serializeAutomationPoint(const AutomationPoint& point) {
    auto* obj = new juce::DynamicObject();

    obj->setProperty("id", point.id);
    obj->setProperty("time", point.time);
    obj->setProperty("value", point.value);
    obj->setProperty("curveType", static_cast<int>(point.curveType));
    obj->setProperty("tension", point.tension);
    obj->setProperty("inHandle", serializeBezierHandle(point.inHandle));
    obj->setProperty("outHandle", serializeBezierHandle(point.outHandle));

    return juce::var(obj);
}

bool ProjectSerializer::deserializeAutomationPoint(const juce::var& json,
                                                   AutomationPoint& outPoint) {
    if (!json.isObject()) {
        lastError_ = "Automation point is not an object";
        return false;
    }

    auto* obj = json.getDynamicObject();

    outPoint.id = obj->getProperty("id");
    outPoint.time = obj->getProperty("time");
    outPoint.value = obj->getProperty("value");
    outPoint.curveType =
        static_cast<AutomationCurveType>(static_cast<int>(obj->getProperty("curveType")));
    outPoint.tension = obj->getProperty("tension");

    if (!deserializeBezierHandle(obj->getProperty("inHandle"), outPoint.inHandle)) {
        return false;
    }
    if (!deserializeBezierHandle(obj->getProperty("outHandle"), outPoint.outHandle)) {
        return false;
    }

    return true;
}

juce::var ProjectSerializer::serializeAutomationTarget(const AutomationTarget& target) {
    auto* obj = new juce::DynamicObject();

    obj->setProperty("type", static_cast<int>(target.type));
    obj->setProperty("trackId", target.trackId);
    obj->setProperty("devicePath", serializeChainNodePath(target.devicePath));
    obj->setProperty("paramIndex", target.paramIndex);
    obj->setProperty("macroIndex", target.macroIndex);
    obj->setProperty("modId", target.modId);
    obj->setProperty("modParamIndex", target.modParamIndex);

    return juce::var(obj);
}

bool ProjectSerializer::deserializeAutomationTarget(const juce::var& json,
                                                    AutomationTarget& outTarget) {
    if (!json.isObject()) {
        lastError_ = "Automation target is not an object";
        return false;
    }

    auto* obj = json.getDynamicObject();

    outTarget.type = static_cast<AutomationTargetType>(static_cast<int>(obj->getProperty("type")));
    outTarget.trackId = obj->getProperty("trackId");
    if (!deserializeChainNodePath(obj->getProperty("devicePath"), outTarget.devicePath)) {
        return false;
    }
    outTarget.paramIndex = obj->getProperty("paramIndex");
    outTarget.macroIndex = obj->getProperty("macroIndex");
    outTarget.modId = obj->getProperty("modId");
    outTarget.modParamIndex = obj->getProperty("modParamIndex");

    return true;
}

juce::var ProjectSerializer::serializeBezierHandle(const BezierHandle& handle) {
    auto* obj = new juce::DynamicObject();

    obj->setProperty("time", handle.time);
    obj->setProperty("value", handle.value);
    obj->setProperty("linked", handle.linked);

    return juce::var(obj);
}

bool ProjectSerializer::deserializeBezierHandle(const juce::var& json, BezierHandle& outHandle) {
    if (!json.isObject()) {
        lastError_ = "Bezier handle is not an object";
        return false;
    }

    auto* obj = json.getDynamicObject();

    outHandle.time = obj->getProperty("time");
    outHandle.value = obj->getProperty("value");
    outHandle.linked = obj->getProperty("linked");

    return true;
}

juce::var ProjectSerializer::serializeChainNodePath(const ChainNodePath& path) {
    auto* obj = new juce::DynamicObject();

    obj->setProperty("trackId", path.trackId);
    obj->setProperty("topLevelDeviceId", path.topLevelDeviceId);

    juce::Array<juce::var> stepsArray;
    for (const auto& step : path.steps) {
        auto* stepObj = new juce::DynamicObject();
        stepObj->setProperty("type", static_cast<int>(step.type));
        stepObj->setProperty("id", step.id);
        stepsArray.add(juce::var(stepObj));
    }
    obj->setProperty("steps", juce::var(stepsArray));

    return juce::var(obj);
}

bool ProjectSerializer::deserializeChainNodePath(const juce::var& json, ChainNodePath& outPath) {
    if (!json.isObject()) {
        lastError_ = "Chain node path is not an object";
        return false;
    }

    auto* obj = json.getDynamicObject();

    outPath.trackId = obj->getProperty("trackId");
    outPath.topLevelDeviceId = obj->getProperty("topLevelDeviceId");

    auto stepsVar = obj->getProperty("steps");
    if (stepsVar.isArray()) {
        auto* arr = stepsVar.getArray();
        for (const auto& stepVar : *arr) {
            if (!stepVar.isObject())
                continue;
            auto* stepObj = stepVar.getDynamicObject();
            ChainPathStep step;
            step.type = static_cast<ChainStepType>(static_cast<int>(stepObj->getProperty("type")));
            step.id = stepObj->getProperty("id");
            outPath.steps.push_back(step);
        }
    }

    return true;
}

// ============================================================================
// Macro and Mod serialization helpers
// ============================================================================

juce::var ProjectSerializer::serializeMacroInfo(const MacroInfo& macro) {
    auto* obj = new juce::DynamicObject();

    obj->setProperty("id", macro.id);
    obj->setProperty("name", macro.name);
    obj->setProperty("value", macro.value);

    // Legacy target
    auto* targetObj = new juce::DynamicObject();
    targetObj->setProperty("deviceId", macro.target.deviceId);
    targetObj->setProperty("paramIndex", macro.target.paramIndex);
    obj->setProperty("target", juce::var(targetObj));

    // Links
    juce::Array<juce::var> linksArray;
    for (const auto& link : macro.links) {
        linksArray.add(serializeMacroLink(link));
    }
    obj->setProperty("links", juce::var(linksArray));

    return juce::var(obj);
}

bool ProjectSerializer::deserializeMacroInfo(const juce::var& json, MacroInfo& outMacro) {
    if (!json.isObject()) {
        lastError_ = "Macro is not an object";
        return false;
    }

    auto* obj = json.getDynamicObject();

    outMacro.id = obj->getProperty("id");
    outMacro.name = obj->getProperty("name").toString();
    outMacro.value = obj->getProperty("value");

    // Legacy target
    auto targetVar = obj->getProperty("target");
    if (targetVar.isObject()) {
        auto* targetObj = targetVar.getDynamicObject();
        outMacro.target.deviceId = targetObj->getProperty("deviceId");
        outMacro.target.paramIndex = targetObj->getProperty("paramIndex");
    }

    // Links
    auto linksVar = obj->getProperty("links");
    if (linksVar.isArray()) {
        auto* arr = linksVar.getArray();
        for (const auto& linkVar : *arr) {
            MacroLink link;
            if (!deserializeMacroLink(linkVar, link))
                return false;
            outMacro.links.push_back(link);
        }
    }

    return true;
}

juce::var ProjectSerializer::serializeModInfo(const ModInfo& mod) {
    // Use `data` alias so SER() macro works alongside manual `mod.` references
    const auto& data = mod;
    auto* obj = new juce::DynamicObject();

    SER(id);
    SER(name);
    SER(type);
    SER(enabled);
    SER(rate);
    SER(waveform);
    SER(phase);
    SER(phaseOffset);
    SER(value);
    SER(tempoSync);
    SER(syncDivision);
    SER(triggerMode);
    SER(oneShot);
    SER(useLoopRegion);
    SER(loopStart);
    SER(loopEnd);
    SER(midiChannel);
    SER(midiNote);
    SER(audioAttackMs);
    SER(audioReleaseMs);
    SER(curvePreset);

    // Curve points
    juce::Array<juce::var> curvePointsArray;
    for (const auto& point : mod.curvePoints) {
        curvePointsArray.add(serializeCurvePointData(point));
    }
    obj->setProperty("curvePoints", juce::var(curvePointsArray));

    // Links
    juce::Array<juce::var> linksArray;
    for (const auto& link : mod.links) {
        linksArray.add(serializeModLink(link));
    }
    obj->setProperty("links", juce::var(linksArray));

    // Legacy target/amount
    auto* targetObj = new juce::DynamicObject();
    targetObj->setProperty("deviceId", mod.target.deviceId);
    targetObj->setProperty("paramIndex", mod.target.paramIndex);
    obj->setProperty("target", juce::var(targetObj));
    obj->setProperty("amount", mod.amount);

    return juce::var(obj);
}

bool ProjectSerializer::deserializeModInfo(const juce::var& json, ModInfo& outMod) {
    if (!json.isObject()) {
        lastError_ = "Mod is not an object";
        return false;
    }

    auto* obj = json.getDynamicObject();
    // Use `data` alias so DESER() macro works alongside manual `outMod.` references
    auto& data = outMod;

    DESER(id);
    DESER(name);
    DESER(type);
    DESER(enabled);
    DESER(rate);
    DESER(waveform);
    DESER(phase);
    DESER(phaseOffset);
    DESER(value);
    DESER(tempoSync);
    DESER(syncDivision);
    DESER(triggerMode);
    DESER(oneShot);
    DESER(useLoopRegion);
    DESER(loopStart);
    DESER(loopEnd);
    DESER(midiChannel);
    DESER(midiNote);
    DESER(audioAttackMs);
    DESER(audioReleaseMs);
    DESER(curvePreset);

    // Curve points
    auto curvePointsVar = obj->getProperty("curvePoints");
    if (curvePointsVar.isArray()) {
        auto* arr = curvePointsVar.getArray();
        for (const auto& pointVar : *arr) {
            CurvePointData point;
            if (!deserializeCurvePointData(pointVar, point))
                return false;
            outMod.curvePoints.push_back(point);
        }
    }

    // Links
    auto linksVar = obj->getProperty("links");
    if (linksVar.isArray()) {
        auto* arr = linksVar.getArray();
        for (const auto& linkVar : *arr) {
            ModLink link;
            if (!deserializeModLink(linkVar, link))
                return false;
            outMod.links.push_back(link);
        }
    }

    // Legacy target/amount
    auto targetVar = obj->getProperty("target");
    if (targetVar.isObject()) {
        auto* targetObj = targetVar.getDynamicObject();
        outMod.target.deviceId = targetObj->getProperty("deviceId");
        outMod.target.paramIndex = targetObj->getProperty("paramIndex");
    }
    outMod.amount = obj->getProperty("amount");

    return true;
}

juce::var ProjectSerializer::serializeParameterInfo(const ParameterInfo& data) {
    auto* obj = new juce::DynamicObject();
    SER(paramIndex);
    SER(name);
    SER(unit);
    SER(minValue);
    SER(maxValue);
    SER(defaultValue);
    SER(currentValue);
    SER(scale);
    SER(skewFactor);
    SER(modulatable);
    SER(bipolarModulation);

    // Choices (vector of strings — stays manual)
    juce::Array<juce::var> choicesArray;
    for (const auto& choice : data.choices) {
        choicesArray.add(choice);
    }
    obj->setProperty("choices", juce::var(choicesArray));

    return juce::var(obj);
}

bool ProjectSerializer::deserializeParameterInfo(const juce::var& json, ParameterInfo& data) {
    if (!json.isObject()) {
        lastError_ = "Parameter is not an object";
        return false;
    }
    auto* obj = json.getDynamicObject();
    DESER(paramIndex);
    DESER(name);
    DESER(unit);
    DESER(minValue);
    DESER(maxValue);
    DESER(defaultValue);
    DESER(currentValue);
    DESER(scale);
    DESER(skewFactor);
    DESER(modulatable);
    DESER(bipolarModulation);

    // Choices (vector of strings — stays manual)
    auto choicesVar = obj->getProperty("choices");
    if (choicesVar.isArray()) {
        auto* arr = choicesVar.getArray();
        for (const auto& choiceVar : *arr) {
            data.choices.push_back(choiceVar.toString());
        }
    }

    return true;
}

juce::var ProjectSerializer::serializeCurvePointData(const CurvePointData& data) {
    auto* obj = new juce::DynamicObject();
    SER(phase);
    SER(value);
    SER(tension);
    return juce::var(obj);
}

bool ProjectSerializer::deserializeCurvePointData(const juce::var& json, CurvePointData& data) {
    if (!json.isObject()) {
        lastError_ = "Curve point data is not an object";
        return false;
    }
    auto* obj = json.getDynamicObject();
    DESER(phase);
    DESER(value);
    DESER(tension);
    return true;
}

juce::var ProjectSerializer::serializeMacroLink(const MacroLink& data) {
    auto* obj = new juce::DynamicObject();
    // Nested target stays manual
    auto* targetObj = new juce::DynamicObject();
    targetObj->setProperty("deviceId", data.target.deviceId);
    targetObj->setProperty("paramIndex", data.target.paramIndex);
    obj->setProperty("target", juce::var(targetObj));
    SER(amount);
    SER(bipolar);
    return juce::var(obj);
}

bool ProjectSerializer::deserializeMacroLink(const juce::var& json, MacroLink& data) {
    if (!json.isObject()) {
        lastError_ = "Macro link is not an object";
        return false;
    }
    auto* obj = json.getDynamicObject();
    // Nested target stays manual
    auto targetVar = obj->getProperty("target");
    if (targetVar.isObject()) {
        auto* targetObj = targetVar.getDynamicObject();
        data.target.deviceId = targetObj->getProperty("deviceId");
        data.target.paramIndex = targetObj->getProperty("paramIndex");
    }
    DESER(amount);
    DESER(bipolar);
    return true;
}

juce::var ProjectSerializer::serializeModLink(const ModLink& data) {
    auto* obj = new juce::DynamicObject();
    // Nested target stays manual
    auto* targetObj = new juce::DynamicObject();
    targetObj->setProperty("deviceId", data.target.deviceId);
    targetObj->setProperty("paramIndex", data.target.paramIndex);
    obj->setProperty("target", juce::var(targetObj));
    SER(amount);
    SER(bipolar);
    return juce::var(obj);
}

bool ProjectSerializer::deserializeModLink(const juce::var& json, ModLink& data) {
    if (!json.isObject()) {
        lastError_ = "Mod link is not an object";
        return false;
    }
    auto* obj = json.getDynamicObject();
    // Nested target stays manual
    auto targetVar = obj->getProperty("target");
    if (targetVar.isObject()) {
        auto* targetObj = targetVar.getDynamicObject();
        data.target.deviceId = targetObj->getProperty("deviceId");
        data.target.paramIndex = targetObj->getProperty("paramIndex");
    }
    DESER(amount);
    DESER(bipolar);
    return true;
}

}  // namespace magda
