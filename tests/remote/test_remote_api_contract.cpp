#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <limits>
#include <set>

#include "AudioClipTestHelpers.hpp"
#include "MockMagdaApi.hpp"
#include "magda/daw/api/remote_api.hpp"
#include "magda/daw/core/AutomationInfo.hpp"
#include "magda/daw/core/DeviceInfo.hpp"
#include "magda/daw/core/DrumGridPads.hpp"
#include "magda/daw/core/RackInfo.hpp"
#include "magda/daw/core/ReferenceImpact.hpp"

namespace {

using namespace magda;
using namespace magda::remote;

template <typename Dto, typename Decoder>
void requireRoundTrip(const Dto& original, Decoder decoder) {
    Error error;
    const auto json = toJson(original);
    const auto decoded = decoder(json, error);
    REQUIRE(decoded.has_value());
    REQUIRE(*decoded == original);
}

juce::var object(std::initializer_list<std::pair<const char*, juce::var>> properties) {
    auto result = new juce::DynamicObject();
    for (const auto& [name, value] : properties)
        result->setProperty(name, value);
    return result;
}

}  // namespace

TEST_CASE("Remote API registry is versioned, discoverable, and unique", "[remote-api][contract]") {
    const auto& registry = OperationRegistry::instance();
    REQUIRE(API_VERSION == "1.0");
    REQUIRE(registry.operations().size() >= 25);
    REQUIRE(registry.find("system.describe") != nullptr);
    REQUIRE(registry.find("project.get") != nullptr);
    REQUIRE(registry.find("project.save") != nullptr);
    REQUIRE(registry.find("project.setLoopRange") != nullptr);
    REQUIRE(registry.find("chordTrack.get") != nullptr);
    REQUIRE(registry.find("chordTrack.ensure") != nullptr);
    REQUIRE(registry.find("trackPresets.list") != nullptr);
    REQUIRE(registry.find("tracks.createFromPreset") != nullptr);
    REQUIRE(registry.find("tracks.applyPreset") != nullptr);
    REQUIRE(registry.find("devices.list") != nullptr);
    REQUIRE(registry.find("devices.listParameters") != nullptr);
    REQUIRE(registry.find("devices.setParameter") != nullptr);
    REQUIRE(registry.find("devices.setParameterConfig") != nullptr);
    REQUIRE(registry.find("devices.add") != nullptr);
    REQUIRE(registry.find("devices.replace") != nullptr);
    REQUIRE(registry.find("devices.remove") != nullptr);
    REQUIRE(registry.find("devices.move") != nullptr);
    REQUIRE(registry.find("devices.setBypassed") != nullptr);
    REQUIRE(registry.find("pads.list") != nullptr);
    for (const auto* name : {"pads.create", "pads.setDevice", "pads.setSample", "pads.clear",
                             "pads.swap", "pads.update"}) {
        const auto* operation = registry.find(name);
        REQUIRE(operation != nullptr);
        REQUIRE(operation->requiredScope == Scope::Edit);
    }
    REQUIRE(registry.find("devicePresets.list") != nullptr);
    REQUIRE(registry.find("devices.openEditor") != nullptr);
    for (const auto* name : {"racks.create", "racks.remove", "racks.update", "chains.create",
                             "chains.remove", "chains.update"})
        REQUIRE(registry.find(name) != nullptr);
    for (const auto* name :
         {"mods.list", "mods.create", "mods.update", "mods.remove", "mods.link", "mods.unlink",
          "macros.list", "macros.setValue", "macros.link", "macros.unlink"})
        REQUIRE(registry.find(name) != nullptr);
    REQUIRE(registry.find("session.launchClip") != nullptr);
    REQUIRE(registry.find("automation.addPoint") != nullptr);
    REQUIRE(registry.find("automation.setPoints") != nullptr);
    REQUIRE(registry.find("automation.deleteLane") != nullptr);
    for (const auto* name :
         {"automation.listClips", "automation.getClip", "automation.createClip",
          "automation.deleteClip", "automation.moveClip", "automation.resizeClip",
          "automation.duplicateClip", "automation.updateClip"})
        REQUIRE(registry.find(name) != nullptr);
    REQUIRE(registry.find("clips.listMidiEvents") != nullptr);
    REQUIRE(registry.find("clips.addMidiEvents") != nullptr);
    REQUIRE(registry.find("clips.updateMidiEvents") != nullptr);
    REQUIRE(registry.find("clips.replaceMidiEvents") != nullptr);
    REQUIRE(registry.find("clips.deleteMidiEvents") != nullptr);
    REQUIRE(registry.find("clips.move") != nullptr);
    REQUIRE(registry.find("clips.resize") != nullptr);
    REQUIRE(registry.find("clips.duplicate") != nullptr);
    REQUIRE(registry.find("routing.endpoints.list") != nullptr);
    REQUIRE(registry.find("routing.get") != nullptr);
    REQUIRE(registry.find("routing.set") != nullptr);
    REQUIRE(registry.find("sends.list") != nullptr);
    REQUIRE(registry.find("sends.create") != nullptr);
    REQUIRE(registry.find("sends.update") != nullptr);
    REQUIRE(registry.find("sends.remove") != nullptr);
    REQUIRE(registry.find("sidechains.list") != nullptr);
    REQUIRE(registry.find("sidechains.get") != nullptr);
    REQUIRE(registry.find("sidechains.set") != nullptr);
    REQUIRE(registry.find("does.not.exist") == nullptr);

    std::set<juce::String> names;
    for (const auto& operation : registry.operations()) {
        REQUIRE(operation.name.isNotEmpty());
        REQUIRE(operation.inputSchema.getDynamicObject() != nullptr);
        REQUIRE(operation.outputSchema.getDynamicObject() != nullptr);
        REQUIRE(names.insert(operation.name).second);
    }

    const auto description = registry.describe();
    REQUIRE(description["apiVersion"].toString() == "1.0");
    REQUIRE(description["operations"].getArray()->size() ==
            static_cast<int>(registry.operations().size()));
}

TEST_CASE("Sidechain operations expose closed path and logical-source schemas",
          "[remote-api][contract][sidechains][2838]") {
    const auto& registry = OperationRegistry::instance();
    const auto* list = registry.find("sidechains.list");
    const auto* get = registry.find("sidechains.get");
    const auto* set = registry.find("sidechains.set");
    REQUIRE(list != nullptr);
    REQUIRE(get != nullptr);
    REQUIRE(set != nullptr);
    CHECK(list->access == OperationAccess::Read);
    CHECK(list->requiredScope == Scope::Read);
    CHECK(list->access == OperationAccess::Read);
    CHECK(list->requiredScope == Scope::Read);
    CHECK(get->access == OperationAccess::Read);
    CHECK(get->requiredScope == Scope::Read);
    CHECK(set->access == OperationAccess::Write);
    CHECK(set->requiredScope == Scope::Edit);

    const auto owner = toJson(makeDevicePathDto(ChainNodePath::topLevelDevice(3, 7)));
    CHECK_FALSE(validateOperationInput(*set, object({{"ownerPath", owner},
                                                     {"sourceEndpointId", "track:2"},
                                                     {"type", "audio"},
                                                     {"tapPoint", "preFx"},
                                                     {"gainDb", -6.0},
                                                     {"enabled", true},
                                                     {"listen", false},
                                                     {"channelMapping", "automatic"}}))
                    .has_value());
    CHECK(validateOperationInput(*set, object({{"ownerPath", owner}, {"sourceTrackId", 2}}))
              .has_value());
    CHECK(validateOperationInput(*set,
                                 object({{"ownerPath", owner}, {"channelMapping", "left_only"}}))
              .has_value());
    CHECK(validateOperationInput(*set, object({{"ownerPath", owner}, {"gainDb", -61.0}}))
              .has_value());
}

TEST_CASE("Routing operations use closed safe schemas and edit scope",
          "[remote-api][contract][routing][2832]") {
    const auto& registry = OperationRegistry::instance();
    const auto* list = registry.find("routing.endpoints.list");
    const auto* get = registry.find("routing.get");
    const auto* set = registry.find("routing.set");
    REQUIRE(list != nullptr);
    REQUIRE(get != nullptr);
    REQUIRE(set != nullptr);
    CHECK(list->access == OperationAccess::Read);
    CHECK(list->requiredScope == Scope::Read);
    CHECK(get->access == OperationAccess::Read);
    CHECK(get->requiredScope == Scope::Read);
    CHECK(set->access == OperationAccess::Write);
    CHECK(set->requiredScope == Scope::Edit);

    CHECK_FALSE(
        validateOperationInput(*set, object({{"trackId", 3}, {"audioInputEndpointId", "track:2"}}))
            .has_value());
    CHECK(
        validateOperationInput(*set, object({{"trackId", 3}, {"audioInputDevice", "/dev/private"}}))
            .has_value());
    CHECK(validateOperationInput(*set, object({{"trackId", 3}, {"audioInputEndpointId", ""}}))
              .has_value());
}

TEST_CASE("Routing endpoint and track routing DTOs round-trip without backend ids",
          "[remote-api][contract][routing][2832]") {
    RoutingEndpoint endpoint;
    endpoint.id = routingEndpointId(RoutingMedia::Audio, RoutingDirection::Input,
                                    "/private/audio-device/input-1");
    endpoint.name = "Input 1";
    endpoint.media = RoutingMedia::Audio;
    endpoint.direction = RoutingDirection::Input;
    endpoint.kind = RoutingEndpointKind::Hardware;
    endpoint.available = true;
    endpoint.channelCount = 1;
    const auto endpointDto = makeRoutingEndpointDto(endpoint);
    REQUIRE_FALSE(endpointDto.id.contains("private"));
    requireRoundTrip(endpointDto, routingEndpointFromJson);
    const auto endpointJson = toJson(endpointDto);
    CHECK_FALSE(endpointJson.getDynamicObject()->hasProperty("internalId"));

    const TrackRoutingDto routing{3, "track:2", "", "master", "track:4", true, "auto"};
    requireRoundTrip(routing, trackRoutingFromJson);
    auto routingJson = toJson(routing);
    routingJson.getDynamicObject()->setProperty("backendHandle", "secret");
    Error error;
    CHECK_FALSE(trackRoutingFromJson(routingJson, error).has_value());
}

