// The MCP protocol layer (#1858), with no sockets involved. What a request
// becomes on the wire — headers, status codes, SSE — belongs to
// test_remote_mcp_server.cpp; this is the mapping from MAGDA's shared operation
// registry into MCP's vocabulary, and back.

#include <catch2/catch_test_macros.hpp>
#include <set>
#include <vector>

#include "MockMagdaApi.hpp"
#include "magda/daw/api/remote_mcp.hpp"
#include "magda/daw/api/remote_service.hpp"
#include "magda/daw/api/remote_subscriptions.hpp"

using namespace magda;
using namespace magda::remote;
using magda::test::MockMagdaApi;

namespace {

/// Reads MagdaApi live state, which asserts the message thread. The Catch2
/// runner has no MessageManager, so suspend that assertion — the same
/// accommodation the other remote tests make. With no message thread to hop to,
/// dispatch runs inline, which makes every call below synchronous.
struct MessageThreadRelaxation {
    ScopedMessageThreadAssertionDisabler disabler;
};

juce::var object(std::initializer_list<std::pair<const char*, juce::var>> fields) {
    auto* result = new juce::DynamicObject();
    for (const auto& [key, value] : fields)
        result->setProperty(key, value);
    return result;
}

/**
 * @brief The fields every call here shares.
 *
 * Assigned by name rather than positionally. `Call` is a plain aggregate, so a
 * braced list silently re-maps when a field is inserted — which is exactly what
 * happened when `clientName` and `scopes` landed in the middle of it (#1860) and
 * `idempotencyScope` quietly became the client's name.
 *
 * `scopes` is every scope: these tests are about how operations project into
 * tools and resources, not about who may call them. The permission behaviour has
 * its own files.
 */
void fillCommon(McpEndpoint::Call& call, const juce::String& method, juce::var params) {
    call.method = method;
    call.params = std::move(params);
    call.clientId = "mcp-test";
    call.clientName = "mcp-test";
    call.scopes = allScopes();
}

/// A modern-era call carrying the `_meta` every 2026-07-28 request must have.
McpEndpoint::Call modernCall(const juce::String& method, juce::var params = {}) {
    if (params.getDynamicObject() == nullptr)
        params = juce::var(new juce::DynamicObject());
    auto meta = params["_meta"];
    if (meta.getDynamicObject() == nullptr) {
        meta = object({{MCP_META_PROTOCOL_VERSION, "2026-07-28"},
                       {MCP_META_CLIENT_CAPABILITIES, juce::var(new juce::DynamicObject())}});
        params.getDynamicObject()->setProperty("_meta", meta);
    }

    McpEndpoint::Call call;
    fillCommon(call, method, std::move(params));
    call.era = McpEra::Modern;
    call.protocolVersion = "2026-07-28";
    return call;
}

McpEndpoint::Call legacyCall(const juce::String& method, juce::var params = {}) {
    if (params.getDynamicObject() == nullptr)
        params = juce::var(new juce::DynamicObject());

    McpEndpoint::Call call;
    fillCommon(call, method, std::move(params));
    call.era = McpEra::Legacy;
    call.protocolVersion = "2025-11-25";
    call.idempotencyScope = "session-1";
    return call;
}

/// Runs one call to completion. Dispatch is inline here, so this is synchronous.
McpReply run(McpEndpoint& endpoint, const McpEndpoint::Call& call) {
    McpReply captured;
    int completions = 0;
    endpoint.handle(call, [&](McpReply reply) {
        captured = std::move(reply);
        ++completions;
    });
    REQUIRE(completions == 1);
    return captured;
}

struct Harness {
    MessageThreadRelaxation relaxation;
    MockMagdaApi api;
    RemoteApiService service{api};
    McpEndpoint endpoint{service, McpEndpoint::Options{}};
};

}  // namespace

