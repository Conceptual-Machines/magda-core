// Lifecycle and credential handling for the remote API (#1856): what MAGDA
// opens, what it publishes, and what it leaves behind.

#include <httplib.h>

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <string>
#include <thread>

#include "MockMagdaApi.hpp"
#include "RemoteTestScopes.hpp"
#include "magda/daw/api/remote_api_host.hpp"
#include "magda/daw/api/remote_clients.hpp"
#include "magda/daw/api/remote_service.hpp"
#include "magda/daw/core/Config.hpp"

#if !JUCE_WINDOWS
    #include <sys/stat.h>
    #include <unistd.h>
#endif

using namespace magda;
using namespace magda::remote;
using magda::test::fullyGrantedContext;
using magda::test::MockMagdaApi;

namespace {

struct MessageThreadRelaxation {
    ScopedMessageThreadAssertionDisabler disabler;
};

/// Config is a process-wide singleton, so a test that changes it has to put it
/// back or it leaks into whatever runs next.
struct ScopedRemoteApiConfig {
    explicit ScopedRemoteApiConfig(bool enabled) {
        auto& config = Config::getInstance();
        previousPort_ = config.getRemoteApiPort();
        previousMcpPort_ = config.getRemoteApiMcpPort();
        // Grants too (#1860): a host persists them on every change, so a test
        // that connects a client would otherwise leave its grant behind in the
        // developer's real config file.
        previousClients_ = config.getRemoteApiClients();
        wasWebSocket_ = config.getRemoteApiWebSocketEnabled();
        wasMcp_ = config.getRemoteApiMcpEnabled();
        config.setRemoteApiWebSocketEnabled(enabled);
        config.setRemoteApiMcpEnabled(enabled);
        config.setRemoteApiPort(0);
        config.setRemoteApiMcpPort(0);
        config.setRemoteApiClients({});
    }
    ~ScopedRemoteApiConfig() {
        auto& config = Config::getInstance();
        config.setRemoteApiWebSocketEnabled(wasWebSocket_);
        config.setRemoteApiMcpEnabled(wasMcp_);
        config.setRemoteApiPort(previousPort_);
        config.setRemoteApiMcpPort(previousMcpPort_);
        config.setRemoteApiClients(previousClients_);
    }

  private:
    bool wasWebSocket_ = false;
    bool wasMcp_ = false;
    int previousPort_ = 0;
    int previousMcpPort_ = 0;
    juce::var previousClients_;
};

juce::var readTokenFile(const juce::File& file) {
    juce::var parsed;
    REQUIRE(juce::JSON::parse(file.loadFileAsString(), parsed).wasOk());
    return parsed;
}

/// Poll until `predicate` holds, or give up. For state a connection's own
/// thread publishes shortly after the client sees the handshake succeed.
template <typename Predicate>
bool waitFor(Predicate predicate, std::chrono::milliseconds timeout = std::chrono::seconds(5)) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (predicate())
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return predicate();
}

}  // namespace

