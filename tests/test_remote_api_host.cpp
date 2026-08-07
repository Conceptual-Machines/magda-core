// Lifecycle and credential handling for the remote API (#1856): what MAGDA
// opens, what it publishes, and what it leaves behind.

#include <httplib.h>

#include <catch2/catch_test_macros.hpp>
#include <string>

#include "MockMagdaApi.hpp"
#include "magda/daw/api/remote_api_host.hpp"
#include "magda/daw/core/Config.hpp"

#if !JUCE_WINDOWS
    #include <sys/stat.h>
    #include <unistd.h>
#endif

using namespace magda;
using namespace magda::remote;
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
        wasEnabled_ = config.getRemoteApiEnabled();
        previousPort_ = config.getRemoteApiPort();
        previousMcpPort_ = config.getRemoteApiMcpPort();
        config.setRemoteApiEnabled(enabled);
        config.setRemoteApiPort(0);
        config.setRemoteApiMcpPort(0);
    }
    ~ScopedRemoteApiConfig() {
        auto& config = Config::getInstance();
        config.setRemoteApiEnabled(wasEnabled_);
        config.setRemoteApiPort(previousPort_);
        config.setRemoteApiMcpPort(previousMcpPort_);
    }

  private:
    bool wasEnabled_ = false;
    int previousPort_ = 0;
    int previousMcpPort_ = 0;
};

juce::var readTokenFile(const juce::File& file) {
    juce::var parsed;
    REQUIRE(juce::JSON::parse(file.loadFileAsString(), parsed).wasOk());
    return parsed;
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

TEST_CASE("The record names both transports, and both take the same token",
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
    const auto token = published["token"].toString();
    REQUIRE(static_cast<int>(published["mcpPort"]) == host.mcpPort());
    REQUIRE(published["mcpUrl"].toString() ==
            "http://127.0.0.1:" + juce::String(host.mcpPort()) + "/mcp");
    // The WebSocket entry is still there: a client picks the transport it wants
    // out of one record rather than needing to know which file to read.
    REQUIRE(published["url"].toString().startsWith("ws://"));

    httplib::Client client("http://127.0.0.1:" + std::to_string(host.mcpPort()));

    // The token authenticates the user at the keyboard, not a protocol, so the
    // one credential opens both transports.
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
