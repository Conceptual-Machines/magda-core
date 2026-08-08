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
    RemoteAuditLog log;
    service.setAuditLog(&log);

    RemoteWebSocketServer::Options wsOptions;
    wsOptions.bearerToken = kToken;
    wsOptions.clients = &registry;
    wsOptions.audit = &log;
    RemoteWebSocketServer wsServer(service, wsOptions);
    REQUIRE(wsServer.start());

    RemoteMcpServer::Options mcpOptions;
    mcpOptions.bearerToken = kToken;
    mcpOptions.clients = &registry;
    mcpOptions.audit = &log;
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
    for (const auto& entry : log.forClient(kClient))
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
}

TEST_CASE("A refused connection is recorded without its credential",
          "[remote][permissions][conformance]") {
    const MessageThreadRelaxation relaxation;
    MockMagdaApi api;
    RemoteApiService service(api);
    RemoteClientRegistry registry;
    RemoteAuditLog log;

    forgetAllRemoteSecrets();
    registerRemoteSecret(kToken);

    RemoteWebSocketServer::Options wsOptions;
    wsOptions.bearerToken = kToken;
    wsOptions.clients = &registry;
    wsOptions.audit = &log;
    RemoteWebSocketServer server(service, wsOptions);
    REQUIRE(server.start());

    {
        httplib::ws::WebSocketClient client(
            wsEndpoint(server), {{"Authorization", std::string("Bearer ") + kToken + "-wrong"}});
        REQUIRE_FALSE(client.connect());
    }

    bool sawRejection = false;
    for (const auto& entry : log.entries()) {
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
    RemoteAuditLog log;
    service.setAuditLog(&log);

    constexpr int kClients = 4;
    constexpr int kRequestsEach = 40;

    RemoteWebSocketServer::Options options;
    options.bearerToken = kToken;
    options.clients = &registry;
    options.audit = &log;
    options.maxConnections = kClients + 2;
    // Above what the loop below can generate, so a refusal here would be the
    // rate limiter rather than the permission model — a different test.
    options.maxRequestsPerSecond = 10'000.0;
    RemoteWebSocketServer server(service, options);
    REQUIRE(server.start());

    registry.setScopes(kClient, ScopeSet{Scope::Read, Scope::Edit});

    std::atomic<int> allowed{0};
    std::atomic<int> denied{0};
    std::atomic<int> unexpected{0};
    /// Requests finished, of any outcome. What the grant flipping below is
    /// paced against, so the test does not depend on how fast the machine is.
    std::atomic<int> completed{0};

    std::vector<std::thread> workers;
    workers.reserve(kClients);
    for (int worker = 0; worker < kClients; ++worker) {
        workers.emplace_back([&] {
            httplib::ws::WebSocketClient client(
                wsEndpoint(server), {{"Authorization", std::string("Bearer ") + kToken}});
            if (!client.connect()) {
                unexpected.fetch_add(1);
                return;
            }

            const Vector write{"write", "project.setTempo", object({{"tempo", 130.0}}),
                               ScopeSet{Scope::Read, Scope::Edit}, true};
            for (int index = 0; index < kRequestsEach; ++index) {
                const auto outcome = runOverWebSocket(client, write);
                if (outcome.allowed)
                    allowed.fetch_add(1);
                else if (outcome.errorCode == "permission_denied")
                    denied.fetch_add(1);
                else
                    // Anything else — a timeout, a conflict, a dropped reply —
                    // means the load itself broke something, which is exactly
                    // what this is watching for.
                    unexpected.fetch_add(1);
                completed.fetch_add(1);
            }
        });
    }

    // Flip the grant underneath them, anchored on their observed progress rather
    // than on a sleep.
    //
    // The first version of this slept between flips and then asserted that both
    // outcomes had occurred — which is a coincidence, not a property. On a
    // machine where the requests outran the flipping, every one of them saw the
    // same grant and the assertion failed with nothing wrong. Waiting for a
    // quarter of the work to finish before revoking makes both halves
    // guaranteed: those requests were allowed, and the three quarters that start
    // afterwards cannot have been.
    const auto total = kClients * kRequestsEach;
    const auto waitForProgress = [&](int target) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        while (completed.load() < target && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        return completed.load() >= target;
    };

    REQUIRE(waitForProgress(total / 4));
    registry.setScopes(kClient, defaultClientScopes());

    // Hold it revoked until another quarter has gone through, so those requests
    // are provably denied rather than probably denied.
    REQUIRE(waitForProgress(total / 2));

    // Only then start moving it, so the concurrent read-vs-write race on the
    // grant is exercised too rather than only the two steady states. Bounded by
    // the same deadline: a stalled worker should fail this test, not hang it.
    const auto flipUntil = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (completed.load() < total && std::chrono::steady_clock::now() < flipUntil) {
        registry.setScopes(kClient, ScopeSet{Scope::Read, Scope::Edit});
        registry.setScopes(kClient, defaultClientScopes());
        // Yielding rather than spinning flat out: on a two-core runner this
        // thread would otherwise compete with the workers it is waiting for.
        // The loop's exit is progress-based, so this cannot affect the outcome.
        std::this_thread::yield();
    }

    for (auto& worker : workers)
        worker.join();

    REQUIRE(unexpected.load() == 0);
    REQUIRE(allowed.load() + denied.load() == total);
    // Both outcomes provably occurred rather than probably: the first quarter
    // ran with the grant in place and the second with it withdrawn, and each
    // was waited for rather than slept through. The margin absorbs the handful
    // of requests already in flight when a flip lands.
    REQUIRE(allowed.load() >= (total / 4) - kClients);
    REQUIRE(denied.load() >= (total / 4) - kClients);

    // Every one of them is in the record, and none of them carries a payload.
    const auto entries = log.forClient(kClient);
    REQUIRE(entries.size() >= static_cast<std::size_t>(kClients * kRequestsEach));
    for (const auto& entry : entries)
        REQUIRE_FALSE(entry.detail.contains("130"));
}
