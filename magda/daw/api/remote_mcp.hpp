#pragma once

#include <juce_core/juce_core.h>

#include <functional>
#include <optional>
#include <vector>

#include "remote_api.hpp"
#include "remote_changes.hpp"

namespace magda {
namespace remote {

class RemoteApiService;
class SubscriptionHub;

/**
 * @brief The Model Context Protocol, expressed over the shared operation registry.
 *
 * This file is the protocol and nothing else: no sockets, no `httplib.h`, no
 * knowledge of HTTP beyond the status code an error implies. `RemoteMcpServer`
 * carries it over Streamable HTTP, and a later stdio proxy would carry the same
 * object without changing a line of it.
 *
 * Every operation, schema, and error code comes from `OperationRegistry`. This
 * layer projects them into MCP's vocabulary — a registry operation becomes a
 * tool, a registry read operation becomes a resource — and translates MAGDA's
 * error taxonomy into MCP's two error channels. It defines no operations of its
 * own and reaches no manager or model object.
 *
 * ## Two eras
 *
 * Revision `2026-07-28` removed protocol-level sessions, the `initialize`
 * handshake, the standalone GET stream, and `resources/subscribe`. Every request
 * now carries its own version, identity, and capabilities in `params._meta`, and
 * long-lived notifications arrive on the response stream of a
 * `subscriptions/listen` request.
 *
 * MAGDA speaks both that revision and the handshake-based ones before it,
 * because the hosts worth connecting to are currently split across the change.
 * `McpEra` is the discriminator, and it is the only thing in this layer that
 * branches: the tool list, the resource table, the schemas, and the dispatch
 * path are identical in both eras. What differs is the envelope — whether a
 * result carries `resultType`, whether subscribing is a filter on a stream or a
 * verb against a session — which is exactly the part a transport revision is
 * allowed to change.
 */

/// Revisions this server implements, newest first. Order is load-bearing: it is
/// what `server/discover` advertises and what a legacy `initialize` falls back
/// to when the client asks for something unknown.
const std::vector<juce::String>& mcpProtocolVersions();

/**
 * @brief Which shape of the protocol a version implies.
 *
 * `Modern` is `2026-07-28` and later: stateless, per-request `_meta`.
 * `Legacy` is `2025-11-25` and earlier: an `initialize` handshake establishes a
 * session, and the version holds for its lifetime.
 */
enum class McpEra { Modern, Legacy };

McpEra eraForVersion(const juce::String& version);
bool isSupportedVersion(const juce::String& version);

/// The newest version this server implements — what an `initialize` naming
/// something unsupported is answered with, per the legacy negotiation rule.
const juce::String& latestProtocolVersion();

/**
 * @brief The version a legacy `initialize` settles on.
 *
 * The handshake rule from `2025-11-25`: echo the client's version when it is one
 * we implement, otherwise answer with the newest we do and let the client decide
 * whether it can live with that. It never fails — a legacy client has no
 * fall-forward mechanism, so an error here is a dead end where a counter-offer is
 * at least actionable.
 *
 * Only ever returns a legacy version. A client that asks for a modern one over
 * `initialize` is confused about the era rather than about the version, and
 * handing it `2026-07-28` would promise a handshake that revision does not have.
 */
juce::String negotiateLegacyVersion(const juce::var& initializeParams);

/**
 * @brief Who answers a given method.
 *
 * Most methods are pure protocol and belong to `McpEndpoint`. Three are not:
 * what a client is subscribed to is per-connection state, and a connection is
 * the one thing the endpoint deliberately knows nothing about — the same split
 * the WebSocket adapter makes with `subscriptions.*`.
 */
enum class McpRouting {
    /// `McpEndpoint::handle` answers it.
    Endpoint,
    /// `subscriptions/listen`: answered with a long-lived stream. Modern era.
    Stream,
    /// `resources/subscribe`. Legacy era.
    Subscribe,
    /// `resources/unsubscribe`. Legacy era.
    Unsubscribe,
};

McpRouting routeFor(const juce::String& method, McpEra era);

// ---------------------------------------------------------------------------
// Error codes
//
// -32020..-32099 is reserved by the MCP specification, and an implementation
// must not emit anything from it that the spec has not defined. These three are
// the whole of what it defines; MAGDA's own failures use the standard JSON-RPC
// codes and carry their MAGDA code in `data`.
// ---------------------------------------------------------------------------

inline constexpr int MCP_PARSE_ERROR = -32700;
inline constexpr int MCP_INVALID_REQUEST = -32600;
inline constexpr int MCP_METHOD_NOT_FOUND = -32601;
inline constexpr int MCP_INVALID_PARAMS = -32602;
inline constexpr int MCP_INTERNAL_ERROR = -32603;
inline constexpr int MCP_HEADER_MISMATCH = -32020;
inline constexpr int MCP_MISSING_CLIENT_CAPABILITY = -32021;
inline constexpr int MCP_UNSUPPORTED_PROTOCOL_VERSION = -32022;
/// A resource read the client's grant does not cover (#1860). Implementation-
/// defined because MCP has no permission code of its own, and deliberately not
/// `-32602`: "you may not read this" and "you asked for this wrongly" send a
/// client to two different places, and only one of them is the settings dialog.
inline constexpr int MCP_PERMISSION_DENIED = -32023;

// ---------------------------------------------------------------------------
// Reserved `_meta` keys
// ---------------------------------------------------------------------------

inline constexpr const char* MCP_META_PROTOCOL_VERSION = "io.modelcontextprotocol/protocolVersion";
inline constexpr const char* MCP_META_CLIENT_INFO = "io.modelcontextprotocol/clientInfo";
inline constexpr const char* MCP_META_CLIENT_CAPABILITIES =
    "io.modelcontextprotocol/clientCapabilities";
inline constexpr const char* MCP_META_SERVER_INFO = "io.modelcontextprotocol/serverInfo";
inline constexpr const char* MCP_META_SUBSCRIPTION_ID = "io.modelcontextprotocol/subscriptionId";

/**
 * MAGDA's own `_meta` extensions.
 *
 * MCP's envelope has nowhere to put an optimistic-concurrency token — the
 * WebSocket transport hangs one off a `meta` sibling of `params`, which MCP does
 * not have — so these ride in `_meta` under a vendor prefix. Any prefix whose
 * second label is `modelcontextprotocol` or `mcp` is reserved by the
 * specification, which is why this is a reverse-DNS name of ours rather than
 * something shorter.
 */
inline constexpr const char* MAGDA_META_EXPECTED_REVISION =
    "com.conceptualmachines.magda/expectedRevision";
inline constexpr const char* MAGDA_META_REVISION = "com.conceptualmachines.magda/revision";
inline constexpr const char* MAGDA_META_REQUEST_ID = "com.conceptualmachines.magda/requestId";

/**
 * @brief A JSON-RPC failure, plus the HTTP status a transport must answer with.
 *
 * The status is part of the error rather than the transport's own decision
 * because MCP specifies it: an unsupported version, a header mismatch, and a
 * missing `_meta` field are all `400`, and an unknown method is `404` — which is
 * not what a JSON-RPC transport would otherwise pick for any of them.
 */
struct McpError {
    int code = MCP_INTERNAL_ERROR;
    juce::String message;
    juce::var data;
    int httpStatus = 200;

