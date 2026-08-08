// Cross-transport conformance (#1860).
//
// The acceptance criterion this file exists for is "WebSocket and MCP pass the
// same operation test vectors". The risk it guards against is not that either
// transport is wrong on its own — each has its own suite for that — but that
// they drift: one refuses a read-only client's write and the other does not, or
// they report the same refusal in shapes a client cannot tell apart.
//
// So the vectors are declared once, as data, and every one of them is driven
// through both transports over real loopback sockets. A case added here is
// automatically a case for both; a transport that grows its own opinion about a
// vector fails here rather than in a bug report.
//
// Each transport is reached the way a real client reaches it — a WebSocket
// upgrade with `?client=`, an HTTP POST with `clientInfo` in `_meta` — because
// the identity plumbing is exactly what would otherwise differ between them.

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
#include "magda/daw/api/remote_audit.hpp"
#include "magda/daw/api/remote_clients.hpp"
#include "magda/daw/api/remote_mcp_server.hpp"
#include "magda/daw/api/remote_service.hpp"
#include "magda/daw/api/remote_websocket_server.hpp"

using namespace magda;
using namespace magda::remote;
using magda::test::MockMagdaApi;

namespace {

constexpr const char* kToken = "conformance-token-51ab";
constexpr const char* kClient = "conformance";
constexpr const char* kMcpVersion = "2026-07-28";

struct MessageThreadRelaxation {
    ScopedMessageThreadAssertionDisabler disabler;
};

juce::var object(std::initializer_list<std::pair<const char*, juce::var>> fields) {
    auto* result = new juce::DynamicObject();
    for (const auto& [key, value] : fields)
        result->setProperty(key, value);
    return result;
}

juce::var emptyObject() {
    return juce::var(new juce::DynamicObject());
}

/**
 * @brief One operation, and what a client holding `granted` should see.
 *
 * `granted` is the whole grant, not the missing piece, because that is what a
 * user actually configures — and a vector written the other way round would
 * stop describing a real configuration the moment scopes were split further.
 */
struct Vector {
    const char* name;
    const char* operation;
    juce::var input;
    ScopeSet granted;
    bool expectAllowed;
};

std::vector<Vector> vectors() {
    const auto read = defaultClientScopes();
    return {
        {"a read is allowed with the default grant", "project.get", emptyObject(), read, true},
        {"a read is allowed with a write grant too", "tracks.list", emptyObject(),
         ScopeSet{Scope::Read, Scope::Edit}, true},

        {"an edit is refused without the edit scope", "project.setTempo",
         object({{"tempo", 132.0}}), read, false},
        {"an edit is allowed with it", "project.setTempo", object({{"tempo", 132.0}}),
         ScopeSet{Scope::Read, Scope::Edit}, true},
        {"an edit is refused by a transport grant", "project.setTempo", object({{"tempo", 132.0}}),
         ScopeSet{Scope::Read, Scope::Transport}, false},

        {"transport control is refused without the transport scope", "transport.play",
         emptyObject(), read, false},
        {"transport control is allowed with it", "transport.play", emptyObject(),
         ScopeSet{Scope::Read, Scope::Transport}, true},
        {"transport control is refused by an edit grant", "transport.stop", emptyObject(),
         ScopeSet{Scope::Read, Scope::Edit}, false},

        {"session launch is refused without the session scope", "session.stopAll", emptyObject(),
         read, false},
        {"session launch is allowed with it", "session.stopAll", emptyObject(),
         ScopeSet{Scope::Read, Scope::Session}, true},
        {"session launch is refused by an edit grant", "session.stopAll", emptyObject(),
         ScopeSet{Scope::Read, Scope::Edit}, false},

        {"selection is an edit", "selection.set", emptyObject(), read, false},
        {"automation is an edit", "automation.listLanes", emptyObject(), read, true},
    };
}

/// What a transport reported back, reduced to the two things both must agree on.
struct Outcome {
    bool allowed = false;
    /// MAGDA's own error code — `permission_denied` when refused. Always
    /// present in both wire formats, which is the point: a client should not
    /// have to learn each transport's numbering to tell why it failed.
    juce::String errorCode;
};

// ---------------------------------------------------------------------------
// WebSocket
// ---------------------------------------------------------------------------

std::string wsEndpoint(const RemoteWebSocketServer& server) {
    return "ws://127.0.0.1:" + std::to_string(server.boundPort()) + "/rpc?client=" + kClient;
}

Outcome runOverWebSocket(httplib::ws::WebSocketClient& client, const Vector& testCase) {
    auto* request = new juce::DynamicObject();
    request->setProperty("jsonrpc", "2.0");
    request->setProperty("id", 1);
    request->setProperty("method", testCase.operation);
    request->setProperty("params", testCase.input);
    REQUIRE(client.send(juce::JSON::toString(juce::var(request), true).toStdString()));

    std::string reply;
    REQUIRE(client.read(reply) == httplib::ws::ReadResult::Text);
    juce::var parsed;
    REQUIRE(juce::JSON::parse(juce::String(reply), parsed).wasOk());

    Outcome outcome;
    outcome.allowed = parsed["error"].isVoid();
    if (!outcome.allowed)
        outcome.errorCode = parsed["error"]["data"]["code"].toString();
    return outcome;
}

/// The same round trip, with no Catch2 macros in it.
///
/// `runOverWebSocket` asserts as it goes, which is right for a single-threaded
/// test and wrong for the load one: Catch2's assertion macros are not
/// thread-safe, and four workers driving them concurrently is undefined
/// behaviour that happens to look like a passing test. This reports failure in
/// its return value instead, and the load test asserts on the totals from the
/// main thread once the workers are joined.
struct RawOutcome {
    /// False when the exchange itself failed — nothing was sent, nothing came
    /// back, or what came back was not JSON.
    bool answered = false;
    bool allowed = false;
    juce::String errorCode;
};

RawOutcome sendWrite(httplib::ws::WebSocketClient& client) {
    auto* request = new juce::DynamicObject();
    request->setProperty("jsonrpc", "2.0");
    request->setProperty("id", 1);
    request->setProperty("method", "project.setTempo");
    request->setProperty("params", object({{"tempo", 130.0}}));

    RawOutcome outcome;
    if (!client.send(juce::JSON::toString(juce::var(request), true).toStdString()))
        return outcome;

    std::string reply;
    if (client.read(reply) != httplib::ws::ReadResult::Text)
        return outcome;

    juce::var parsed;
    if (!juce::JSON::parse(juce::String(reply), parsed).wasOk())
        return outcome;

    outcome.answered = true;
    outcome.allowed = parsed["error"].isVoid();
    if (!outcome.allowed)
        outcome.errorCode = parsed["error"]["data"]["code"].toString();
    return outcome;
}

// ---------------------------------------------------------------------------
// MCP
// ---------------------------------------------------------------------------

juce::var mcpParams(juce::var params) {
    if (params.getDynamicObject() == nullptr)
        params = emptyObject();
    params.getDynamicObject()->setProperty(
        "_meta", object({{MCP_META_PROTOCOL_VERSION, kMcpVersion},
                         {MCP_META_CLIENT_INFO, object({{"name", kClient}, {"version", "1.0"}})},
                         {MCP_META_CLIENT_CAPABILITIES, emptyObject()}}));
    return params;
}

Outcome runOverMcp(httplib::Client& client, const Vector& testCase) {
    const auto params =
        mcpParams(object({{"name", testCase.operation}, {"arguments", testCase.input}}));

    auto* request = new juce::DynamicObject();
    request->setProperty("jsonrpc", "2.0");
    request->setProperty("id", 1);
    request->setProperty("method", "tools/call");
    request->setProperty("params", params);

    httplib::Headers headers = {{"Authorization", std::string("Bearer ") + kToken},
                                {"MCP-Protocol-Version", kMcpVersion},
                                {"Mcp-Method", "tools/call"},
                                {"Mcp-Name", testCase.operation}};

    auto result =
        client.Post("/mcp", headers, juce::JSON::toString(juce::var(request), true).toStdString(),
                    "application/json");
    REQUIRE(result);

    juce::var parsed;
    REQUIRE(juce::JSON::parse(juce::String(result->body), parsed).wasOk());

    Outcome outcome;
    // A tool failure is an execution error carrying the MAGDA envelope, not a
    // JSON-RPC failure — that is deliberate (a model can act on it), and it is
    // why this reads `isError` rather than `error`.
    outcome.allowed = !static_cast<bool>(parsed["result"]["isError"]);
    if (!outcome.allowed) {
        const auto* content = parsed["result"]["content"].getArray();
        REQUIRE(content != nullptr);
        REQUIRE_FALSE(content->isEmpty());
        juce::var envelope;
        REQUIRE(juce::JSON::parse((*content)[0]["text"].toString(), envelope).wasOk());
        outcome.errorCode = envelope["code"].toString();
    }
    return outcome;
}

}  // namespace

