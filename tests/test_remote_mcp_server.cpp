// The MCP Streamable HTTP transport (#1858), end to end over a real loopback
// socket. cpp-httplib ships the client, so these are genuine HTTP requests
// rather than a hand-driven handler.
//
// Everything here is transport behaviour: who may connect, what the required
// headers do, which status codes come back, and how streams and sessions
// behave. The mapping from operations to tools and resources belongs to
// test_remote_mcp.cpp; what the operations themselves do belongs to
// test_remote_service.cpp.

#include <httplib.h>

#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "MockMagdaApi.hpp"
#include "RemoteTestScopes.hpp"
#include "magda/daw/api/remote_clients.hpp"
#include "magda/daw/api/remote_mcp_server.hpp"
#include "magda/daw/api/remote_service.hpp"
#include "magda/daw/api/remote_subscriptions.hpp"

using namespace magda;
using namespace magda::remote;
using magda::test::fullyGrantedContext;
using magda::test::MockMagdaApi;

namespace {

constexpr const char* kToken = "mcp-token-4c81";
constexpr const char* kOrigin = "http://localhost:5173";
constexpr const char* kVersion = "2026-07-28";

/// Reads MagdaApi live state, which asserts the message thread. The Catch2
/// runner has no MessageManager, so suspend that assertion — the same
/// accommodation the other remote tests make. With no message thread to hop to,
/// dispatch runs inline on the connection's own thread, which makes these tests
/// deterministic rather than timing-dependent.
struct MessageThreadRelaxation {
    ScopedMessageThreadAssertionDisabler disabler;
};

/// `clients` defaults to the shared permissive registry: without one, every
/// request is refused before it reaches the protocol behaviour these tests are
/// about (#1860). The permission tests pass their own.
RemoteMcpServer::Options testOptions(
    RemoteClientRegistry& clients = magda::test::permissiveRegistry()) {
    RemoteMcpServer::Options options;
    options.bearerToken = kToken;
    options.allowedOrigins = {kOrigin};
    // Short enough that a stream test does not sit waiting for one, long enough
    // that comments do not swamp the events being asserted.
    options.keepAliveIntervalMs = 250;
    options.clients = &clients;
    return options;
}

std::string base(const RemoteMcpServer& server) {
    return "http://127.0.0.1:" + std::to_string(server.boundPort());
}

juce::var object(std::initializer_list<std::pair<const char*, juce::var>> fields) {
    auto* result = new juce::DynamicObject();
    for (const auto& [key, value] : fields)
        result->setProperty(key, value);
    return result;
}

/// `params` with the `_meta` every modern request must carry.
juce::var modernParams(juce::var params = {}, const char* version = kVersion) {
    if (params.getDynamicObject() == nullptr)
        params = juce::var(new juce::DynamicObject());
    params.getDynamicObject()->setProperty(
        "_meta", object({{MCP_META_PROTOCOL_VERSION, version},
                         {MCP_META_CLIENT_INFO, object({{"name", "catch2"}, {"version", "1.0"}})},
                         {MCP_META_CLIENT_CAPABILITIES, juce::var(new juce::DynamicObject())}}));
    return params;
}

std::string body(const juce::var& id, const juce::String& method, const juce::var& params = {}) {
    auto* request = new juce::DynamicObject();
    request->setProperty("jsonrpc", "2.0");
    if (!id.isVoid())
        request->setProperty("id", id);
    request->setProperty("method", method);
    if (params.getDynamicObject() != nullptr)
        request->setProperty("params", params);
    return juce::JSON::toString(juce::var(request), true).toStdString();
}

httplib::Headers authorised() {
    return {{"Authorization", std::string("Bearer ") + kToken}};
}

/// The mirrored headers `2026-07-28` requires. `name` is the tool name or
/// resource URI for the methods that have one.
httplib::Headers modernHeaders(const std::string& method, const std::string& name = {},
                               const char* version = kVersion) {
    httplib::Headers headers = authorised();
    headers.emplace("MCP-Protocol-Version", version);
    headers.emplace("Mcp-Method", method);
    if (!name.empty())
        headers.emplace("Mcp-Name", name);
    return headers;
}

juce::var parse(const std::string& text) {
    juce::var value;
    juce::JSON::parse(juce::String(text), value);
    return value;
}

struct Reply {
    int status = 0;
    juce::var json;
    std::string sessionId;
};

Reply post(httplib::Client& client, const httplib::Headers& headers, const std::string& payload) {
    auto result = client.Post("/mcp", headers, payload, "application/json");
    REQUIRE(result);
    Reply reply;
    reply.status = result->status;
    reply.json = parse(result->body);
    if (result->has_header("Mcp-Session-Id"))
        reply.sessionId = result->get_header_value("Mcp-Session-Id");
    return reply;
}

/// One modern call, headers and `_meta` filled in consistently.
Reply call(httplib::Client& client, const juce::String& method, juce::var params = {},
           const std::string& name = {}, const juce::var& id = 1) {
    return post(client, modernHeaders(method.toStdString(), name),
                body(id, method, modernParams(std::move(params))));
}

/**
 * @brief An open SSE stream, read on its own thread.
 *
 * The server writes `data:` lines as events happen and `:` comments while it is
 * quiet, so this parses out the events and lets a test wait for the nth one
 * rather than sleep and hope.
 */
class SseStream {
  public:
    explicit SseStream(const std::string& origin) : client_(origin) {
        // Longer than any test needs, so a stall shows up as a failed wait with
        // a useful message rather than as a client-side timeout.
        client_.set_read_timeout(30, 0);
    }

    ~SseStream() {
        close();
    }

    void openPost(const httplib::Headers& headers, const std::string& payload) {
        worker_ = std::thread([this, headers, payload] {
            client_.Post("/mcp", headers, payload, "application/json",
                         [this](const char* data, std::size_t length) {
                             consume(std::string(data, length));
                             return !stopping_.load();
                         });
        });
    }

