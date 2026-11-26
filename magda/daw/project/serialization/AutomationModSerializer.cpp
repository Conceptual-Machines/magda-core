// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ProjectMigration.hpp"
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

}  // namespace

void ser::readControlTarget(const ObjectReader& in, ControlTarget& target) {
    in.optional("kind", target.kind);
    target.devicePath.steps.clear();
    if (in.has("trackId")) {
        in.required("trackId", target.devicePath.trackId);
        in.required("topLevelDeviceId", target.devicePath.topLevelDeviceId);
        in.optional("isTrackLevel", target.devicePath.isTrackLevel);
        in.forEachObject("steps", [&target](const ObjectReader& item) {
            ChainPathStep step;
            item.required("type", step.type);
            item.required("id", step.id);
            target.devicePath.steps.push_back(step);
        });
    }
    in.required("paramIndex", target.paramIndex);
    in.optional("modId", target.modId);
    in.optional("modParamIndex", target.modParamIndex);
    in.optional("sendBusIndex", target.sendBusIndex);
}

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
    // Keep the existing project key for backward compatibility. Runtime
    // Touching/Writing states are intentionally normalized to enabled.
    obj->setProperty("bypass", isAutomationPersistentlyDisabled(lane.authorityState));
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

    return {obj};
}

bool ProjectSerializer::deserializeAutomationLaneInfo(const juce::var& json,
                                                      AutomationLaneInfo& outLane) {
    outLane = {};
    return ser::decodeDocument(json, "lane", lastError_,
                               [&outLane](const ser::ObjectReader& in) { read(in, outLane); });
}

void ProjectSerializer::read(const ser::ObjectReader& raw, AutomationLaneInfo& outLane) {
    const auto in = project_migration::upgradeAutomationLane(raw);
    in.required("id", outLane.id);
    read(in.object("target"), outLane.target);
    in.required("type", outLane.type);
    in.required("name", outLane.name);
    in.optional("visible", outLane.visible);
    in.optional("expanded", outLane.expanded);
    if (bool bypass = false; in.optional("bypass", bypass))
        outLane.authorityState =
            bypass ? AutomationAuthorityState::Disabled : AutomationAuthorityState::Reading;
    in.optional("snapEditsToBeatGrid", outLane.snapEditsToBeatGrid);
    in.optional("snapValue", outLane.snapValue);
    in.optional("height", outLane.height);

    in.forEachObject("absolutePoints", [&outLane](const ser::ObjectReader& item) {
        AutomationPoint point;
        read(item, point);
        outLane.absolutePoints.push_back(point);
    });
    in.readArray("clipIds", outLane.clipIds);
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
    obj->setProperty("snapXEnabled", clip.snapXEnabled);
    obj->setProperty("snapXNumerator", clip.snapXNumerator);
    obj->setProperty("snapXDenominator", clip.snapXDenominator);
    obj->setProperty("snapYEnabled", clip.snapYEnabled);
    obj->setProperty("snapYNumerator", clip.snapYNumerator);
    obj->setProperty("snapYDenominator", clip.snapYDenominator);

    // Points
    juce::Array<juce::var> pointsArray;
    for (const auto& point : clip.points) {
        pointsArray.add(serializeAutomationPoint(point));
    }
    obj->setProperty("points", juce::var(pointsArray));

    return {obj};
}

bool ProjectSerializer::deserializeAutomationClipInfo(const juce::var& json,
                                                      AutomationClipInfo& outClip) {
    outClip = {};
    return ser::decodeDocument(json, "automationClip", lastError_,
                               [&outClip](const ser::ObjectReader& in) { read(in, outClip); });
}

void ProjectSerializer::read(const ser::ObjectReader& raw, AutomationClipInfo& outClip) {
    const auto in = project_migration::upgradeAutomationClip(raw);
    in.required("id", outClip.id);
    in.required("laneId", outClip.laneId);
    in.required("name", outClip.name);
    in.required("colour", outClip.colour);
    in.required("startBeats", outClip.startBeats);
    in.required("lengthBeats", outClip.lengthBeats);
    in.optional("looping", outClip.looping);
    in.optional("loopLengthBeats", outClip.loopLengthBeats);
    in.optional("snapXEnabled", outClip.snapXEnabled);
    in.optional("snapXNumerator", outClip.snapXNumerator);
    in.optional("snapXDenominator", outClip.snapXDenominator);
    in.optional("snapYEnabled", outClip.snapYEnabled);
    in.optional("snapYNumerator", outClip.snapYNumerator);
    in.optional("snapYDenominator", outClip.snapYDenominator);

    in.forEachObject("points", [&outClip](const ser::ObjectReader& item) {
        AutomationPoint point;
        read(item, point);
        outClip.points.push_back(point);
    });
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

    return {obj};
}

bool ProjectSerializer::deserializeAutomationPoint(const juce::var& json,
                                                   AutomationPoint& outPoint) {
    outPoint = {};
    return ser::decodeDocument(json, "point", lastError_,
                               [&outPoint](const ser::ObjectReader& in) { read(in, outPoint); });
}