    /// `{"code":…,"message":…,"data":…}` — the `error` member of a reply.
    juce::var toJson() const;
};

/// What a handled call produces: exactly one of a result value or an error.
struct McpReply {
    std::optional<McpError> error;
    juce::var result;

    static McpReply ok(juce::var result) {
        return {std::nullopt, std::move(result)};
    }
    static McpReply fail(McpError error) {
        return {std::move(error), {}};
    }
    static McpReply fail(int code, const juce::String& message, int httpStatus = 200,
                         juce::var data = {});

    bool failed() const {
        return error.has_value();
    }
};

/**
 * @brief One resource this server publishes, or a family of them.
 *
 * A resource is a read operation addressed by URI instead of by name. The
 * mapping is declared once, here, so `resources/list` and `resources/read`
 * cannot disagree about what exists — and so a URI that resolves to no operation
 * is a lookup failure rather than a dispatch of something invented.
 */
struct McpResource {
    juce::String uri;          ///< Empty for a template.
    juce::String uriTemplate;  ///< Empty for a fixed resource.
    juce::String name;
    juce::String title;
    juce::String description;
    /// The registry operation a read resolves to.
    juce::String operation;
    /// The topic whose movement invalidates this resource, if any. Absent means
    /// the resource is static within a run and never sends an update.
    std::optional<Topic> topic;
};

/**
 * @brief MCP over the shared remote API.
 *
 * Construct one per server. It is immutable after construction apart from the
 * dispatch it forwards, so it is safe to call `handle` from any number of
 * transport threads at once.
 */
class McpEndpoint {
  public:
    struct Options {
        juce::String serverName{"MAGDA"};
        juce::String serverVersion{"1.0"};
        /// How long a call may wait before the dispatcher abandons it. A client
        /// cannot ask for more.
        int defaultDeadlineMs = 10'000;
    };

    /// `subscriptions` is optional. Without one, resource subscriptions are not
    /// advertised as a capability and asking for them fails with a clear
    /// message; everything else works unchanged.
    McpEndpoint(RemoteApiService& service, Options options,
                SubscriptionHub* subscriptions = nullptr);