TEST_CASE("Both transports enforce the same scopes on the same operations",
          "[remote][permissions][conformance]") {
    const MessageThreadRelaxation relaxation;

    for (const auto& testCase : vectors()) {
        INFO("vector: " << testCase.name);

        // A fresh service, registry, and pair of listeners per vector. Sharing
        // them would let one vector's mutation decide the next one's outcome,
        // and the grants under test differ per vector anyway.
        MockMagdaApi api;
        RemoteApiService service(api);
        RemoteClientRegistry registry;
        registry.setScopes(kClient, testCase.granted);

        RemoteWebSocketServer::Options wsOptions;
        wsOptions.bearerToken = kToken;
        wsOptions.clients = &registry;
        RemoteWebSocketServer wsServer(service, wsOptions);
        REQUIRE(wsServer.start());

        RemoteMcpServer::Options mcpOptions;
        mcpOptions.bearerToken = kToken;
        mcpOptions.clients = &registry;
        RemoteMcpServer mcpServer(service, mcpOptions);
        REQUIRE(mcpServer.start());

        Outcome overWebSocket;
        {
            httplib::ws::WebSocketClient client(
                wsEndpoint(wsServer), {{"Authorization", std::string("Bearer ") + kToken}});
            REQUIRE(client.connect());
            overWebSocket = runOverWebSocket(client, testCase);
        }

        httplib::Client mcpClient("http://127.0.0.1:" + std::to_string(mcpServer.boundPort()));
        const auto overMcp = runOverMcp(mcpClient, testCase);

        // The vector's own expectation, on each transport…
        REQUIRE(overWebSocket.allowed == testCase.expectAllowed);
        REQUIRE(overMcp.allowed == testCase.expectAllowed);
        // …and, whatever it was, the same answer from both. Asserted separately
        // because it is the property that survives someone changing a vector's
        // expectation without noticing they changed only one transport.
        REQUIRE(overWebSocket.allowed == overMcp.allowed);

        if (!testCase.expectAllowed) {
            REQUIRE(overWebSocket.errorCode == "permission_denied");
            REQUIRE(overMcp.errorCode == "permission_denied");
        }
    }
}