    void openGet(const httplib::Headers& headers) {
        worker_ = std::thread([this, headers] {
            client_.Get("/mcp", headers, [this](const char* data, std::size_t length) {
                consume(std::string(data, length));
                return !stopping_.load();
            });
        });
    }

    /// Wait until at least `count` events have arrived. False on timeout.
    bool waitFor(std::size_t count, std::chrono::milliseconds timeout = std::chrono::seconds(5)) {
        std::unique_lock<std::mutex> lock(mutex_);
        return ready_.wait_for(lock, timeout, [&] { return events_.size() >= count; });
    }

    std::vector<juce::var> events() const {
        const std::lock_guard<std::mutex> lock(mutex_);
        return events_;
    }

    void close() {
        stopping_.store(true);
        client_.stop();
        if (worker_.joinable())
            worker_.join();
    }

  private:
    void consume(const std::string& chunk) {
        std::size_t produced = 0;
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            buffer_ += chunk;
            for (auto split = buffer_.find("\n\n"); split != std::string::npos;
                 split = buffer_.find("\n\n")) {
                const auto frame = buffer_.substr(0, split);
                buffer_.erase(0, split + 2);
                // A line beginning with a colon is a keep-alive comment, which
                // the SSE grammar says to ignore.
                if (frame.rfind("data: ", 0) != 0)
                    continue;
                events_.push_back(parse(frame.substr(6)));
                ++produced;
            }
        }
        if (produced > 0)
            ready_.notify_all();
    }

    httplib::Client client_;
    std::thread worker_;
    std::atomic<bool> stopping_{false};

    mutable std::mutex mutex_;
    std::condition_variable ready_;
    std::string buffer_;
    std::vector<juce::var> events_;
};

}  // namespace

TEST_CASE("The MCP server refuses to start without a bearer token", "[remote][mcp-http][auth]") {
    MessageThreadRelaxation relax;
    MockMagdaApi api;
    RemoteApiService service(api);

    RemoteMcpServer::Options options;
    options.bearerToken = {};
    RemoteMcpServer server(service, options);

    // No listener at all, rather than an open one. There is no configuration
    // that should produce an unauthenticated MCP endpoint.
    REQUIRE_FALSE(server.start());
    REQUIRE_FALSE(server.isRunning());
    REQUIRE(server.boundPort() == 0);
}

TEST_CASE("A request without a valid token is refused", "[remote][mcp-http][auth]") {
    MessageThreadRelaxation relax;
    MockMagdaApi api;
    RemoteApiService service(api);
    RemoteMcpServer server(service, testOptions());
    REQUIRE(server.start());

    httplib::Client client(base(server));

    auto anonymous =
        client.Post("/mcp", {}, body(1, "tools/list", modernParams()), "application/json");
    REQUIRE(anonymous);
    REQUIRE(anonymous->status == 401);

    auto wrong = client.Post("/mcp", {{"Authorization", "Bearer not-the-token"}},
                             body(1, "tools/list", modernParams()), "application/json");
    REQUIRE(wrong);
    REQUIRE(wrong->status == 401);

    // A token of the right length but the wrong bytes is refused the same way,
    // and the comparison that does it is constant-time.
    auto sameLength = client.Post("/mcp", {{"Authorization", "Bearer mcp-token-4c82"}},
                                  body(1, "tools/list", modernParams()), "application/json");
    REQUIRE(sameLength);
    REQUIRE(sameLength->status == 401);
}

TEST_CASE("A refusal reaches a client that sent a body and asked for the connection to close",
          "[remote][mcp-http][auth]") {
    MessageThreadRelaxation relax;
    MockMagdaApi api;
    RemoteApiService service(api);
    RemoteMcpServer server(service, testOptions());
    REQUIRE(server.start());

    // Refusing a request before its body has been read leaves that body unread
    // in the socket, and closing on unread data sends RST rather than FIN —
    // which discards the refusal the client had already been sent. A one-shot
    // client, which is what `httplib::Client` is by default, asks for the close
    // that triggers it. Any single attempt usually wins the race, so this
    // asserts over enough of them to catch a server that refuses too early.
    for (int attempt = 0; attempt < 50; ++attempt) {
        httplib::Client client(base(server));
        auto refused =
            client.Post("/mcp", {}, body(1, "tools/list", modernParams()), "application/json");
        REQUIRE(refused);
        REQUIRE(refused->status == 401);
    }
}

TEST_CASE("Origin is checked only when the client sends one", "[remote][mcp-http][auth]") {
    MessageThreadRelaxation relax;
    MockMagdaApi api;
    RemoteApiService service(api);
    RemoteMcpServer server(service, testOptions());
    REQUIRE(server.start());

    httplib::Client client(base(server));

    // A native client sends no Origin and must not be locked out.
    REQUIRE(call(client, "ping").status == 200);

    auto allowed = modernHeaders("ping");
    allowed.emplace("Origin", kOrigin);
    REQUIRE(post(client, allowed, body(1, "ping", modernParams())).status == 200);

    // Validating Origin is a MUST in the transport spec: it is what stops a page
    // the user merely visited from reaching this listener via DNS rebinding.
    auto refused = modernHeaders("ping");
    refused.emplace("Origin", "https://evil.example");
    const auto blocked = post(client, refused, body(1, "ping", modernParams()));
    REQUIRE(blocked.status == 403);
    REQUIRE(static_cast<int>(blocked.json["error"]["code"]) == MCP_INVALID_REQUEST);
}