TEST_CASE("MCP version negotiation is a table, not a pinned constant", "[remote-api][mcp]") {
    // The acceptance criterion from #1858: negotiation must not be hardcoded to
    // the 2024-11-05 the outbound MCPClient asks for.
    const auto& versions = mcpProtocolVersions();
    REQUIRE(versions.size() >= 2);
    REQUIRE(versions.front() == "2026-07-28");
    for (const auto& version : versions)
        REQUIRE(version != "2024-11-05");

    REQUIRE(isSupportedVersion("2026-07-28"));
    REQUIRE(isSupportedVersion("2025-11-25"));
    REQUIRE_FALSE(isSupportedVersion("2024-11-05"));
    REQUIRE_FALSE(isSupportedVersion("1900-01-01"));

    // The date format is ordered, which is what lets one comparison separate the
    // stateless revision from every handshake-based one.
    REQUIRE(eraForVersion("2026-07-28") == McpEra::Modern);
    REQUIRE(eraForVersion("2027-01-01") == McpEra::Modern);
    REQUIRE(eraForVersion("2025-11-25") == McpEra::Legacy);
    REQUIRE(eraForVersion("2025-06-18") == McpEra::Legacy);
}

TEST_CASE("A legacy initialize is answered with a version it can use", "[remote-api][mcp]") {
    REQUIRE(negotiateLegacyVersion(object({{"protocolVersion", "2025-11-25"}})) == "2025-11-25");
    REQUIRE(negotiateLegacyVersion(object({{"protocolVersion", "2025-06-18"}})) == "2025-06-18");

    // Unknown version: counter-offer rather than refuse. A legacy client has no
    // way to fall forward, so an error would end the conversation.
    const auto counter = negotiateLegacyVersion(object({{"protocolVersion", "1900-01-01"}}));
    REQUIRE(isSupportedVersion(counter));
    REQUIRE(eraForVersion(counter) == McpEra::Legacy);

    // Asking for a modern version over `initialize` is an era confusion, not a
    // version one: answering "2026-07-28" would promise a handshake that
    // revision removed.
    REQUIRE(eraForVersion(negotiateLegacyVersion(object({{"protocolVersion", "2026-07-28"}}))) ==
            McpEra::Legacy);
}

TEST_CASE("Subscription methods route to the transport, not the endpoint", "[remote-api][mcp]") {
    REQUIRE(routeFor("tools/call", McpEra::Modern) == McpRouting::Endpoint);
    REQUIRE(routeFor("subscriptions/listen", McpEra::Modern) == McpRouting::Stream);
    REQUIRE(routeFor("resources/subscribe", McpEra::Legacy) == McpRouting::Subscribe);
    REQUIRE(routeFor("resources/unsubscribe", McpEra::Legacy) == McpRouting::Unsubscribe);

    // Each era knows only its own verbs. `subscriptions/listen` did not exist
    // before 2026-07-28, and `resources/subscribe` does not exist after it, so
    // reaching one from the wrong era is an unknown method rather than a
    // silently accepted no-op.
    REQUIRE(routeFor("subscriptions/listen", McpEra::Legacy) == McpRouting::Endpoint);
    REQUIRE(routeFor("resources/subscribe", McpEra::Modern) == McpRouting::Endpoint);
}

TEST_CASE("Every executable operation is exposed as a tool, and nothing else is",
          "[remote-api][mcp]") {
    Harness harness;
    const auto& tools = harness.endpoint.tools();

    std::set<juce::String> toolNames;
    for (const auto& tool : tools) {
        const auto name = tool["name"].toString();
        REQUIRE(toolNames.insert(name).second);

        // MCP names may contain letters, digits, underscore, hyphen, and dot,
        // which is what lets the registry's own dotted names survive unchanged.
        REQUIRE(name.isNotEmpty());
        REQUIRE(name.length() <= 128);
        REQUIRE(
            name.containsOnly("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-."));

        REQUIRE(tool["description"].toString().isNotEmpty());

        // Both schemas must be object schemas. MCP constrains `inputSchema` and
        // `outputSchema` to `type: "object"`, and a client validates the whole
        // `tools/list` response against that — so a single tool declaring an
        // array-valued output does not go merely unvalidated, it rejects the
        // entire response and the server appears to have no tools at all. A real
        // host did exactly that over `tracks.list`, which is why the four
        // array-valued operations are wrapped rather than declared as they are.
        REQUIRE(tool["inputSchema"]["type"].toString() == "object");
        REQUIRE(tool["outputSchema"]["type"].toString() == "object");
    }

    std::set<juce::String> expected;
    for (const auto& operation : OperationRegistry::instance().operations()) {
        if (!operation.transportScoped)
            expected.insert(operation.name);
    }
    REQUIRE(toolNames == expected);

    // Transport-scoped operations are real and advertised by system.describe,
    // but MCP subscribes through its own verbs. Listing them as tools would
    // advertise a second route that cannot work.
    REQUIRE(toolNames.count("subscriptions.subscribe") == 0);
    REQUIRE(toolNames.count("tracks.list") == 1);
    REQUIRE(toolNames.count("devices.catalog") == 1);
}