TEST_CASE("Send operations use closed safe schemas and edit scope",
          "[remote-api][contract][sends][2837]") {
    const auto& registry = OperationRegistry::instance();
    const auto* list = registry.find("sends.list");
    const auto* create = registry.find("sends.create");
    const auto* update = registry.find("sends.update");
    const auto* remove = registry.find("sends.remove");
    REQUIRE(list != nullptr);
    REQUIRE(create != nullptr);
    REQUIRE(update != nullptr);
    REQUIRE(remove != nullptr);
    CHECK(list->access == OperationAccess::Read);
    CHECK(list->requiredScope == Scope::Read);
    for (const auto* operation : {create, update, remove}) {
        CHECK(operation->access == OperationAccess::Write);
        CHECK(operation->requiredScope == Scope::Edit);
    }

    CHECK_FALSE(validateOperationInput(
                    *create, object({{"trackId", 3}, {"destinationEndpointId", "track:2"}}))
                    .has_value());
    CHECK(validateOperationInput(
              *create,
              object({{"trackId", 3}, {"destinationEndpointId", "track:2"}, {"busIndex", 7}}))
              .has_value());
    CHECK(validateOperationInput(*create, object({{"trackId", 3}, {"destinationTrackId", 2}}))
              .has_value());
    CHECK(validateOperationInput(*update, object({{"sendId", "send:1"}, {"level", 1.1}}))
              .has_value());
    CHECK(validateOperationInput(*update, object({{"sendId", "send:1"}, {"position", "mid_fader"}}))
              .has_value());
}

TEST_CASE("Send DTO round-trips without physical routing fields",
          "[remote-api][contract][sends][2837]") {
    const TrackSendDto send{"send:stable", 3, "track:2", 0.5, false, "pre_fader"};
    requireRoundTrip(send, trackSendFromJson);

    auto json = toJson(send);
    CHECK_FALSE(json.getDynamicObject()->hasProperty("busIndex"));
    CHECK_FALSE(json.getDynamicObject()->hasProperty("destinationTrackId"));
    json.getDynamicObject()->setProperty("backendHandle", "secret");
    Error error;
    CHECK_FALSE(trackSendFromJson(json, error).has_value());
}

TEST_CASE("Reference impact inventory covers every replacement-sensitive reference class",
          "[remote-api][contract][reference-impact]") {
    TrackInfo sourceTrack;
    sourceTrack.id = 1;
    sourceTrack.sends.push_back({2, 0.5f, false, 3});
    sourceTrack.audioInputDevice = "track:4";
    sourceTrack.midiInputDevice = "track:5";

    DeviceInfo device;
    device.id = 9;
    ParameterInfo parameter;
    parameter.paramIndex = 4;
    parameter.stableId = "cutoff";
    device.parameters.push_back(parameter);
    device.sidechain.type = SidechainConfig::Type::Audio;
    device.sidechain.sourceTrackId = 2;
    sourceTrack.chain.fxChainElements.push_back(makeDeviceElement(device));

    const auto devicePath = ChainNodePath::topLevelDevice(1, 9);
    const auto parameterTarget = ControlTarget::pluginParam(devicePath, 4);
    sourceTrack.macros[0].links.push_back({parameterTarget, 0.5f, false});
    ModInfo mod(7);
    mod.links.push_back({parameterTarget, 0.25f, false, true});
    sourceTrack.mods.push_back(mod);

    TrackInfo multiOutTrack;
    multiOutTrack.id = 6;
    multiOutTrack.multiOutLink = MultiOutTrackLink{1, 9, 1};

    std::vector<TrackInfo> tracks;
    tracks.push_back(sourceTrack);
    tracks.push_back(multiOutTrack);

    AutomationLaneInfo lane;
    lane.id = 22;
    lane.target = parameterTarget;
    const std::vector<AutomationLaneInfo> lanes{lane};
    const std::vector<BoundControlReference> bound{{"binding-1", parameterTarget}};

    const auto inventory = inventoryReferences({tracks, nullptr, lanes, bound});
    const auto count = [&inventory](ReferenceKind kind) {
        return std::ranges::count(inventory, kind, &ReferenceDescriptor::kind);
    };
    CHECK(count(ReferenceKind::Automation) == 1);
    CHECK(count(ReferenceKind::MacroLink) == 1);
    CHECK(count(ReferenceKind::ModulatorLink) == 1);
    CHECK(count(ReferenceKind::ControllerBinding) == 1);
    CHECK(count(ReferenceKind::Sidechain) == 1);
    CHECK(count(ReferenceKind::Routing) == 4);

    const auto automation =
        std::ranges::find(inventory, ReferenceKind::Automation, &ReferenceDescriptor::kind);
    REQUIRE(automation != inventory.end());
    CHECK(automation->target.parameterIndex == 4);
    CHECK(automation->target.parameterStableId == "cutoff");

    const std::array affectedPaths{devicePath};
    const auto affected = referencesAffectedBy(inventory, affectedPaths);
    CHECK(std::ranges::count(affected, ReferenceKind::Automation, &ReferenceDescriptor::kind) == 1);
    CHECK(std::ranges::count(affected, ReferenceKind::MacroLink, &ReferenceDescriptor::kind) == 1);
    CHECK(std::ranges::count(affected, ReferenceKind::ModulatorLink, &ReferenceDescriptor::kind) ==
          1);
    CHECK(std::ranges::count(affected, ReferenceKind::ControllerBinding,
                             &ReferenceDescriptor::kind) == 1);
    CHECK(std::ranges::count(affected, ReferenceKind::Sidechain, &ReferenceDescriptor::kind) == 1);
    CHECK(std::ranges::count(affected, ReferenceKind::Routing, &ReferenceDescriptor::kind) == 1);

    ReferencePolicySet injectedFailure(ReferencePolicy::Remap);
    const auto failedPlan = planReferenceImpacts(affected, {}, injectedFailure);
    CHECK_FALSE(failedPlan.canCommit());
    CHECK(failedPlan.rejected.size() == affected.size());
    CHECK(tracks[0].macros[0].links[0].target == parameterTarget);
    CHECK(tracks[0].mods[0].links[0].target == parameterTarget);
    CHECK(getDevice(tracks[0].chain.fxChainElements[0]).sidechain.sourceTrackId == 2);
}

TEST_CASE("Reference impact planning is pure and remaps only proven stable identity",
          "[remote-api][contract][reference-impact]") {
    const auto oldPath = ChainNodePath::topLevelDevice(1, 9);
    const auto newPath = ChainNodePath::topLevelDevice(1, 10);

    ReferenceAddress oldTarget;
    oldTarget.kind = ReferenceAddressKind::Parameter;
    oldTarget.trackId = 1;
    oldTarget.devicePath = oldPath;
    oldTarget.parameterIndex = 4;
    oldTarget.parameterStableId = "cutoff";
    auto newTarget = oldTarget;
    newTarget.devicePath = newPath;
    newTarget.parameterIndex = 17;

    ReferenceAddress source;
    source.kind = ReferenceAddressKind::AutomationLane;
    source.automationLaneId = 20;
    std::vector<ReferenceDescriptor> references{
        {ReferenceKind::Automation, source, oldTarget},
        {ReferenceKind::MacroLink, source, oldTarget},
        {ReferenceKind::Sidechain, source, oldTarget},
        {ReferenceKind::Routing, source, oldTarget},
    };
    const auto unchanged = references;

    ReferencePolicySet policy;
    policy.set(ReferenceKind::Automation, ReferencePolicy::Preserve)
        .set(ReferenceKind::MacroLink, ReferencePolicy::Remap)
        .set(ReferenceKind::Sidechain, ReferencePolicy::Drop)
        .set(ReferenceKind::Routing, ReferencePolicy::Reject);
    const std::vector<ReferenceTargetMapping> mappings{{oldTarget, newTarget, "cutoff", "cutoff"}};

    const auto plan = planReferenceImpacts(references, mappings, policy);
    CHECK_FALSE(plan.canCommit());
    REQUIRE(plan.preserved.size() == 1);
    REQUIRE(plan.remapped.size() == 1);
    CHECK(plan.remapped.front().newTarget.parameterIndex == 17);
    REQUIRE(plan.dropped.size() == 1);
    REQUIRE(plan.rejected.size() == 1);
    CHECK(references == unchanged);

    const std::vector<ReferenceDescriptor> numericIndexOnly{
        {ReferenceKind::MacroLink, source, oldTarget}};
    const std::vector<ReferenceTargetMapping> unproven{{oldTarget, newTarget, {}, {}}};
    const auto refused = planReferenceImpacts(numericIndexOnly, unproven, policy);
    REQUIRE(refused.rejected.size() == 1);
    CHECK(refused.rejected.front().reason == ReferenceImpactReason::MissingStableIdentity);
    CHECK_FALSE(refused.canCommit());
}

TEST_CASE("Reference impact DTO is closed, safe, and round-trips every decision variant",
          "[remote-api][contract][reference-impact]") {
    ReferenceAddress target;
    target.kind = ReferenceAddressKind::Parameter;
    target.trackId = 1;
    target.devicePath = ChainNodePath::topLevelDevice(1, 9);
    target.parameterIndex = 4;
    target.parameterStableId = "cutoff";

    ReferenceAddress newTarget = target;
    newTarget.devicePath = ChainNodePath::topLevelDevice(1, 10);
    newTarget.parameterIndex = 17;

    ReferenceAddress source;
    source.kind = ReferenceAddressKind::AutomationLane;
    source.automationLaneId = 20;
    const ReferenceDescriptor reference{ReferenceKind::Automation, source, target};

    ReferenceImpactPlan plan;
    plan.preserved.push_back({reference, ReferenceImpactReason::PolicyPreserve});
    plan.remapped.push_back({reference, newTarget, ReferenceImpactReason::StableIdentityMatch});
    plan.dropped.push_back({reference, ReferenceImpactReason::PolicyDrop});
    plan.rejected.push_back({reference, ReferenceImpactReason::PolicyReject});

    const auto dto = makeReferenceImpactResultDto(plan);
    const auto json = toJson(dto);
    CHECK(validateJson(json, referenceImpactResultSchema()).empty());
    requireRoundTrip(dto, referenceImpactResultFromJson);

    CHECK(json["remappedReferences"][0]["target"]["parameterStableId"].toString() == "cutoff");
    CHECK(json["remappedReferences"][0]["target"].getDynamicObject()->hasProperty(
              "fileOrIdentifier") == false);
    CHECK(json["remappedReferences"][0]["target"].getDynamicObject()->hasProperty("pluginId") ==
          false);

    auto withUnknown = json.clone();
    withUnknown.getDynamicObject()->setProperty("pluginState", "forbidden");
    CHECK_FALSE(validateJson(withUnknown, referenceImpactResultSchema()).empty());
}

