#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <limits>
#include <set>

#include "AudioClipTestHelpers.hpp"
#include "MockMagdaApi.hpp"
#include "magda/daw/api/remote_api.hpp"
#include "magda/daw/core/AutomationInfo.hpp"
#include "magda/daw/core/DeviceInfo.hpp"
#include "magda/daw/core/RackInfo.hpp"

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
    REQUIRE(registry.find("devices.list") != nullptr);
    REQUIRE(registry.find("session.launchClip") != nullptr);
    REQUIRE(registry.find("automation.addPoint") != nullptr);
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
                               -3.0};
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
        const Error error{ErrorCode::NotFound, "No such clip", {{"$.clipId", "not_found", "42"}}};
        const auto envelope = errorEnvelope(error);
        REQUIRE(static_cast<bool>(envelope["ok"]) == false);
        REQUIRE(envelope["apiVersion"].toString() == "1.0");
        REQUIRE(envelope["error"]["code"].toString() == "not_found");
        REQUIRE(envelope["error"]["issues"].getArray()->size() == 1);
    }
}

TEST_CASE("Remote API DTOs round-trip through JSON", "[remote-api][contract][dto]") {
    const ProjectDto project{"Demo", 128.0, 7, 8, 48000.0, 128, 9, "minor", true, 4.0, 12.0};
    requireRoundTrip(project, projectFromJson);

    const TrackDto track{3,     "audio",   "Bass",   0xff102030, std::nullopt, {4, 5},
                         0.8,   -0.25,     false,    true,       true,         false,
                         "all", "track:2", "master", ""};
    requireRoundTrip(track, trackFromJson);

    const ClipDto clip{9,         3,
                       "midi",    "session",
                       "Pattern", 0xffaabbcc,
                       0.0,       4.0,
                       true,      2,
                       "trigger", "1_bar",
                       "next",    {{60, 110, 0.0, 0.5}, {64, 100, 1.0, 0.5}}};
    requireRoundTrip(clip, clipFromJson);

    const DeviceGraphDto graph{
        {{10, 3, 20, 30, makeDevicePathDto(ChainNodePath::chainDevice(3, 20, 30, 10)), "Synth",
          "instrument", "internal", true, false, -3.0}},
        {{20, 3, std::nullopt, std::nullopt, "Parallel", false, 0.0, 0.0, {30}}},
        {{30, 20, "Main", 0, false, false, false, 0.0, 0.0, {10}, {}}}};
    requireRoundTrip(graph, deviceGraphFromJson);

    const DeviceCatalogEntryDto catalogEntry{
        "4osc",     "4OSC",       "Tracktion", "Synth", "Four oscillator synth",
        "internal", "instrument", true};
    requireRoundTrip(catalogEntry, deviceCatalogEntryFromJson);

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

    const auto projectJson = juce::JSON::toString(toJson(makeProjectDto(api.project_.info)));
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

    requireRoundTrip(graph, deviceGraphFromJson);
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