TEST_CASE("Tool schemas are the registry's own, not a copy", "[remote-api][mcp]") {
    Harness harness;
    const auto* operation = OperationRegistry::instance().find("clips.addMidiNote");
    REQUIRE(operation != nullptr);

    const juce::var* tool = nullptr;
    for (const auto& candidate : harness.endpoint.tools()) {
        if (candidate["name"].toString() == "clips.addMidiNote")
            tool = &candidate;
    }
    REQUIRE(tool != nullptr);

    // Byte-identical, which is the property that keeps a WebSocket client and an
    // MCP client validating against the same contract.
    REQUIRE(juce::JSON::toString((*tool)["inputSchema"]) ==
            juce::JSON::toString(operation->inputSchema));
    REQUIRE(juce::JSON::toString((*tool)["outputSchema"]) ==
            juce::JSON::toString(operation->outputSchema));

    // readOnlyHint is derived from OperationAccess rather than guessed.
    REQUIRE_FALSE(static_cast<bool>((*tool)["annotations"]["readOnlyHint"]));
    for (const auto& candidate : harness.endpoint.tools()) {
        const auto* declared = OperationRegistry::instance().find(candidate["name"].toString());
        REQUIRE(declared != nullptr);
        REQUIRE(static_cast<bool>(candidate["annotations"]["readOnlyHint"]) ==
                (declared->access == OperationAccess::Read));
    }
}

TEST_CASE("An array-valued operation is wrapped, schema and result together", "[remote-api][mcp]") {
    Harness harness;

    // MCP types `outputSchema` and `structuredContent` as JSON objects, and a
    // client validates both. Two real-host failures came from this: declaring an
    // array schema rejected the whole `tools/list`, and then returning a bare
    // array rejected every list *call*. Wrapping fixes both, and has to be done
    // in both places or they describe different shapes.
    for (const char* name :
         {"tracks.list", "clips.list", "devices.catalog", "automation.listLanes"}) {
        const auto* operation = OperationRegistry::instance().find(name);
        REQUIRE(operation != nullptr);
        REQUIRE(operation->outputSchema["type"].toString() == "array");

        const juce::var* tool = nullptr;
        for (const auto& candidate : harness.endpoint.tools())
            if (candidate["name"].toString() == name)
                tool = &candidate;
        REQUIRE(tool != nullptr);

        const auto schema = (*tool)["outputSchema"];
        REQUIRE(schema["type"].toString() == "object");
        // The array survives intact underneath, so nothing about the element
        // shape is lost by wrapping.
        REQUIRE(schema["properties"]["items"]["type"].toString() == "array");
        REQUIRE(juce::JSON::toString(schema["properties"]["items"]) ==
                juce::JSON::toString(operation->outputSchema));
    }

    TrackInfo track;
    track.id = 1;
    track.name = "Bass";
    harness.api.tracks_.tracks.push_back(track);

    const auto reply =
        run(harness.endpoint, modernCall("tools/call", object({{"name", "tracks.list"}})));
    REQUIRE_FALSE(reply.failed());
    REQUIRE_FALSE(static_cast<bool>(reply.result["isError"]));

    // An object, not an array — the thing a host actually enforces.
    const auto structured = reply.result["structuredContent"];
    REQUIRE_FALSE(structured.isArray());
    REQUIRE(structured.getDynamicObject() != nullptr);
    REQUIRE(structured["items"].isArray());
    REQUIRE(structured["items"].getArray()->size() == 1);
    REQUIRE(structured["items"][0]["name"].toString() == "Bass");

    // And it validates against the schema the tool advertises, which is the
    // property the two earlier failures each violated in turn.
    const juce::var* tool = nullptr;
    for (const auto& candidate : harness.endpoint.tools())
        if (candidate["name"].toString() == "tracks.list")
            tool = &candidate;
    REQUIRE(tool != nullptr);
    REQUIRE(validateJson(structured, (*tool)["outputSchema"]).empty());

    // The text block carries the same shape, so a host reading either sees the
    // same thing.
    REQUIRE((*reply.result["content"].getArray())[0]["text"].toString() ==
            juce::JSON::toString(structured, true));
}