void ProjectSerializer::read(const ser::ObjectReader& raw, AutomationPoint& outPoint) {
    const auto in = project_migration::upgradeAutomationPoint(raw);
    in.required("id", outPoint.id);
    in.required("beatPosition", outPoint.beatPosition);
    in.required("value", outPoint.value);
    in.required("curveType", outPoint.curveType);
    in.required("tension", outPoint.tension);
    read(in.object("inHandle"), outPoint.inHandle);
    read(in.object("outHandle"), outPoint.outHandle);
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

    return {obj};
}

void ProjectSerializer::read(const ser::ObjectReader& raw, AutomationTarget& outTarget) {
    const auto in = project_migration::upgradeAutomationTarget(raw);
    in.required("type", outTarget.kind);
    if (!in.has("devicePath"))
        in.fail("devicePath", "missing");
    else if (!fromVar(in.get("devicePath"), outTarget.devicePath))
        in.fail("devicePath", "not a valid chain path");
    in.required("paramIndex", outTarget.paramIndex);
    in.required("modId", outTarget.modId);
    in.required("modParamIndex", outTarget.modParamIndex);
    outTarget.sendBusIndex = -1;
    in.optional("sendBusIndex", outTarget.sendBusIndex);
}

juce::var ProjectSerializer::serializeBezierHandle(const BezierHandle& handle) {
    auto* obj = new juce::DynamicObject();

    obj->setProperty("beatOffset", handle.beatOffset);
    obj->setProperty("value", handle.value);
    obj->setProperty("linked", handle.linked);

    return {obj};
}

void ProjectSerializer::read(const ser::ObjectReader& in, BezierHandle& outHandle) {
    in.required("beatOffset", outHandle.beatOffset);
    in.required("value", outHandle.value);
    in.required("linked", outHandle.linked);
}

// Project persistence shares the canonical ChainNodePath serializer rather than
// keeping its own copy. The previous local writer omitted isTrackLevel, so every
// saved TrackVolume/TrackPan/SendLevel target reloaded as an invalid path with
// only a track id; fromVar recovers those legacy files.
juce::var ProjectSerializer::serializeChainNodePath(const ChainNodePath& path) {
    return toVar(path);
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

    return {obj};
}

void ProjectSerializer::read(const ser::ObjectReader& in, MacroInfo& outMacro) {
    in.required("id", outMacro.id);
    in.required("name", outMacro.name);
    in.required("value", outMacro.value);
    in.forEachObject("links", [&outMacro](const ser::ObjectReader& item) {
        MacroLink link;
        read(item, link);
        outMacro.links.push_back(link);
    });
    project_migration::liftLegacyTarget(in, outMacro.links);
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
    SER(invertOutput);
    SER(useLoopRegion);
    SER(loopStart);
    SER(loopEnd);
    SER(midiChannel);
    SER(midiNote);
    SER(audioAttackMs);
    SER(audioReleaseMs);
    SER(curvePreset);

    // ADSR envelope generator (type == Envelope)
    SER(envAttackMs);
    SER(envDecayMs);
    SER(envSustain);
    SER(envReleaseMs);
    SER(envAttackCurve);
    SER(envDecayCurve);
    SER(envReleaseCurve);

    // Random modulator (type == Random)
    SER(randomType);
    SER(randomShape);
    SER(randomSmooth);
    SER(randomStepDepth);

    // Envelope follower (type == Follower)
    SER(followerGainDb);
    obj->setProperty("tapPoint", static_cast<int>(mod.tapPoint));
    SER(followerAttackMs);
    SER(followerHoldMs);
    SER(followerReleaseMs);
    SER(followerHpEnabled);
    SER(followerHpFreq);
    SER(followerLpEnabled);
    SER(followerLpFreq);

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

    return {obj};
}

bool ProjectSerializer::deserializeModInfo(const juce::var& json, ModInfo& outMod) {
    outMod = {};
    return ser::decodeDocument(json, "mod", lastError_,
                               [&outMod](const ser::ObjectReader& in) { read(in, outMod); });
}

void ProjectSerializer::read(const ser::ObjectReader& in, ModInfo& data) {
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
    DESER_OPT(invertOutput);
    DESER(useLoopRegion);
    DESER(loopStart);
    DESER(loopEnd);
    DESER(midiChannel);
    DESER(midiNote);
    DESER(audioAttackMs);
    DESER(audioReleaseMs);
    DESER(curvePreset);

    // Each type's own fields arrived after the mod itself did.
    DESER_OPT(envAttackMs);
    DESER_OPT(envDecayMs);
    DESER_OPT(envSustain);
    DESER_OPT(envReleaseMs);
    DESER_OPT(envAttackCurve);
    DESER_OPT(envDecayCurve);
    DESER_OPT(envReleaseCurve);
    DESER_OPT(randomType);
    DESER_OPT(randomShape);
    DESER_OPT(randomSmooth);
    DESER_OPT(randomStepDepth);
    DESER_OPT(followerGainDb);
    DESER_OPT(followerAttackMs);
    DESER_OPT(followerHoldMs);
    DESER_OPT(followerReleaseMs);
    DESER_OPT(followerHpEnabled);
    DESER_OPT(followerHpFreq);
    DESER_OPT(followerLpEnabled);
    DESER_OPT(followerLpFreq);

    // Absent before the point was a choice: the type's default tap point.
    data.tapPoint = defaultModTapPoint(data.type);
    DESER_OPT(tapPoint);

    in.forEachObject("curvePoints", [&data](const ser::ObjectReader& item) {
        CurvePointData point;
        read(item, point);
        data.curvePoints.push_back(point);
    });
    in.forEachObject("links", [&data](const ser::ObjectReader& item) {
        ModLink link;
        read(item, link);
        data.links.push_back(link);
    });
    project_migration::liftLegacyTarget(in, data.links);
}