TEST_CASE("Chord track operations expose a singleton-safe progression projection",
          "[remote-api][contract][chord-track]") {
    const ScopedMessageThreadAssertionDisabler threadAssertionGuard;
    magda::test::MockMagdaApi api;
    const auto& registry = OperationRegistry::instance();

    const auto* get = registry.find("chordTrack.get");
    const auto* ensure = registry.find("chordTrack.ensure");
    REQUIRE(get != nullptr);
    REQUIRE(ensure != nullptr);
    CHECK(get->access == OperationAccess::Read);
    CHECK(get->requiredScope == Scope::Read);
    CHECK(ensure->access == OperationAccess::Write);
    CHECK(ensure->requiredScope == Scope::Edit);

    const auto absent = get->handler(api, object({}), {});
    REQUIRE_FALSE(absent.failed());
    CHECK(absent.value["track"].isVoid());
    REQUIRE(absent.value["chords"].getArray() != nullptr);
    CHECK(absent.value["chords"].getArray()->isEmpty());
    CHECK(validateJson(absent.value, get->outputSchema).empty());

    TrackInfo chordTrack;
    chordTrack.id = 7;
    chordTrack.type = TrackType::Chord;
    chordTrack.name = "Chord Track";
    chordTrack.normalizeForType();
    api.tracks_.tracks.push_back(chordTrack);

    ClipInfo later;
    later.id = 20;
    later.trackId = chordTrack.id;
    later.setMidiContent();
    later.setPlacementBeats(8.0, 4.0);
    later.chordAnnotations.push_back({0.0, 4.0, "G7", 91});
    api.clips_.clips.emplace(later.id, later);

    ClipInfo earlier;
    earlier.id = 10;
    earlier.trackId = chordTrack.id;
    earlier.setMidiContent();
    earlier.setPlacementBeats(0.0, 8.0);
    // Deliberately stored out of order: the public progression is chronological.
    earlier.chordAnnotations.push_back({4.0, 4.0, "Fmaj7", 42});
    earlier.chordAnnotations.push_back({0.0, 4.0, "Cmaj7", 17});
    api.clips_.clips.emplace(earlier.id, earlier);
    api.clips_.clipsOnTrack[chordTrack.id] = {later.id, earlier.id};

    const auto present = get->handler(api, object({}), {});
    REQUIRE_FALSE(present.failed());
    REQUIRE(present.value["track"].getDynamicObject() != nullptr);
    CHECK(static_cast<int>(present.value["track"]["id"]) == chordTrack.id);
    const auto* chords = present.value["chords"].getArray();
    REQUIRE(chords != nullptr);
    REQUIRE(chords->size() == 3);
    CHECK((*chords)[0]["name"].toString() == "Cmaj7");
    CHECK(static_cast<double>((*chords)[0]["clipBeat"]) == 0.0);
    CHECK(static_cast<double>((*chords)[0]["startBeat"]) == 0.0);
    CHECK((*chords)[1]["name"].toString() == "Fmaj7");
    CHECK(static_cast<double>((*chords)[1]["startBeat"]) == 4.0);
    CHECK((*chords)[2]["name"].toString() == "G7");
    CHECK(static_cast<int>((*chords)[2]["clipId"]) == later.id);
    CHECK_FALSE((*chords)[0].getDynamicObject()->hasProperty("chordGroup"));
    CHECK(validateJson(present.value, get->outputSchema).empty());

    const auto* create = registry.find("tracks.create");
    REQUIRE(create != nullptr);
    const auto duplicate =
        create->handler(api, object({{"name", "Another"}, {"type", "chord"}}), {});
    REQUIRE(duplicate.failed());
    CHECK(duplicate.error->code == ErrorCode::Conflict);
    CHECK(api.undo_.executeCalls == 0);
}

TEST_CASE("Automation clip operations expose a closed edit-scoped contract",
          "[remote-api][contract][automation]") {
    const auto& registry = OperationRegistry::instance();
    for (const auto* name : {"automation.listClips", "automation.getClip"}) {
        const auto* operation = registry.find(name);
        REQUIRE(operation != nullptr);
        CHECK(operation->requiredScope == Scope::Read);
    }
    for (const auto* name :
         {"automation.createClip", "automation.deleteClip", "automation.moveClip",
          "automation.resizeClip", "automation.duplicateClip", "automation.updateClip"}) {
        const auto* operation = registry.find(name);
        REQUIRE(operation != nullptr);
        CHECK(operation->requiredScope == Scope::Edit);
    }

    const auto* create = registry.find("automation.createClip");
    REQUIRE(create != nullptr);
    CHECK_FALSE(validateOperationInput(
                    *create, object({{"laneId", 3}, {"startBeat", 8.0}, {"lengthBeats", 4.0}}))
                    .has_value());
    CHECK(validateOperationInput(*create,
                                 object({{"laneId", 3}, {"startBeat", 8.0}, {"lengthBeats", 0.0}}))
              .has_value());

    const auto* update = registry.find("automation.updateClip");
    REQUIRE(update != nullptr);
    juce::Array<juce::var> points;
    points.add(object({{"beatPosition", 1.0}, {"value", 0.25}, {"curve", "bezier"}}));
    CHECK_FALSE(
        validateOperationInput(*update, object({{"clipId", 4}, {"points", juce::var(points)}}))
            .has_value());
    juce::Array<juce::var> pointsWithId;
    pointsWithId.add(
        object({{"id", 9}, {"beatPosition", 1.0}, {"value", 0.25}, {"curve", "bezier"}}));
    CHECK(validateOperationInput(*update,
                                 object({{"clipId", 4}, {"points", juce::var(pointsWithId)}}))
              .has_value());

    AutomationClipDto clip;
    clip.id = 4;
    clip.laneId = 3;
    clip.name = "Filter shape";
    clip.colourArgb = 0xFF112233;
    clip.startBeat = 8.0;
    clip.lengthBeats = 4.0;
    clip.looping = true;
    clip.loopLengthBeats = 2.0;
    clip.points.push_back({9, 1.0, 0.25, "bezier"});
    requireRoundTrip(clip, automationClipFromJson);
    CHECK(validateJson(toJson(clip), update->outputSchema).empty());
}

TEST_CASE("Automation lane bulk writes are closed and edit-scoped",
          "[remote-api][contract][automation]") {
    const auto& registry = OperationRegistry::instance();
    const auto* setPoints = registry.find("automation.setPoints");
    const auto* deleteLane = registry.find("automation.deleteLane");
    REQUIRE(setPoints != nullptr);
    REQUIRE(deleteLane != nullptr);
    CHECK(setPoints->requiredScope == Scope::Edit);
    CHECK(deleteLane->requiredScope == Scope::Edit);

    juce::Array<juce::var> points;
    points.add(object({{"beatPosition", 4.0}, {"value", 0.75}, {"curve", "hard_corner"}}));
    const auto valid = object({{"laneId", 7}, {"points", juce::var(points)}});
    CHECK_FALSE(validateOperationInput(*setPoints, valid).has_value());

    juce::Array<juce::var> withId;
    withId.add(object({{"id", 99}, {"beatPosition", 4.0}, {"value", 0.75}, {"curve", "linear"}}));
    CHECK(validateOperationInput(*setPoints, object({{"laneId", 7}, {"points", juce::var(withId)}}))
              .has_value());

    juce::Array<juce::var> outOfRange;
    outOfRange.add(object({{"beatPosition", 4.0}, {"value", 1.25}, {"curve", "linear"}}));
    CHECK(validateOperationInput(*setPoints,
                                 object({{"laneId", 7}, {"points", juce::var(outOfRange)}}))
              .has_value());
    CHECK_FALSE(validateOperationInput(*deleteLane, object({{"laneId", 7}})).has_value());
}

TEST_CASE("Clip placement operations require explicit view-specific destinations",
          "[remote-api][contract][clips]") {
    const auto& registry = OperationRegistry::instance();
    const auto* move = registry.find("clips.move");
    const auto* resize = registry.find("clips.resize");
    const auto* duplicate = registry.find("clips.duplicate");
    REQUIRE(move != nullptr);
    REQUIRE(resize != nullptr);
    REQUIRE(duplicate != nullptr);
    CHECK(move->requiredScope == Scope::Edit);
    CHECK(resize->requiredScope == Scope::Edit);
    CHECK(duplicate->requiredScope == Scope::Edit);

    const auto arrangement = object({{"view", "arrangement"}, {"trackId", 2}, {"startBeat", 8.0}});
    const auto session = object({{"view", "session"}, {"trackId", 2}, {"sceneIndex", 3}});
    CHECK_FALSE(validateOperationInput(*move, object({{"clipId", 1}, {"destination", arrangement}}))
                    .has_value());
    CHECK_FALSE(
        validateOperationInput(*duplicate, object({{"clipId", 1}, {"destination", session}}))
            .has_value());
    CHECK_FALSE(validateOperationInput(
                    *resize, object({{"clipId", 1}, {"lengthBeats", 4.0}, {"edge", "end"}}))
                    .has_value());

    CHECK(
        validateOperationInput(
            *move, object({{"clipId", 1},
                           {"destination",
                            object({{"view", "arrangement"}, {"trackId", 2}, {"sceneIndex", 3}})}}))
            .has_value());
    CHECK(validateOperationInput(
              *duplicate,
              object({{"clipId", 1},
                      {"destination",
                       object({{"view", "session"}, {"trackId", 2}, {"startBeat", 8.0}})}}))
              .has_value());
}