TEST_CASE("A resource read still returns the operation's own shape", "[remote-api][mcp]") {
    Harness harness;

    TrackInfo track;
    track.id = 1;
    track.name = "Bass";
    harness.api.tracks_.tracks.push_back(track);

    // Resources are not tool results, so MCP's object constraint does not reach
    // them: `magda://tracks` is a list of tracks and reads as one. The wrapping
    // above is a tool-surface accommodation, not a change to the contract.
    const auto read =
        run(harness.endpoint, modernCall("resources/read", object({{"uri", "magda://tracks"}})));
    REQUIRE_FALSE(read.failed());

    const auto text = (*read.result["contents"].getArray())[0]["text"].toString();
    juce::var parsed;
    REQUIRE(juce::JSON::parse(text, parsed).wasOk());
    REQUIRE(parsed.isArray());
    REQUIRE(parsed[0]["name"].toString() == "Bass");
}

TEST_CASE("server/discover advertises every supported version and the real capabilities",
          "[remote-api][mcp]") {
    Harness harness;
    const auto reply = run(harness.endpoint, modernCall("server/discover"));
    REQUIRE_FALSE(reply.failed());

    REQUIRE(reply.result["resultType"].toString() == "complete");
    const auto* versions = reply.result["supportedVersions"].getArray();
    REQUIRE(versions != nullptr);
    REQUIRE(versions->size() == static_cast<int>(mcpProtocolVersions().size()));
    REQUIRE((*versions)[0].toString() == "2026-07-28");

    REQUIRE(reply.result["capabilities"]["tools"].getDynamicObject() != nullptr);
    REQUIRE(reply.result["capabilities"]["resources"].getDynamicObject() != nullptr);
    // No subscription hub was given, so `subscribe` is not claimed. Advertising
    // a capability the server cannot honour is worse than not having it.
    REQUIRE(reply.result["capabilities"]["resources"]["subscribe"].isVoid());
    REQUIRE(reply.result["instructions"].toString().isNotEmpty());
    REQUIRE(reply.result["_meta"][MCP_META_SERVER_INFO]["name"].toString() == "MAGDA");

    // server/discover is a modern method; a legacy connection has initialize.
    const auto legacy = run(harness.endpoint, legacyCall("server/discover"));
    REQUIRE(legacy.failed());
    REQUIRE(legacy.error->code == MCP_METHOD_NOT_FOUND);
    // In the handshake era 404 means the session itself is gone. A method the
    // negotiated revision does not support is a JSON-RPC error over HTTP 200.
    REQUIRE(legacy.error->httpStatus == 200);
}

TEST_CASE("initialize belongs to the legacy era only", "[remote-api][mcp]") {
    Harness harness;

    const auto reply = run(harness.endpoint, legacyCall("initialize"));
    REQUIRE_FALSE(reply.failed());
    REQUIRE(reply.result["protocolVersion"].toString() == "2025-11-25");
    REQUIRE(reply.result["serverInfo"]["name"].toString() == "MAGDA");
    // No resultType: the revision that negotiated this handshake has never
    // heard of the field.
    REQUIRE(reply.result["resultType"].isVoid());

    const auto modern = run(harness.endpoint, modernCall("initialize"));
    REQUIRE(modern.failed());
    REQUIRE(modern.error->code == MCP_METHOD_NOT_FOUND);
    // The message has to name the versions that do have a handshake: a legacy
    // client cannot fall forward, so this is the only diagnostic it can show.
    REQUIRE(modern.error->message.contains("2025-11-25"));
}

TEST_CASE("An unknown method is a 404 with a JSON-RPC body", "[remote-api][mcp]") {
    Harness harness;
    const auto reply = run(harness.endpoint, modernCall("does/not/exist"));
    REQUIRE(reply.failed());
    REQUIRE(reply.error->code == MCP_METHOD_NOT_FOUND);
    // The status and the body together are what tell a dual-era client that
    // this *is* an MCP endpoint that lacks the method, rather than a host with
    // no MCP endpoint at all.
    REQUIRE(reply.error->httpStatus == 404);
    REQUIRE(reply.error->toJson()["code"].isInt());
}