TEST_CASE("Both transports attribute a request to the name the client declared",
          "[remote][permissions][conformance]") {
    const MessageThreadRelaxation relaxation;
    MockMagdaApi api;
    RemoteApiService service(api);
    RemoteClientRegistry registry;
    auto log = std::make_shared<RemoteAuditLog>();
    service.setAuditLog(log);

    RemoteWebSocketServer::Options wsOptions;
    wsOptions.bearerToken = kToken;
    wsOptions.clients = &registry;
    wsOptions.audit = log;
    RemoteWebSocketServer wsServer(service, wsOptions);
    REQUIRE(wsServer.start());

    RemoteMcpServer::Options mcpOptions;
    mcpOptions.bearerToken = kToken;
    mcpOptions.clients = &registry;
    mcpOptions.audit = log;
    RemoteMcpServer mcpServer(service, mcpOptions);
    REQUIRE(mcpServer.start());

    const Vector read{"read", "project.get", emptyObject(), defaultClientScopes(), true};
    {
        httplib::ws::WebSocketClient client(wsEndpoint(wsServer),
                                            {{"Authorization", std::string("Bearer ") + kToken}});
        REQUIRE(client.connect());
        REQUIRE(runOverWebSocket(client, read).allowed);
    }
    httplib::Client mcpClient("http://127.0.0.1:" + std::to_string(mcpServer.boundPort()));
    REQUIRE(runOverMcp(mcpClient, read).allowed);

    // One client, seen over two transports, is one entry in the settings list —
    // which is the whole reason grants are keyed by declared name rather than
    // by connection.
    const auto grants = registry.grants();
    REQUIRE(grants.size() == 1);
    REQUIRE(grants.front().name == kClient);

    // And both transports named it the same way in the record.
    juce::StringArray transports;
    for (const auto& entry : log->forClient(kClient))
        transports.addIfNotAlreadyThere(entry.transport);
    REQUIRE(transports.contains(TRANSPORT_WEBSOCKET));
    REQUIRE(transports.contains(TRANSPORT_MCP));
}