TEST_CASE("Device modulation reads and writes declare their scopes",
          "[remote-api][contract][2294]") {
    const auto& registry = OperationRegistry::instance();
    for (const auto* name : {"mods.list", "macros.list"}) {
        const auto* operation = registry.find(name);
        REQUIRE(operation != nullptr);
        CHECK(operation->access == OperationAccess::Read);
        CHECK(operation->requiredScope == Scope::Read);
    }
    for (const auto* name : {"mods.create", "mods.update", "mods.remove", "mods.link",
                             "mods.unlink", "macros.setValue", "macros.link", "macros.unlink"}) {
        const auto* operation = registry.find(name);
        REQUIRE(operation != nullptr);
        CHECK(operation->access == OperationAccess::Write);
        CHECK(operation->requiredScope == Scope::Edit);
    }
}

TEST_CASE("Project loop range uses a closed edit-scoped beat contract",
          "[remote-api][contract][project]") {
    const auto* operation = OperationRegistry::instance().find("project.setLoopRange");
    REQUIRE(operation != nullptr);
    CHECK(operation->requiredScope == Scope::Edit);
    CHECK_FALSE(validateOperationInput(*operation, object({{"startBeat", 4.0}, {"endBeat", 12.0}}))
                    .has_value());
    CHECK(validateOperationInput(*operation, object({{"startBeat", -1.0}, {"endBeat", 12.0}}))
              .has_value());
    CHECK(validateOperationInput(*operation,
                                 object({{"startBeat", 4.0}, {"endBeat", 12.0}, {"enabled", true}}))
              .has_value());
}

TEST_CASE("Track preset operations expose only safe metadata and an opaque-id create contract",
          "[remote-api][contract][presets]") {
    const auto& registry = OperationRegistry::instance();
    const auto* list = registry.find("trackPresets.list");
    const auto* create = registry.find("tracks.createFromPreset");
    const auto* apply = registry.find("tracks.applyPreset");
    REQUIRE(list != nullptr);
    REQUIRE(create != nullptr);
    REQUIRE(apply != nullptr);
    CHECK(list->access == OperationAccess::Read);
    CHECK(list->requiredScope == Scope::Read);
    CHECK(create->access == OperationAccess::Write);
    CHECK(create->requiredScope == Scope::Edit);
    CHECK(apply->access == OperationAccess::Write);
    CHECK(apply->requiredScope == Scope::Edit);

    const auto itemSchema = list->outputSchema["items"];
    const auto* properties = itemSchema["properties"].getDynamicObject();
    REQUIRE(properties != nullptr);
    CHECK(properties->hasProperty("id"));
    CHECK(properties->hasProperty("name"));
    CHECK(properties->hasProperty("category"));
    CHECK_FALSE(properties->hasProperty("path"));
    CHECK_FALSE(properties->hasProperty("state"));

    CHECK_FALSE(
        validateOperationInput(*create, object({{"presetId", "track-preset:abc"}})).has_value());
    const auto unknown = validateOperationInput(
        *create, object({{"presetId", "track-preset:abc"}, {"filePath", "/tmp/preset.mps"}}));
    REQUIRE(unknown.has_value());
    CHECK(unknown->issues.front().code == "unknown_field");

    const auto* output = create->outputSchema["properties"].getDynamicObject();
    REQUIRE(output != nullptr);
    CHECK(output->hasProperty("trackId"));
    CHECK(output->hasProperty("deviceGraph"));

    CHECK_FALSE(
        validateOperationInput(*apply, object({{"trackId", 3}, {"presetId", "track-preset:abc"}}))
            .has_value());
    CHECK(validateOperationInput(
              *apply,
              object({{"trackId", 3}, {"presetId", "track-preset:abc"}, {"pluginState", "opaque"}}))
              .has_value());
    const auto* applyOutput = apply->outputSchema["properties"].getDynamicObject();
    REQUIRE(applyOutput != nullptr);
    CHECK(applyOutput->hasProperty("trackId"));
    CHECK(applyOutput->hasProperty("deviceGraph"));
    CHECK(applyOutput->hasProperty("referenceImpact"));
}

TEST_CASE("Device preset discovery is path-free and device-scoped",
          "[remote-api][contract][presets]") {
    const auto* operation = OperationRegistry::instance().find("devicePresets.list");
    REQUIRE(operation != nullptr);
    CHECK(operation->access == OperationAccess::Read);
    CHECK(operation->requiredScope == Scope::Read);

    const auto* properties = operation->outputSchema["items"]["properties"].getDynamicObject();
    REQUIRE(properties != nullptr);
    CHECK(properties->hasProperty("id"));
    CHECK(properties->hasProperty("name"));
    CHECK(properties->hasProperty("category"));
    CHECK(properties->hasProperty("source"));
    CHECK_FALSE(properties->hasProperty("path"));
    CHECK_FALSE(properties->hasProperty("file"));
    CHECK_FALSE(properties->hasProperty("state"));
}

TEST_CASE("Device preset application is a closed edit with safe structured output",
          "[remote-api][contract][presets]") {
    const auto* operation = OperationRegistry::instance().find("devices.applyPreset");
    REQUIRE(operation != nullptr);
    CHECK(operation->access == OperationAccess::Write);
    CHECK(operation->requiredScope == Scope::Edit);

    auto input =
        object({{"devicePath", toJson(makeDevicePathDto(ChainNodePath::topLevelDevice(1, 5)))},
                {"presetId", "device-preset:abc"}});
    CHECK_FALSE(validateOperationInput(*operation, input).has_value());
    input.getDynamicObject()->setProperty("presetPath", "/tmp/secret.mps");
    const auto unknown = validateOperationInput(*operation, input);
    REQUIRE(unknown.has_value());
    CHECK(unknown->issues.front().code == "unknown_field");

    const auto* output = operation->outputSchema["properties"].getDynamicObject();
    REQUIRE(output != nullptr);
    CHECK(output->hasProperty("deviceGraph"));
    CHECK(output->hasProperty("referenceImpact"));
    CHECK_FALSE(output->hasProperty("pluginId"));
    CHECK_FALSE(output->hasProperty("state"));
}

TEST_CASE("Device replacement is a closed edit with safe structured output",
          "[remote-api][contract][devices][replace]") {
    const auto* operation = OperationRegistry::instance().find("devices.replace");
    REQUIRE(operation != nullptr);
    CHECK(operation->access == OperationAccess::Write);
    CHECK(operation->requiredScope == Scope::Edit);

    auto input =
        object({{"devicePath", toJson(makeDevicePathDto(ChainNodePath::topLevelDevice(1, 5)))},
                {"catalogId", "filter"},
                {"presetId", "device-preset:abc"}});
    CHECK_FALSE(validateOperationInput(*operation, input).has_value());
    input.getDynamicObject()->setProperty("pluginPath", "/tmp/secret.vst3");
    const auto unknown = validateOperationInput(*operation, input);
    REQUIRE(unknown.has_value());
    CHECK(unknown->issues.front().code == "unknown_field");

    const auto* output = operation->outputSchema["properties"].getDynamicObject();
    REQUIRE(output != nullptr);
    CHECK(output->hasProperty("devicePath"));
    CHECK(output->hasProperty("deviceGraph"));
    CHECK(output->hasProperty("referenceImpact"));
    CHECK_FALSE(output->hasProperty("pluginId"));
    CHECK_FALSE(output->hasProperty("state"));
}