TEST_CASE("tools/call returns structured content and the committed revision", "[remote-api][mcp]") {
    Harness harness;

    const auto reply =
        run(harness.endpoint,
            modernCall("tools/call", object({{"name", "project.setTempo"},
                                             {"arguments", object({{"tempo", 132.0}})}})));
    REQUIRE_FALSE(reply.failed());
    REQUIRE(reply.result["resultType"].toString() == "complete");
    REQUIRE_FALSE(static_cast<bool>(reply.result["isError"]));

    // structuredContent is the operation's own output, and content carries the
    // same bytes as text for a host that only shows text to the model.
    REQUIRE(static_cast<double>(reply.result["structuredContent"]["tempo"]) == 132.0);
    const auto* content = reply.result["content"].getArray();
    REQUIRE(content != nullptr);
    REQUIRE(content->size() == 1);
    REQUIRE((*content)[0]["type"].toString() == "text");
    REQUIRE((*content)[0]["text"].toString().contains("\"tempo\""));

    // The output validates against the schema the tool advertises, which is what
    // a client checking `structuredContent` will do.
    const auto* operation = OperationRegistry::instance().find("project.setTempo");
    REQUIRE(operation != nullptr);
    REQUIRE(validateJson(reply.result["structuredContent"], operation->outputSchema).empty());

    // A write advances the revision, and the caller learns the new one without a
    // second round trip.
    REQUIRE(static_cast<juce::int64>(reply.result["_meta"][MAGDA_META_REVISION]) ==
            static_cast<juce::int64>(harness.service.currentRevision()));
    REQUIRE(harness.service.currentRevision() > INITIAL_REVISION);

    // A read leaves it alone.
    const auto read =
        run(harness.endpoint, modernCall("tools/call", object({{"name", "project.get"}})));
    REQUIRE_FALSE(read.failed());
    REQUIRE(static_cast<juce::int64>(read.result["_meta"][MAGDA_META_REVISION]) ==
            static_cast<juce::int64>(harness.service.currentRevision()));
}

TEST_CASE("An operation failure is a tool execution error, not a protocol error",
          "[remote-api][mcp]") {
    Harness harness;

    // A validation failure is exactly what the model is supposed to see and fix,
    // so it must survive as a tool result rather than becoming a JSON-RPC error
    // the host swallows.
    const auto invalid =
        run(harness.endpoint,
            modernCall("tools/call", object({{"name", "project.setTempo"},
                                             {"arguments", object({{"tempo", 9000.0}})}})));
    REQUIRE_FALSE(invalid.failed());
    REQUIRE(static_cast<bool>(invalid.result["isError"]));
    const auto* content = invalid.result["content"].getArray();
    REQUIRE(content != nullptr);
    REQUIRE((*content)[0]["text"].toString().contains("validation_failed"));

    // No structuredContent on a failure: the tool declares an outputSchema, and
    // an error object does not satisfy it.
    REQUIRE(invalid.result["structuredContent"].isVoid());

    // A missing track is the same kind of thing — actionable, so isError.
    const auto missing = run(
        harness.endpoint,
        modernCall("tools/call",
                   object({{"name", "tracks.get"}, {"arguments", object({{"trackId", 4242}})}})));
    REQUIRE_FALSE(missing.failed());
    REQUIRE(static_cast<bool>(missing.result["isError"]));
    REQUIRE((*missing.result["content"].getArray())[0]["text"].toString().contains("not_found"));

    // An unknown tool is not actionable — no arguments would fix it — so it is a
    // protocol error.
    const auto unknown =
        run(harness.endpoint, modernCall("tools/call", object({{"name", "tracks.teleport"}})));
    REQUIRE(unknown.failed());
    REQUIRE(unknown.error->code == MCP_INVALID_PARAMS);
    REQUIRE(unknown.error->message.contains("tracks.teleport"));

    // Nor is a transport-scoped operation reachable by pretending it is a tool.
    const auto scoped = run(
        harness.endpoint, modernCall("tools/call", object({{"name", "subscriptions.subscribe"}})));
    REQUIRE(scoped.failed());
    REQUIRE(scoped.error->code == MCP_INVALID_PARAMS);
}

