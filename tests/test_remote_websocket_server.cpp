// The WebSocket transport for the remote API (#1856), end to end over a real
// loopback socket. cpp-httplib ships the client, so these are genuine
// connections rather than a hand-driven handler.
//
// Everything here is transport behaviour: who is allowed to connect, what a
// malformed frame does, and how limits and shutdown behave. What the operations
// themselves do belongs to test_remote_service.cpp.

#define CPPHTTPLIB_WEBSOCKET_MAX_PAYLOAD_LENGTH (256 * 1024)

#include <httplib.h>

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <string>
#include <thread>
#include <vector>

#include "MockMagdaApi.hpp"
#include "magda/daw/api/remote_service.hpp"
#include "magda/daw/api/remote_websocket_server.hpp"

using namespace magda;
using namespace magda::remote;
using magda::test::MockMagdaApi;

namespace {

constexpr const char* kToken = "spike-token-9f3a";
constexpr const char* kOrigin = "http://localhost:5173";

/// Reads MagdaApi live state, which asserts the message thread. The Catch2
/// runner has no MessageManager, so suspend that assertion — the same
/// accommodation the other remote tests make. With no message thread to hop to,
/// dispatch also runs inline on the connection's own thread, which makes these
/// tests deterministic rather than timing-dependent.
struct MessageThreadRelaxation {
    ScopedMessageThreadAssertionDisabler disabler;
};

RemoteWebSocketServer::Options testOptions() {
    RemoteWebSocketServer::Options options;
    options.bearerToken = kToken;
    options.allowedOrigins = {kOrigin};
    return options;
}

std::string endpoint(const RemoteWebSocketServer& server) {
    return "ws://127.0.0.1:" + std::to_string(server.boundPort()) + "/rpc";
}

httplib::Headers authorised() {
    return {{"Authorization", std::string("Bearer ") + kToken}};
}

std::string request(const juce::String& method, const juce::var& params = {},
                    const juce::var& id = 1) {
    auto* object = new juce::DynamicObject();
    object->setProperty("jsonrpc", "2.0");
    object->setProperty("id", id);
    object->setProperty("method", method);
    object->setProperty("params", params.isVoid() ? juce::var(new juce::DynamicObject()) : params);
    return juce::JSON::toString(juce::var(object), true).toStdString();
}

/// Send one request, read one reply, parse it.
juce::var roundTrip(httplib::ws::WebSocketClient& client, const std::string& payload) {
    REQUIRE(client.send(payload));
    std::string reply;
    REQUIRE(client.read(reply) == httplib::ws::ReadResult::Text);
    juce::var parsed;
    REQUIRE(juce::JSON::parse(juce::String(reply), parsed).wasOk());
    return parsed;
}

int errorCodeOf(const juce::var& reply) {
    return static_cast<int>(reply["error"]["code"]);
}

}  // namespace

TEST_CASE("The server refuses to start without a bearer token", "[remote][websocket][auth]") {
    MessageThreadRelaxation relax;
    MockMagdaApi api;
    RemoteApiService service(api);

    RemoteWebSocketServer::Options options;
    options.bearerToken = {};
    RemoteWebSocketServer server(service, options);

    // An open listener is never a valid outcome of a missing credential, so this
    // fails rather than starting something anonymous.
    REQUIRE_FALSE(server.start());
    REQUIRE_FALSE(server.isRunning());
    REQUIRE(server.boundPort() == 0);
}

TEST_CASE("An upgrade without a valid token is refused before the handshake",
          "[remote][websocket][auth]") {
    MessageThreadRelaxation relax;
    MockMagdaApi api;
    RemoteApiService service(api);
    RemoteWebSocketServer server(service, testOptions());
    REQUIRE(server.start());

    SECTION("no Authorization header at all") {
        httplib::ws::WebSocketClient client(endpoint(server));
        REQUIRE_FALSE(client.connect());
    }

    SECTION("a token that is close but wrong") {
        httplib::ws::WebSocketClient client(
            endpoint(server), {{"Authorization", std::string("Bearer ") + kToken + "x"}});
        REQUIRE_FALSE(client.connect());
    }

    SECTION("the right token under the wrong scheme") {
        httplib::ws::WebSocketClient client(endpoint(server),
                                            {{"Authorization", std::string("Basic ") + kToken}});
        REQUIRE_FALSE(client.connect());
    }

    // No connection was ever established, which is the difference between
    // failing closed and hanging up after the fact.
    REQUIRE(server.connectionCount() == 0);
    server.stop();
}

TEST_CASE("Origin is checked only when the client sends one", "[remote][websocket][auth]") {
    MessageThreadRelaxation relax;
    MockMagdaApi api;
    RemoteApiService service(api);
    RemoteWebSocketServer server(service, testOptions());
    REQUIRE(server.start());

    SECTION("an unlisted browser origin is refused") {
        auto headers = authorised();
        headers.emplace("Origin", "http://evil.example");
        httplib::ws::WebSocketClient client(endpoint(server), headers);
        REQUIRE_FALSE(client.connect());
    }

    SECTION("a listed browser origin is allowed") {
        auto headers = authorised();
        headers.emplace("Origin", kOrigin);
        httplib::ws::WebSocketClient client(endpoint(server), headers);
        REQUIRE(client.connect());
    }

    SECTION("a native client sending no Origin is allowed") {
        // Absent is not the same as unrecognised. Treating it as a refusal would
        // lock out every non-browser client, which is most of them.
        httplib::ws::WebSocketClient client(endpoint(server), authorised());
        REQUIRE(client.connect());
    }

    server.stop();
}