TEST_CASE("An unnamed client is the anonymous one on both transports",
          "[remote][permissions][conformance]") {
    // Refusing an anonymous client outright would break every conforming MCP
    // host that simply does not send `clientInfo`, and every WebSocket library
    // whose user did not know about `?client=`. They get a shared, read-only
    // bucket instead.
    const MessageThreadRelaxation relaxation;
    MockMagdaApi api;
    RemoteApiService service(api);
    RemoteClientRegistry registry;

    RemoteWebSocketServer::Options wsOptions;
    wsOptions.bearerToken = kToken;
    wsOptions.clients = &registry;
    RemoteWebSocketServer wsServer(service, wsOptions);
    REQUIRE(wsServer.start());

    {
        httplib::ws::WebSocketClient client(
            "ws://127.0.0.1:" + std::to_string(wsServer.boundPort()) + "/rpc",
            {{"Authorization", std::string("Bearer ") + kToken}});
        REQUIRE(client.connect());

        const Vector read{"read", "project.get", emptyObject(), defaultClientScopes(), true};
        REQUIRE(runOverWebSocket(client, read).allowed);

        const Vector write{"write", "project.setTempo", object({{"tempo", 140.0}}),
                           defaultClientScopes(), false};
        const auto refused = runOverWebSocket(client, write);
        REQUIRE_FALSE(refused.allowed);
        REQUIRE(refused.errorCode == "permission_denied");
    }

    REQUIRE(registry.grants().size() == 1);
    REQUIRE(registry.grants().front().name == ANONYMOUS_CLIENT);
}

TEST_CASE("Revoking a grant takes effect on the next request, with no reconnect",
          "[remote][permissions][conformance]") {
    // The acceptance criterion in its most direct form: the client stays
    // connected across the revocation and simply stops being able to write.
    const MessageThreadRelaxation relaxation;
    MockMagdaApi api;
    api.project_.info.tempo = 120.0;
    RemoteApiService service(api);
    RemoteClientRegistry registry;
    registry.setScopes(kClient, ScopeSet{Scope::Read, Scope::Edit});

    RemoteWebSocketServer::Options wsOptions;
    wsOptions.bearerToken = kToken;
    wsOptions.clients = &registry;
    RemoteWebSocketServer server(service, wsOptions);
    REQUIRE(server.start());

    httplib::ws::WebSocketClient client(wsEndpoint(server),
                                        {{"Authorization", std::string("Bearer ") + kToken}});
    REQUIRE(client.connect());

    const Vector write{"write", "project.setTempo", object({{"tempo", 140.0}}),
                       ScopeSet{Scope::Read, Scope::Edit}, true};
    REQUIRE(runOverWebSocket(client, write).allowed);
    REQUIRE(api.project_.info.tempo == 140.0);

    registry.setScopes(kClient, defaultClientScopes());

    const auto refused = runOverWebSocket(client, write);
    REQUIRE_FALSE(refused.allowed);
    REQUIRE(refused.errorCode == "permission_denied");
    REQUIRE(api.project_.info.tempo == 140.0);

    // Granting again is equally immediate, on the same socket.
    registry.setScopes(kClient, ScopeSet{Scope::Read, Scope::Edit});
    REQUIRE(runOverWebSocket(client, write).allowed);
}