juce::var ProjectSerializer::serializeParameterInfo(const ParameterInfo& data) {
    auto* obj = new juce::DynamicObject();
    SER(paramIndex);
    SER(stableId);
    SER(name);
    SER(unit);
    SER(minValue);
    SER(maxValue);
    SER(valueConvention);
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
    SER(momentary);

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

    return {obj};
}

void ProjectSerializer::read(const ser::ObjectReader& in, ParameterInfo& data) {
    DESER(paramIndex);
    DESER_OPT(stableId);
    DESER(name);
    DESER(unit);
    DESER(minValue);
    DESER(maxValue);
    // Always present once ProjectMigration has inferred it for an older device.
    if (int convention = 0; in.required("valueConvention", convention)) {
        if (convention < static_cast<int>(ParameterValueConvention::Real) ||
            convention > static_cast<int>(ParameterValueConvention::Normalized))
            in.fail("valueConvention", "not a value convention: " + juce::String(convention));
        else
            data.valueConvention = static_cast<ParameterValueConvention>(convention);
    }
    DESER(defaultValue);
    DESER(currentValue);
    DESER(scale);
    DESER(skewFactor);
    DESER(modulatable);
    DESER(bipolarModulation);
    DESER_OPT(teMinValue);
    DESER_OPT(teMaxValue);
    DESER_OPT(scaleAnchor);
    DESER_OPT(displayFormat);
    DESER_OPT(gateSlotIndex);
    DESER_OPT(gateNegated);
    DESER_OPT(hidden);
    DESER_OPT(momentary);

    in.readArray("choices", data.choices, true);
    in.forEachObject("labelTicks", [&data](const ser::ObjectReader& tick) {
        float value = 0.0f;
        juce::String label;
        tick.required("value", value);
        tick.required("label", label);
        data.labelTicks.emplace_back(value, label);
    });
    in.readArray("valueTable", data.valueTable);
}

juce::var ProjectSerializer::serializeCurvePointData(const CurvePointData& data) {
    auto* obj = new juce::DynamicObject();
    SER(phase);
    SER(value);
    SER(tension);
    SER(curveType);
    SER(inHandleX);
    SER(inHandleY);
    SER(outHandleX);
    SER(outHandleY);
    return {obj};
}

bool ProjectSerializer::deserializeCurvePointData(const juce::var& json, CurvePointData& data) {
    data = {};
    return ser::decodeDocument(json, "point", lastError_,
                               [&data](const ser::ObjectReader& in) { read(in, data); });
}

void ProjectSerializer::read(const ser::ObjectReader& in, CurvePointData& data) {
    DESER(phase);
    DESER(value);
    DESER(tension);
    DESER_OPT(curveType);
    DESER_OPT(inHandleX);
    DESER_OPT(inHandleY);
    DESER_OPT(outHandleX);
    DESER_OPT(outHandleY);
}

juce::var ProjectSerializer::serializeMacroLink(const MacroLink& data) {
    auto* obj = new juce::DynamicObject();
    auto* targetObj = new juce::DynamicObject();
    serializeControlTarget(targetObj, data.target);
    obj->setProperty("target", juce::var(targetObj));
    SER(amount);
    SER(bipolar);
    return {obj};
}

void ProjectSerializer::read(const ser::ObjectReader& in, MacroLink& data) {
    if (const auto target = in.optionalObject("target"))
        ser::readControlTarget(*target, data.target);
    DESER(amount);
    DESER(bipolar);
}

juce::var ProjectSerializer::serializeModLink(const ModLink& data) {
    auto* obj = new juce::DynamicObject();
    auto* targetObj = new juce::DynamicObject();
    serializeControlTarget(targetObj, data.target);
    obj->setProperty("target", juce::var(targetObj));
    SER(amount);
    SER(bipolar);
    SER(enabled);
    return {obj};
}

void ProjectSerializer::read(const ser::ObjectReader& in, ModLink& data) {
    if (const auto target = in.optionalObject("target"))
        ser::readControlTarget(*target, data.target);
    DESER(amount);
    DESER(bipolar);
    DESER_OPT(enabled);
}

// ============================================================================
}  // namespace magda