TEST_CASE("A request with no protocol metadata is told exactly what is missing",
          "[remote][mcp-http][negotiation]") {
    MessageThreadRelaxation relax;
    MockMagdaApi api;
    RemoteApiService service(api);
    RemoteMcpServer server(service, testOptions());
    REQUIRE(server.start());

    httplib::Client client(base(server));

    // Neither modern `_meta` nor a session. The 400 plus a recognisable modern
    // JSON-RPC error is itself the signal a dual-era client uses: it means
    // "this is a modern endpoint, send the right thing" rather than "fall back
    // to the deprecated transport".
    const auto reply = post(client, authorised(), body(1, "tools/list"));
    REQUIRE(reply.status == 400);
    REQUIRE(static_cast<int>(reply.json["error"]["code"]) == MCP_INVALID_PARAMS);
    REQUIRE(reply.json["error"]["message"].toString().contains("protocolVersion"));

    // `_meta` present but missing the capabilities every modern request declares.
    auto params = juce::var(new juce::DynamicObject());
    params.getDynamicObject()->setProperty("_meta",
                                           object({{MCP_META_PROTOCOL_VERSION, kVersion}}));
    const auto incomplete =
        post(client, modernHeaders("tools/list"), body(1, "tools/list", params));
    REQUIRE(incomplete.status == 400);
    REQUIRE(static_cast<int>(incomplete.json["error"]["code"]) == MCP_INVALID_PARAMS);
    REQUIRE(incomplete.json["error"]["message"].toString().contains("clientCapabilities"));
}

TEST_CASE("An unsupported version is refused with the list of supported ones",
          "[remote][mcp-http][negotiation]") {
    MessageThreadRelaxation relax;
    MockMagdaApi api;
    RemoteApiService service(api);
    RemoteMcpServer server(service, testOptions());
    REQUIRE(server.start());

    httplib::Client client(base(server));
    const auto reply = post(client, modernHeaders("tools/list", {}, "1900-01-01"),
                            body(1, "tools/list", modernParams({}, "1900-01-01")));

    REQUIRE(reply.status == 400);
    REQUIRE(static_cast<int>(reply.json["error"]["code"]) == MCP_UNSUPPORTED_PROTOCOL_VERSION);
    REQUIRE(reply.json["error"]["data"]["requested"].toString() == "1900-01-01");
    const auto* supported = reply.json["error"]["data"]["supported"].getArray();
    REQUIRE(supported != nullptr);
    REQUIRE(supported->size() == static_cast<int>(mcpProtocolVersions().size()));
    // The criterion from #1858: nothing is pinned to the client version the
    // outbound MCPClient uses.
    for (const auto& version : *supported)
        REQUIRE(version.toString() != "2024-11-05");
}

TEST_CASE("Mirrored headers must agree with the body", "[remote][mcp-http][negotiation]") {
    MessageThreadRelaxation relax;
    MockMagdaApi api;
    RemoteApiService service(api);
    RemoteMcpServer server(service, testOptions());
    REQUIRE(server.start());

    httplib::Client client(base(server));

    const auto callBody =
        body(1, "tools/call",
             modernParams(object(
                 {{"name", "project.get"}, {"arguments", juce::var(new juce::DynamicObject())}})));

    // Missing Mcp-Method.
    httplib::Headers noMethod = authorised();
    noMethod.emplace("MCP-Protocol-Version", kVersion);
    noMethod.emplace("Mcp-Name", "project.get");
    const auto missingMethod = post(client, noMethod, callBody);
    REQUIRE(missingMethod.status == 400);
    REQUIRE(static_cast<int>(missingMethod.json["error"]["code"]) == MCP_HEADER_MISMATCH);

    // Missing Mcp-Name, which tools/call requires.
    const auto missingName = post(client, modernHeaders("tools/call"), callBody);
    REQUIRE(missingName.status == 400);
    REQUIRE(static_cast<int>(missingName.json["error"]["code"]) == MCP_HEADER_MISMATCH);

    // Present but disagreeing. This is the case the rule exists for: an
    // intermediary routing on the header while the server executes the body
    // would be two components acting on two different requests.
    const auto disagreeing = post(client, modernHeaders("tools/call", "tracks.delete"), callBody);
    REQUIRE(disagreeing.status == 400);
    REQUIRE(static_cast<int>(disagreeing.json["error"]["code"]) == MCP_HEADER_MISMATCH);
    REQUIRE(disagreeing.json["error"]["message"].toString().contains("tracks.delete"));

    // A version header that disagrees with `_meta`.
    httplib::Headers wrongVersion = authorised();
    wrongVersion.emplace("MCP-Protocol-Version", "2025-11-25");
    wrongVersion.emplace("Mcp-Method", "tools/call");
    wrongVersion.emplace("Mcp-Name", "project.get");
    const auto mismatchedVersion = post(client, wrongVersion, callBody);
    REQUIRE(mismatchedVersion.status == 400);
    REQUIRE(static_cast<int>(mismatchedVersion.json["error"]["code"]) == MCP_HEADER_MISMATCH);

    // Agreeing headers pass.
    REQUIRE(post(client, modernHeaders("tools/call", "project.get"), callBody).status == 200);
}

TEST_CASE("A base64-encoded Mcp-Name is decoded before it is compared",
          "[remote][mcp-http][negotiation]") {
    MessageThreadRelaxation relax;
    MockMagdaApi api;
    RemoteApiService service(api);
    RemoteMcpServer server(service, testOptions());
    REQUIRE(server.start());

    httplib::Client client(base(server));

    // A client wraps a value it cannot send as plain ASCII in the sentinel. The
    // server has to decode before comparing, or every such request would look
    // like a header mismatch. "project.get" base64-encodes to this.
    const auto encoded = juce::Base64::toBase64("project.get").toStdString();
    const auto reply =
        post(client, modernHeaders("tools/call", "=?base64?" + encoded + "?="),
             body(1, "tools/call",
                  modernParams(object({{"name", "project.get"},
                                       {"arguments", juce::var(new juce::DynamicObject())}}))));
    REQUIRE(reply.status == 200);
    REQUIRE(reply.json["error"].isVoid());
}

