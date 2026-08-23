#include "remote_mcp.hpp"

#include <algorithm>
#include <cstring>
#include <limits>
#include <utility>

#include "remote_clients.hpp"
#include "remote_service.hpp"
#include "remote_subscriptions.hpp"

namespace magda {
namespace remote {

namespace {

constexpr const char* kUriScheme = "magda://";

/**
 * The key an array-valued result is carried under in a tool result.
 *
 * MCP types `structuredContent` as a JSON *object*, and a client validates
 * against that — an array there is rejected outright. Four operations
 * (`tracks.list` and the other three list reads) return arrays, so on this
 * surface they are wrapped rather than dropped: `{"items": [...]}` keeps the
 * data, keeps it validated against a real `outputSchema`, and keeps one shape
 * for every list operation instead of a different key per domain.
 *
 * The WebSocket transport is unaffected and still returns the bare array; this
 * is MCP's constraint, not the contract's.
 */
constexpr const char* kArrayResultKey = "items";

/// `application/json` for everything: every resource here is a projection of the
/// same DTOs the tools return, so a client parses one shape rather than two.
constexpr const char* kResourceMimeType = "application/json";

/**
 * What a host is told this server is for.
 *
 * Worth writing rather than leaving empty: `instructions` is the only place a
 * server can tell a model that these tools act on a *live* session — that a
 * write is heard immediately by whoever is sitting in front of MAGDA, and that
 * `expected_revision` exists for the case where that matters.
 */
constexpr const char* kInstructions =
    "Inspect and control the MAGDA session that is running right now. Tools are named "
    "<domain>.<verb> and take the JSON Schema shown in each tool's inputSchema; resources under "
    "magda:// are read-only projections of the same state. Every mutation is applied immediately "
    "to the open project and is undoable as a single step. Reads are cheap; prefer a resource "
    "read over a tool call when you only need current state. Pass "
    "com.conceptualmachines.magda/expectedRevision in a request's _meta to make a write "
    "conditional on the project not having changed since the revision you last saw.";

juce::var makeObject() {
    return juce::var(new juce::DynamicObject());
}

void setProperty(juce::var& object, const char* name, const juce::var& value) {
    if (auto* dynamicObject = object.getDynamicObject())
        dynamicObject->setProperty(juce::Identifier(name), value);
}

juce::var textContent(const juce::String& text) {
    auto block = makeObject();
    setProperty(block, "type", "text");
    setProperty(block, "text", text);
    return block;
}

/// Whether an operation's declared output is a JSON array rather than an object.
bool hasArrayOutput(const OperationDescriptor& operation) {
    return operation.outputSchema["type"].toString() == "array";
}

/// `{"items": <the array schema>}`, closed — the object form of an array-valued
/// output schema. Mirrors exactly what `wrapArrayResult` does to the value.
juce::var wrappedArraySchema(const juce::var& arraySchema) {
    auto properties = makeObject();
    setProperty(properties, kArrayResultKey, arraySchema);

    juce::Array<juce::var> required;
    required.add(kArrayResultKey);

    auto schema = makeObject();
    setProperty(schema, "type", "object");
    setProperty(schema, "properties", properties);
    setProperty(schema, "required", required);
    setProperty(schema, "additionalProperties", false);
    return schema;
}

/// `{"items": [...]}` — the object form of an array result.
juce::var wrapArrayResult(const juce::var& array) {
    auto wrapped = makeObject();
    setProperty(wrapped, kArrayResultKey, array);
    return wrapped;
}

/**
 * @brief A path segment as a model id, or nothing.
 *
 * `getIntValue` is total — it answers 0 for "cheese" and for "" alike — so the
 * shape has to be checked first or `magda://tracks/cheese` would silently read
 * track 0. The leading `-` is allowed because the master track is id -2.
 */
std::optional<int> parseId(const juce::String& segment) {
    if (segment.isEmpty())
        return std::nullopt;
    const auto digits = segment.startsWithChar('-') ? segment.substring(1) : segment;
    if (digits.isEmpty() || !digits.containsOnly("0123456789"))
        return std::nullopt;
    return segment.getIntValue();
}

/// The path segments of a `magda://` URI, or empty for anything else.
juce::StringArray uriSegments(const juce::String& uri) {
    if (!uri.startsWith(kUriScheme))
        return {};
    const auto path = uri.substring(static_cast<int>(std::strlen(kUriScheme)));
    if (path.isEmpty())
        return {};
    juce::StringArray segments;
    segments.addTokens(path, "/", "");
    segments.removeEmptyStrings();
    return segments;
}

juce::var idInput(const char* field, int id) {
    auto input = makeObject();
    setProperty(input, field, id);
    return input;
}

/**
 * MAGDA's error taxonomy onto the codes MCP defines for a *resource read*.
 *
 * Narrower than the WebSocket's mapping, and deliberately so: a resource read
 * has no `isError` channel to put an actionable failure in, so everything
 * becomes a JSON-RPC error and the spec pins two of them. `-32602` for a
 * resource that is not there — `-32002` said that in earlier revisions and is
 * now forbidden to emit — and `-32603` for anything internal.
 */
/**
 * @brief Whether a method is answered from the catalogue rather than dispatched.
 *
 * These three describe the API surface: what tools exist, what resources exist,
 * and what their schemas are. They never touch the model, which is why they
 * return before a `RequestContext` is ever built — and why the scope they need
 * has to be checked at that point rather than downstream.
 */
bool isCatalogueMethod(const juce::String& method) {
    return method == "tools/list" || method == "resources/list" ||
           method == "resources/templates/list";
}

/**
 * @brief Methods whose modern result the schema types as cacheable.
 *
 * `2026-07-28` gives a cacheable result its own cache directives, and makes
 * them required rather than optional: `resultType` alone does not satisfy the
 * schema. A client that validates against it — the official SDK does — rejects
 * the response outright, so omitting them is not a client being lenient about a
 * missing field, it is the catalogue failing to parse and the session being
 * useless from its first call.
 *
 * `tools/call` is deliberately absent: its result is not cacheable, and
 * stamping directives on it would put fields on the wire the schema for that
 * method does not have. `prompts/list` is the sixth cacheable method and is
 * not served here.
 */
bool isCacheableResult(const juce::String& method) {
    return method == "tools/list" || method == "resources/list" ||
           method == "resources/templates/list" || method == "resources/read" ||
           method == "server/discover";
}

/**
 * @brief Say that nothing here may be held.
 *
 * `ttlMs` of zero, scope `private`. Both are the honest answer rather than a
 * placeholder: this endpoint projects a live session that the person at the
 * keyboard is editing, so a result cached for any length of time is a result
 * that can be wrong, and everything behind the bearer token belongs to one
 * user rather than to a shared cache.
 */
void setCacheDirectives(juce::var& result) {
    setProperty(result, "ttlMs", 0);
    setProperty(result, "cacheScope", "private");
}

int resourceReadCodeFor(ErrorCode code) {
    switch (code) {
        case ErrorCode::NotFound:
        case ErrorCode::ValidationFailed:
        case ErrorCode::InvalidRequest:
        case ErrorCode::UnknownOperation:
            return MCP_INVALID_PARAMS;
        case ErrorCode::PermissionDenied:
            return MCP_PERMISSION_DENIED;
        case ErrorCode::Conflict:
        case ErrorCode::Timeout:
        case ErrorCode::Cancelled:
        case ErrorCode::InternalError:
            break;
    }
    return MCP_INTERNAL_ERROR;
}

}  // namespace

// ===========================================================================
// Versions
// ===========================================================================

const std::vector<juce::String>& mcpProtocolVersions() {
    // Newest first. `2025-06-18` is the oldest kept: it is the first revision to
    // require the `MCP-Protocol-Version` header, and the ones before it are the
    // deprecated HTTP+SSE transport, which is a different endpoint shape rather
    // than a different version of this one.
    static const std::vector<juce::String> versions{"2026-07-28", "2025-11-25", "2025-06-18"};
    return versions;
}

McpEra eraForVersion(const juce::String& version) {
    // Lexicographic comparison is exact for `YYYY-MM-DD`, which is why MCP
    // chose that format. Anything at or after the stateless revision is modern.
    return version >= "2026-07-28" ? McpEra::Modern : McpEra::Legacy;
}

bool isSupportedVersion(const juce::String& version) {
    const auto& versions = mcpProtocolVersions();
    return std::find(versions.begin(), versions.end(), version) != versions.end();
}

const juce::String& latestProtocolVersion() {
    return mcpProtocolVersions().front();
}

juce::String negotiateLegacyVersion(const juce::var& initializeParams) {
    const auto requested = initializeParams["protocolVersion"].toString();
    if (isSupportedVersion(requested) && eraForVersion(requested) == McpEra::Legacy)
        return requested;

    for (const auto& version : mcpProtocolVersions()) {
        if (eraForVersion(version) == McpEra::Legacy)
            return version;
    }
    return latestProtocolVersion();
}

McpRouting routeFor(const juce::String& method, McpEra era) {
    if (era == McpEra::Modern) {
        if (method == "subscriptions/listen")
            return McpRouting::Stream;
        return McpRouting::Endpoint;
    }
    if (method == "resources/subscribe")
        return McpRouting::Subscribe;
    if (method == "resources/unsubscribe")
        return McpRouting::Unsubscribe;
    return McpRouting::Endpoint;
}

// ===========================================================================
// Errors
// ===========================================================================

juce::var McpError::toJson() const {
    auto object = makeObject();
    setProperty(object, "code", code);
    setProperty(object, "message", message);
    if (!data.isVoid())
        setProperty(object, "data", data);
    return object;
}

McpReply McpReply::fail(int code, const juce::String& message, int httpStatus, juce::var data) {
    return McpReply{McpError{code, message, std::move(data), httpStatus}, {}};
}

// ===========================================================================
// McpEndpoint
// ===========================================================================

McpEndpoint::McpEndpoint(RemoteApiService& service, Options options, SubscriptionHub* subscriptions)
    : service_(service), options_(std::move(options)), subscriptions_(subscriptions) {
    // Built once, from the registry, in registry order. Order matters: MCP asks
    // for a deterministic `tools/list` so clients can cache it and so a model's
    // prompt prefix stays stable between calls.
    for (const auto& operation : OperationRegistry::instance().operations()) {
        // A transport-scoped operation has no implementation to call. MCP
        // subscribes through its own verbs, so exposing `subscriptions.subscribe`
        // as a tool would advertise a second way to do it that does not work.
        if (operation.transportScoped)
            continue;

        auto tool = makeObject();
        setProperty(tool, "name", operation.name);
        setProperty(tool, "description", operation.summary);
        setProperty(tool, "inputSchema", operation.inputSchema);

        // MCP types both schemas, and `structuredContent` itself, as JSON
        // *objects* — a client validates against that and rejects anything else.
        // An array-valued operation therefore cannot be expressed directly: its
        // schema is wrapped here and its result is wrapped identically in
        // `callTool`, so the two always describe the same shape.
        //
        // Wrapping rather than dropping. Omitting the schema silences the
        // validator but leaves the result invalid, which is how this was found
        // the second time — `tools/list` succeeded and every list *call* then
        // failed. Declaring the wrapper keeps the data, keeps it validated, and
        // keeps one shape across all four list operations.
        setProperty(tool, "outputSchema",
                    hasArrayOutput(operation) ? wrappedArraySchema(operation.outputSchema)
                                              : operation.outputSchema);

        // Only hints that are facts about the operation. `destructiveHint` and
        // `idempotentHint` would each be a guess per operation, and the
        // specification tells clients to distrust annotations already — a
        // guessed one spends that trust for nothing.
        auto annotations = makeObject();
        setProperty(annotations, "readOnlyHint", operation.access == OperationAccess::Read);
        setProperty(annotations, "openWorldHint", false);
        setProperty(tool, "annotations", annotations);

        tools_.push_back(tool);
    }

    resources_ = {
        {"magda://project/current", "", "project", "Project",
         "Tempo, time signature, key, and loop for the open project", "project.get",
         Topic::Project},
        {"magda://tracks", "", "tracks", "Tracks", "Every track in the project, in order",
         "tracks.list", Topic::Tracks},
        {"magda://selection", "", "selection", "Selection",
         "The tracks, clips, and notes currently selected", "selection.get", Topic::Selection},
        {"magda://transport", "", "transport", "Transport",
         "Play and record state, loop, and playhead position", "transport.get", Topic::Transport},
        {"magda://session", "", "session", "Session",
         "The session clip grid: which slot holds which clip, and what it is doing", "session.get",
         Topic::Session},
        {"magda://devices", "", "devices", "Devices",
         "The device, rack, and chain graph for every track", "devices.list", Topic::Devices},
        // No topic: the catalogue changes only when plugins are rescanned, which
        // nothing in the model publishes. Saying so by omission is better than
        // accepting a subscription that would never fire.
        {"magda://devices/catalog", "", "device-catalog", "Device catalogue",
         "Devices that can be added, addressed by catalogue id", "devices.catalog", std::nullopt},

        {"", "magda://tracks/{track_id}", "track", "Track",
         "One track by id; -2 is the master track", "tracks.get", Topic::Tracks},
        {"", "magda://tracks/{track_id}/clips", "track-clips", "Track clips",
         "Every clip on one track", "clips.list", Topic::Clips},
        {"", "magda://tracks/{track_id}/devices", "track-devices", "Track devices",
         "The device graph for one track", "devices.list", Topic::Devices},
        {"", "magda://clips/{clip_id}", "clip", "Clip", "One clip by id, with its MIDI notes",
         "clips.get", Topic::Clips},
    };
}

const std::vector<juce::var>& McpEndpoint::tools() const {
    return tools_;
}

const std::vector<McpResource>& McpEndpoint::resources() const {
    return resources_;
}

juce::var McpEndpoint::serverInfo() const {
    auto info = makeObject();
    setProperty(info, "name", options_.serverName);
    setProperty(info, "version", options_.serverVersion);
    return info;
}

juce::var McpEndpoint::capabilities() const {
    // `listChanged` is declared on neither: the tool list is the operation
    // registry, which is fixed at build time, and the resource list is this
    // table. Declaring a notification that can never fire would have clients
    // opening a stream to wait for it.
    auto tools = makeObject();
    auto resources = makeObject();
    if (subscriptions_ != nullptr)
        setProperty(resources, "subscribe", true);

    auto capabilities = makeObject();
    setProperty(capabilities, "tools", tools);
    setProperty(capabilities, "resources", resources);
    return capabilities;
}

juce::var McpEndpoint::discoverResult() const {
    juce::Array<juce::var> versions;
    for (const auto& version : mcpProtocolVersions())
        versions.add(version);

    auto meta = makeObject();
    setProperty(meta, MCP_META_SERVER_INFO, serverInfo());

    auto result = makeObject();
    setProperty(result, "resultType", "complete");
    // Modern by definition: server/discover exists only in this revision.
    setCacheDirectives(result);
    setProperty(result, "supportedVersions", versions);
    setProperty(result, "capabilities", capabilities());
    setProperty(result, "instructions", kInstructions);
    setProperty(result, "_meta", meta);
    return result;
}

juce::var McpEndpoint::initializeResult(const juce::String& negotiatedVersion) const {
    auto result = makeObject();
    setProperty(result, "protocolVersion", negotiatedVersion);
    setProperty(result, "capabilities", capabilities());
    setProperty(result, "serverInfo", serverInfo());
    setProperty(result, "instructions", kInstructions);
    return result;
}

// ---------------------------------------------------------------------------
// Resources
// ---------------------------------------------------------------------------

const McpResource* McpEndpoint::findResource(const juce::String& uri) const {
    if (auto resolved = resolveResource(uri))
        return resolved->resource;
    return nullptr;
}

std::optional<McpEndpoint::ResolvedResource> McpEndpoint::resolveResource(
    const juce::String& uri) const {
    // Fixed URIs first, so `magda://devices/catalog` is never mistaken for a
    // two-segment template.
    for (const auto& resource : resources_) {
        if (resource.uri.isNotEmpty() && resource.uri == uri)
            return ResolvedResource{&resource, resource.operation, makeObject()};
    }

    const auto segments = uriSegments(uri);
    const auto templateFor = [this](const char* pattern) -> const McpResource* {
        for (const auto& resource : resources_) {
            if (resource.uriTemplate == pattern)
                return &resource;
        }
        return nullptr;
    };

    if (segments.size() == 2) {
        const auto id = parseId(segments[1]);
        if (!id)
            return std::nullopt;
        if (segments[0] == "tracks") {
            if (const auto* resource = templateFor("magda://tracks/{track_id}"))
                return ResolvedResource{resource, resource->operation, idInput("trackId", *id)};
        }
        if (segments[0] == "clips") {
            if (const auto* resource = templateFor("magda://clips/{clip_id}"))
                return ResolvedResource{resource, resource->operation, idInput("clipId", *id)};
        }
        return std::nullopt;
    }

    if (segments.size() == 3 && segments[0] == "tracks") {
        const auto id = parseId(segments[1]);
        if (!id)
            return std::nullopt;
        const char* pattern = segments[2] == "clips"     ? "magda://tracks/{track_id}/clips"
                              : segments[2] == "devices" ? "magda://tracks/{track_id}/devices"
                                                         : nullptr;
        if (pattern != nullptr) {
            if (const auto* resource = templateFor(pattern))
                return ResolvedResource{resource, resource->operation, idInput("trackId", *id)};
        }
    }

    return std::nullopt;
}

std::optional<Topic> McpEndpoint::topicForResource(const juce::String& uri) const {
    if (const auto* resource = findResource(uri))
        return resource->topic;
    return std::nullopt;
}

// ---------------------------------------------------------------------------
// Subscriptions
// ---------------------------------------------------------------------------

bool McpEndpoint::ListenFilter::wantsAnything() const {
    return toolsListChanged || promptsListChanged || resourcesListChanged ||
           !resourceSubscriptions.empty();
}

McpEndpoint::ListenFilter McpEndpoint::parseListenFilter(const juce::var& params) const {
    ListenFilter filter;
    const auto notifications = params["notifications"];
    if (notifications.getDynamicObject() == nullptr)
        return filter;

    // `toolsListChanged` and the two other list-change flags are parsed but not
    // honoured: neither list can change in a running MAGDA, so agreeing to send
    // a notification that will never arrive would be a promise, not a courtesy.
    // They are dropped here and therefore absent from the acknowledgment.

    if (subscriptions_ == nullptr)
        return filter;

    if (const auto* uris = notifications["resourceSubscriptions"].getArray()) {
        for (const auto& value : *uris) {
            if (!value.isString())
                continue;
            const auto uri = value.toString();
            // Only URIs that name something, and only ones something can move.
            // Accepting `magda://devices/catalog` here would leave a client
            // waiting on a stream for an event with no source.
            if (!topicForResource(uri).has_value())
                continue;
            if (std::find(filter.resourceSubscriptions.begin(), filter.resourceSubscriptions.end(),
                          uri) == filter.resourceSubscriptions.end()) {
                filter.resourceSubscriptions.push_back(uri);
            }
        }
    }
    return filter;
}

std::vector<Topic> McpEndpoint::topicsFor(const ListenFilter& filter) const {
    std::vector<Topic> topics;
    for (const auto& uri : filter.resourceSubscriptions) {
        if (const auto topic = topicForResource(uri)) {
            if (std::find(topics.begin(), topics.end(), *topic) == topics.end())
                topics.push_back(*topic);
        }
    }
    return topics;
}

std::vector<juce::String> McpEndpoint::urisAffectedBy(Topic topic,
                                                      const ListenFilter& filter) const {
    std::vector<juce::String> uris;
    for (const auto& uri : filter.resourceSubscriptions) {
        if (topicForResource(uri) == topic)
            uris.push_back(uri);
    }
    return uris;
}

juce::var McpEndpoint::acknowledgment(const ListenFilter& filter,
                                      const juce::var& subscriptionId) const {
    juce::Array<juce::var> uris;
    for (const auto& uri : filter.resourceSubscriptions)
        uris.add(uri);

    // Only what the server agreed to. A client compares this against what it
    // asked for, so echoing the request back would defeat the point of it.
    auto notifications = makeObject();
    if (!uris.isEmpty())
        setProperty(notifications, "resourceSubscriptions", uris);

    auto meta = makeObject();
    setProperty(meta, MCP_META_SUBSCRIPTION_ID, subscriptionId);

    auto params = makeObject();
    setProperty(params, "_meta", meta);
    setProperty(params, "notifications", notifications);

    auto notification = makeObject();
    setProperty(notification, "jsonrpc", "2.0");
    setProperty(notification, "method", "notifications/subscriptions/acknowledged");
    setProperty(notification, "params", params);
    return notification;
}

juce::var McpEndpoint::resourceUpdated(const juce::String& uri, const juce::var& subscriptionId) {
    auto params = makeObject();
    if (!subscriptionId.isVoid()) {
        auto meta = makeObject();
        setProperty(meta, MCP_META_SUBSCRIPTION_ID, subscriptionId);
        setProperty(params, "_meta", meta);
    }
    setProperty(params, "uri", uri);

    auto notification = makeObject();
    setProperty(notification, "jsonrpc", "2.0");
    setProperty(notification, "method", "notifications/resources/updated");
    setProperty(notification, "params", params);
    return notification;
}

juce::var McpEndpoint::listenClosedResult(const juce::var& subscriptionId) {
    auto meta = makeObject();
    setProperty(meta, MCP_META_SUBSCRIPTION_ID, subscriptionId);

    auto result = makeObject();
    setProperty(result, "resultType", "complete");
    setProperty(result, "_meta", meta);
    return result;
}

// ---------------------------------------------------------------------------
// Dispatch
// ---------------------------------------------------------------------------

void McpEndpoint::handle(const Call& call, Completion onComplete) {
    const auto modern = call.era == McpEra::Modern;

    auto complete = [modern, method = call.method, this](juce::var result) {
        // `resultType` exists only in the modern era. Stamping it on a legacy
        // reply would put a field on the wire that the revision the client
        // negotiated has never heard of. The same goes for the cache
        // directives, which that revision introduced alongside it.
        if (modern) {
            setProperty(result, "resultType", "complete");
            if (isCacheableResult(method))
                setCacheDirectives(result);
            auto meta = result["_meta"];
            if (meta.getDynamicObject() == nullptr)
                meta = makeObject();
            setProperty(meta, MCP_META_SERVER_INFO, serverInfo());
            setProperty(result, "_meta", meta);
        }
        return McpReply::ok(result);
    };

    if (call.method == "ping") {
        onComplete(complete(makeObject()));
        return;
    }

    if (call.method == "server/discover") {
        if (!modern) {
            onComplete(McpReply::fail(MCP_METHOD_NOT_FOUND,
                                      "server/discover requires protocol version 2026-07-28 or "
                                      "later; this connection negotiated " +
                                          call.protocolVersion));
            return;
        }
        onComplete(McpReply::ok(discoverResult()));
        return;
    }

    if (call.method == "initialize") {
        if (modern) {
            // A modern request carrying `initialize` is a client that has mixed
            // the two eras. Saying which versions do have a handshake is the
            // only diagnostic it can act on.
            onComplete(McpReply::fail(MCP_METHOD_NOT_FOUND,
                                      "initialize belongs to protocol versions 2025-11-25 and "
                                      "earlier; this request declared " +
                                          call.protocolVersion,
                                      404));
            return;
        }
        onComplete(McpReply::ok(initializeResult(call.protocolVersion)));
        return;
    }

    // The catalogue methods answer from this object without ever reaching
    // `requestContext` or the dispatcher, so nothing downstream would check a
    // scope for them (#1860). Without this they stayed readable with no registry
    // configured at all, which contradicts the fail-closed rule the rest of the
    // permission model is built on — and lets a client that may do nothing
    // enumerate the whole operation surface and its schemas.
    //
    // `server/discover` and `initialize` above are deliberately not gated: they
    // are how a client learns this endpoint exists and negotiates a protocol
    // version, they carry no catalogue and no project data, and a client that
    // could not complete a handshake could not be told why it was refused.
    if (isCatalogueMethod(call.method) && !call.scopes.has(Scope::Read)) {
        onComplete(McpReply::fail(MCP_PERMISSION_DENIED,
                                  call.method + " requires the 'read' permission, which this "
                                                "client has not been granted"));
        return;
    }

    if (call.method == "tools/list") {
        juce::Array<juce::var> tools;
        for (const auto& tool : tools_)
            tools.add(tool);
        auto result = makeObject();
        setProperty(result, "tools", tools);
        onComplete(complete(result));
        return;
    }

    if (call.method == "resources/list") {
        juce::Array<juce::var> entries;
        for (const auto& resource : resources_) {
            if (resource.uri.isEmpty())
                continue;
            auto entry = makeObject();
            setProperty(entry, "uri", resource.uri);
            setProperty(entry, "name", resource.name);
            setProperty(entry, "title", resource.title);
            setProperty(entry, "description", resource.description);
            setProperty(entry, "mimeType", kResourceMimeType);
            entries.add(entry);
        }
        auto result = makeObject();
        setProperty(result, "resources", entries);
        onComplete(complete(result));
        return;
    }

    if (call.method == "resources/templates/list") {
        juce::Array<juce::var> entries;
        for (const auto& resource : resources_) {
            if (resource.uriTemplate.isEmpty())
                continue;
            auto entry = makeObject();
            setProperty(entry, "uriTemplate", resource.uriTemplate);
            setProperty(entry, "name", resource.name);
            setProperty(entry, "title", resource.title);
            setProperty(entry, "description", resource.description);
            setProperty(entry, "mimeType", kResourceMimeType);
            entries.add(entry);
        }
        auto result = makeObject();
        setProperty(result, "resourceTemplates", entries);
        onComplete(complete(result));
        return;
    }

    if (call.method == "tools/call") {
        callTool(call, std::move(onComplete));
        return;
    }

    if (call.method == "resources/read") {
        readResource(call, std::move(onComplete));
        return;
    }

    // In the session-based transport a 404 means the session itself is gone and
    // forces a re-initialize. Only the stateless revision uses 404 as its
    // method-not-found signal.
    onComplete(
        McpReply::fail(MCP_METHOD_NOT_FOUND, "Unknown method: " + call.method, modern ? 404 : 200));
}

void McpEndpoint::callTool(const Call& call, Completion onComplete) {
    const auto nameValue = call.params["name"];
    if (!nameValue.isString() || nameValue.toString().isEmpty()) {
        onComplete(McpReply::fail(MCP_INVALID_PARAMS, "tools/call requires a string params.name"));
        return;
    }

    const auto name = nameValue.toString();
    const auto* operation = OperationRegistry::instance().find(name);
    // An unknown tool is a protocol error rather than an execution error: a
    // model cannot fix a name that does not exist by trying different
    // arguments, so there is nothing to hand back for self-correction.
    if (operation == nullptr || operation->transportScoped) {
        onComplete(McpReply::fail(MCP_INVALID_PARAMS, "Unknown tool: " + name));
        return;
    }

    auto arguments = call.params["arguments"];
    if (arguments.isVoid() || arguments.isUndefined())
        arguments = makeObject();
    if (arguments.getDynamicObject() == nullptr) {
        onComplete(
            McpReply::fail(MCP_INVALID_PARAMS, "tools/call params.arguments must be an object"));
        return;
    }

    McpError contextError;
    const auto context = requestContext(call, contextError);
    if (!context) {
        onComplete(McpReply::fail(contextError));
        return;
    }

    const auto modern = call.era == McpEra::Modern;
    const auto info = modern ? serverInfo() : juce::var();
    const auto wrapResult = hasArrayOutput(*operation);

    service_.dispatch(
        name, arguments, *context, [onComplete, modern, info, wrapResult](Response response) {
            auto result = makeObject();
            if (modern)
                setProperty(result, "resultType", "complete");

            juce::Array<juce::var> content;
            if (response.ok) {
                // An array result is wrapped to `{"items": [...]}`: MCP types
                // `structuredContent` as an object and a client rejects anything
                // else. The wrap matches the tool's declared `outputSchema` exactly,
                // so the two never disagree.
                const auto structured =
                    wrapResult ? wrapArrayResult(response.result) : response.result;

                // The serialized JSON goes in `content` as well as in
                // `structuredContent`. The specification asks for it, and the reason
                // holds: a host that predates structured results, or one that only
                // ever shows text to the model, would otherwise render an empty
                // tool result for a call that succeeded. It carries the same wrapped
                // shape, so text and structure never describe different things.
                content.add(textContent(juce::JSON::toString(structured, true)));
                setProperty(result, "structuredContent", structured);
                setProperty(result, "isError", false);
            } else {
                // Validation, not-found, conflict, and timeout are all things a
                // model can act on — a wrong id, a stale revision, a value out of
                // range — so they come back as tool *execution* errors with the
                // MAGDA envelope intact, not as JSON-RPC failures the host would
                // swallow before the model ever saw them.
                //
                // No `structuredContent` here even though the tool declares an
                // `outputSchema`: the schema describes what the operation returns,
                // and a failure did not return it. Sending an error object under
                // that key would be the one thing a validating client is entitled
                // to reject.
                content.add(textContent(juce::JSON::toString(toJson(response.error), true)));
                setProperty(result, "isError", true);
            }
            setProperty(result, "content", content);

            auto meta = makeObject();
            setProperty(meta, MAGDA_META_REVISION, static_cast<juce::int64>(response.revision));
            if (modern)
                setProperty(meta, MCP_META_SERVER_INFO, info);
            setProperty(result, "_meta", meta);

            onComplete(McpReply::ok(result));
        });
}

void McpEndpoint::readResource(const Call& call, Completion onComplete) {
    const auto uriValue = call.params["uri"];
    if (!uriValue.isString() || uriValue.toString().isEmpty()) {
        onComplete(
            McpReply::fail(MCP_INVALID_PARAMS, "resources/read requires a string params.uri"));
        return;
    }

    const auto uri = uriValue.toString();
    const auto resolved = resolveResource(uri);
    if (!resolved) {
        auto data = makeObject();
        setProperty(data, "uri", uri);
        onComplete(McpReply::fail(MCP_INVALID_PARAMS, "Resource not found: " + uri, 200, data));
        return;
    }

    McpError contextError;
    const auto context = requestContext(call, contextError);
    if (!context) {
        onComplete(McpReply::fail(contextError));
        return;
    }

    const auto modern = call.era == McpEra::Modern;
    const auto info = modern ? serverInfo() : juce::var();

    service_.dispatch(
        resolved->operation, resolved->input, *context,
        [onComplete, uri, modern, info](Response response) {
            if (!response.ok) {
                // A read has no `isError` channel, so a failure is
                // a JSON-RPC error whatever caused it. The MAGDA
                // envelope rides in `data` so a client that knows
                // MAGDA still gets the code and the issue list.
                auto data = toJson(response.error);
                setProperty(data, "uri", uri);
                onComplete(McpReply::fail(resourceReadCodeFor(response.error.code),
                                          response.error.message, 200, data));
                return;
            }

            auto contents = makeObject();
            setProperty(contents, "uri", uri);
            setProperty(contents, "mimeType", kResourceMimeType);
            setProperty(contents, "text", juce::JSON::toString(response.result, true));

            juce::Array<juce::var> all;
            all.add(contents);

            auto meta = makeObject();
            setProperty(meta, MAGDA_META_REVISION, static_cast<juce::int64>(response.revision));
            if (modern)
                setProperty(meta, MCP_META_SERVER_INFO, info);

            auto result = makeObject();
            if (modern) {
                setProperty(result, "resultType", "complete");
                setCacheDirectives(result);
            }
            setProperty(result, "contents", all);
            setProperty(result, "_meta", meta);
            onComplete(McpReply::ok(result));
        });
}

std::optional<RequestContext> McpEndpoint::requestContext(const Call& call, McpError& error) const {
    RequestContext context;
    context.clientId = call.clientId;
    context.clientName = call.clientName;
    context.transport = TRANSPORT_MCP;
    context.scopes = call.scopes;
    context.deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(options_.defaultDeadlineMs);

    const auto meta = call.params["_meta"];
    if (meta.getDynamicObject() == nullptr)
        return context;

    if (const auto expected = meta[MAGDA_META_EXPECTED_REVISION]; !expected.isVoid()) {
        const auto revision = jsonInteger(expected, 0, std::numeric_limits<juce::int64>::max());
        if (!revision) {
            error = McpError{MCP_INVALID_PARAMS,
                             juce::String(MAGDA_META_EXPECTED_REVISION) +
                                 " must be a non-negative whole number",
                             {},
                             200};
            return std::nullopt;
        }
        context.expectedRevision = static_cast<Revision>(*revision);
    }

    // Idempotency is opt-in, and the key is the client's to make unique.
    //
    // The WebSocket transport scopes its keys by connection, which it can
    // because a connection is a client. MCP has no such thing: a modern request
    // may arrive on a fresh socket every time, and every client behind the one
    // bearer token looks identical from here. Deriving a key from the JSON-RPC
    // id would therefore let two clients that both counted from 1 receive each
    // other's cached writes — a silent wrong answer, which is strictly worse
    // than no caching at all.
    //
    // So a client that wants retry safety supplies its own key and is
    // responsible for its uniqueness (a UUID). The `mcp:` prefix keeps this
    // namespace clear of the WebSocket's in the shared cache.
    if (const auto requestId = meta[MAGDA_META_REQUEST_ID]; !requestId.isVoid()) {
        if (!requestId.isString() || requestId.toString().isEmpty()) {
            error = McpError{MCP_INVALID_PARAMS,
                             juce::String(MAGDA_META_REQUEST_ID) + " must be a non-empty string",
                             {},
                             200};
            return std::nullopt;
        }
        context.requestId = "mcp:" + call.idempotencyScope + ":" + requestId.toString();
    }

    return context;
}

}  // namespace remote
}  // namespace magda