TEST_CASE("Resources and templates resolve to registry read operations", "[remote-api][mcp]") {
    Harness harness;

    struct Expectation {
        const char* uri;
        const char* operation;
        const char* idField;
        int idValue;
    };

    const Expectation expectations[] = {
        {"magda://project/current", "project.get", nullptr, 0},
        {"magda://tracks", "tracks.list", nullptr, 0},
        {"magda://selection", "selection.get", nullptr, 0},
        {"magda://transport", "transport.get", nullptr, 0},
        {"magda://session", "session.get", nullptr, 0},
        {"magda://devices", "devices.list", nullptr, 0},
        {"magda://devices/catalog", "devices.catalog", nullptr, 0},
        {"magda://tracks/7", "tracks.get", "trackId", 7},
        {"magda://tracks/-2", "tracks.get", "trackId", -2},
        {"magda://tracks/7/clips", "clips.list", "trackId", 7},
        {"magda://tracks/7/devices", "devices.list", "trackId", 7},
        {"magda://clips/12", "clips.get", "clipId", 12},
    };

    for (const auto& expectation : expectations) {
        const auto resolved = harness.endpoint.resolveResource(expectation.uri);
        REQUIRE(resolved.has_value());
        REQUIRE(resolved->operation == expectation.operation);
        // Every resource must name an operation that actually exists, or a read
        // would dispatch something invented.
        REQUIRE(OperationRegistry::instance().find(resolved->operation) != nullptr);
        if (expectation.idField != nullptr)
            REQUIRE(static_cast<int>(resolved->input[expectation.idField]) == expectation.idValue);
    }

    // `magda://devices/catalog` is a fixed URI and must not be mistaken for a
    // two-segment template with a non-numeric id.
    REQUIRE(harness.endpoint.resolveResource("magda://devices/catalog")->operation ==
            "devices.catalog");

    // A non-numeric id is a lookup failure, not track 0. getIntValue is total,
    // so without a shape check "magda://tracks/cheese" would read a real track.
    REQUIRE_FALSE(harness.endpoint.resolveResource("magda://tracks/cheese").has_value());
    REQUIRE_FALSE(harness.endpoint.resolveResource("magda://tracks/").has_value());
    REQUIRE_FALSE(harness.endpoint.resolveResource("magda://tracks/1/notes").has_value());
    REQUIRE_FALSE(harness.endpoint.resolveResource("file:///etc/passwd").has_value());
    REQUIRE_FALSE(harness.endpoint.resolveResource("magda://").has_value());
}

TEST_CASE("resources/list and templates cover exactly what can be read", "[remote-api][mcp]") {
    Harness harness;

    const auto listed = run(harness.endpoint, modernCall("resources/list"));
    REQUIRE_FALSE(listed.failed());
    const auto* resources = listed.result["resources"].getArray();
    REQUIRE(resources != nullptr);

    std::set<juce::String> uris;
    for (const auto& entry : *resources) {
        const auto uri = entry["uri"].toString();
        uris.insert(uri);
        REQUIRE(entry["name"].toString().isNotEmpty());
        REQUIRE(entry["mimeType"].toString() == "application/json");
        // Everything advertised must be readable.
        REQUIRE(harness.endpoint.resolveResource(uri).has_value());
    }

    // The set #1858 names.
    for (const char* required : {"magda://project/current", "magda://tracks", "magda://selection",
                                 "magda://transport", "magda://session", "magda://devices/catalog"})
        REQUIRE(uris.count(required) == 1);

    const auto templates = run(harness.endpoint, modernCall("resources/templates/list"));
    REQUIRE_FALSE(templates.failed());
    const auto* entries = templates.result["resourceTemplates"].getArray();
    REQUIRE(entries != nullptr);

    std::set<juce::String> patterns;
    for (const auto& entry : *entries)
        patterns.insert(entry["uriTemplate"].toString());
    REQUIRE(patterns.count("magda://tracks/{track_id}") == 1);
    REQUIRE(patterns.count("magda://clips/{clip_id}") == 1);
}