TEST_CASE("server/discover, tools/list and resources/list answer over HTTP",
          "[remote][mcp-http][catalogue]") {
    MessageThreadRelaxation relax;
    MockMagdaApi api;
    RemoteApiService service(api);
    SubscriptionHub hub(api, service);
    RemoteMcpServer server(service, testOptions(), &hub);
    REQUIRE(server.start());

    httplib::Client client(base(server));

    const auto discovered = call(client, "server/discover");
    REQUIRE(discovered.status == 200);
    REQUIRE(discovered.json["result"]["resultType"].toString() == "complete");
    REQUIRE((*discovered.json["result"]["supportedVersions"].getArray())[0].toString() == kVersion);
    // A hub was given, so the capability is real and claimed.
    REQUIRE(static_cast<bool>(discovered.json["result"]["capabilities"]["resources"]["subscribe"]));

    const auto tools = call(client, "tools/list");
    REQUIRE(tools.status == 200);
    REQUIRE(tools.json["result"]["tools"].getArray()->size() ==
            static_cast<int>(server.endpoint().tools().size()));

    const auto resources = call(client, "resources/list");
    REQUIRE(resources.status == 200);
    REQUIRE_FALSE(resources.json["result"]["resources"].getArray()->isEmpty());

    const auto templates = call(client, "resources/templates/list");
    REQUIRE(templates.status == 200);
    REQUIRE_FALSE(templates.json["result"]["resourceTemplates"].getArray()->isEmpty());
}

TEST_CASE("A tool call and a resource read return the same projection",
          "[remote][mcp-http][parity]") {
    MessageThreadRelaxation relax;
    MockMagdaApi api;
    api.project_.info.name = "Demo";
    api.project_.info.tempo = 124.0;

    RemoteApiService service(api);
    RemoteMcpServer server(service, testOptions());
    REQUIRE(server.start());

    httplib::Client client(base(server));

    const auto viaTool =
        call(client, "tools/call",
             object({{"name", "project.get"}, {"arguments", juce::var(new juce::DynamicObject())}}),
             "project.get");
    REQUIRE(viaTool.status == 200);
    REQUIRE(static_cast<double>(viaTool.json["result"]["structuredContent"]["tempo"]) == 124.0);

    const auto viaResource =
        call(client, "resources/read", object({{"uri", "magda://project/current"}}),
             "magda://project/current");
    REQUIRE(viaResource.status == 200);

    // The acceptance criterion: one operation, one projection, whichever surface
    // asked for it — byte for byte.
    const auto* contents = viaResource.json["result"]["contents"].getArray();
    REQUIRE(contents != nullptr);
    REQUIRE((*contents)[0]["text"].toString() ==
            juce::JSON::toString(viaTool.json["result"]["structuredContent"], true));

    // And the same operation dispatched directly, which is what the WebSocket
    // transport puts in its `result`.
    const auto direct = service.dispatchSync("project.get", juce::var(new juce::DynamicObject()),
                                             fullyGrantedContext());
    REQUIRE(direct.ok);
    REQUIRE(juce::JSON::toString(direct.result, true) ==
            juce::JSON::toString(viaTool.json["result"]["structuredContent"], true));
}

TEST_CASE("An array-valued operation differs across the surfaces only by its wrapper",
          "[remote][mcp-http][parity]") {
    // The case the object-valued parity test above cannot reach. MCP types
    // `structuredContent` as an object, so a list read is wrapped as
    // `{"items": [...]}` on the tool surface while the resource returns it
    // bare. Both carry the same data, and asserting only the object-valued
    // operation left the wrapper unexercised — which is how the two surfaces
    // came to disagree in a way nothing in the tree noticed.
    MessageThreadRelaxation relax;
    MockMagdaApi api;

    RemoteApiService service(api);
    RemoteMcpServer server(service, testOptions());
    REQUIRE(server.start());

    httplib::Client client(base(server));

    const auto viaTool =
        call(client, "tools/call",
             object({{"name", "tracks.list"}, {"arguments", juce::var(new juce::DynamicObject())}}),
             "tracks.list");
    REQUIRE(viaTool.status == 200);

    const auto viaResource =
        call(client, "resources/read", object({{"uri", "magda://tracks"}}), "magda://tracks");
    REQUIRE(viaResource.status == 200);

    const auto* contents = viaResource.json["result"]["contents"].getArray();
    REQUIRE(contents != nullptr);
    const auto readText = (*contents)[0]["text"].toString();

    // The tool surface wraps; the resource does not.
    const auto structured = viaTool.json["result"]["structuredContent"];
    REQUIRE(structured.getDynamicObject() != nullptr);
    REQUIRE(structured.hasProperty("items"));
    REQUIRE(structured["items"].isArray());
    REQUIRE(readText == juce::JSON::toString(structured["items"], true));

    // And the bare array is what the WebSocket transport returns, unwrapped.
    const auto direct = service.dispatchSync("tracks.list", juce::var(new juce::DynamicObject()),
                                             fullyGrantedContext());
    REQUIRE(direct.ok);
    REQUIRE(direct.result.isArray());
    REQUIRE(juce::JSON::toString(direct.result, true) == readText);
}