TEST_CASE("Remote API input validation returns structured issues",
          "[remote-api][contract][validation]") {
    const auto& registry = OperationRegistry::instance();

    SECTION("invalid id") {
        const auto* operation = registry.find("clips.get");
        REQUIRE(operation != nullptr);
        const auto error = validateOperationInput(*operation, object({{"clipId", -1}}));
        REQUIRE(error.has_value());
        REQUIRE(error->code == ErrorCode::ValidationFailed);
        REQUIRE(error->issues.front().path == "$.clipId");
        REQUIRE(error->issues.front().code == "minimum");
    }

    SECTION("invalid enum") {
        const auto* operation = registry.find("tracks.create");
        REQUIRE(operation != nullptr);
        const auto error =
            validateOperationInput(*operation, object({{"name", "Bass"}, {"type", "banana"}}));
        REQUIRE(error.has_value());
        REQUIRE(error->issues.front().code == "enum");
    }

    SECTION("out of range") {
        const auto* operation = registry.find("clips.addMidiNote");
        REQUIRE(operation != nullptr);
        const auto error = validateOperationInput(*operation, object({{"clipId", 1},
                                                                      {"note", 200},
                                                                      {"velocity", 100},
                                                                      {"startBeat", 0.0},
                                                                      {"lengthBeats", 1.0}}));
        REQUIRE(error.has_value());
        REQUIRE(error->issues.front().path == "$.note");
        REQUIRE(error->issues.front().code == "maximum");
    }

    SECTION("ids cannot overflow their C++ representation") {
        const auto* operation = registry.find("clips.get");
        REQUIRE(operation != nullptr);
        const auto error = validateOperationInput(
            *operation,
            object({{"clipId", static_cast<juce::int64>(std::numeric_limits<int>::max()) + 1}}));
        REQUIRE(error.has_value());
        REQUIRE(error->issues.front().path == "$.clipId");
        REQUIRE(error->issues.front().code == "maximum");
    }

    SECTION("nullable ids reject negative int64 wrap-around") {
        const DeviceDto device{10,
                               3,
                               5,
                               std::nullopt,
                               makeDevicePathDto(ChainNodePath::chainDevice(3, 5, 7, 10)),
                               "Synth",
                               "instrument",
                               "internal",
                               true,
                               false,
                               -3.0,
                               {}};
        auto json = toJson(device);
        // 5 - 2^32 decodes back to rack 5 if the id is truncated to 32 bits.
        json.getDynamicObject()->setProperty(
            "rackId", juce::var(static_cast<juce::int64>(5) - (static_cast<juce::int64>(1) << 32)));
        Error error;
        REQUIRE_FALSE(deviceFromJson(json, error).has_value());
        REQUIRE(error.code == ErrorCode::ValidationFailed);
        REQUIRE(error.issues.front().path == "$.rackId");
        REQUIRE(error.issues.front().code == "minimum");
    }

    SECTION("non-id integer selectors reject int64 truncation") {
        const auto* operation = registry.find("session.launchScene");
        REQUIRE(operation != nullptr);
        const auto error = validateOperationInput(
            *operation,
            object({{"sceneIndex",
                     juce::var(static_cast<juce::int64>(std::numeric_limits<int>::max()) + 1)}}));
        REQUIRE(error.has_value());
        REQUIRE(error->issues.front().path == "$.sceneIndex");
        REQUIRE(error->issues.front().code == "maximum");
    }

    SECTION("anyOf failures include branch-level issues") {
        const auto* operation = registry.find("tracks.get");
        REQUIRE(operation != nullptr);
        const auto error = validateOperationInput(*operation, object({{"trackId", -1}}));
        REQUIRE(error.has_value());
        REQUIRE(error->issues.front().code == "any_of");
        REQUIRE(error->issues.size() > 1);
        REQUIRE(error->issues[1].path.startsWith("$.trackId<anyOf:"));
    }

    SECTION("the invalid track sentinel is not a public track id") {
        for (const auto* operationName : {"tracks.get", "selection.set"}) {
            const auto* operation = registry.find(operationName);
            REQUIRE(operation != nullptr);
            juce::var input = juce::String(operationName) == "tracks.get"
                                  ? object({{"trackId", -1}})
                                  : toJson(SelectionDto{.trackId = INVALID_TRACK_ID});
            const auto error = validateOperationInput(*operation, input);
            REQUIRE(error.has_value());
            REQUIRE(error->issues.front().path == "$.trackId");
            REQUIRE(error->issues.front().code == "any_of");
        }
    }

    SECTION("master track ids remain valid") {
        for (const auto* operationName : {"tracks.get", "selection.set"}) {
            const auto* operation = registry.find(operationName);
            REQUIRE(operation != nullptr);
            juce::var input = juce::String(operationName) == "tracks.get"
                                  ? object({{"trackId", MASTER_TRACK_ID}})
                                  : toJson(SelectionDto{.trackId = MASTER_TRACK_ID});
            REQUIRE_FALSE(validateOperationInput(*operation, input).has_value());
        }
    }

    SECTION("request inputs enforce blanket string and array limits") {
        const auto* createTrack = registry.find("tracks.create");
        REQUIRE(createTrack != nullptr);
        const juce::String oversizedName = juce::String::repeatedString("x", 16 * 1024 + 1);
        auto stringError = validateOperationInput(
            *createTrack, object({{"name", oversizedName}, {"type", "audio"}}));
        REQUIRE(stringError.has_value());
        REQUIRE(stringError->issues.front().code == "max_length");

        const auto* selection = registry.find("selection.set");
        REQUIRE(selection != nullptr);
        juce::Array<juce::var> oversizedIds;
        for (int index = 0; index < 4097; ++index)
            oversizedIds.add(index);
        auto input = toJson(SelectionDto{});
        input.getDynamicObject()->setProperty("clipIds", oversizedIds);
        auto arrayError = validateOperationInput(*selection, input);
        REQUIRE(arrayError.has_value());
        REQUIRE(arrayError->issues.front().code == "max_items");
    }

    SECTION("response schemas are not capped by the request DoS limits") {
        SelectionDto selection;
        for (int index = 0; index < 4200; ++index)
            selection.clipIds.push_back(index);
        const auto json = toJson(selection);
        const auto* get = registry.find("selection.get");
        REQUIRE(get != nullptr);
        REQUIRE(validateJson(json, get->outputSchema).empty());
        const auto* set = registry.find("selection.set");
        REQUIRE(set != nullptr);
        const auto error = validateOperationInput(*set, json);
        REQUIRE(error.has_value());
        REQUIRE(error->issues.front().code == "max_items");
    }

    SECTION("unknown field") {
        const auto* operation = registry.find("transport.seek");
        REQUIRE(operation != nullptr);
        const auto error =
            validateOperationInput(*operation, object({{"positionBeats", 4.0}, {"seconds", 2.0}}));
        REQUIRE(error.has_value());
        REQUIRE(error->issues.front().path == "$.seconds");
        REQUIRE(error->issues.front().code == "unknown_field");
    }

    SECTION("exactly one of deltaBeats and deltaBars") {
        // transport.seekRelative is the first schema to use oneOf, and the
        // validator ignores keywords it does not implement — so an advertised
        // constraint that is not enforced would let both of these through to
        // the handler, where "neither" is a silent no-op and "both" silently
        // prefers one.
        const auto* operation = registry.find("transport.seekRelative");
        REQUIRE(operation != nullptr);

        REQUIRE_FALSE(
            validateOperationInput(*operation, object({{"deltaBeats", 2.5}})).has_value());
        REQUIRE_FALSE(validateOperationInput(*operation, object({{"deltaBars", -1}})).has_value());

        const auto neither = validateOperationInput(*operation, object({}));
        REQUIRE(neither.has_value());
        REQUIRE(neither->issues.front().code == "one_of");

        const auto both =
            validateOperationInput(*operation, object({{"deltaBeats", 2.5}, {"deltaBars", -1}}));
        REQUIRE(both.has_value());
        REQUIRE(both->issues.front().code == "one_of");
    }

    SECTION("bar offsets are bounded") {
        // The count is narrowed on the way to the tempo sequence, so a value
        // no project can mean is refused at the edge rather than clamped
        // silently somewhere behind it.
        const auto* operation = registry.find("transport.seekRelative");
        REQUIRE(operation != nullptr);

        const auto error = validateOperationInput(*operation, object({{"deltaBars", 2000000000}}));
        REQUIRE(error.has_value());
        REQUIRE(error->issues.front().code == "maximum");
    }

    SECTION("NaN and Infinity") {
        const auto* operation = registry.find("transport.seek");
        REQUIRE(operation != nullptr);
        for (const auto value :
             {std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity()}) {
            const auto error =
                validateOperationInput(*operation, object({{"positionBeats", value}}));
            REQUIRE(error.has_value());
            REQUIRE(error->issues.front().code == "finite");
        }
    }

    SECTION("error envelope") {
        const Error error{ErrorCode::NotFound,
                          "No such clip",
                          {{"$.clipId", "not_found", "42"}},
                          object({{"referenceImpact", object({{"safe", true}})}})};
        const auto envelope = errorEnvelope(error);
        REQUIRE(static_cast<bool>(envelope["ok"]) == false);
        REQUIRE(envelope["apiVersion"].toString() == "1.0");
        REQUIRE(envelope["error"]["code"].toString() == "not_found");
        REQUIRE(envelope["error"]["issues"].getArray()->size() == 1);
        REQUIRE(static_cast<bool>(envelope["error"]["details"]["referenceImpact"]["safe"]));
    }
}

TEST_CASE("Remote API DTOs round-trip through JSON", "[remote-api][contract][dto]") {
    const ProjectDto project{"Demo",  128.0, 7,   8,    48000.0, 128, 9,
                             "minor", true,  4.0, 12.0, true,    true};
    requireRoundTrip(project, projectFromJson);

    const TrackDto track{3,         "audio",  "Bass", 0xff102030, std::nullopt, {4, 5}, 0.8,
                         -0.25,     false,    true,   true,       "in",         false,  "all",
                         "track:2", "master", ""};
    requireRoundTrip(track, trackFromJson);

    const ClipDto clip{9,         3,
                       "midi",    "session",
                       "Pattern", 0xffaabbcc,
                       0.0,       4.0,
                       true,      2,
                       "trigger", "1_bar",
                       "next",    {{60, 110, 0.0, 0.5}, {64, 100, 1.0, 0.5}},
                       {}};
    requireRoundTrip(clip, clipFromJson);

    requireRoundTrip(MidiEventDto{1, "note", 36, 110, 0, 0, 0.0, 0.25, true}, midiEventFromJson);
    requireRoundTrip(MidiEventDto{2, "controlChange", 0, 0, 74, 96, 0.5}, midiEventFromJson);
    requireRoundTrip(MidiEventDto{3, "pitchBend", 0, 0, 0, 9000, 1.0}, midiEventFromJson);
    requireRoundTrip(MidiEventDto{4, "channelPressure", 0, 0, 0, 80, 1.5}, midiEventFromJson);
    requireRoundTrip(MidiEventDto{5, "polyAftertouch", 60, 0, 0, 70, 2.0}, midiEventFromJson);

    // Every sidechain field carries a non-default value: a decoder that dropped
    // one would round-trip through the defaults and prove nothing.
    const DeviceSidechainDto sidechain{"audio", 2, "audio", 4, "preFx", -4.5, true};
    const DeviceGraphDto graph{
        {{10, 3, 20, 30, makeDevicePathDto(ChainNodePath::chainDevice(3, 20, 30, 10)), "Synth",
          "instrument", "internal", true, false, -3.0, sidechain}},
        {{20,
          3,
          std::nullopt,
          std::nullopt,
          makeDevicePathDto(ChainNodePath::rack(3, 20)),
          "Parallel",
          false,
          0.0,
          0.0,
          {30}}},
        {{30,
          20,
          makeDevicePathDto(ChainNodePath::chain(3, 20, 30)),
          "Main",
          0,
          false,
          false,
          false,
          0.0,
          0.0,
          {10},
          {}}}};
    requireRoundTrip(graph, deviceGraphFromJson);

    const DeviceCatalogEntryDto catalogEntry{
        "4osc",     "4OSC",       "Tracktion", "Synth", "Four oscillator synth",
        "internal", "instrument", true};
    requireRoundTrip(catalogEntry, deviceCatalogEntryFromJson);

    const DevicePresetDto preset{"device-preset:opaque", "Reese 808", "Bass", "magda"};
    requireRoundTrip(preset, devicePresetFromJson);

    const SelectionDto selection{3, 9, {9, 12}, 5, 6, 9, {0, 2}};
    requireRoundTrip(selection, selectionFromJson);

    const TransportDto transport{true, false, true, 16.5};
    requireRoundTrip(transport, transportFromJson);

    const SessionDto session{{{3, 2, 9, "playing"}, {4, 2, 12, "queued"}}};
    requireRoundTrip(session, sessionFromJson);

    const AutomationLaneDto lane{
        5,
        "absolute",
        "Volume",
        {"track_volume", DevicePathDto{3, "fx", true, std::nullopt, {}}, -1, -1, -1, -1},
        {{7, 0.0, 0.5, "linear"}, {8, 4.0, 0.75, "bezier"}},
        {}};
    requireRoundTrip(lane, automationLaneFromJson);
}

