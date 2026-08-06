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
    // No explicit stop(): the server is declared before the client, so the
    // client is destroyed first and its socket shutdown wakes the reader at
    // once. Stopping while a client is still connected would instead wait out
    // the read timeout, which is what the shutdown test below measures.
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

    // No explicit stop(): the server is declared before the client, so the
    // client is destroyed first and its socket shutdown wakes the reader at
    // once. Stopping while a client is still connected would instead wait out
    // the read timeout, which is what the shutdown test below measures.
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
    // The revision rides in meta beside the result, so a client can seed its
    // next write from the reply rather than making a second call for it, and so
    // the result stays exactly what the operation returned.
    REQUIRE(static_cast<juce::int64>(reply["meta"]["revision"]) >= INITIAL_REVISION);
    REQUIRE(reply["meta"]["apiVersion"].toString().isNotEmpty());

    // No explicit stop(): the server is declared before the client, so the
    // client is destroyed first and its socket shutdown wakes the reader at
    // once. Stopping while a client is still connected would instead wait out
    // the read timeout, which is what the shutdown test below measures.
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

    // No explicit stop(): the server is declared before the client, so the
    // client is destroyed first and its socket shutdown wakes the reader at
    // once. Stopping while a client is still connected would instead wait out
    // the read timeout, which is what the shutdown test below measures.
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

    // No explicit stop(): the server is declared before the client, so the
    // client is destroyed first and its socket shutdown wakes the reader at
    // once. Stopping while a client is still connected would instead wait out
    // the read timeout, which is what the shutdown test below measures.
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

    // No explicit stop(): the server is declared before the client, so the
    // client is destroyed first and its socket shutdown wakes the reader at
    // once. Stopping while a client is still connected would instead wait out
    // the read timeout, which is what the shutdown test below measures.
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

    // Stopping with a client still connected is the bounded case: nothing can
    // interrupt a reader blocked in read(), so stop() marks the connection and
    // waits for the reader to come up for air on its own. It must not touch the
    // socket itself — close() reads as well as writes, and a second reader on
    // one buffered stream is a data race, not merely a slow shutdown.
    server.stop();

    REQUIRE_FALSE(server.isRunning());
    REQUIRE(server.boundPort() == 0);
    REQUIRE(server.connectionCount() == 0);

    std::string ignored;
    REQUIRE(client.read(ignored) == httplib::ws::ReadResult::Fail);

    // Idempotent: the destructor calls it too.
    server.stop();
}

TEST_CASE("meta.deadlineMs may only shorten the deadline", "[remote][websocket][limits][errors]") {
    MessageThreadRelaxation relax;
    MockMagdaApi api;
    RemoteApiService service(api);
    RemoteWebSocketServer server(service, testOptions());
    REQUIRE(server.start());

    httplib::ws::WebSocketClient client(endpoint(server), authorised());
    REQUIRE(client.connect());

    // A negative value used to win the min() against the server's default, and
    // a non-positive deadline then read as "no deadline at all" — so the way to
    // escape the bound was to ask for less than none of it.
    SECTION("negative is refused") {
        REQUIRE(
            errorCodeOf(roundTrip(
                client,
                R"({"jsonrpc":"2.0","id":1,"method":"tracks.list","meta":{"deadlineMs":-1}})")) ==
            -32600);
    }

    SECTION("zero is refused") {
        REQUIRE(
            errorCodeOf(roundTrip(
                client,
                R"({"jsonrpc":"2.0","id":1,"method":"tracks.list","meta":{"deadlineMs":0}})")) ==
            -32600);
    }

    SECTION("a non-number is refused") {
        REQUIRE(
            errorCodeOf(roundTrip(
                client,
                R"({"jsonrpc":"2.0","id":1,"method":"tracks.list","meta":{"deadlineMs":"soon"}})")) ==
            -32600);
    }

    SECTION("a shorter one is accepted") {
        const auto reply = roundTrip(
            client, R"({"jsonrpc":"2.0","id":1,"method":"tracks.list","meta":{"deadlineMs":250}})");
        REQUIRE(reply["error"].isVoid());
    }
}

TEST_CASE("A numeric id and the same digits as a string are different requests",
          "[remote][websocket][framing]") {
    MessageThreadRelaxation relax;
    MockMagdaApi api;
    RemoteApiService service(api);
    RemoteWebSocketServer server(service, testOptions());
    REQUIRE(server.start());

    httplib::ws::WebSocketClient client(endpoint(server), authorised());
    REQUIRE(client.connect());

    const auto setTempo = R"({"jsonrpc":"2.0","id":%ID%,"method":"project.setTempo",)"
                          R"("params":{"tempo":%BPM%}})";
    const auto build = [&setTempo](const char* id, const char* bpm) {
        return juce::String(setTempo).replace("%ID%", id).replace("%BPM%", bpm).toStdString();
    };

    REQUIRE(roundTrip(client, build("1", "120.0"))["error"].isVoid());
    REQUIRE(api.project_.info.tempo == 120.0);

    REQUIRE(roundTrip(client, build("\"1\"", "140.0"))["error"].isVoid());

    // JSON-RPC treats the number 1 and the string "1" as distinct identifiers.
    // Flattening both to `1` in the idempotency key made the second write replay
    // the first one's response instead of executing — and the cache is keyed on
    // the request id alone, not the method, so the mutation vanished silently.
    REQUIRE(api.project_.info.tempo == 140.0);
}