TEST_CASE("resources/read returns the same projection the tool would", "[remote-api][mcp]") {
    Harness harness;

    TrackInfo track;
    track.id = 1;
    track.name = "Bass";
    harness.api.tracks_.tracks.push_back(track);

    const auto read =
        run(harness.endpoint, modernCall("resources/read", object({{"uri", "magda://tracks/1"}})));
    REQUIRE_FALSE(read.failed());
    const auto* contents = read.result["contents"].getArray();
    REQUIRE(contents != nullptr);
    REQUIRE(contents->size() == 1);
    REQUIRE((*contents)[0]["uri"].toString() == "magda://tracks/1");
    REQUIRE((*contents)[0]["mimeType"].toString() == "application/json");

    // The acceptance criterion: one operation, one projection, whichever surface
    // asked for it.
    const auto viaTool =
        run(harness.endpoint,
            modernCall("tools/call",
                       object({{"name", "tracks.get"}, {"arguments", object({{"trackId", 1}})}})));
    REQUIRE_FALSE(viaTool.failed());
    REQUIRE((*contents)[0]["text"].toString() ==
            juce::JSON::toString(viaTool.result["structuredContent"], true));
}

TEST_CASE("A resource read failure is a JSON-RPC error with the MAGDA envelope",
          "[remote-api][mcp]") {
    Harness harness;

    // A URI naming nothing this server publishes.
    const auto unknown =
        run(harness.endpoint, modernCall("resources/read", object({{"uri", "magda://nowhere"}})));
    REQUIRE(unknown.failed());
    REQUIRE(unknown.error->code == MCP_INVALID_PARAMS);
    REQUIRE(unknown.error->data["uri"].toString() == "magda://nowhere");

    // A well-formed URI for something that does not exist. -32602 rather than
    // the -32002 earlier revisions used, which this revision forbids emitting.
    const auto missing =
        run(harness.endpoint, modernCall("resources/read", object({{"uri", "magda://tracks/99"}})));
    REQUIRE(missing.failed());
    REQUIRE(missing.error->code == MCP_INVALID_PARAMS);
    REQUIRE(missing.error->code != -32002);
    REQUIRE(missing.error->data["code"].toString() == "not_found");
}

TEST_CASE("expectedRevision rides in _meta under a vendor prefix", "[remote-api][mcp]") {
    Harness harness;

    // Any prefix whose second label is `modelcontextprotocol` or `mcp` is
    // reserved by the specification, so MAGDA's own keys must not use one.
    const juce::String key(MAGDA_META_EXPECTED_REVISION);
    REQUIRE(key.startsWith("com.conceptualmachines.magda/"));
    REQUIRE_FALSE(key.startsWith("io.modelcontextprotocol/"));

    auto call = modernCall("tools/call", object({{"name", "project.setTempo"},
                                                 {"arguments", object({{"tempo", 130.0}})}}));
    call.params["_meta"].getDynamicObject()->setProperty(MAGDA_META_EXPECTED_REVISION, 999);

    const auto stale = run(harness.endpoint, call);
    REQUIRE_FALSE(stale.failed());
    REQUIRE(static_cast<bool>(stale.result["isError"]));
    REQUIRE((*stale.result["content"].getArray())[0]["text"].toString().contains("conflict"));

    // A value that is not a whole number is the client's mistake, and it is
    // reported rather than quietly ignored — silently dropping it would run the
    // write unconditionally, which is the opposite of what was asked for.
    auto malformed = modernCall("tools/call", object({{"name", "project.setTempo"},
                                                      {"arguments", object({{"tempo", 130.0}})}}));
    malformed.params["_meta"].getDynamicObject()->setProperty(MAGDA_META_EXPECTED_REVISION, "soon");
    const auto rejected = run(harness.endpoint, malformed);
    REQUIRE(rejected.failed());
    REQUIRE(rejected.error->code == MCP_INVALID_PARAMS);
}