TEST_CASE("Track updates expose bounded colour and safe input state",
          "[remote-api][contract][tracks]") {
    const auto* update = OperationRegistry::instance().find("tracks.update");
    REQUIRE(update != nullptr);
    CHECK(update->requiredScope == Scope::Edit);

    for (const auto* mode : {"off", "in", "auto"}) {
        CHECK_FALSE(validateOperationInput(
                        *update, object({{"trackId", 1},
                                         {"colourArgb", static_cast<juce::int64>(0xffffffffu)},
                                         {"recordArmed", true},
                                         {"inputMonitor", mode}}))
                        .has_value());
    }

    CHECK(validateOperationInput(*update, object({{"trackId", 1}, {"inputMonitor", "always"}}))
              .has_value());
    CHECK(validateOperationInput(
              *update,
              object({{"trackId", 1}, {"colourArgb", static_cast<juce::int64>(0x100000000)}}))
              .has_value());
}

TEST_CASE("Dense response payloads round-trip and validate against the published schema",
          "[remote-api][contract][dto]") {
    ClipDto clip;
    clip.id = 9;
    clip.trackId = 3;
    clip.type = "midi";
    clip.view = "arrangement";
    clip.name = "Dense";
    clip.colourArgb = 0xffaabbcc;
    clip.lengthBeats = 1250.0;
    clip.launchMode = "trigger";
    clip.launchQuantize = "none";
    clip.followAction = "none";
    clip.notes.reserve(5000);
    for (int index = 0; index < 5000; ++index)
        clip.notes.push_back({36 + index % 48, 100, index * 0.25, 0.25});
    requireRoundTrip(clip, clipFromJson);
    const auto* clipsGet = OperationRegistry::instance().find("clips.get");
    REQUIRE(clipsGet != nullptr);
    REQUIRE(validateJson(toJson(clip), clipsGet->outputSchema).empty());

    AutomationLaneDto lane;
    lane.id = 5;
    lane.type = "absolute";
    lane.name = "Volume";
    lane.target = {"track_volume", DevicePathDto{3, "fx", true, std::nullopt, {}}, -1, -1, -1, -1};
    lane.points.reserve(5000);
    for (int index = 0; index < 5000; ++index)
        lane.points.push_back({index, index * 0.25, 0.5, "linear"});
    requireRoundTrip(lane, automationLaneFromJson);
    const auto* getLane = OperationRegistry::instance().find("automation.getLane");
    REQUIRE(getLane != nullptr);
    REQUIRE(validateJson(toJson(lane), getLane->outputSchema).empty());
}

TEST_CASE("Remote projections expose only allow-listed state",
          "[remote-api][contract][projection]") {
    // Catch2 runs on a plain thread with no MessageManager; suspend the
    // projections' message-thread assertion for this scope.
    const ScopedMessageThreadAssertionDisabler threadAssertionGuard;
    magda::test::MockMagdaApi api;

    api.project_.info.name = "Safe project";
    api.project_.info.filePath = "/Users/private/secret.mgd";
    api.project_.info.paramAliases = object({{"private", true}});

    TrackInfo track;
    track.id = 1;
    track.name = "Track";
    track.audioInputDevice = "/dev/private-input";
    track.globalModsPanelOpen = true;

    DeviceInfo device;
    device.id = 10;
    device.name = "Instrument";
    device.deviceType = DeviceType::Instrument;
    device.format = PluginFormat::Internal;
    device.isInstrument = true;
    device.fileOrIdentifier = "/Library/Audio/Plug-Ins/private.vst3";
    device.pluginState = "base64-private-state";
    device.aiConversation = "private AI context";
    device.aiPanelOutput = "private AI output";
    track.chain.fxChainElements.push_back(makeDeviceElement(device));

    RackInfo rack;
    rack.id = 20;
    rack.name = "Rack";
    ChainInfo chain;
    chain.id = 30;
    chain.name = "Chain";
    DeviceInfo nestedDevice;
    nestedDevice.id = 11;
    nestedDevice.name = "Effect";
    nestedDevice.deviceType = DeviceType::Effect;
    nestedDevice.format = PluginFormat::VST3;
    chain.elements.push_back(makeDeviceElement(nestedDevice));
    rack.chains.push_back(std::move(chain));
    track.chain.fxChainElements.push_back(makeRackElement(std::move(rack)));
    api.tracks_.tracks.push_back(std::move(track));

    ClipInfo clip;
    clip.id = 40;
    clip.trackId = 1;
    clip.name = "Audio";
    clip.setAudioContent();
    magda::test::giveAudioEvent(clip, "/Users/private/source.wav");
    clip.setPlacementBeats(2.0, 8.0);
    api.clips_.clips.emplace(clip.id, clip);
    api.clips_.clipsOnTrack[1] = {clip.id};

    const auto projectJson =
        juce::JSON::toString(toJson(makeProjectDto(api.project_.info, false, false)));
    const auto trackJson = juce::JSON::toString(toJson(makeTrackDto(api.tracks_.tracks.front())));
    const auto clipJson = juce::JSON::toString(toJson(makeClipDto(api.clips_.clips.at(40))));
    const auto graph = makeDeviceGraphDto(api.tracks_.tracks);
    const auto graphJson = juce::JSON::toString(toJson(graph));

    REQUIRE_FALSE(projectJson.containsIgnoreCase("file"));
    REQUIRE_FALSE(projectJson.contains("secret"));
    REQUIRE_FALSE(trackJson.contains("/dev/private-input"));
    REQUIRE_FALSE(clipJson.containsIgnoreCase("source"));
    REQUIRE_FALSE(clipJson.contains("private"));
    REQUIRE_FALSE(graphJson.contains("fileOrIdentifier"));
    REQUIRE_FALSE(graphJson.contains("pluginState"));
    REQUIRE_FALSE(graphJson.contains("aiConversation"));
    REQUIRE_FALSE(graphJson.contains("private"));
    REQUIRE(graph.devices.size() == 2);
    REQUIRE(graph.racks.size() == 1);
    REQUIRE(graph.chains.size() == 1);

    api.selection_.selectedTrack = 1;
    api.selection_.selectedClip = 40;
    api.selection_.selectedClips = {42, 40};
    const auto selection = makeSelectionDto(api);
    REQUIRE(selection.clipIds == std::vector<ClipId>{40, 42});

    api.transport_.playing = true;
    api.transport_.positionBeats = 6.0;
    REQUIRE(makeTransportDto(api) == TransportDto{true, false, false, 6.0});
}

TEST_CASE("devices.catalog lists addable devices without their file locations",
          "[remote-api][contract][projection]") {
    const ScopedMessageThreadAssertionDisabler threadAssertionGuard;
    magda::test::MockMagdaApi api;

    api.devices_.catalog = {{"4osc", "4OSC", "Tracktion", "Synth", "Four oscillator synth",
                             PluginFormat::Internal, DeviceType::Instrument, true},
                            {"VST3-Reverb-1a2b3c4d-5e6f7a8b", "Reverb", "Acme", "Reverb",
                             "Plate reverb", PluginFormat::VST3, DeviceType::Effect, false}};

    const auto* operation = OperationRegistry::instance().find("devices.catalog");
    REQUIRE(operation != nullptr);
    REQUIRE(operation->access == OperationAccess::Read);
    REQUIRE_FALSE(operation->transportScoped);

    const auto result = operation->handler(api, juce::var(new juce::DynamicObject()), {});
    REQUIRE_FALSE(result.failed());

    // The output has to satisfy the schema the registry publishes, or a client
    // validating against `system.describe` would reject a legitimate response.
    REQUIRE(validateJson(result.value, operation->outputSchema).empty());

    const auto* entries = result.value.getArray();
    REQUIRE(entries != nullptr);
    REQUIRE(entries->size() == 2);
    REQUIRE((*entries)[0]["catalogId"].toString() == "4osc");
    REQUIRE((*entries)[0]["format"].toString() == "internal");
    REQUIRE((*entries)[0]["type"].toString() == "instrument");
    REQUIRE(static_cast<bool>((*entries)[0]["instrument"]));

    // The allow-list, asserted as a set rather than as an absence: what could
    // regress here is a field being *added*, and naming every excluded field is
    // a list nobody would keep current. An external plugin is addressed by its
    // scanned identifier, so nothing on this entry is a filesystem path.
    REQUIRE((*entries)[1]["catalogId"].toString() == "VST3-Reverb-1a2b3c4d-5e6f7a8b");
    std::set<juce::String> fields;
    for (const auto& property : (*entries)[1].getDynamicObject()->getProperties())
        fields.insert(property.name.toString());
    REQUIRE(fields == std::set<juce::String>{"catalogId", "name", "manufacturer", "category",
                                             "description", "format", "type", "instrument"});
}

TEST_CASE("devices.listParameters projects real units and customization flags",
          "[remote-api][contract]") {
    DeviceInfo device;
    device.format = PluginFormat::VST3;
    device.parameters.emplace_back(0, "Cutoff", "Hz", 20.0f, 20000.0f, 800.0f,
                                   ParameterScale::Logarithmic);
    device.parameters.emplace_back(1, "Resonance", "%", 0.0f, 100.0f, 10.0f);
    device.parameters.emplace_back(2, "Mode", "", 0.0f, 2.0f, 0.0f, ParameterScale::Discrete);
    device.parameters[0].stableId = "cutoff";
    device.parameters[0].currentValue = 800.0f;
    device.parameters[1].currentValue = 25.0f;
    device.parameters[2].choices = {"LP", "BP", "HP"};
    device.visibleParameters = {0, 1};
    device.miniMixerParameters = {1};
    device.aiSoundDesignerParameters = {1};

    const auto parameters = makeDeviceParameterDtos(device, {});
    REQUIRE(parameters.size() == 3);

    REQUIRE(parameters[0].index == 0);
    REQUIRE(parameters[0].stableId == "cutoff");
    REQUIRE(parameters[0].name == "Cutoff");
    REQUIRE(parameters[0].unit == "Hz");
    REQUIRE(parameters[0].minValue == 20.0);
    REQUIRE(parameters[0].maxValue == 20000.0);
    REQUIRE(parameters[0].currentValue == 800.0);
    REQUIRE(parameters[0].visible);
    REQUIRE_FALSE(parameters[0].miniMixer);

    // The user ticked only Resonance for AI control.
    REQUIRE_FALSE(parameters[0].aiAgentEnabled);
    REQUIRE(parameters[1].aiAgentEnabled);
    REQUIRE(parameters[1].miniMixer);
    REQUIRE(std::abs(parameters[1].normalizedValue - 0.25) < 1e-6);

    // A discrete parameter is interpretable, not just a number in a range.
    REQUIRE(parameters[0].scale == ParameterScale::Logarithmic);
    REQUIRE(parameters[1].scale == ParameterScale::Linear);
    REQUIRE(parameters[1].choices.empty());
    REQUIRE(parameters[2].scale == ParameterScale::Discrete);
    REQUIRE(parameters[2].choices == std::vector<juce::String>{"LP", "BP", "HP"});

    requireRoundTrip(parameters[0], deviceParameterFromJson);
    requireRoundTrip(parameters[1], deviceParameterFromJson);
    requireRoundTrip(parameters[2], deviceParameterFromJson);
}