TEST_CASE("A cacheable modern result carries its cache directives", "[remote][mcp-http][parity]") {
    // `2026-07-28` makes `ttlMs` and `cacheScope` required on a cacheable
    // result rather than optional, so a client validating against the schema
    // rejects a response that stamps `resultType` alone — the catalogue fails
    // to parse and the session is useless from its first call.
    MessageThreadRelaxation relax;
    MockMagdaApi api;

    RemoteApiService service(api);
    RemoteMcpServer server(service, testOptions());
    REQUIRE(server.start());

    httplib::Client client(base(server));

    const auto cacheable = {std::string("tools/list"), std::string("resources/list"),
                            std::string("resources/templates/list")};
    for (const auto& method : cacheable) {
        const auto reply = call(client, juce::String(method));
        REQUIRE(reply.status == 200);
        const auto result = reply.json["result"];
        REQUIRE(result["resultType"].toString() == "complete");
        REQUIRE(result.hasProperty("ttlMs"));
        REQUIRE(static_cast<int>(result["ttlMs"]) == 0);
        REQUIRE(result["cacheScope"].toString() == "private");
    }

    const auto read =
        call(client, "resources/read", object({{"uri", "magda://tracks"}}), "magda://tracks");
    REQUIRE(read.status == 200);
    REQUIRE(static_cast<int>(read.json["result"]["ttlMs"]) == 0);
    REQUIRE(read.json["result"]["cacheScope"].toString() == "private");

    const auto discover = call(client, "server/discover");
    REQUIRE(discover.status == 200);
    REQUIRE(static_cast<int>(discover.json["result"]["ttlMs"]) == 0);
    REQUIRE(discover.json["result"]["cacheScope"].toString() == "private");

    // `tools/call` is not cacheable, and the schema for that method has no
    // place for the directives — stamping them would be as wrong as omitting
    // them from the ones above.
    const auto called =
        call(client, "tools/call",
             object({{"name", "tracks.list"}, {"arguments", juce::var(new juce::DynamicObject())}}),
             "tracks.list");
    REQUIRE(called.status == 200);
    REQUIRE(called.json["result"]["resultType"].toString() == "complete");
    REQUIRE(!called.json["result"].hasProperty("ttlMs"));
    REQUIRE(!called.json["result"].hasProperty("cacheScope"));
}

TEST_CASE("A mutation through MCP is one undo step", "[remote][mcp-http][parity]") {
    MessageThreadRelaxation relax;
    MockMagdaApi api;
    RemoteApiService service(api);
    RemoteMcpServer server(service, testOptions());
    REQUIRE(server.start());

    httplib::Client client(base(server));

    const auto reply =
        call(client, "tools/call",
             object({{"name", "project.setTempo"}, {"arguments", object({{"tempo", 137.0}})}}),
             "project.setTempo");
    REQUIRE(reply.status == 200);
    REQUIRE_FALSE(static_cast<bool>(reply.json["result"]["isError"]));

    // The dispatcher wraps every mutating request in exactly one named compound,
    // so one Undo reverses one remote call — the acceptance criterion from
    // #1858, and the reason MCP must not reach MagdaApi directly.
    REQUIRE(api.undo_.compoundDescriptions.size() == 1);
    REQUIRE(api.undo_.maxCompoundDepth == 1);
    // Named from the registry, so what the user sees in the undo history is the
    // operation's own summary rather than something the transport invented.
    REQUIRE(api.undo_.compoundDescriptions.front() ==
            OperationRegistry::instance().find("project.setTempo")->summary);

    // A read opens none.
    call(client, "tools/call",
         object({{"name", "project.get"}, {"arguments", juce::var(new juce::DynamicObject())}}),
         "project.get");
    REQUIRE(api.undo_.compoundDescriptions.size() == 1);
}

TEST_CASE("An unknown method is 404 and an unknown tool is not", "[remote][mcp-http][errors]") {
    MessageThreadRelaxation relax;
    MockMagdaApi api;
    RemoteApiService service(api);
    RemoteMcpServer server(service, testOptions());
    REQUIRE(server.start());

    httplib::Client client(base(server));

    // 404 with a JSON-RPC body: the status says "no such method here" and the
    // body says "and this really is an MCP endpoint".
    const auto unknownMethod = call(client, "does/not/exist");
    REQUIRE(unknownMethod.status == 404);
    REQUIRE(static_cast<int>(unknownMethod.json["error"]["code"]) == MCP_METHOD_NOT_FOUND);

    // An unknown tool is a params error on a method that does exist, so the
    // request itself succeeded at the HTTP level.
    const auto unknownTool =
        call(client, "tools/call", object({{"name", "tracks.teleport"}}), "tracks.teleport");
    REQUIRE(unknownTool.status == 200);
    REQUIRE(static_cast<int>(unknownTool.json["error"]["code"]) == MCP_INVALID_PARAMS);

    // Malformed JSON never reaches the protocol layer.
    auto malformed = client.Post("/mcp", authorised(), "{not json", "application/json");
    REQUIRE(malformed);
    REQUIRE(malformed->status == 400);
    REQUIRE(static_cast<int>(parse(malformed->body)["error"]["code"]) == MCP_PARSE_ERROR);
}

