#include "ProjectSerializer.hpp"
#include "SerializationHelpers.hpp"

namespace magda {

namespace {

void serializeControlTarget(juce::DynamicObject* targetObj, const ControlTarget& target) {
    targetObj->setProperty("kind", static_cast<int>(target.kind));
    targetObj->setProperty("trackId", target.devicePath.trackId);
    targetObj->setProperty("topLevelDeviceId", target.devicePath.topLevelDeviceId);
    targetObj->setProperty("isTrackLevel", target.devicePath.isTrackLevel);
    juce::Array<juce::var> stepsArray;
    for (const auto& step : target.devicePath.steps) {
        auto* stepObj = new juce::DynamicObject();
        stepObj->setProperty("type", static_cast<int>(step.type));
        stepObj->setProperty("id", step.id);
        stepsArray.add(juce::var(stepObj));
    }
    targetObj->setProperty("steps", juce::var(stepsArray));
    targetObj->setProperty("paramIndex", target.paramIndex);
    targetObj->setProperty("modId", target.modId);
    targetObj->setProperty("modParamIndex", target.modParamIndex);
    targetObj->setProperty("sendBusIndex", target.sendBusIndex);
}

void deserializeControlTarget(juce::DynamicObject* targetObj, ControlTarget& target) {
    if (targetObj->hasProperty("kind"))
        target.kind =
            static_cast<ControlTarget::Kind>(static_cast<int>(targetObj->getProperty("kind")));
    if (targetObj->hasProperty("trackId")) {
        target.devicePath.trackId = static_cast<int>(targetObj->getProperty("trackId"));
        target.devicePath.topLevelDeviceId =
            static_cast<int>(targetObj->getProperty("topLevelDeviceId"));
        target.devicePath.isTrackLevel = static_cast<bool>(targetObj->getProperty("isTrackLevel"));
        auto stepsVar = targetObj->getProperty("steps");
        target.devicePath.steps.clear();
        if (stepsVar.isArray()) {
            for (const auto& stepVar : *stepsVar.getArray()) {
                if (!stepVar.isObject())
                    continue;
                auto* stepObj = stepVar.getDynamicObject();
                ChainPathStep step;
                step.type =
                    static_cast<ChainStepType>(static_cast<int>(stepObj->getProperty("type")));
                step.id = static_cast<int>(stepObj->getProperty("id"));
                target.devicePath.steps.push_back(step);
            }
        }
    }
    target.paramIndex = targetObj->getProperty("paramIndex");
    if (targetObj->hasProperty("modId"))
        target.modId = targetObj->getProperty("modId");
    if (targetObj->hasProperty("modParamIndex"))
        target.modParamIndex = targetObj->getProperty("modParamIndex");
    if (targetObj->hasProperty("sendBusIndex"))
        target.sendBusIndex = targetObj->getProperty("sendBusIndex");
}

}  // namespace

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
    obj->setProperty("bypass", lane.bypass);
    obj->setProperty("snapEditsToBeatGrid", lane.snapEditsToBeatGrid);
    obj->setProperty("snapValue", lane.snapValue);
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
    if (obj->hasProperty("bypass"))
        outLane.bypass = obj->getProperty("bypass");
    if (obj->hasProperty("snapEditsToBeatGrid"))
        outLane.snapEditsToBeatGrid = obj->getProperty("snapEditsToBeatGrid");
    else if (obj->hasProperty("snapToBeatGrid"))
        outLane.snapEditsToBeatGrid = obj->getProperty("snapToBeatGrid");
    else if (obj->hasProperty("snapTime"))
        outLane.snapEditsToBeatGrid = obj->getProperty("snapTime");
    if (obj->hasProperty("snapValue"))
        outLane.snapValue = obj->getProperty("snapValue");
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
    obj->setProperty("startBeats", clip.startBeats);
    obj->setProperty("lengthBeats", clip.lengthBeats);
    obj->setProperty("looping", clip.looping);
    obj->setProperty("loopLengthBeats", clip.loopLengthBeats);

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
    outClip.startBeats = obj->hasProperty("startBeats") ? obj->getProperty("startBeats")
                                                        : obj->getProperty("startTime");
    outClip.lengthBeats = obj->hasProperty("lengthBeats") ? obj->getProperty("lengthBeats")
                                                          : obj->getProperty("length");
    outClip.looping = obj->getProperty("looping");
    outClip.loopLengthBeats = obj->hasProperty("loopLengthBeats")
                                  ? obj->getProperty("loopLengthBeats")
                                  : obj->getProperty("loopLength");

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
    obj->setProperty("beatPosition", point.beatPosition);
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
    outPoint.beatPosition = obj->hasProperty("beatPosition") ? obj->getProperty("beatPosition")
                                                             : obj->getProperty("time");
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