TEST_CASE("configured external parameters project model values into display units",
          "[remote-api][contract]") {
    // The realistic external-override shape: the plugin's TE parameter runs
    // 0..1 and keeps storing TE-native values, while a saved config gave the
    // parameter a real display range. Its explicit convention keeps the model
    // in normalized TE-native values even when no live display provider is
    // available (the shape of a parameter mirrored into project state).
    DeviceInfo device;
    device.format = PluginFormat::VST3;
    ParameterInfo gain(0, "Gain", "dB", -24.0f, 24.0f, 0.0f);
    gain.teMinValue = 0.0f;
    gain.teMaxValue = 1.0f;
    gain.valueConvention = ParameterValueConvention::Normalized;
    gain.currentValue = 0.75f;  // model / TE domain
    gain.defaultValue = 0.5f;   // model / TE domain
    device.parameters.push_back(gain);

    const auto parameters = makeDeviceParameterDtos(device, {});
    REQUIRE(parameters.size() == 1);
    // 0.75 of a -24..24 dB range reads +12 dB, not 0.75 dB.
    REQUIRE(std::abs(parameters[0].currentValue - 12.0) < 1e-4);
    REQUIRE(std::abs(parameters[0].normalizedValue - 0.75) < 1e-6);
    REQUIRE(std::abs(parameters[0].defaultValue - 0.0) < 1e-4);
}

TEST_CASE("internal devices accept agent writes on every parameter", "[remote-api][contract]") {
    // Configure Parameters offers the AI opt-in only for external plugins, so
    // an internal device with no allowlist is fully controllable, not locked.
    DeviceInfo device;
    device.format = PluginFormat::Internal;
    device.parameters.emplace_back(0, "Drive", "", 0.0f, 1.0f, 0.5f);
    device.parameters.emplace_back(1, "Tone", "", 0.0f, 1.0f, 0.5f);

    const auto parameters = makeDeviceParameterDtos(device, {});
    REQUIRE(parameters.size() == 2);
    for (const auto& parameter : parameters)
        REQUIRE(parameter.aiAgentEnabled);
}

TEST_CASE("devices.list addresses colliding fx and post-fx device ids",
          "[remote-api][contract][device-path]") {
    // Post-fx devices allocate from nextPostFxDeviceId_, the fx chain from
    // nextFxDeviceId_, so id 3 legitimately exists in both on one track. Both
    // project with rackId and chainId null, so before devicePath the two rows
    // were identical addresses.
    TrackInfo track;
    track.id = 1;

    DeviceInfo fxDevice;
    fxDevice.id = 3;
    fxDevice.name = "Chorus";
    track.chain.fxChainElements.push_back(makeDeviceElement(fxDevice));

    DeviceInfo postFxDevice;
    postFxDevice.id = 3;
    postFxDevice.name = "Limiter";
    track.chain.postFxChainElements.push_back({postFxDevice});

    const auto graph = makeDeviceGraphDto({track});
    REQUIRE(graph.devices.size() == 2);
    REQUIRE(graph.devices[0].id == graph.devices[1].id);
    REQUIRE(graph.devices[0].rackId == graph.devices[1].rackId);
    REQUIRE(graph.devices[0].chainId == graph.devices[1].chainId);

    // The path is what tells them apart.
    REQUIRE(graph.devices[0].devicePath.section == "fx");
    REQUIRE(graph.devices[1].devicePath.section == "post_fx");
    REQUIRE_FALSE(graph.devices[0].devicePath == graph.devices[1].devicePath);

    REQUIRE(toChainNodePath(graph.devices[0].devicePath) == ChainNodePath::topLevelDevice(1, 3));
    REQUIRE(toChainNodePath(graph.devices[1].devicePath) == ChainNodePath::postFxDevice(1, 3));

    requireRoundTrip(graph, deviceGraphFromJson);
}

TEST_CASE("devices.list carries full depth for nested racks",
          "[remote-api][contract][device-path]") {
    // rackId/chainId name the immediate parent only; a device two racks deep
    // needs the whole route to be addressable.
    TrackInfo track;
    track.id = 1;

    DeviceInfo leaf;
    leaf.id = 9;
    leaf.name = "Filter";

    RackInfo inner;
    inner.id = 7;
    ChainInfo innerChain;
    innerChain.id = 8;
    innerChain.elements.push_back(makeDeviceElement(leaf));
    inner.chains.push_back(std::move(innerChain));

    RackInfo outer;
    outer.id = 2;
    ChainInfo outerChain;
    outerChain.id = 4;
    outerChain.elements.push_back(makeRackElement(std::move(inner)));
    outer.chains.push_back(std::move(outerChain));

    track.chain.fxChainElements.push_back(makeRackElement(std::move(outer)));

    const auto graph = makeDeviceGraphDto({track});
    REQUIRE(graph.devices.size() == 1);

    const auto& device = graph.devices.front();
    REQUIRE(device.rackId == 7);
    REQUIRE(device.chainId == 8);
    REQUIRE(toChainNodePath(device.devicePath) ==
            ChainNodePath::chain(1, 2, 4).withRack(7).withChain(8).withDevice(9));

    REQUIRE(graph.racks.size() == 2);
    REQUIRE(graph.chains.size() == 2);
    REQUIRE(toChainNodePath(graph.racks[0].nodePath) == ChainNodePath::rack(1, 2));
    REQUIRE(toChainNodePath(graph.racks[1].nodePath) == ChainNodePath::chain(1, 2, 4).withRack(7));
    const auto hasChainPath = [&graph](const ChainNodePath& path) {
        return std::ranges::contains(graph.chains, makeDevicePathDto(path), &ChainDto::nodePath);
    };
    REQUIRE(hasChainPath(ChainNodePath::chain(1, 2, 4)));
    REQUIRE(hasChainPath(ChainNodePath::chain(1, 2, 4).withRack(7).withChain(8)));

    requireRoundTrip(graph, deviceGraphFromJson);
}

TEST_CASE("Rack and chain writes have closed path-addressed contracts",
          "[remote-api][contract][racks][chains]") {
    const auto& registry = OperationRegistry::instance();
    const auto rackPath = toJson(makeDevicePathDto(ChainNodePath::rack(1, 2)));
    const auto chainPath = toJson(makeDevicePathDto(ChainNodePath::chain(1, 2, 3)));

    const auto requireEditOperation = [&](const char* name) -> const OperationDescriptor& {
        const auto* operation = registry.find(name);
        REQUIRE(operation != nullptr);
        CHECK(operation->access == OperationAccess::Write);
        CHECK(operation->requiredScope == Scope::Edit);
        return *operation;
    };

    const auto& rackCreate = requireEditOperation("racks.create");
    CHECK(validateJson(object({{"parentPath", chainPath}, {"name", "Nested"}}),
                       rackCreate.inputSchema)
              .empty());
    CHECK(validateJson(object({{"trackId", 1}, {"name", "Top"}}), rackCreate.inputSchema).empty());
    CHECK_FALSE(validateJson(object({{"name", "Ownerless"}}), rackCreate.inputSchema).empty());
    CHECK_FALSE(
        validateJson(object({{"trackId", 1}, {"parentPath", chainPath}, {"name", "Ambiguous"}}),
                     rackCreate.inputSchema)
            .empty());

    const auto& rackUpdate = requireEditOperation("racks.update");
    CHECK(validateJson(object({{"rackPath", rackPath}, {"volumeDb", -3.0}}), rackUpdate.inputSchema)
              .empty());
    CHECK(validateJson(object({{"rackPath", rackPath}}), rackUpdate.inputSchema).empty());
    CHECK_FALSE(validateJson(object({{"rackPath", rackPath}, {"pan", 0.5}}), rackUpdate.inputSchema)
                    .empty());

    requireEditOperation("racks.remove");
    const auto& chainCreate = requireEditOperation("chains.create");
    CHECK(validateJson(object({{"rackPath", rackPath}, {"name", "Parallel"}}),
                       chainCreate.inputSchema)
              .empty());
    const auto& chainUpdate = requireEditOperation("chains.update");
    CHECK(validateJson(object({{"chainPath", chainPath}, {"name", "Wet"}, {"pan", -0.5}}),
                       chainUpdate.inputSchema)
              .empty());
    CHECK(validateJson(object({{"chainPath", chainPath}}), chainUpdate.inputSchema).empty());
    CHECK_FALSE(
        validateJson(object({{"chainPath", chainPath}, {"pan", 2.0}}), chainUpdate.inputSchema)
            .empty());
    requireEditOperation("chains.remove");

    magda::test::MockMagdaApi api;
    const auto emptyPatch = rackUpdate.handler(api, object({{"rackPath", rackPath}}), {});
    REQUIRE(emptyPatch.failed());
    CHECK(emptyPatch.error->code == ErrorCode::ValidationFailed);
    const auto emptyChainPatch = chainUpdate.handler(api, object({{"chainPath", chainPath}}), {});
    REQUIRE(emptyChainPatch.failed());
    CHECK(emptyChainPatch.error->code == ErrorCode::ValidationFailed);
    const auto wrongNode =
        rackUpdate.handler(api, object({{"rackPath", chainPath}, {"bypassed", true}}), {});
    REQUIRE(wrongNode.failed());
    CHECK(wrongNode.error->code == ErrorCode::ValidationFailed);
    CHECK(api.undo_.executeCalls == 0);
}