TEST_CASE("A request round-trips as JSON-RPC 2.0", "[remote][websocket][framing]") {
    MessageThreadRelaxation relax;
    MockMagdaApi api;
    RemoteApiService service(api);
    RemoteWebSocketServer server(service, testOptions());
    REQUIRE(server.start());

    httplib::ws::WebSocketClient client(endpoint(server), authorised());
    REQUIRE(client.connect());

    const auto reply = roundTrip(client, request("tracks.list"));

    REQUIRE(reply["jsonrpc"].toString() == "2.0");
    REQUIRE(static_cast<int>(reply["id"]) == 1);
    REQUIRE(reply["error"].isVoid());
    // The revision rides in the result so a client can seed its next write from
    // the reply rather than making a second call for it.
    REQUIRE(static_cast<juce::int64>(reply["result"]["revision"]) >= INITIAL_REVISION);
    REQUIRE(reply["result"]["apiVersion"].toString().isNotEmpty());

    server.stop();
}

TEST_CASE("Malformed requests fail with the right JSON-RPC code",
          "[remote][websocket][framing][errors]") {
    MessageThreadRelaxation relax;
    MockMagdaApi api;
    RemoteApiService service(api);
    RemoteWebSocketServer server(service, testOptions());
    REQUIRE(server.start());

    httplib::ws::WebSocketClient client(endpoint(server), authorised());
    REQUIRE(client.connect());

    SECTION("unparseable JSON is a parse error") {
        REQUIRE(errorCodeOf(roundTrip(client, "{not json at all")) == -32700);
    }

    SECTION("a missing jsonrpc version is an invalid request") {
        REQUIRE(errorCodeOf(roundTrip(client, R"({"id":1,"method":"tracks.list"})")) == -32600);
    }

    SECTION("a notification is refused, because every reply carries a revision") {
        REQUIRE(errorCodeOf(roundTrip(client, R"({"jsonrpc":"2.0","method":"tracks.list"})")) ==
                -32600);
    }

    SECTION("an unknown operation is method-not-found") {
        REQUIRE(errorCodeOf(roundTrip(client, request("tracks.summonDragon"))) == -32601);
    }

    SECTION("schema-invalid params are invalid-params") {
        const auto reply = roundTrip(client, request("project.setTempo"));
        REQUIRE(errorCodeOf(reply) == -32602);
        // The MAGDA code survives the mapping, so a client that knows MAGDA does
        // not have to reason backwards from the JSON-RPC code.
        REQUIRE(reply["error"]["data"]["code"].toString() == "validation_failed");
    }

    SECTION("params of the wrong shape are rejected before dispatch") {
        REQUIRE(errorCodeOf(roundTrip(
                    client, R"({"jsonrpc":"2.0","id":1,"method":"tracks.list","params":42})")) ==
                -32602);
    }

    server.stop();
}

TEST_CASE("Connections are capped", "[remote][websocket][limits]") {
    MessageThreadRelaxation relax;
    MockMagdaApi api;
    RemoteApiService service(api);

    auto options = testOptions();
    options.maxConnections = 2;
    RemoteWebSocketServer server(service, options);
    REQUIRE(server.start());

    httplib::ws::WebSocketClient first(endpoint(server), authorised());
    httplib::ws::WebSocketClient second(endpoint(server), authorised());
    REQUIRE(first.connect());
    REQUIRE(second.connect());

    // Connections are a thread each, so the cap is a resource bound rather than
    // a policy knob, and it is enforced at the handshake.
    httplib::ws::WebSocketClient third(endpoint(server), authorised());
    REQUIRE_FALSE(third.connect());

    server.stop();
}

TEST_CASE("An oversized frame closes the connection", "[remote][websocket][limits]") {
    MessageThreadRelaxation relax;
    MockMagdaApi api;
    RemoteApiService service(api);
    RemoteWebSocketServer server(service, testOptions());
    REQUIRE(server.start());

    httplib::ws::WebSocketClient client(endpoint(server), authorised());
    REQUIRE(client.connect());

    client.send(std::string(CPPHTTPLIB_WEBSOCKET_MAX_PAYLOAD_LENGTH + 1024, 'x'));
    std::string ignored;
    REQUIRE(client.read(ignored) == httplib::ws::ReadResult::Fail);

    server.stop();
}

TEST_CASE("Shutdown closes live connections and joins every thread",
          "[remote][websocket][lifecycle]") {
    MessageThreadRelaxation relax;
    MockMagdaApi api;
    RemoteApiService service(api);
    RemoteWebSocketServer server(service, testOptions());
    REQUIRE(server.start());

    httplib::ws::WebSocketClient client(endpoint(server), authorised());
    REQUIRE(client.connect());
    REQUIRE(roundTrip(client, request("tracks.list"))["error"].isVoid());

    // Stopping the acceptor alone would leave this client's reading thread
    // parked in read() until its timeout; stop() has to close it explicitly.
    server.stop();

    REQUIRE_FALSE(server.isRunning());
    REQUIRE(server.boundPort() == 0);
    REQUIRE(server.connectionCount() == 0);

    std::string ignored;
    REQUIRE(client.read(ignored) == httplib::ws::ReadResult::Fail);

    // Idempotent: the destructor calls it too.
    server.stop();
}