    obj->setProperty("type", static_cast<int>(target.kind));
    obj->setProperty("trackId", target.devicePath.trackId);
    obj->setProperty("devicePath", serializeChainNodePath(target.devicePath));
    obj->setProperty("paramIndex", target.paramIndex);
    obj->setProperty("modId", target.modId);
    obj->setProperty("modParamIndex", target.modParamIndex);
    obj->setProperty("sendBusIndex", target.sendBusIndex);

    return juce::var(obj);
}

bool ProjectSerializer::deserializeAutomationTarget(const juce::var& json,
                                                    AutomationTarget& outTarget) {
    if (!json.isObject()) {
        lastError_ = "Automation target is not an object";
        return false;
    }

    auto* obj = json.getDynamicObject();

    outTarget.kind = static_cast<ControlTarget::Kind>(static_cast<int>(obj->getProperty("type")));
    if (!deserializeChainNodePath(obj->getProperty("devicePath"), outTarget.devicePath)) {
        return false;
    }
    // Pre-unification projects stored trackId on the target; the unified path
    // carries it now. If the path didn't deserialize a trackId, fall back.
    if (outTarget.devicePath.trackId == INVALID_TRACK_ID && obj->hasProperty("trackId"))
        outTarget.devicePath.trackId = obj->getProperty("trackId");
    outTarget.paramIndex = obj->getProperty("paramIndex");
    // Pre-unification format used a separate macroIndex field for Macro kind;
    // collapse onto paramIndex.
    if (obj->hasProperty("macroIndex") && outTarget.kind == ControlTarget::Kind::DeviceMacro)
        outTarget.paramIndex = obj->getProperty("macroIndex");
    outTarget.modId = obj->getProperty("modId");
    outTarget.modParamIndex = obj->getProperty("modParamIndex");
    // Migration: pre-unification projects had a separate sync-division lane
    // at modParamIndex == 1. The unified Rate lane (index 0) now covers both.
    if (outTarget.kind == ControlTarget::Kind::ModParam && outTarget.modParamIndex == 1)
        outTarget.modParamIndex = 0;
    outTarget.sendBusIndex =
        obj->hasProperty("sendBusIndex") ? static_cast<int>(obj->getProperty("sendBusIndex")) : -1;

    return true;
}