TEST_CASE("Disconnecting a client stops it executing anything further",
          "[remote][permissions][conformance]") {
    const MessageThreadRelaxation relaxation;
    MockMagdaApi api;
    api.project_.info.tempo = 120.0;
    RemoteApiService service(api);
    RemoteClientRegistry registry;
    registry.setScopes(kClient, allScopes());

    RemoteWebSocketServer::Options wsOptions;
    wsOptions.bearerToken = kToken;
    wsOptions.clients = &registry;
    RemoteWebSocketServer server(service, wsOptions);
    REQUIRE(server.start());

    httplib::ws::WebSocketClient client(wsEndpoint(server),
                                        {{"Authorization", std::string("Bearer ") + kToken}});
    REQUIRE(client.connect());

    const Vector write{"write", "project.setTempo", object({{"tempo", 140.0}}), allScopes(), true};
    REQUIRE(runOverWebSocket(client, write).allowed);
    REQUIRE(api.project_.info.tempo == 140.0);

    REQUIRE(registry.connectionCount() == 1);
    REQUIRE(registry.disconnectClient(kClient) == 1);

    // The socket closes within a read timeout, but the effect that matters is
    // immediate: a request sent after the disconnect is never executed, whether
    // the frame lands before the reader notices or the send fails outright.
    auto* request = new juce::DynamicObject();
    request->setProperty("jsonrpc", "2.0");
    request->setProperty("id", 2);
    request->setProperty("method", "project.setTempo");
    request->setProperty("params", object({{"tempo", 90.0}}));
    client.send(juce::JSON::toString(juce::var(request), true).toStdString());

    std::string reply;
    client.read(reply);
    REQUIRE(api.project_.info.tempo == 140.0);
}

TEST_CASE("Neither transport opens a listener without a credential",
          "[remote][permissions][conformance]") {
    // "Disabled means disabled", asserted for both: there is no configuration
    // that produces an anonymous listener on either port.
    const MessageThreadRelaxation relaxation;
    MockMagdaApi api;
    RemoteApiService service(api);
    RemoteClientRegistry registry;

    RemoteWebSocketServer::Options wsOptions;
    wsOptions.clients = &registry;
    RemoteWebSocketServer wsServer(service, wsOptions);
    REQUIRE_FALSE(wsServer.start());
    REQUIRE(wsServer.boundPort() == 0);

    RemoteMcpServer::Options mcpOptions;
    mcpOptions.clients = &registry;
    RemoteMcpServer mcpServer(service, mcpOptions);
    REQUIRE_FALSE(mcpServer.start());
    REQUIRE(mcpServer.boundPort() == 0);
}

TEST_CASE("A transport with no registry refuses everything rather than allowing it",
          "[remote][permissions][conformance]") {
    // Fail closed, and the reason it matters: a third transport that forgot to
    // consult the registry should refuse every request loudly on its first test
    // run, not hand out the project silently and pass.
    const MessageThreadRelaxation relaxation;
    MockMagdaApi api;
    RemoteApiService service(api);

    RemoteWebSocketServer::Options wsOptions;
    wsOptions.bearerToken = kToken;
    // Deliberately no `clients`.
    RemoteWebSocketServer server(service, wsOptions);
    REQUIRE(server.start());

    httplib::ws::WebSocketClient client(wsEndpoint(server),
                                        {{"Authorization", std::string("Bearer ") + kToken}});
    REQUIRE(client.connect());

    const Vector read{"read", "project.get", emptyObject(), ScopeSet{}, false};
    const auto outcome = runOverWebSocket(client, read);
    REQUIRE_FALSE(outcome.allowed);
    REQUIRE(outcome.errorCode == "permission_denied");

    // MCP too, and not only its dispatched surface. `tools/list` and the two
    // resource listings are answered from the endpoint's own catalogue without
    // ever building a RequestContext, so they were readable with no registry
    // configured at all — the whole operation surface and its schemas, handed to
    // a client entitled to nothing. Fail-closed has to mean these as well.
    RemoteMcpServer::Options mcpOptions;
    mcpOptions.bearerToken = kToken;
    // Deliberately no `clients` here either.
    RemoteMcpServer mcpServer(service, mcpOptions);
    REQUIRE(mcpServer.start());

    httplib::Client mcpClient("http://127.0.0.1:" + std::to_string(mcpServer.boundPort()));

    const auto catalogueCall = [&](const juce::String& method) {
        auto* request = new juce::DynamicObject();
        request->setProperty("jsonrpc", "2.0");
        request->setProperty("id", 1);
        request->setProperty("method", method);
        request->setProperty("params", mcpParams(emptyObject()));

        const httplib::Headers headers = {{"Authorization", std::string("Bearer ") + kToken},
                                          {"MCP-Protocol-Version", kMcpVersion},
                                          {"Mcp-Method", method.toStdString()}};
        auto result = mcpClient.Post("/mcp", headers,
                                     juce::JSON::toString(juce::var(request), true).toStdString(),
                                     "application/json");
        REQUIRE(result);
        juce::var parsed;
        REQUIRE(juce::JSON::parse(juce::String(result->body), parsed).wasOk());
        return parsed;
    };

    for (const auto* method : {"tools/list", "resources/list", "resources/templates/list"}) {
        INFO("method: " << method);
        const auto reply = catalogueCall(method);
        REQUIRE_FALSE(reply["error"].isVoid());
        REQUIRE(static_cast<int>(reply["error"]["code"]) == MCP_PERMISSION_DENIED);
        // Nothing leaked alongside the refusal.
        REQUIRE(reply["result"].isVoid());
    }

    // And the dispatched surface, for the same server.
    const Vector mcpRead{"read", "project.get", emptyObject(), ScopeSet{}, false};
    const auto mcpOutcome = runOverMcp(mcpClient, mcpRead);
    REQUIRE_FALSE(mcpOutcome.allowed);
    REQUIRE(mcpOutcome.errorCode == "permission_denied");
}