TEST_CASE("Idempotency is opt-in and scoped by the client's own key", "[remote-api][mcp]") {
    Harness harness;

    auto setTempo = [](double tempo, const char* key) {
        auto call = modernCall("tools/call", object({{"name", "project.setTempo"},
                                                     {"arguments", object({{"tempo", tempo}})}}));
        if (key != nullptr)
            call.params["_meta"].getDynamicObject()->setProperty(MAGDA_META_REQUEST_ID, key);
        return call;
    };

    const auto first = run(harness.endpoint, setTempo(140.0, "11111111-2222-3333"));
    REQUIRE_FALSE(first.failed());
    const auto revisionAfterFirst = harness.service.currentRevision();
    REQUIRE(harness.api.project_.info.tempo == 140.0);

    // The same key replays the first answer rather than applying the write
    // again: the tempo the caller asked for the second time never lands, and the
    // revision does not move.
    const auto retry = run(harness.endpoint, setTempo(90.0, "11111111-2222-3333"));
    REQUIRE_FALSE(retry.failed());
    REQUIRE(static_cast<double>(retry.result["structuredContent"]["tempo"]) == 140.0);
    REQUIRE(harness.api.project_.info.tempo == 140.0);
    REQUIRE(harness.service.currentRevision() == revisionAfterFirst);

    // A different key is a different request.
    const auto second = run(harness.endpoint, setTempo(90.0, "44444444-5555-6666"));
    REQUIRE_FALSE(second.failed());
    REQUIRE(harness.api.project_.info.tempo == 90.0);
    REQUIRE(harness.service.currentRevision() > revisionAfterFirst);

    // Without a key there is no caching at all. MCP has no session and every
    // client behind the one bearer token looks alike from here, so deriving a
    // key from the JSON-RPC id would let two clients that both counted from 1
    // receive each other's writes — a silent wrong answer, which is worse than
    // no idempotency.
    run(harness.endpoint, setTempo(111.0, nullptr));
    run(harness.endpoint, setTempo(122.0, nullptr));
    REQUIRE(harness.api.project_.info.tempo == 122.0);
}

TEST_CASE("A listen filter is honoured only where the server can deliver", "[remote-api][mcp]") {
    MessageThreadRelaxation relaxation;
    MockMagdaApi api;
    RemoteApiService service{api};
    SubscriptionHub hub{api, service};
    McpEndpoint endpoint{service, McpEndpoint::Options{}, &hub};

    juce::Array<juce::var> requested;
    requested.add("magda://tracks");
    requested.add("magda://transport");
    requested.add("magda://devices/catalog");  // real, but nothing publishes it
    requested.add("magda://nowhere");          // not a resource at all
    requested.add("magda://tracks");           // duplicate

    const auto filter = endpoint.parseListenFilter(
        object({{"notifications",
                 object({{"resourceSubscriptions", requested}, {"toolsListChanged", true}})}}));

    // What survives is what the server can actually send. The acknowledgment is
    // built from this, so a client comparing it against its request learns
    // exactly what it is and is not going to hear about.
    REQUIRE(filter.resourceSubscriptions ==
            std::vector<juce::String>{"magda://tracks", "magda://transport"});
    // Neither list can change in a running MAGDA, so agreeing to notify would be
    // a promise that never comes due.
    REQUIRE_FALSE(filter.toolsListChanged);
    REQUIRE(filter.wantsAnything());

    const auto topics = endpoint.topicsFor(filter);
    REQUIRE(topics.size() == 2);

    REQUIRE(endpoint.urisAffectedBy(Topic::Tracks, filter) ==
            std::vector<juce::String>{"magda://tracks"});
    REQUIRE(endpoint.urisAffectedBy(Topic::Clips, filter).empty());

    const auto acknowledged = endpoint.acknowledgment(filter, 1);
    REQUIRE(acknowledged["method"].toString() == "notifications/subscriptions/acknowledged");
    REQUIRE(static_cast<int>(acknowledged["params"]["_meta"][MCP_META_SUBSCRIPTION_ID]) == 1);
    REQUIRE(acknowledged["params"]["notifications"]["resourceSubscriptions"].getArray()->size() ==
            2);
    REQUIRE(acknowledged["params"]["notifications"]["toolsListChanged"].isVoid());

    const auto updated = McpEndpoint::resourceUpdated("magda://tracks", 1);
    REQUIRE(updated["method"].toString() == "notifications/resources/updated");
    REQUIRE(updated["params"]["uri"].toString() == "magda://tracks");
    REQUIRE(static_cast<int>(updated["params"]["_meta"][MCP_META_SUBSCRIPTION_ID]) == 1);
    REQUIRE(updated["id"].isVoid());  // a notification is never answered

    hub.shutdown();
}

TEST_CASE("Without a subscription hub, resource updates are not advertised", "[remote-api][mcp]") {
    Harness harness;
    REQUIRE(harness.endpoint.capabilities()["resources"]["subscribe"].isVoid());

    juce::Array<juce::var> requested;
    requested.add("magda://tracks");
    const auto filter = harness.endpoint.parseListenFilter(
        object({{"notifications", object({{"resourceSubscriptions", requested}})}}));
    REQUIRE(filter.resourceSubscriptions.empty());
    REQUIRE_FALSE(filter.wantsAnything());
}