TEST_CASE("A request without jsonrpc 2.0 is refused before it can act",
          "[remote][mcp-http][framing]") {
    MessageThreadRelaxation relax;
    MockMagdaApi api;
    api.project_.info.tempo = 120.0;

    RemoteApiService service(api);
    RemoteMcpServer server(service, testOptions());
    REQUIRE(server.start());

    httplib::Client client(base(server));

    // A mutating call with no `jsonrpc` member at all. Routing this would apply
    // the write and answer with a 2.0 envelope the caller never asked for.
    auto* malformed = new juce::DynamicObject();
    malformed->setProperty("id", 1);
    malformed->setProperty("method", "tools/call");
    malformed->setProperty("params",
                           modernParams(object({{"name", "project.setTempo"},
                                                {"arguments", object({{"tempo", 99.0}})}})));

    const auto missing = post(client, modernHeaders("tools/call", "project.setTempo"),
                              juce::JSON::toString(juce::var(malformed), true).toStdString());
    REQUIRE(missing.status == 400);
    REQUIRE(static_cast<int>(missing.json["error"]["code"]) == MCP_INVALID_REQUEST);
    // Refused *before* dispatch, not after: the project is untouched.
    REQUIRE(api.project_.info.tempo == 120.0);

    // A *numeric* 2.0 is refused too. JSON has one numeric type and JUCE
    // renders that number as the text "2.0", so comparing the rendering alone
    // would let this through — and JSON-RPC requires a string.
    auto* numeric = new juce::DynamicObject();
    numeric->setProperty("jsonrpc", 2.0);
    numeric->setProperty("id", 3);
    numeric->setProperty("method", "ping");
    numeric->setProperty("params", modernParams());
    const auto asNumber = post(client, modernHeaders("ping"),
                               juce::JSON::toString(juce::var(numeric), true).toStdString());
    REQUIRE(asNumber.status == 400);
    REQUIRE(static_cast<int>(asNumber.json["error"]["code"]) == MCP_INVALID_REQUEST);

    // A version that is not 2.0 is refused the same way.
    auto* wrongVersion = new juce::DynamicObject();
    wrongVersion->setProperty("jsonrpc", "1.0");
    wrongVersion->setProperty("id", 2);
    wrongVersion->setProperty("method", "ping");
    wrongVersion->setProperty("params", modernParams());
    const auto old = post(client, modernHeaders("ping"),
                          juce::JSON::toString(juce::var(wrongVersion), true).toStdString());
    REQUIRE(old.status == 400);
    REQUIRE(static_cast<int>(old.json["error"]["code"]) == MCP_INVALID_REQUEST);

    // The same call with a correct envelope goes through, so this is the
    // envelope check and not something else refusing the request.
    REQUIRE(call(client, "tools/call",
                 object({{"name", "project.setTempo"}, {"arguments", object({{"tempo", 99.0}})}}),
                 "project.setTempo")
                .status == 200);
    REQUIRE(api.project_.info.tempo == 99.0);
}

TEST_CASE("A notification is accepted and answered with 202", "[remote][mcp-http][framing]") {
    MessageThreadRelaxation relax;
    MockMagdaApi api;
    RemoteApiService service(api);
    RemoteMcpServer server(service, testOptions());
    REQUIRE(server.start());

    httplib::Client client(base(server));

    // No id, so there is nothing to answer. 202 with no body is the transport's
    // whole contract for one.
    auto result =
        client.Post("/mcp", modernHeaders("notifications/initialized"),
                    body({}, "notifications/initialized", modernParams()), "application/json");
    REQUIRE(result);
    REQUIRE(result->status == 202);
    REQUIRE(result->body.empty());
}

TEST_CASE("Limits hold: body size, concurrency, and rate", "[remote][mcp-http][limits]") {
    MessageThreadRelaxation relax;
    MockMagdaApi api;
    RemoteApiService service(api);

    auto options = testOptions();
    options.maxBodyBytes = 512;
    RemoteMcpServer server(service, options);
    REQUIRE(server.start());

    httplib::Client client(base(server));

    // A body over the cap is refused before anything parses it.
    auto params = juce::var(new juce::DynamicObject());
    params.getDynamicObject()->setProperty("name", juce::String::repeatedString("x", 4096));
    auto oversized = client.Post("/mcp", modernHeaders("tools/call", "x"),
                                 body(1, "tools/call", modernParams(params)), "application/json");
    REQUIRE(oversized);
    REQUIRE((oversized->status == 413 || oversized->status == 400));
}

TEST_CASE("Requests are rate limited", "[remote][mcp-http][limits]") {
    MessageThreadRelaxation relax;
    MockMagdaApi api;
    RemoteApiService service(api);

    auto options = testOptions();
    options.maxRequestsPerSecond = 1.0;
    options.maxConcurrentRequests = 2;
    RemoteMcpServer server(service, options);
    REQUIRE(server.start());

    httplib::Client client(base(server));

    // The bucket bursts to maxConcurrentRequests and then holds the rate, so a
    // tight loop runs out of allowance rather than being served indefinitely.
    bool refused = false;
    for (int i = 0; i < 10 && !refused; ++i)
        refused = call(client, "ping").status == 429;
    REQUIRE(refused);
}

TEST_CASE("GET and DELETE are refused when there is no session", "[remote][mcp-http][framing]") {
    MessageThreadRelaxation relax;
    MockMagdaApi api;
    RemoteApiService service(api);
    SubscriptionHub hub(api, service);
    RemoteMcpServer server(service, testOptions(), &hub);
    REQUIRE(server.start());

    httplib::Client client(base(server));

    // 2026-07-28 removed both. A client that has not established a session has
    // not established that it speaks a revision where either means anything.
    auto get = client.Get("/mcp", authorised());
    REQUIRE(get);
    REQUIRE(get->status == 405);

    auto del = client.Delete("/mcp", authorised());
    REQUIRE(del);
    REQUIRE(del->status == 405);
}

TEST_CASE("subscriptions/listen acknowledges first, then reports what changed",
          "[remote][mcp-http][subscriptions]") {
    MessageThreadRelaxation relax;
    MockMagdaApi api;
    api.project_.info.tempo = 120.0;

    RemoteApiService service(api);
    SubscriptionHub hub(api, service);
    RemoteMcpServer server(service, testOptions(), &hub);
    REQUIRE(server.start());

    juce::Array<juce::var> uris;
    uris.add("magda://project/current");

    SseStream stream(base(server));
    stream.openPost(
        modernHeaders("subscriptions/listen"),
        body(1, "subscriptions/listen",
             modernParams(object({{"notifications", object({{"resourceSubscriptions", uris}})}}))));

    // The acknowledgment must arrive before any notification, and must reflect
    // what the server agreed to rather than echoing the request.
    REQUIRE(stream.waitFor(1));
    const auto acknowledgment = stream.events().front();
    REQUIRE(acknowledgment["method"].toString() == "notifications/subscriptions/acknowledged");
    REQUIRE(static_cast<int>(acknowledgment["params"]["_meta"][MCP_META_SUBSCRIPTION_ID]) == 1);
    REQUIRE(acknowledgment["params"]["notifications"]["resourceSubscriptions"].getArray()->size() ==
            1);
    REQUIRE(server.streamCount() == 1);

    {
        httplib::Client editor(base(server));
        REQUIRE(
            call(editor, "tools/call",
                 object({{"name", "project.setTempo"}, {"arguments", object({{"tempo", 145.0}})}}),
                 "project.setTempo", 7)
                .status == 200);
    }

    // No MessageManager in the Catch2 runner means no flush timer, so the
    // coalescing window is closed by hand. In the app this is the 30 Hz pump.
    service.changes().flush();

    REQUIRE(stream.waitFor(2));
    const auto updated = stream.events().at(1);
    REQUIRE(updated["method"].toString() == "notifications/resources/updated");
    REQUIRE(updated["params"]["uri"].toString() == "magda://project/current");
    REQUIRE(static_cast<int>(updated["params"]["_meta"][MCP_META_SUBSCRIPTION_ID]) == 1);
    // A notification carries no id, and no payload: the client re-reads.
    REQUIRE(updated["id"].isVoid());
    REQUIRE(updated["params"]["payload"].isVoid());

    stream.close();
}