    /**
     * @brief One inbound JSON-RPC request, with what the transport learned about it.
     *
     * `era` and `protocolVersion` come from the transport because they arrive
     * differently in each era — in `_meta` and a header for a modern request, or
     * from the session an `initialize` established for a legacy one.
     *
     * `clientId` scopes the request in the dispatcher's audit and idempotency
     * records. It is the transport's own identifier for the caller, never
     * anything the client asserted about itself: `clientInfo` is self-reported
     * and the specification says not to make decisions on it.
     */
    struct Call {
        juce::String method;
        juce::var params;
        McpEra era = McpEra::Modern;
        juce::String protocolVersion;
        juce::String clientId;
        /**
         * What the client says it is, normalised — the key its grant is stored
         * under (#1860).
         *
         * This is `clientInfo.name`, and using it at all needs saying, because
         * the comment above says the specification tells servers not to act on
         * `clientInfo`. That rule is about *authorisation*: a server must not
         * decide who may connect from a field the client wrote. It does not,
         * and here it cannot — admission was the bearer token, before this
         * struct existed. What the name decides is which of the user's own
         * grants applies to a caller that is already inside, which is the only
         * way a user can say "my editor may edit, my monitoring script may not"
         * about two processes that present the same token.
         *
         * `clientId` remains the transport's identifier and is never this.
         */
        juce::String clientName;
        /// What that grant covers, as of this request. Resolved per call by the
        /// transport, so a revocation applies to the next one.
        ScopeSet scopes;
        /// Scopes the idempotency key so two clients cannot collide in it. Empty
        /// disables caching for this call — see `requestContext`.
        juce::String idempotencyScope;
    };

    using Completion = std::function<void(McpReply)>;

    /**
     * @brief Answer one call.
     *
     * `onComplete` runs exactly once: on the calling thread for anything decided
     * without touching the model, and on the message thread for a call that had
     * to dispatch. Never blocks, and never touches the message thread itself —
     * `RemoteApiService::dispatch` owns that hop.
     *
     * `subscriptions/listen` is *not* handled here: it answers with a stream
     * rather than a value, which is per-connection state the transport owns.
     * Use `isStreamMethod` to route it.
     */
    void handle(const Call& call, Completion onComplete);

    // -----------------------------------------------------------------------
    // Catalogue
    // -----------------------------------------------------------------------

    /// Every registry operation that is not transport-scoped, as an MCP tool.
    const std::vector<juce::var>& tools() const;

    /// Fixed resources and templates, in the order `resources/list` reports them.
    const std::vector<McpResource>& resources() const;

    /// What this server can do, in the shape the era expects.
    juce::var capabilities() const;

    /// The `server/discover` result. Modern era only.
    juce::var discoverResult() const;

    /// The `initialize` result for a negotiated version. Legacy era only.
    juce::var initializeResult(const juce::String& negotiatedVersion) const;

    /// `{"name":…,"version":…}`.
    juce::var serverInfo() const;

    // -----------------------------------------------------------------------
    // Resources and subscriptions
    // -----------------------------------------------------------------------

    /**
     * @brief The resource a URI names, or nullptr.
     *
     * Resolves fixed URIs and templates alike. A template match also yields the
     * operation input the id in the URI implies, through `resolveResource`.
     */
    const McpResource* findResource(const juce::String& uri) const;

    /// A URI resolved to the operation and input that reads it.
    struct ResolvedResource {
        const McpResource* resource = nullptr;
        juce::String operation;
        juce::var input;
    };

    /// Nullopt when the URI names nothing this server publishes.
    std::optional<ResolvedResource> resolveResource(const juce::String& uri) const;

    /// The topic whose movement invalidates `uri`, or nullopt when nothing does.
    std::optional<Topic> topicForResource(const juce::String& uri) const;

    /**
     * @brief What a client asked to be told about.
     *
     * Parsed rather than passed through so the acknowledgment can be honest: a
     * URI naming nothing, or naming a resource that never changes, is dropped
     * here and therefore absent from what the server says it agreed to.
     */
    struct ListenFilter {
        bool toolsListChanged = false;
        bool promptsListChanged = false;
        bool resourcesListChanged = false;
        std::vector<juce::String> resourceSubscriptions;

        bool wantsAnything() const;
    };

    ListenFilter parseListenFilter(const juce::var& params) const;

    /// The distinct topics a filter needs the subscription hub to watch.
    std::vector<Topic> topicsFor(const ListenFilter& filter) const;

    /// `notifications/subscriptions/acknowledged`, carrying the honoured filter.
    juce::var acknowledgment(const ListenFilter& filter, const juce::var& subscriptionId) const;

    /// `notifications/resources/updated` for one URI. `subscriptionId` is void
    /// in the legacy era, which has no such correlation.
    static juce::var resourceUpdated(const juce::String& uri, const juce::var& subscriptionId);

    /// The URIs in `filter` that `topic` invalidates. Empty is common and means
    /// this flush is not interesting to this client.
    std::vector<juce::String> urisAffectedBy(Topic topic, const ListenFilter& filter) const;

    /// The empty result that ends a `subscriptions/listen` gracefully.
    static juce::var listenClosedResult(const juce::var& subscriptionId);

    SubscriptionHub* subscriptions() const {
        return subscriptions_;
    }

  private:
    void callTool(const Call& call, Completion onComplete);
    void readResource(const Call& call, Completion onComplete);

    /// Build the dispatcher context from `_meta`. Nullopt when a field is
    /// present but unusable, which is a client error rather than a default to
    /// fall back on — `error` then says which field and why.
    std::optional<RequestContext> requestContext(const Call& call, McpError& error) const;

    RemoteApiService& service_;
    const Options options_;
    SubscriptionHub* const subscriptions_;

    std::vector<juce::var> tools_;
    std::vector<McpResource> resources_;
};

}  // namespace remote
}  // namespace magda