TEST_CASE("Drum Grid pad discovery returns empty slots and addressable child devices",
          "[remote-api][contract][pads]") {
    TrackInfo track;
    track.id = 1;
    DeviceInfo grid;
    grid.id = 7;
    grid.pluginId = "drumgrid";
    grid.name = "Drum Grid";
    auto& pads = ensurePads(grid);
    auto& kick = ensurePadChain(pads, 0);
    DeviceInfo sampler;
    sampler.id = 11;
    sampler.name = "Sampler";
    kick.elements.push_back(makeDeviceElement(sampler));
    const auto chainId = kick.id;
    track.chain.fxChainElements.push_back(makeDeviceElement(grid));

    const auto gridPath = ChainNodePath::topLevelDevice(1, 7);
    const auto slots = makePadDtos(grid, gridPath);
    REQUIRE(slots.size() == 64);
    REQUIRE(slots[0].populated);
    REQUIRE(slots[0].chainId == chainId);
    REQUIRE(toChainNodePath(*slots[0].chainPath) == ChainNodePath::padChain(1, 7, chainId));
    REQUIRE(slots[0].devicePaths.size() == 1);
    REQUIRE(toChainNodePath(slots[0].devicePaths.front()) ==
            ChainNodePath::padChain(1, 7, chainId).withDevice(11));
    REQUIRE_FALSE(slots[1].populated);
    REQUIRE_FALSE(slots[1].chainPath);

    const auto graph = makeDeviceGraphDto({track});
    REQUIRE(graph.devices.size() == 2);
    REQUIRE(graph.pads.size() == 64);
    REQUIRE(graph.pads[0].gridPath == makeDevicePathDto(gridPath));
    requireRoundTrip(graph, deviceGraphFromJson);
    auto changed = track;
    magda::getDevice(changed.chain.fxChainElements.front()).pads->chains.front().volume = -3.0f;
    REQUIRE(makeDeviceGraphDto({changed}) != graph);
    REQUIRE(toChainNodePath(graph.devices[1].devicePath) ==
            ChainNodePath::padChain(1, 7, chainId).withDevice(11));
    const auto* list = OperationRegistry::instance().find("pads.list");
    REQUIRE(list != nullptr);
    for (const auto& slot : slots)
        REQUIRE(validateJson(toJson(slot), list->outputSchema["items"]).empty());
}

TEST_CASE("Device paths distinguish the three per-section DeviceId spaces",
          "[remote-api][contract][device-path]") {
    // The main FX chain, the post-fader list, and the mixer-analysis section
    // each allocate DeviceIds from their own counter, so the same id exists in
    // all three at once. A projection that carried only trackId + deviceId
    // collapsed them into one address.
    const auto fx = ChainNodePath::topLevelDevice(1, 3);
    const auto postFx = ChainNodePath::postFxDevice(1, 3);
    const auto analysis = ChainNodePath::mixerAnalysisDevice(1, 3);

    const auto fxDto = makeDevicePathDto(fx);
    const auto postFxDto = makeDevicePathDto(postFx);
    const auto analysisDto = makeDevicePathDto(analysis);

    REQUIRE(fxDto.section == "fx");
    REQUIRE(postFxDto.section == "post_fx");
    REQUIRE(analysisDto.section == "mixer_analysis");

    REQUIRE_FALSE(fxDto == postFxDto);
    REQUIRE_FALSE(postFxDto == analysisDto);
    REQUIRE_FALSE(fxDto == analysisDto);

    // ...and each survives the trip back to an internal path.
    REQUIRE(toChainNodePath(fxDto) == fx);
    REQUIRE(toChainNodePath(postFxDto) == postFx);
    REQUIRE(toChainNodePath(analysisDto) == analysis);
}

TEST_CASE("Device paths round-trip through nested racks and chains",
          "[remote-api][contract][device-path]") {
    SECTION("track level") {
        const auto path = ChainNodePath::trackLevel(2);
        const auto dto = makeDevicePathDto(path);
        REQUIRE(dto.trackLevel);
        REQUIRE(toChainNodePath(dto) == path);
    }

    SECTION("device inside a rack chain") {
        const auto path = ChainNodePath::chainDevice(2, 4, 5, 6);
        const auto dto = makeDevicePathDto(path);
        REQUIRE(dto.steps.size() == 3);
        REQUIRE(toChainNodePath(dto) == path);
    }

    SECTION("arbitrarily nested racks") {
        const auto path = ChainNodePath::chain(2, 4, 5).withRack(7).withChain(8).withDevice(9);
        const auto dto = makeDevicePathDto(path);
        REQUIRE(toChainNodePath(dto) == path);
    }

    SECTION("pad-owned steps") {
        // A pad address names its owning Drum Grid by that device's own id, and
        // the two step names say so rather than leaving a client to infer
        // ownership from position.
        const auto path = ChainNodePath::padChain(2, 9, 5).withDevice(6);
        const auto dto = makeDevicePathDto(path);
        REQUIRE(dto.steps.size() == 3);
        REQUIRE(dto.steps[0].type == "pad_rack");
        REQUIRE(dto.steps[0].id == 9);
        REQUIRE(dto.steps[1].type == "pad_chain");
        REQUIRE(dto.steps[1].id == 5);
        REQUIRE(toChainNodePath(dto) == path);

        // Including through a rack nested inside the pad chain.
        const auto nested =
            ChainNodePath::padChain(2, 9, 5).withRack(7).withChain(8).withDevice(10);
        REQUIRE(toChainNodePath(makeDevicePathDto(nested)) == nested);

        // And the published schema admits what the projection emits, so the
        // output does not violate the contract and a client can send the same
        // address back.
        const auto* get = OperationRegistry::instance().find("devices.list");
        REQUIRE(get != nullptr);
        const DeviceGraphDto graph{{{6,
                                     2,
                                     std::nullopt,
                                     std::nullopt,
                                     dto,
                                     "Kick",
                                     "instrument",
                                     "internal",
                                     true,
                                     false,
                                     0.0,
                                     {}}},
                                   {},
                                   {}};
        REQUIRE(validateJson(toJson(graph), get->outputSchema).empty());
    }

    SECTION("unknown section and step types are rejected, not guessed") {
        auto dto = makeDevicePathDto(ChainNodePath::chainDevice(2, 4, 5, 6));
        dto.section = "not_a_section";
        REQUIRE_FALSE(toChainNodePath(dto).has_value());

        auto stepDto = makeDevicePathDto(ChainNodePath::chainDevice(2, 4, 5, 6));
        stepDto.steps[0].type = "not_a_step";
        REQUIRE_FALSE(toChainNodePath(stepDto).has_value());
    }
}

TEST_CASE("Automation targets carry a full device path through JSON",
          "[remote-api][contract][device-path]") {
    AutomationLaneDto lane;
    lane.id = 5;
    lane.type = "absolute";
    lane.name = "Cutoff";
    lane.target.kind = "plugin_param";
    lane.target.devicePath = makeDevicePathDto(ChainNodePath::postFxDevice(1, 3));
    lane.target.parameterIndex = 2;

    requireRoundTrip(lane, automationLaneFromJson);

    const auto* getLane = OperationRegistry::instance().find("automation.getLane");
    REQUIRE(getLane != nullptr);
    REQUIRE(validateJson(toJson(lane), getLane->outputSchema).empty());

    // The same parameter index on the fx-chain device is a different target.
    AutomationLaneDto fxLane = lane;
    fxLane.target.devicePath = makeDevicePathDto(ChainNodePath::topLevelDevice(1, 3));
    REQUIRE_FALSE(toJson(lane).toString() == toJson(fxLane).toString());
}

TEST_CASE("Edit-scoped automation targets carry no device path",
          "[remote-api][contract][device-path]") {
    AutomationLaneDto lane;
    lane.id = 6;
    lane.type = "absolute";
    lane.name = "Tempo";
    lane.target.kind = "tempo";
    lane.target.devicePath = std::nullopt;

    requireRoundTrip(lane, automationLaneFromJson);

    const auto* getLane = OperationRegistry::instance().find("automation.getLane");
    REQUIRE(getLane != nullptr);
    REQUIRE(validateJson(toJson(lane), getLane->outputSchema).empty());
}

TEST_CASE("Remote session and automation projections use MagdaApi values",
          "[remote-api][contract][projection]") {
    const ScopedMessageThreadAssertionDisabler threadAssertionGuard;
    magda::test::MockMagdaApi api;
    TrackInfo track;
    track.id = 1;
    api.tracks_.tracks.push_back(track);

    ClipInfo sessionClip;
    sessionClip.id = 50;
    sessionClip.trackId = 1;
    sessionClip.view = ClipView::Session;
    sessionClip.sceneIndex = 3;
    api.clips_.clips.emplace(50, sessionClip);
    api.clips_.clipsOnTrack[1] = {50};
    api.session_.clipStates[50] = SessionClipPlayState::Queued;

    const auto session = makeSessionDto(api);
    REQUIRE(session.slots == std::vector<SessionSlotDto>{{1, 3, 50, "queued"}});

    AutomationLaneInfo lane;
    lane.id = 60;
    lane.type = AutomationLaneType::Absolute;
    lane.name = "Pan";
    lane.target = AutomationTarget::trackPan(1);
    AutomationPoint firstPoint;
    firstPoint.id = 70;
    firstPoint.beatPosition = 0.0;
    firstPoint.value = 0.5;
    firstPoint.curveType = AutomationCurveType::Linear;
    AutomationPoint secondPoint;
    secondPoint.id = 71;
    secondPoint.beatPosition = 4.0;
    secondPoint.value = 0.75;
    secondPoint.curveType = AutomationCurveType::Step;
    lane.absolutePoints = {firstPoint, secondPoint};
    const auto dto = makeAutomationLaneDto(lane);
    REQUIRE(dto.id == 60);
    REQUIRE(dto.target.kind == "track_pan");
    REQUIRE(dto.target.devicePath.has_value());
    REQUIRE(dto.target.devicePath->trackId == 1);
    REQUIRE(dto.points.size() == 2);
    REQUIRE(dto.points[1].curve == "step");

    lane.target = AutomationTarget::trackVolume(MASTER_TRACK_ID);
    const auto masterDto = makeAutomationLaneDto(lane);
    REQUIRE(masterDto.target.devicePath.has_value());
    REQUIRE(masterDto.target.devicePath->trackId == MASTER_TRACK_ID);
    const auto* operation = OperationRegistry::instance().find("automation.getLane");
    REQUIRE(operation != nullptr);
    REQUIRE(validateJson(toJson(masterDto), operation->outputSchema).empty());
}