TEST_CASE("The catalogue is readable by an ordinary client", "[remote][permissions][conformance]") {
    // The other half of the gate above: `read` is the default every client has,
    // so requiring it must not have made the catalogue unreachable for anyone
    // who simply connected.
    const MessageThreadRelaxation relaxation;
    MockMagdaApi api;
    RemoteApiService service(api);
    RemoteClientRegistry registry;

    RemoteMcpServer::Options mcpOptions;
    mcpOptions.bearerToken = kToken;
    mcpOptions.clients = &registry;
    RemoteMcpServer mcpServer(service, mcpOptions);
    REQUIRE(mcpServer.start());

    httplib::Client mcpClient("http://127.0.0.1:" + std::to_string(mcpServer.boundPort()));

    auto* request = new juce::DynamicObject();
    request->setProperty("jsonrpc", "2.0");
    request->setProperty("id", 1);
    request->setProperty("method", "tools/list");
    request->setProperty("params", mcpParams(emptyObject()));

    const httplib::Headers headers = {{"Authorization", std::string("Bearer ") + kToken},
                                      {"MCP-Protocol-Version", kMcpVersion},
                                      {"Mcp-Method", "tools/list"}};
    auto result = mcpClient.Post("/mcp", headers,
                                 juce::JSON::toString(juce::var(request), true).toStdString(),
                                 "application/json");
    REQUIRE(result);
    juce::var parsed;
    REQUIRE(juce::JSON::parse(juce::String(result->body), parsed).wasOk());

    REQUIRE(parsed["error"].isVoid());
    REQUIRE(parsed["result"]["tools"].getArray() != nullptr);
    REQUIRE_FALSE(parsed["result"]["tools"].getArray()->isEmpty());
}

TEST_CASE("A refused connection is recorded without its credential",
          "[remote][permissions][conformance]") {
    const MessageThreadRelaxation relaxation;
    MockMagdaApi api;
    RemoteApiService service(api);
    RemoteClientRegistry registry;
    auto log = std::make_shared<RemoteAuditLog>();

    forgetAllRemoteSecrets();
    registerRemoteSecret(kToken);

    RemoteWebSocketServer::Options wsOptions;
    wsOptions.bearerToken = kToken;
    wsOptions.clients = &registry;
    wsOptions.audit = log;
    RemoteWebSocketServer server(service, wsOptions);
    REQUIRE(server.start());

    {
        httplib::ws::WebSocketClient client(
            wsEndpoint(server), {{"Authorization", std::string("Bearer ") + kToken + "-wrong"}});
        REQUIRE_FALSE(client.connect());
    }

    bool sawRejection = false;
    for (const auto& entry : log->entries()) {
        if (entry.outcome != AuditOutcome::Rejected)
            continue;
        sawRejection = true;
        REQUIRE(entry.operation == AUDIT_CONNECTION_REJECTED);
        // The reason, and nothing that could be a credential.
        REQUIRE(entry.detail.contains("token"));
        REQUIRE_FALSE(entry.detail.contains(kToken));
    }
    REQUIRE(sawRejection);

    forgetAllRemoteSecrets();
}