juce::var ProjectSerializer::serializeBezierHandle(const BezierHandle& handle) {
    auto* obj = new juce::DynamicObject();

    obj->setProperty("beatOffset", handle.beatOffset);
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

    outHandle.beatOffset =
        obj->hasProperty("beatOffset") ? obj->getProperty("beatOffset") : obj->getProperty("time");
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

    // One-shot migration of pre-#1149 projects: if no `links` were stored but
    // a populated legacy single `target` was, lift it into links so the macro
    // keeps working. New projects never write `target`, so this branch is dead
    // for anything saved after this commit.
    if (outMacro.links.empty()) {
        auto targetVar = obj->getProperty("target");
        if (targetVar.isObject()) {
            ControlTarget legacy;
            deserializeControlTarget(targetVar.getDynamicObject(), legacy);
            if (legacy.isValid()) {
                MacroLink link;
                link.target = legacy;
                outMacro.links.push_back(link);
            }
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

    // One-shot migration of pre-#1149 projects: if no `links` were stored but
    // a populated legacy single `target` was, lift it (and the legacy `amount`)
    // into links so the modulator keeps working. New projects never write
    // `target`/`amount`, so this branch is dead for anything saved after this
    // commit.
    if (outMod.links.empty()) {
        auto targetVar = obj->getProperty("target");
        if (targetVar.isObject()) {
            ControlTarget legacy;
            deserializeControlTarget(targetVar.getDynamicObject(), legacy);
            if (legacy.isValid()) {
                ModLink link;
                link.target = legacy;
                link.amount = static_cast<float>(obj->getProperty("amount"));
                outMod.links.push_back(link);
            }
        }
    }

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
    SER(teMinValue);
    SER(teMaxValue);
    SER(scale);
    SER(skewFactor);
    SER(scaleAnchor);
    SER(displayFormat);
    SER(modulatable);
    SER(bipolarModulation);
    SER(gateSlotIndex);
    SER(gateNegated);
    SER(hidden);

    // Choices (vector of strings — stays manual)
    juce::Array<juce::var> choicesArray;
    for (const auto& choice : data.choices) {
        choicesArray.add(choice);
    }
    obj->setProperty("choices", juce::var(choicesArray));

    juce::Array<juce::var> labelTicksArray;
    for (const auto& [value, label] : data.labelTicks) {
        auto* tickObj = new juce::DynamicObject();
        tickObj->setProperty("value", value);
        tickObj->setProperty("label", label);
        labelTicksArray.add(juce::var(tickObj));
    }
    obj->setProperty("labelTicks", juce::var(labelTicksArray));

    juce::Array<juce::var> valueTableArray;
    for (const auto& value : data.valueTable) {
        valueTableArray.add(value);
    }
    obj->setProperty("valueTable", juce::var(valueTableArray));

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

    if (obj->hasProperty("teMinValue"))
        DESER(teMinValue);
    if (obj->hasProperty("teMaxValue"))
        DESER(teMaxValue);
    if (obj->hasProperty("scaleAnchor"))
        DESER(scaleAnchor);
    if (obj->hasProperty("displayFormat"))
        DESER(displayFormat);
    if (obj->hasProperty("gateSlotIndex"))
        DESER(gateSlotIndex);
    if (obj->hasProperty("gateNegated"))
        DESER(gateNegated);
    if (obj->hasProperty("hidden"))
        DESER(hidden);

    // Choices (vector of strings — stays manual)
    auto choicesVar = obj->getProperty("choices");
    if (choicesVar.isArray()) {
        auto* arr = choicesVar.getArray();
        for (const auto& choiceVar : *arr) {
            data.choices.push_back(choiceVar.toString());
        }
    }

    auto labelTicksVar = obj->getProperty("labelTicks");
    if (labelTicksVar.isArray()) {
        auto* arr = labelTicksVar.getArray();
        for (const auto& tickVar : *arr) {
            if (auto* tickObj = tickVar.getDynamicObject()) {
                data.labelTicks.emplace_back(
                    static_cast<float>(static_cast<double>(tickObj->getProperty("value"))),
                    tickObj->getProperty("label").toString());
            }
        }
    }

    auto valueTableVar = obj->getProperty("valueTable");
    if (valueTableVar.isArray()) {
        auto* arr = valueTableVar.getArray();
        for (const auto& valueVar : *arr) {
            data.valueTable.push_back(valueVar.toString());
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
    auto* targetObj = new juce::DynamicObject();
    serializeControlTarget(targetObj, data.target);
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
    auto targetVar = obj->getProperty("target");
    if (targetVar.isObject()) {
        deserializeControlTarget(targetVar.getDynamicObject(), data.target);
    }
    DESER(amount);
    DESER(bipolar);
    return true;
}

juce::var ProjectSerializer::serializeModLink(const ModLink& data) {
    auto* obj = new juce::DynamicObject();
    auto* targetObj = new juce::DynamicObject();
    serializeControlTarget(targetObj, data.target);
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
    auto targetVar = obj->getProperty("target");
    if (targetVar.isObject()) {
        deserializeControlTarget(targetVar.getDynamicObject(), data.target);
    }
    DESER(amount);
    DESER(bipolar);
    return true;
}

// ============================================================================
}  // namespace magda