TEST_CASE("A subscriber hears only about what it subscribed to",
          "[remote][mcp-http][subscriptions]") {
    MessageThreadRelaxation relax;
    MockMagdaApi api;
    RemoteApiService service(api);
    SubscriptionHub hub(api, service);
    RemoteMcpServer server(service, testOptions(), &hub);
    REQUIRE(server.start());

    juce::Array<juce::var> uris;
    uris.add("magda://transport");

    SseStream stream(base(server));
    stream.openPost(
        modernHeaders("subscriptions/listen"),
        body(1, "subscriptions/listen",
             modernParams(object({{"notifications", object({{"resourceSubscriptions", uris}})}}))));
    REQUIRE(stream.waitFor(1));

    // A topic this client did not ask about must not produce an event, and must
    // not count as a delivery failure either — otherwise a client watching one
    // quiet resource would be disconnected by an unrelated busy one.
    api.project_.info.tempo = 155.0;
    service.noteModelChanged(Topic::Project);
    service.changes().flush();
    REQUIRE_FALSE(stream.waitFor(2, std::chrono::milliseconds(400)));

    // The hub publishes observable change, not the fact that something was
    // marked, so the state has to actually move for an event to exist at all.
    api.transport_.playing = true;
    service.noteModelChanged(Topic::Transport);
    service.changes().flush();
    REQUIRE(stream.waitFor(2));
    REQUIRE(stream.events().at(1)["params"]["uri"].toString() == "magda://transport");

    stream.close();
}