TEST_CASE("Permission decisions hold up under concurrent load",
          "[remote][permissions][conformance][load]") {
    // Enforcement reads a grant on every request from every connection at once,
    // while the settings dialog may be rewriting that grant from another thread.
    // The property under test is not throughput — the dispatcher serialises
    // handlers anyway — but that the answer is always one of the two correct
    // ones, and that a revocation is never overtaken by a request that started
    // before it.
    const MessageThreadRelaxation relaxation;
    MockMagdaApi api;
    api.project_.info.tempo = 120.0;
    RemoteApiService service(api);
    RemoteClientRegistry registry;
    auto log = std::make_shared<RemoteAuditLog>();
    service.setAuditLog(log);

    constexpr int kClients = 4;
    constexpr int kGrantedRound = 15;
    constexpr int kRevokedRound = 15;
    constexpr int kRacingRound = 10;
    constexpr int kRequestsEach = kGrantedRound + kRevokedRound + kRacingRound;
    constexpr int kTotal = kClients * kRequestsEach;

    RemoteWebSocketServer::Options options;
    options.bearerToken = kToken;
    options.clients = &registry;
    options.audit = log;
    options.maxConnections = kClients + 2;
    // Above what the rounds below can generate, so a refusal here would be the
    // rate limiter rather than the permission model — a different test.
    options.maxRequestsPerSecond = 10'000.0;
    RemoteWebSocketServer server(service, options);
    REQUIRE(server.start());

    registry.setScopes(kClient, ScopeSet{Scope::Read, Scope::Edit});

    std::atomic<int> allowed{0};
    std::atomic<int> denied{0};
    std::atomic<int> unexpected{0};

    /**
     * @brief The rounds, and the barrier between them.
     *
     * Polling a progress counter is not enough, and the earlier version of this
     * test proved it twice: a sleep-paced one flaked on CI, and the counter-paced
     * replacement was still wrong, because "a quarter of the work has finished"
     * does not mean the main thread *resumes* at that boundary — it can wake with
     * the workers nearly done and have almost nothing left to deny.
     *
     * So the grant only ever moves while every worker is parked here. A request
     * therefore belongs to the round it was issued in, by construction, and the
     * bounds below are arithmetic rather than a race the test usually wins.
     */
    struct Rounds {
        std::mutex mutex;
        std::condition_variable cv;
        int open = 0;     ///< The highest round workers may run.
        int waiting = 0;  ///< Workers parked at the current boundary.
        bool aborted = false;

        /// Worker: park until `round` opens. False means give up — the main
        /// thread has abandoned the test and is trying to join.
        bool await(int round) {
            std::unique_lock<std::mutex> lock(mutex);
            ++waiting;
            cv.notify_all();
            cv.wait(lock, [&] { return aborted || open >= round; });
            return !aborted;
        }

        /// Main: block until every worker is parked, so nothing is mid-request.
        bool allParked(int count) {
            std::unique_lock<std::mutex> lock(mutex);
            return cv.wait_for(lock, std::chrono::seconds(60),
                               [&] { return aborted || waiting >= count; });
        }

        void openRound(int round) {
            {
                const std::lock_guard<std::mutex> lock(mutex);
                open = round;
                waiting = 0;
            }
            cv.notify_all();
        }

        void abort() {
            {
                const std::lock_guard<std::mutex> lock(mutex);
                aborted = true;
            }
            cv.notify_all();
        }
    } rounds;

    /**
     * @brief Owns the workers, and joins them however this test leaves.
     *
     * A failed `REQUIRE` throws, and unwinding past a `std::thread` that is
     * still joinable calls `std::terminate` — turning a legible assertion
     * failure into a process abort with no output. Aborting first is what makes
     * the join safe: workers parked at a barrier that will now never open would
     * otherwise deadlock the join they are being joined by.
     */
    struct WorkerPool {
        Rounds& rounds;
        std::vector<std::thread> threads;

        ~WorkerPool() {
            rounds.abort();
            for (auto& thread : threads)
                if (thread.joinable())
                    thread.join();
        }
    } pool{rounds, {}};

    pool.threads.reserve(kClients);
    for (int worker = 0; worker < kClients; ++worker) {
        pool.threads.emplace_back([&] {
            // No Catch2 macros on this thread. `REQUIRE` is not thread-safe, so
            // the worker records into atomics and every assertion is made on the
            // main thread once the workers have been joined.
            httplib::ws::WebSocketClient client(
                wsEndpoint(server), {{"Authorization", std::string("Bearer ") + kToken}});
            const auto connected = client.connect();
            if (!connected)
                unexpected.fetch_add(1);

            const auto round = [&](int count) {
                for (int index = 0; index < count && connected; ++index) {
                    const auto outcome = sendWrite(client);
                    if (!outcome.answered)
                        unexpected.fetch_add(1);
                    else if (outcome.allowed)
                        allowed.fetch_add(1);
                    else if (outcome.errorCode == "permission_denied")
                        denied.fetch_add(1);
                    else
                        // A timeout, a conflict, a dropped reply — the load
                        // itself broke something, which is what this watches for.
                        unexpected.fetch_add(1);
                }
            };

            round(kGrantedRound);
            if (!rounds.await(1))
                return;
            round(kRevokedRound);
            if (!rounds.await(2))
                return;
            round(kRacingRound);
            // Parking here too, so the main thread knows the racing round is
            // over and can stop flipping before it starts asserting.
            rounds.await(3);
        });
    }

    // Round 1 ran entirely with the grant in place. Every worker is now parked,
    // so nothing is in flight and the revoke below cannot be read by a request
    // that already started.
    REQUIRE(rounds.allParked(kClients));
    registry.setScopes(kClient, defaultClientScopes());
    rounds.openRound(1);

    // Round 2 ran entirely without it.
    REQUIRE(rounds.allParked(kClients));

    // Round 3 is the actual concurrency: the grant has to be moving *while* the
    // requests run. Releasing the workers and then starting to toggle on this
    // thread does not achieve that — if this thread is descheduled at the
    // handover, the workers can finish the whole round before a single write
    // lands, and the test passes having exercised nothing.
    //
    // So the flipper is its own thread, started first, and the workers are not
    // released until it has demonstrably flipped at least once.
    std::atomic<bool> flipping{true};
    std::atomic<int> flips{0};
    std::thread flipper([&] {
        while (flipping.load()) {
            registry.setScopes(kClient, ScopeSet{Scope::Read, Scope::Edit});
            registry.setScopes(kClient, defaultClientScopes());
            flips.fetch_add(1);
            // Yielding rather than spinning flat out: on a two-core runner this
            // would otherwise compete with the workers it exists to interfere
            // with, which is the opposite of the point.
            std::this_thread::yield();
        }
    });
    // Joined however this test leaves, for the same reason the workers are.
    struct FlipperGuard {
        std::atomic<bool>& flipping;
        std::thread& thread;
        ~FlipperGuard() {
            flipping.store(false);
            if (thread.joinable())
                thread.join();
        }
    } flipperGuard{flipping, flipper};

    const auto flipDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (flips.load() == 0 && std::chrono::steady_clock::now() < flipDeadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    REQUIRE(flips.load() > 0);

    const auto flipsBefore = flips.load();
    rounds.openRound(2);

    // Every worker parks again at the end of the round, so this returns only
    // once the racing requests are done — with the flipper still running
    // throughout, rather than stopped early by a counter this thread was racing.
    REQUIRE(rounds.allParked(kClients));
    // The flipper kept working for the whole round, so the requests in it really
    // did run against a moving grant.
    REQUIRE(flips.load() > flipsBefore);
    rounds.openRound(3);

    for (auto& thread : pool.threads)
        thread.join();

    REQUIRE(unexpected.load() == 0);
    REQUIRE(allowed.load() + denied.load() == kTotal);
    // Arithmetic, not a race: round 1 is all-allowed and round 2 all-denied
    // because the grant only moved while every worker was parked between them.
    REQUIRE(allowed.load() >= kClients * kGrantedRound);
    REQUIRE(denied.load() >= kClients * kRevokedRound);

    // Every one of them is in the record, and none of them carries a payload.
    const auto entries = log->forClient(kClient);
    REQUIRE(entries.size() >= static_cast<std::size_t>(kClients * kRequestsEach));
    for (const auto& entry : entries)
        REQUIRE_FALSE(entry.detail.contains("130"));
}
