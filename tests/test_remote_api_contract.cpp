#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <limits>
#include <set>

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

    SECTION("strings and arrays have contract-wide limits") {
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

    SECTION("unknown field") {
        const auto* operation = registry.find("transport.seek");
        REQUIRE(operation != nullptr);
        const auto error =
            validateOperationInput(*operation, object({{"positionBeats", 4.0}, {"seconds", 2.0}}));
        REQUIRE(error.has_value());
        REQUIRE(error->issues.front().path == "$.seconds");
        REQUIRE(error->issues.front().code == "unknown_field");
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
        {{10, 3, 20, 30, "Synth", "instrument", "internal", true, false, -3.0}},
        {{20, 3, std::nullopt, std::nullopt, "Parallel", false, 0.0, 0.0, {30}}},
        {{30, 20, "Main", 0, false, false, false, 0.0, 0.0, {10}, {}}}};
    requireRoundTrip(graph, deviceGraphFromJson);

    const SelectionDto selection{3, 9, {9, 12}, 5, 6, 9, {0, 2}};
    requireRoundTrip(selection, selectionFromJson);

    const TransportDto transport{true, false, true, 16.5};
    requireRoundTrip(transport, transportFromJson);

    const SessionDto session{{{3, 2, 9, "playing"}, {4, 2, 12, "queued"}}};
    requireRoundTrip(session, sessionFromJson);

    const AutomationLaneDto lane{5,
                                 "absolute",
                                 "Volume",
                                 {"track_volume", 3, std::nullopt, -1, -1, -1, -1},
                                 {{7, 0.0, 0.5, "linear"}, {8, 4.0, 0.75, "bezier"}},
                                 {}};
    requireRoundTrip(lane, automationLaneFromJson);
}

TEST_CASE("Remote projections expose only allow-listed state",
          "[remote-api][contract][projection]") {
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
    clip.audio().source.filePath = "/Users/private/source.wav";
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

TEST_CASE("Remote session and automation projections use MagdaApi values",
          "[remote-api][contract][projection]") {
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
    REQUIRE(dto.target.trackId == 1);
    REQUIRE(dto.points.size() == 2);
    REQUIRE(dto.points[1].curve == "step");

    lane.target = AutomationTarget::trackVolume(MASTER_TRACK_ID);
    const auto masterDto = makeAutomationLaneDto(lane);
    REQUIRE(masterDto.target.trackId == MASTER_TRACK_ID);
    const auto* operation = OperationRegistry::instance().find("automation.getLane");
    REQUIRE(operation != nullptr);
    REQUIRE(validateJson(toJson(masterDto), operation->outputSchema).empty());
}