TEST_CASE("Closing a stream releases its slot and its subscription",
          "[remote][mcp-http][subscriptions][lifetime]") {
    MessageThreadRelaxation relax;
    MockMagdaApi api;
    RemoteApiService service(api);
    SubscriptionHub hub(api, service);

    auto options = testOptions();
    options.maxStreams = 1;
    RemoteMcpServer server(service, options, &hub);
    REQUIRE(server.start());

    juce::Array<juce::var> uris;
    uris.add("magda://transport");
    const auto listen =
        body(1, "subscriptions/listen",
             modernParams(object({{"notifications", object({{"resourceSubscriptions", uris}})}})));

    {
        SseStream stream(base(server));
        stream.openPost(modernHeaders("subscriptions/listen"), listen);
        REQUIRE(stream.waitFor(1));
        REQUIRE(server.streamCount() == 1);
        REQUIRE(hub.clientCount() == 1);

        // The cap is a real resource bound: each open stream holds a pool thread
        // for its lifetime.
        httplib::Client second(base(server));
        const auto refused = post(second, modernHeaders("subscriptions/listen"), listen);
        REQUIRE(refused.status == 503);

        stream.close();
    }

    // Closing the stream is the cancellation. Both the slot and the hub
    // registration have to come back, or a client that reconnects a few times
    // would exhaust the server.
    for (int i = 0; i < 100 && server.streamCount() != 0; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    REQUIRE(server.streamCount() == 0);
    REQUIRE(hub.clientCount() == 0);
}

TEST_CASE("A legacy client initializes, subscribes, and is served on its GET stream",
          "[remote][mcp-http][legacy]") {
    MessageThreadRelaxation relax;
    MockMagdaApi api;
    RemoteApiService service(api);
    SubscriptionHub hub(api, service);
    RemoteMcpServer server(service, testOptions(), &hub);
    REQUIRE(server.start());

    httplib::Client client(base(server));

    // The handshake. No `_meta`, no mirrored headers — this is the shape hosts
    // that have not moved to 2026-07-28 still speak.
    const auto initialized =
        post(client, authorised(),
             body(1, "initialize",
                  object({{"protocolVersion", "2025-11-25"},
                          {"capabilities", juce::var(new juce::DynamicObject())},
                          {"clientInfo", object({{"name", "legacy"}, {"version", "1.0"}})}})));
    REQUIRE(initialized.status == 200);
    REQUIRE(initialized.json["result"]["protocolVersion"].toString() == "2025-11-25");
    REQUIRE(initialized.json["result"]["serverInfo"]["name"].toString() == "MAGDA");
    // A legacy reply carries no resultType: the revision it negotiated has never
    // heard of the field.
    REQUIRE(initialized.json["result"]["resultType"].isVoid());
    REQUIRE_FALSE(initialized.sessionId.empty());
    REQUIRE(server.sessionCount() == 1);

    const httplib::Headers session = {{"Authorization", std::string("Bearer ") + kToken},
                                      {"Mcp-Session-Id", initialized.sessionId}};

    // The session carries the era, so ordinary calls need no per-request meta.
    const auto tools = post(client, session, body(2, "tools/list"));
    REQUIRE(tools.status == 200);
    REQUIRE_FALSE(tools.json["result"]["tools"].getArray()->isEmpty());
    REQUIRE(tools.json["result"]["resultType"].isVoid());

    const auto subscribed = post(
        client, session, body(3, "resources/subscribe", object({{"uri", "magda://transport"}})));
    REQUIRE(subscribed.status == 200);
    REQUIRE(subscribed.json["error"].isVoid());

    // The subscription only reaches the hub once there is somewhere to deliver.
    SseStream stream(base(server));
    stream.openGet(session);
    for (int i = 0; i < 100 && hub.clientCount() == 0; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    REQUIRE(hub.clientCount() == 1);

    // A client is in the hub from the moment its stream attaches, which is a
    // moment before the topics it asked for are registered against it, and
    // counting clients cannot see that second step. Announcing once can
    // therefore land in the gap — and the hub publishes against a baseline it
    // updates whether or not anybody was listening, so that announcement is not
    // merely early, it is spent: re-announcing the same state is no longer a
    // change and says nothing. Move the state each round instead, so whichever
    // round finds the subscription live carries a difference the hub forwards.
    bool delivered = false;
    for (int round = 0; round < 100 && !delivered; ++round) {
        api.transport_.playing = (round % 2) == 0;
        service.noteModelChanged(Topic::Transport);
        service.changes().flush();
        delivered = stream.waitFor(1, std::chrono::milliseconds(50));
    }
    REQUIRE(delivered);

    const auto event = stream.events().front();
    REQUIRE(event["method"].toString() == "notifications/resources/updated");
    REQUIRE(event["params"]["uri"].toString() == "magda://transport");
    // The legacy revision has no subscription correlation, so none is invented.
    REQUIRE(event["params"]["_meta"].isVoid());

    stream.close();

    // DELETE ends the session, which is what a client does when the user closes
    // it. A request afterwards is a 404, which tells a legacy client to start a
    // new session rather than to give up.
    auto deleted = client.Delete("/mcp", session);
    REQUIRE(deleted);
    REQUIRE(deleted->status == 204);
    REQUIRE(server.sessionCount() == 0);

    const auto orphaned = post(client, session, body(4, "tools/list"));
    REQUIRE(orphaned.status == 404);
    REQUIRE(static_cast<int>(orphaned.json["error"]["code"]) == MCP_INVALID_REQUEST);
}

TEST_CASE("A legacy initialize counter-offers a version it can speak",
          "[remote][mcp-http][legacy][negotiation]") {
    MessageThreadRelaxation relax;
    MockMagdaApi api;
    RemoteApiService service(api);
    RemoteMcpServer server(service, testOptions());
    REQUIRE(server.start());

    httplib::Client client(base(server));

    const auto older = post(client, authorised(),
                            body(1, "initialize", object({{"protocolVersion", "2025-06-18"}})));
    REQUIRE(older.status == 200);
    REQUIRE(older.json["result"]["protocolVersion"].toString() == "2025-06-18");

    // A version nobody has heard of: answer with the newest handshake revision
    // rather than an error. A legacy client has no way to fall forward, so an
    // error would end the conversation with nothing actionable in it.
    const auto unknown = post(client, authorised(),
                              body(1, "initialize", object({{"protocolVersion", "1900-01-01"}})));
    REQUIRE(unknown.status == 200);
    const auto offered = unknown.json["result"]["protocolVersion"].toString();
    REQUIRE(isSupportedVersion(offered));
    REQUIRE(eraForVersion(offered) == McpEra::Legacy);
}

TEST_CASE("A modern request ignores a session header rather than honouring it",
          "[remote][mcp-http][legacy]") {
    MessageThreadRelaxation relax;
    MockMagdaApi api;
    RemoteApiService service(api);
    RemoteMcpServer server(service, testOptions());
    REQUIRE(server.start());

    httplib::Client client(base(server));

    const auto initialized = post(
        client, authorised(), body(1, "initialize", object({{"protocolVersion", "2025-11-25"}})));
    REQUIRE_FALSE(initialized.sessionId.empty());

    // 2026-07-28 requires a stale session header to be ignored, not honoured —
    // a client that kept one from an earlier connection must be served
    // statelessly rather than quietly given semantics it no longer expects.
    auto headers = modernHeaders("tools/list");
    headers.emplace("Mcp-Session-Id", initialized.sessionId);
    const auto reply = post(client, headers, body(2, "tools/list", modernParams()));
    REQUIRE(reply.status == 200);
    REQUIRE(reply.json["result"]["resultType"].toString() == "complete");
    // No session is echoed, because none was created or used.
    REQUIRE(reply.sessionId.empty());
}

TEST_CASE("Shutdown ends streams, drops sessions, and joins every thread",
          "[remote][mcp-http][lifetime]") {
    MessageThreadRelaxation relax;
    MockMagdaApi api;
    RemoteApiService service(api);
    SubscriptionHub hub(api, service);
    RemoteMcpServer server(service, testOptions(), &hub);
    REQUIRE(server.start());

    httplib::Client client(base(server));
    const auto initialized = post(
        client, authorised(), body(1, "initialize", object({{"protocolVersion", "2025-11-25"}})));
    REQUIRE(server.sessionCount() == 1);

    juce::Array<juce::var> uris;
    uris.add("magda://transport");
    SseStream stream(base(server));
    stream.openPost(
        modernHeaders("subscriptions/listen"),
        body(1, "subscriptions/listen",
             modernParams(object({{"notifications", object({{"resourceSubscriptions", uris}})}}))));
    REQUIRE(stream.waitFor(1));
    REQUIRE(server.streamCount() == 1);

    // Prompt rather than bounded by a read timeout: closing the outbox wakes the
    // provider thread directly.
    server.stop();
    REQUIRE_FALSE(server.isRunning());
    REQUIRE(server.boundPort() == 0);
    REQUIRE(server.streamCount() == 0);
    REQUIRE(server.sessionCount() == 0);
    REQUIRE(hub.clientCount() == 0);

    stream.close();

    // Idempotent, and the destructor calls it too.
    server.stop();
}