TEST_CASE("A disabled remote API opens no listener", "[remote][host][lifecycle]") {
    MessageThreadRelaxation relax;
    ScopedRemoteApiConfig config(false);

    MockMagdaApi api;
    RemoteApiHost host(api);

    // A crashed run never reaches stop(), so its token file outlives it. Left
    // alone, it advertises a port nothing is listening on and a credential that
    // opens nothing — and if the OS hands that port to some other process, it
    // points a client at a stranger.
    host.tokenFile().getParentDirectory().createDirectory();
    host.tokenFile().replaceWithText(R"({"port":1,"token":"stale","url":"ws://127.0.0.1:1/rpc"})");
    REQUIRE(host.tokenFile().existsAsFile());

    // The acceptance criterion is about absence: not a listener that refuses
    // everything, but no socket and no published credential at all.
    REQUIRE_FALSE(host.start());
    REQUIRE_FALSE(host.isRunning());
    REQUIRE(host.boundPort() == 0);
    REQUIRE_FALSE(host.tokenFile().existsAsFile());
}

// A "port already taken" case would cover the other early returns, but there is
// no portable way to make binding fail here: cpp-httplib sets SO_REUSEPORT where
// it exists (httplib.h:10050), so a second listener on the same port succeeds on
// macOS and Linux, and the alternatives — privileged ports — pass or fail
// depending on who the test runs as. The clearing happens before every early
// return rather than on the disabled path alone, so the case above exercises the
// same statement.

TEST_CASE("Stopping removes only this instance's record", "[remote][host][lifecycle]") {
    MessageThreadRelaxation relax;
    ScopedRemoteApiConfig config(true);

    MockMagdaApi api;
    RemoteApiHost host(api);
    REQUIRE(host.start());

    // Written after start(), so the sweep has already run and cannot be what
    // decides this. It stands in for a second MAGDA instance, which the app
    // permits — moreThanOneInstanceAllowed() defaults to true. Shutdown deleting
    // this would strand a live instance with no way for a client to find it.
    const auto other = host.tokenFile().getSiblingFile("remote-api-424242.json");
    other.replaceWithText(
        R"({"port":1,"token":"theirs","url":"ws://127.0.0.1:1/rpc","pid":424242})");
    REQUIRE(host.tokenFile() != other);

    host.stop();

    REQUIRE_FALSE(host.tokenFile().existsAsFile());
    REQUIRE(other.existsAsFile());

    other.deleteFile();
}

#if !JUCE_WINDOWS
TEST_CASE("A live instance's record survives the sweep", "[remote][host][lifecycle]") {
    MessageThreadRelaxation relax;
    ScopedRemoteApiConfig config(true);

    MockMagdaApi api;
    RemoteApiHost host(api);

    // The parent of this test process: alive, and not us. POSIX only — pid 1 is
    // init there and would do just as well, but on Windows it is not a process
    // at all, and reaching a foreign pid's liveness portably needs a toolhelp
    // snapshot that is not worth a test. The abandoned-record case below covers
    // the other half of the same branch on every platform.
    const auto parent = static_cast<juce::int64>(getppid());
    const auto other =
        host.tokenFile().getSiblingFile("remote-api-" + juce::String(parent) + ".json");
    REQUIRE(host.tokenFile() != other);
    other.replaceWithText(R"({"port":1,"token":"theirs","url":"ws://127.0.0.1:1/rpc"})");

    REQUIRE(host.start());
    REQUIRE(other.existsAsFile());

    other.deleteFile();
}
#endif

TEST_CASE("A record whose process is gone is collected", "[remote][host][lifecycle]") {
    MessageThreadRelaxation relax;
    ScopedRemoteApiConfig config(true);

    MockMagdaApi api;
    RemoteApiHost host(api);

    // No live process carries this id: macOS caps pids below 100000 and Linux's
    // default pid_max is 4194304. A crashed instance cannot clear its own
    // record, and with a record per process nobody else would have.
    const auto abandoned = host.tokenFile().getSiblingFile("remote-api-2147483647.json");
    abandoned.replaceWithText(R"({"port":2,"token":"dead","url":"ws://127.0.0.1:2/rpc"})");

    REQUIRE(host.start());
    REQUIRE_FALSE(abandoned.existsAsFile());

    abandoned.deleteFile();
}

TEST_CASE("The record is named after the process that owns it", "[remote][host][lifecycle]") {
    MessageThreadRelaxation relax;
    ScopedRemoteApiConfig config(true);

    MockMagdaApi api;
    RemoteApiHost host(api);
    REQUIRE(host.start());

    const auto published = readTokenFile(host.tokenFile());
    const auto pid = static_cast<juce::int64>(published["pid"]);

    REQUIRE(pid > 0);
    REQUIRE(host.tokenFile().getFileName() == "remote-api-" + juce::String(pid) + ".json");
}

TEST_CASE("Starting publishes a token a local client can use", "[remote][host][auth]") {
    MessageThreadRelaxation relax;
    ScopedRemoteApiConfig config(true);

    MockMagdaApi api;
    RemoteApiHost host(api);
    REQUIRE(host.start());
    REQUIRE(host.isRunning());
    REQUIRE(host.boundPort() > 0);

    const auto file = host.tokenFile();
    REQUIRE(file.existsAsFile());

    const auto published = readTokenFile(file);
    const auto token = published["token"].toString();

    // 256 bits, hex encoded. A short or empty token would still "work" while
    // being guessable, so the width is worth asserting.
    REQUIRE(token.length() == 64);
    REQUIRE(static_cast<int>(published["port"]) == host.boundPort());
    REQUIRE(published["url"].toString() ==
            "ws://127.0.0.1:" + juce::String(host.boundPort()) + "/rpc");

    const auto endpoint = published["url"].toString().toStdString();

    SECTION("the published token is accepted") {
        httplib::ws::WebSocketClient client(endpoint,
                                            {{"Authorization", "Bearer " + token.toStdString()}});
        REQUIRE(client.connect());
    }

    SECTION("anything else is not") {
        httplib::ws::WebSocketClient client(endpoint, {{"Authorization", "Bearer not-the-token"}});
        REQUIRE_FALSE(client.connect());
    }
}

TEST_CASE("Turning the remote API off and on again leaves it working",
          "[remote][host][lifecycle]") {
    MessageThreadRelaxation relax;
    ScopedRemoteApiConfig config(true);

    MockMagdaApi api;
    RemoteApiHost host(api);
    REQUIRE(host.start());
    const auto firstPort = host.boundPort();
    REQUIRE(firstPort > 0);

    // What the settings toggle does. `stop()` would also shut the dispatcher
    // down, and that is one-way: the listeners would come back up around a
    // service whose execution state has been retired, so every operation
    // through them would answer `cancelled` and every subscription would fail
    // to register — a server that looks healthy and does nothing.
    host.stopListening();
    REQUIRE_FALSE(host.isRunning());
    REQUIRE(host.boundPort() == 0);
    REQUIRE(host.mcpPort() == 0);
    REQUIRE_FALSE(host.tokenFile().existsAsFile());

    REQUIRE(host.start());
    REQUIRE(host.isRunning());
    REQUIRE(host.boundPort() > 0);
    REQUIRE(host.tokenFile().existsAsFile());

    // The dispatcher still executes, which is the whole point of the
    // distinction.
    REQUIRE_FALSE(host.service().isShutdown());
    const auto response = host.service().dispatchSync(
        "project.get", juce::var(new juce::DynamicObject()), fullyGrantedContext());
    REQUIRE(response.ok);

    // And a fresh credential, because the old one was withdrawn.
    const auto token = readTokenFile(host.tokenFile())["token"].toString();
    REQUIRE(token.length() == 64);
}

TEST_CASE("Restarting the listeners voids cached request identities", "[remote][host][lifecycle]") {
    MessageThreadRelaxation relax;
    ScopedRemoteApiConfig config(true);

    MockMagdaApi api;
    api.project_.info.tempo = 120.0;
    RemoteApiHost host(api);
    REQUIRE(host.start());

    // A client MAGDA has not seen is read-only (#1860), and these clients send
    // no `?client=`, so they are all `unknown`. Granting edit up front is what
    // the user would have done in the settings dialog; without it every write
    // below would come back `permission_denied` and the test would be measuring
    // the permission model rather than identity reuse.
    host.clients().setScopes(ANONYMOUS_CLIENT, allScopes());

    // Driven through real sockets, because the identity under test is the one
    // the transport builds. A hand-written context would assert whatever shape
    // the test happened to choose, which is exactly the thing that changed.
    const auto setTempo = [](const juce::String& endpoint, const juce::String& token,
                             double tempo) {
        httplib::ws::WebSocketClient client(endpoint.toStdString(),
                                            {{"Authorization", "Bearer " + token.toStdString()}});
        REQUIRE(client.connect());
        auto* params = new juce::DynamicObject();
        params->setProperty("tempo", tempo);
        auto* request = new juce::DynamicObject();
        request->setProperty("jsonrpc", "2.0");
        // The same JSON-RPC id every time — a client that counts from 1, which
        // is every client.
        request->setProperty("id", 1);
        request->setProperty("method", "project.setTempo");
        request->setProperty("params", juce::var(params));
        REQUIRE(client.send(juce::JSON::toString(juce::var(request), true).toStdString()));
        std::string reply;
        REQUIRE(client.read(reply) != httplib::ws::ReadResult::Fail);
    };

    const auto endpointOf = [](const RemoteApiHost& h) {
        return "ws://127.0.0.1:" + juce::String(h.boundPort()) + "/rpc";
    };

    setTempo(endpointOf(host), readTokenFile(host.tokenFile())["token"].toString(), 140.0);
    REQUIRE(api.project_.info.tempo == 140.0);

    host.stopListening();
    REQUIRE(host.start());

    // A new listener, a new client, and the same JSON-RPC id 1 on the same
    // connection id 1. Without a generation in the identity this key would hit
    // the entry cached above and replay it, leaving the tempo at 140 for a
    // write that was never executed.
    setTempo(endpointOf(host), readTokenFile(host.tokenFile())["token"].toString(), 90.0);
    REQUIRE(api.project_.info.tempo == 90.0);
}

TEST_CASE("A client-supplied idempotency key still dedupes across a restart",
          "[remote][host][lifecycle]") {
    MessageThreadRelaxation relax;
    ScopedRemoteApiConfig config(true);

    MockMagdaApi api;
    api.project_.info.tempo = 120.0;
    RemoteApiHost host(api);
    REQUIRE(host.start());

    // MCP keys are UUIDs the client chooses, and they are *meant* to outlive a
    // restart: that is what makes a retry safe when the endpoint went away
    // mid-request. Clearing the shared cache on restart would have broken this
    // — the retry would apply the write a second time.
    auto context = fullyGrantedContext();
    context.clientId = "mcp:stateless";
    context.requestId = "mcp::0f7c1a2b-3d4e-5f60-8a9b-cbd0e1f23456";

    auto* first = new juce::DynamicObject();
    first->setProperty("tempo", 140.0);
    REQUIRE(host.service().dispatchSync("project.setTempo", juce::var(first), context).ok);
    REQUIRE(api.project_.info.tempo == 140.0);

    host.stopListening();
    REQUIRE(host.start());

    auto* retry = new juce::DynamicObject();
    retry->setProperty("tempo", 90.0);
    const auto replayed =
        host.service().dispatchSync("project.setTempo", juce::var(retry), context);
    REQUIRE(replayed.ok);
    REQUIRE(api.project_.info.tempo == 140.0);
}

TEST_CASE("The record names both transports, each with its own token",
          "[remote][host][auth][mcp]") {
    MessageThreadRelaxation relax;
    ScopedRemoteApiConfig config(true);

    MockMagdaApi api;
    RemoteApiHost host(api);
    REQUIRE(host.start());
    REQUIRE(host.mcpPort() > 0);
    // Two listeners, so two ports. Sharing one would mean sharing a resource
    // budget between a WebSocket connection cap and an SSE stream cap, which are
    // not the same thing.
    REQUIRE(host.mcpPort() != host.boundPort());

    const auto published = readTokenFile(host.tokenFile());
    const auto token = published["mcpToken"].toString();
    // Separate credentials since #2142, so that either transport can be
    // re-credentialled without dropping the other's sessions.
    REQUIRE(token.isNotEmpty());
    REQUIRE(published["token"].toString().isNotEmpty());
    REQUIRE(token != published["token"].toString());
    REQUIRE(static_cast<int>(published["mcpPort"]) == host.mcpPort());
    REQUIRE(published["mcpUrl"].toString() ==
            "http://127.0.0.1:" + juce::String(host.mcpPort()) + "/mcp");
    // The WebSocket entry is still there: a client picks the transport it wants
    // out of one record rather than needing to know which file to read.
    REQUIRE(published["url"].toString().startsWith("ws://"));

    httplib::Client client("http://127.0.0.1:" + std::to_string(host.mcpPort()));

    // The MCP endpoint takes the MCP token.
    const httplib::Headers headers = {{"Authorization", "Bearer " + token.toStdString()},
                                      {"MCP-Protocol-Version", "2026-07-28"},
                                      {"Mcp-Method", "server/discover"}};
    auto discovered =
        client.Post("/mcp", headers,
                    R"({"jsonrpc":"2.0","id":1,"method":"server/discover","params":{"_meta":{)"
                    R"("io.modelcontextprotocol/protocolVersion":"2026-07-28",)"
                    R"("io.modelcontextprotocol/clientCapabilities":{}}}})",
                    "application/json");
    REQUIRE(discovered);
    REQUIRE(discovered->status == 200);

    auto refused =
        client.Post("/mcp", {{"Authorization", "Bearer not-the-token"}}, "{}", "application/json");
    REQUIRE(refused);
    REQUIRE(refused->status == 401);

    // And specifically not the WebSocket's, which is the whole point of them
    // being two credentials rather than one.
    auto crossed = client.Post(
        "/mcp", {{"Authorization", "Bearer " + published["token"].toString().toStdString()}}, "{}",
        "application/json");
    REQUIRE(crossed);
    REQUIRE(crossed->status == 401);

    // Both listeners go down together, and the record goes with them.
    host.stop();
    REQUIRE(host.mcpPort() == 0);
    REQUIRE_FALSE(host.tokenFile().existsAsFile());
}

#if !JUCE_WINDOWS
TEST_CASE("The token file is readable only by its owner", "[remote][host][auth]") {
    MessageThreadRelaxation relax;
    ScopedRemoteApiConfig config(true);

    MockMagdaApi api;
    RemoteApiHost host(api);
    REQUIRE(host.start());

    struct stat info {};
    REQUIRE(stat(host.tokenFile().getFullPathName().toRawUTF8(), &info) == 0);

    // A credential every account on the machine can read is not a credential.
    REQUIRE((info.st_mode & 0777) == (S_IRUSR | S_IWUSR));
}
#endif

TEST_CASE("Stopping takes the credential down with the listener", "[remote][host][lifecycle]") {
    MessageThreadRelaxation relax;
    ScopedRemoteApiConfig config(true);

    MockMagdaApi api;
    RemoteApiHost host(api);
    REQUIRE(host.start());

    const auto file = host.tokenFile();
    const auto endpoint = readTokenFile(file)["url"].toString().toStdString();
    const auto token = readTokenFile(file)["token"].toString().toStdString();
    REQUIRE(file.existsAsFile());

    host.stop();

    // Leaving the file behind would advertise a dead port and a token that no
    // longer opens anything.
    REQUIRE_FALSE(host.isRunning());
    REQUIRE(host.boundPort() == 0);
    REQUIRE_FALSE(file.existsAsFile());

    httplib::ws::WebSocketClient client(endpoint, {{"Authorization", "Bearer " + token}});
    REQUIRE_FALSE(client.connect());

    host.stop();  // idempotent
}

TEST_CASE("Each run generates its own token", "[remote][host][auth]") {
    MessageThreadRelaxation relax;
    ScopedRemoteApiConfig config(true);

    MockMagdaApi api;

    juce::String first;
    {
        RemoteApiHost host(api);
        REQUIRE(host.start());
        first = readTokenFile(host.tokenFile())["token"].toString();
    }

    RemoteApiHost second(api);
    REQUIRE(second.start());

    // Per run, not per install: a token that survived a restart would outlive
    // the reason anyone was trusted with it.
    REQUIRE(readTokenFile(second.tokenFile())["token"].toString() != first);
}

// ===========================================================================
// Permissions, at the level the application actually assembles them (#1860)
// ===========================================================================

TEST_CASE("A brand-new client reaches a real host read-only", "[remote][host][permissions]") {
    MessageThreadRelaxation relax;
    ScopedRemoteApiConfig config(true);

    MockMagdaApi api;
    api.project_.info.tempo = 120.0;
    RemoteApiHost host(api);
    REQUIRE(host.start());

    const auto token = readTokenFile(host.tokenFile())["token"].toString();
    const auto endpoint =
        "ws://127.0.0.1:" + juce::String(host.boundPort()).toStdString() + "/rpc?client=probe";

    const auto send = [&](httplib::ws::WebSocketClient& client, const juce::String& method,
                          juce::var params) {
        auto* request = new juce::DynamicObject();
        request->setProperty("jsonrpc", "2.0");
        request->setProperty("id", 1);
        request->setProperty("method", method);
        request->setProperty("params", params);
        REQUIRE(client.send(juce::JSON::toString(juce::var(request), true).toStdString()));
        std::string reply;
        REQUIRE(client.read(reply) == httplib::ws::ReadResult::Text);
        juce::var parsed;
        REQUIRE(juce::JSON::parse(juce::String(reply), parsed).wasOk());
        return parsed;
    };

    httplib::ws::WebSocketClient client(endpoint,
                                        {{"Authorization", "Bearer " + token.toStdString()}});
    REQUIRE(client.connect());

    // Reads work out of the box, so a client is useful the moment it connects.
    const auto read = send(client, "project.get", juce::var(new juce::DynamicObject()));
    REQUIRE(read["error"].isVoid());

    // Writes do not, and say which permission is missing.
    //
    // Held as a `var`, not as the raw pointer: `juce::var` takes ownership of a
    // `DynamicObject*` by reference count, so wrapping the same pointer twice
    // frees it after the first request and sends whatever lands in that memory
    // next with the second.
    juce::var params(new juce::DynamicObject());
    params.getDynamicObject()->setProperty("tempo", 140.0);
    const auto write = send(client, "project.setTempo", params);
    REQUIRE(write["error"]["data"]["code"].toString() == "permission_denied");
    REQUIRE(api.project_.info.tempo == 120.0);

    // It is listed and connected, so the user has something to grant.
    REQUIRE(host.clients().peekScopes("probe").has_value());
    REQUIRE(host.clients().connectionCount() == 1);

    // Granting applies to the next request on the socket it is already holding.
    host.clients().setScopes("probe", ScopeSet{Scope::Read, Scope::Edit});
    const auto granted = send(client, "project.setTempo", params);
    REQUIRE(granted["error"].isVoid());
    REQUIRE(api.project_.info.tempo == 140.0);
}

TEST_CASE("Grants outlive the host that recorded them", "[remote][host][permissions]") {
    // The user's decision about a client is not per-run state: a toggle they set
    // once must not be forgotten because MAGDA restarted, or the permission
    // prompt becomes something they learn to click through.
    MessageThreadRelaxation relax;
    ScopedRemoteApiConfig config(true);

    MockMagdaApi api;
    {
        RemoteApiHost host(api);
        host.clients().setScopes("cursor", ScopeSet{Scope::Read, Scope::Transport});
    }

    // The grant reached config on the way out…
    REQUIRE(Config::getInstance().getRemoteApiClients().isArray());

    // …and a fresh host loads it rather than starting the client over.
    RemoteApiHost restored(api);
    REQUIRE(restored.clients().scopesFor("cursor") == ScopeSet{Scope::Read, Scope::Transport});
    REQUIRE(restored.clients().scopesFor("someone-else") == defaultClientScopes());
}

TEST_CASE("Rotating the token invalidates the old one and drops its clients",
          "[remote][host][permissions]") {
    MessageThreadRelaxation relax;
    ScopedRemoteApiConfig config(true);

    MockMagdaApi api;
    RemoteApiHost host(api);
    REQUIRE(host.start());

    const auto before = readTokenFile(host.tokenFile())["token"].toString();
    const auto endpointFor = [&] {
        return "ws://127.0.0.1:" + juce::String(host.boundPort()).toStdString() + "/rpc";
    };

    {
        httplib::ws::WebSocketClient client(endpointFor(),
                                            {{"Authorization", "Bearer " + before.toStdString()}});
        REQUIRE(client.connect());
        // `connect()` returns once the 101 is written, which is a moment before
        // the server's own thread has registered the connection. Waiting for
        // the registry rather than asserting on it immediately is the
        // difference between testing rotation and testing that race.
        REQUIRE(waitFor([&] { return host.clients().connectionCount() == 1; }));

        REQUIRE(host.rotateToken(Transport::WebSocket));
    }

    // A new credential, published where a client will look for it.
    const auto after = readTokenFile(host.tokenFile())["token"].toString();
    REQUIRE(after.isNotEmpty());
    REQUIRE(after != before);
    REQUIRE(host.isRunning());

    // Nothing admitted by the old one is still connected — a token that left its
    // own sessions running would not have been revoked.
    REQUIRE(host.clients().connectionCount() == 0);

    // And the old one no longer opens anything.
    httplib::ws::WebSocketClient stale(endpointFor(),
                                       {{"Authorization", "Bearer " + before.toStdString()}});
    REQUIRE_FALSE(stale.connect());

    httplib::ws::WebSocketClient fresh(endpointFor(),
                                       {{"Authorization", "Bearer " + after.toStdString()}});
    REQUIRE(fresh.connect());
}

TEST_CASE("Rotation is refused when nothing is listening", "[remote][host][permissions]") {
    // There is no credential to rotate, and minting one would publish a record
    // for a port nobody is answering on.
    MessageThreadRelaxation relax;
    ScopedRemoteApiConfig config(false);

    MockMagdaApi api;
    RemoteApiHost host(api);
    REQUIRE_FALSE(host.rotateToken(Transport::WebSocket));
    REQUIRE_FALSE(host.tokenFile().existsAsFile());
}

// =============================================================================
// Independent transports (#2142)
// =============================================================================

TEST_CASE("Either transport runs without the other", "[remote][host][lifecycle]") {
    MessageThreadRelaxation relax;
    ScopedRemoteApiConfig config(false);
    auto& settings = Config::getInstance();

    SECTION("WebSocket alone publishes no MCP entry") {
        settings.setRemoteApiWebSocketEnabled(true);
        settings.setRemoteApiMcpEnabled(false);

        MockMagdaApi api;
        RemoteApiHost host(api);
        REQUIRE(host.start());
        REQUIRE(host.isRunning(Transport::WebSocket));
        REQUIRE_FALSE(host.isRunning(Transport::Mcp));
        REQUIRE(host.mcpPort() == 0);

        const auto published = readTokenFile(host.tokenFile());
        REQUIRE(published["url"].toString().startsWith("ws://"));
        REQUIRE(published["token"].toString().isNotEmpty());
        // Absent, not empty: a client that finds the key and an unusable value
        // has to special-case it, where a missing key is already the answer.
        REQUIRE_FALSE(published.hasProperty("mcpUrl"));
        REQUIRE_FALSE(published.hasProperty("mcpToken"));
    }

    SECTION("MCP alone publishes no WebSocket entry") {
        settings.setRemoteApiWebSocketEnabled(false);
        settings.setRemoteApiMcpEnabled(true);

        MockMagdaApi api;
        RemoteApiHost host(api);
        // The WebSocket used to be the anchor for both `isRunning` and the
        // record, so MCP-only was not expressible at all before #2142.
        REQUIRE(host.start());
        REQUIRE(host.isRunning());
        REQUIRE_FALSE(host.isRunning(Transport::WebSocket));
        REQUIRE(host.isRunning(Transport::Mcp));
        REQUIRE(host.boundPort() == 0);

        const auto published = readTokenFile(host.tokenFile());
        REQUIRE(published["mcpUrl"].toString().startsWith("http://"));
        REQUIRE(published["mcpToken"].toString().isNotEmpty());
        REQUIRE_FALSE(published.hasProperty("url"));
        REQUIRE_FALSE(published.hasProperty("token"));
    }

    SECTION("neither means no record at all") {
        MockMagdaApi api;
        RemoteApiHost host(api);
        REQUIRE_FALSE(host.start());
        REQUIRE_FALSE(host.isRunning());
        REQUIRE_FALSE(host.tokenFile().existsAsFile());
    }
}

TEST_CASE("Rotating one transport leaves the other's credential alone",
          "[remote][host][auth][mcp]") {
    // The reason the tokens are separate: re-credentialling a misbehaving script
    // must not drop an AI host mid-conversation.
    MessageThreadRelaxation relax;
    ScopedRemoteApiConfig config(true);

    MockMagdaApi api;
    RemoteApiHost host(api);
    REQUIRE(host.start());

    const auto before = readTokenFile(host.tokenFile());
    const auto wsBefore = before["token"].toString();
    const auto mcpBefore = before["mcpToken"].toString();
    const auto mcpPortBefore = static_cast<int>(before["mcpPort"]);
    REQUIRE(wsBefore.isNotEmpty());
    REQUIRE(mcpBefore.isNotEmpty());

    REQUIRE(host.rotateToken(Transport::WebSocket));

    const auto after = readTokenFile(host.tokenFile());
    REQUIRE(after["token"].toString() != wsBefore);
    REQUIRE(after["mcpToken"].toString() == mcpBefore);
    // Not restarted either, so an open MCP session survives on its own port.
    REQUIRE(static_cast<int>(after["mcpPort"]) == mcpPortBefore);
    REQUIRE(host.isRunning(Transport::Mcp));

    // And the other way round.
    REQUIRE(host.rotateToken(Transport::Mcp));
    const auto last = readTokenFile(host.tokenFile());
    REQUIRE(last["mcpToken"].toString() != mcpBefore);
    REQUIRE(last["token"].toString() == after["token"].toString());
}

TEST_CASE("Stopping one transport keeps the other's record entry",
          "[remote][host][lifecycle][mcp]") {
    MessageThreadRelaxation relax;
    ScopedRemoteApiConfig config(true);

    MockMagdaApi api;
    RemoteApiHost host(api);
    REQUIRE(host.start());
    const auto wsToken = readTokenFile(host.tokenFile())["token"].toString();

    host.stopTransport(Transport::Mcp);

    REQUIRE(host.isRunning(Transport::WebSocket));
    REQUIRE_FALSE(host.isRunning(Transport::Mcp));
    const auto published = readTokenFile(host.tokenFile());
    // Rewritten, not deleted: a WebSocket client re-reading the record on
    // reconnect must still find its own entry, unchanged.
    REQUIRE(published["token"].toString() == wsToken);
    REQUIRE_FALSE(published.hasProperty("mcpUrl"));

    host.stopTransport(Transport::WebSocket);
    REQUIRE_FALSE(host.tokenFile().existsAsFile());
}

TEST_CASE("Config switches survive the transport split", "[remote][host][lifecycle]") {
    auto switchesFor = [](const char* json) {
        juce::var parsed;
        REQUIRE(juce::JSON::parse(json, parsed).wasOk());
        return magda::remoteApiSwitchesFrom(parsed);
    };

    SECTION("a file predating the split starts both") {
        // `enabled` was one switch for two listeners. Reading it as anything
        // less would silently turn off a transport already in use.
        const auto s = switchesFor(R"({"enabled":true,"port":0,"mcpPort":0})");
        REQUIRE(s.websocket);
        REQUIRE(s.mcp);
    }

    SECTION("a legacy file that was off stays off") {
        const auto s = switchesFor(R"({"enabled":false})");
        REQUIRE_FALSE(s.websocket);
        REQUIRE_FALSE(s.mcp);
    }

    SECTION("an explicit switch beats the legacy flag, including turning one off") {
        const auto s = switchesFor(R"({"enabled":true,"mcp":false})");
        REQUIRE(s.websocket);
        REQUIRE_FALSE(s.mcp);
    }

    SECTION("the new keys stand on their own") {
        const auto s = switchesFor(R"({"websocket":false,"mcp":true})");
        REQUIRE_FALSE(s.websocket);
        REQUIRE(s.mcp);
    }

    SECTION("no remoteApi block at all means nothing listens") {
        const auto s = magda::remoteApiSwitchesFrom(juce::var());
        REQUIRE_FALSE(s.websocket);
        REQUIRE_FALSE(s.mcp);
    }
}