TEST_CASE("An id of an unusable shape is refused", "[remote][websocket][framing][errors]") {
    MessageThreadRelaxation relax;
    MockMagdaApi api;
    RemoteApiService service(api);
    RemoteWebSocketServer server(service, testOptions());
    REQUIRE(server.start());

    httplib::ws::WebSocketClient client(endpoint(server), authorised());
    REQUIRE(client.connect());

    REQUIRE(errorCodeOf(roundTrip(
                client, R"({"jsonrpc":"2.0","id":{"a":1},"method":"tracks.list"})")) == -32600);
    REQUIRE(errorCodeOf(roundTrip(
                client, R"({"jsonrpc":"2.0","id":[1],"method":"tracks.list"})")) == -32600);
}

TEST_CASE("Garbage is rate limited like anything else", "[remote][websocket][limits]") {
    MessageThreadRelaxation relax;
    MockMagdaApi api;
    RemoteApiService service(api);

    auto options = testOptions();
    options.maxRequestsPerSecond = 5.0;
    options.maxInFlightPerConnection = 2;  // also the burst
    RemoteWebSocketServer server(service, options);
    REQUIRE(server.start());

    httplib::ws::WebSocketClient client(endpoint(server), authorised());
    REQUIRE(client.connect());

    // Admission used to happen after parsing, which left the cheapest thing a
    // client can send as the one thing no limit applied to.
    bool refused = false;
    for (int i = 0; i < 6 && !refused; ++i)
        refused = errorCodeOf(roundTrip(client, "{not json at all")) == -32005;

    REQUIRE(refused);
}

TEST_CASE("A malformed upgrade cannot strand a connection slot", "[remote][websocket][limits]") {
    MessageThreadRelaxation relax;
    MockMagdaApi api;
    RemoteApiService service(api);

    auto options = testOptions();
    options.maxConnections = 2;
    RemoteWebSocketServer server(service, options);
    REQUIRE(server.start());

    // Authenticated, addressed to the endpoint, and carrying an Upgrade header,
    // but with a key cpp-httplib rejects: it wants 24 base64 characters. These
    // fall through to a 404 without ever reaching the WebSocket handler, so a
    // slot claimed before the handshake would never be given back — and enough
    // of them would answer every honest client with 503 until restart.
    httplib::Client http("127.0.0.1", server.boundPort());
    const httplib::Headers malformed{{"Authorization", std::string("Bearer ") + kToken},
                                     {"Upgrade", "websocket"},
                                     {"Connection", "Upgrade"},
                                     {"Sec-WebSocket-Key", "too-short"},
                                     {"Sec-WebSocket-Version", "13"}};
    for (int i = 0; i < 10; ++i)
        http.Get("/rpc", malformed);

    REQUIRE(server.connectionCount() == 0);

    httplib::ws::WebSocketClient client(endpoint(server), authorised());
    REQUIRE(client.connect());
    REQUIRE(roundTrip(client, request("tracks.list"))["error"].isVoid());
}

TEST_CASE("Numbers in meta must be whole and in range", "[remote][websocket][framing][errors]") {
    MessageThreadRelaxation relax;
    MockMagdaApi api;
    RemoteApiService service(api);
    RemoteWebSocketServer server(service, testOptions());
    REQUIRE(server.start());

    httplib::ws::WebSocketClient client(endpoint(server), authorised());
    REQUIRE(client.connect());

    const auto send = [&client](const char* meta) {
        return errorCodeOf(roundTrip(
            client, juce::String(R"({"jsonrpc":"2.0","id":1,"method":"tracks.list","meta":%META%})")
                        .replace("%META%", meta)
                        .toStdString()));
    };

    // JSON has one numeric type, so any of these arrives as a double. Casting
    // that to int truncates a fraction silently and is undefined behaviour when
    // it does not fit — neither is acceptable on a path fed by whatever a client
    // decided to send.
    REQUIRE(send(R"({"deadlineMs":250.5})") == -32600);
    REQUIRE(send(R"({"deadlineMs":1e18})") == -32600);
    REQUIRE(send(R"({"expectedRevision":-1})") == -32600);
    REQUIRE(send(R"({"expectedRevision":2.5})") == -32600);

    // Whole and in range still works.
    REQUIRE(send(R"({"deadlineMs":250})") != -32600);
}

TEST_CASE("The connection cap holds when clients arrive together", "[remote][websocket][limits]") {
    MessageThreadRelaxation relax;
    MockMagdaApi api;
    RemoteApiService service(api);

    auto options = testOptions();
    options.maxConnections = 2;
    RemoteWebSocketServer server(service, options);
    REQUIRE(server.start());

    // The cap used to be a count read before the handshake and a registration
    // performed after it, so upgrades racing each other all saw room.
    constexpr int kAttempts = 8;
    std::atomic<int> connected{0};
    std::atomic<int> finished{0};
    std::atomic<bool> release{false};
    std::vector<std::thread> clients;

    for (int i = 0; i < kAttempts; ++i) {
        clients.emplace_back([&] {
            httplib::ws::WebSocketClient client(endpoint(server), authorised());
            if (client.connect())
                ++connected;
            ++finished;
            // Hold on until every attempt has been made, so a slot freed early
            // cannot be handed to a later arrival and hide the overshoot.
            while (!release.load())
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
        });
    }

    while (finished.load() < kAttempts)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));

    // Registered connections are the real bound, and it is exact: the check and
    // the insert happen under one lock, on the connection's own thread.
    const auto peak = server.connectionCount();

    release.store(true);
    for (auto& thread : clients)
        thread.join();

    REQUIRE(peak <= options.maxConnections);
    REQUIRE(peak > 0);

    // A racer can still see its handshake succeed and be closed immediately
    // afterwards: the pre-handshake 503 is best-effort, because reserving a slot
    // there would mean reserving for requests cpp-httplib may never hand to a
    // handler, and those reservations would never come back.
    REQUIRE(connected.load() >= peak);
}
