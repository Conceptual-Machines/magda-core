// The stdio-to-HTTP bridge (#1858), driven as a real child process against a
// real MCP endpoint.
//
// This is the piece that makes MAGDA registrable in an MCP host's config at all:
// the endpoint's port is ephemeral and its bearer token is regenerated every
// launch, so a host cannot hold a static URL and credential. The bridge resolves
// both from the discovery record on each request. That property is only worth
// anything if it actually works, and it cannot be tested in-process — the whole
// point is the process boundary — so this forks the real binary and talks to it
// over real pipes.

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <string>
#include <thread>
#include <vector>

#include "MockMagdaApi.hpp"
#include "RemoteTestScopes.hpp"
#include "magda/daw/api/remote_mcp_server.hpp"
#include "magda/daw/api/remote_service.hpp"
#include "magda/daw/api/remote_subscriptions.hpp"

#if !JUCE_WINDOWS
    #include <fcntl.h>
    #include <signal.h>
    #include <sys/wait.h>
    #include <unistd.h>
#endif

using namespace magda;
using namespace magda::remote;
using magda::test::MockMagdaApi;

#if !JUCE_WINDOWS

namespace {

constexpr const char* kToken = "bridge-token-77a1";

struct MessageThreadRelaxation {
    ScopedMessageThreadAssertionDisabler disabler;
};

/// The bridge binary, path injected by CMake so the test cannot drift from the
/// target it is exercising.
const char* bridgeBinary() {
    return MAGDA_MCP_BRIDGE_BINARY;
}

/**
 * @brief A running `magda-mcp`, with its stdin and stdout as pipes.
 *
 * `juce::ChildProcess` reads a child's output but cannot write to its input,
 * which is exactly half of what a stdio transport needs — hence the raw
 * fork/exec. POSIX only; the bridge's logic is platform-independent and the
 * Windows path differs only in how the process is spawned.
 */
class BridgeProcess {
  public:
    explicit BridgeProcess(const juce::File& dataDir) {
        int toChild[2] = {};
        int fromChild[2] = {};
        REQUIRE(pipe(toChild) == 0);
        REQUIRE(pipe(fromChild) == 0);

        pid_ = fork();
        REQUIRE(pid_ >= 0);

        if (pid_ == 0) {
            dup2(toChild[0], STDIN_FILENO);
            dup2(fromChild[1], STDOUT_FILENO);
            ::close(toChild[1]);
            ::close(fromChild[0]);
            // The bridge finds MAGDA through this, which is what lets the test
            // point it at a record it wrote rather than at the developer's real
            // application-support directory.
            setenv("MAGDA_DATA_DIR", dataDir.getFullPathName().toRawUTF8(), 1);
            execl(bridgeBinary(), bridgeBinary(), nullptr);
            _exit(127);
        }

        ::close(toChild[0]);
        ::close(fromChild[1]);
        stdin_ = toChild[1];
        stdout_ = fromChild[0];
    }

    ~BridgeProcess() {
        close();
    }

    void send(const juce::String& json) {
        const auto line = json.toStdString() + "\n";
        REQUIRE(write(stdin_, line.data(), line.size()) == static_cast<ssize_t>(line.size()));
    }

    /// One message from the bridge, or a void var if none arrived in time.
    juce::var receive(std::chrono::milliseconds timeout = std::chrono::seconds(10)) {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (std::chrono::steady_clock::now() < deadline) {
            const auto newline = buffer_.find('\n');
            if (newline != std::string::npos) {
                const auto line = buffer_.substr(0, newline);
                buffer_.erase(0, newline + 1);
                juce::var parsed;
                juce::JSON::parse(juce::String(line), parsed);
                return parsed;
            }

            char chunk[4096];
            // Non-blocking so a bridge that says nothing fails as a timeout with
            // a readable assertion rather than hanging the suite.
            const auto flags = fcntl(stdout_, F_GETFL, 0);
            fcntl(stdout_, F_SETFL, flags | O_NONBLOCK);
            const auto count = read(stdout_, chunk, sizeof(chunk));
            if (count > 0)
                buffer_.append(chunk, static_cast<std::size_t>(count));
            else
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        return {};
    }

    void close() {
        if (stdin_ >= 0) {
            ::close(stdin_);
            stdin_ = -1;
        }
        if (pid_ > 0) {
            int status = 0;
            // Closing stdin is how a host shuts a stdio server down, so the
            // bridge should exit on its own. Kill only if it does not.
            for (int i = 0; i < 200 && waitpid(pid_, &status, WNOHANG) == 0; ++i)
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            if (waitpid(pid_, &status, WNOHANG) == 0) {
                kill(pid_, SIGKILL);
                waitpid(pid_, &status, 0);
            }
            pid_ = -1;
        }
        if (stdout_ >= 0) {
            ::close(stdout_);
            stdout_ = -1;
        }
    }

  private:
    pid_t pid_ = -1;
    int stdin_ = -1;
    int stdout_ = -1;
    std::string buffer_;
};

juce::var object(std::initializer_list<std::pair<const char*, juce::var>> fields) {
    auto* result = new juce::DynamicObject();
    for (const auto& [key, value] : fields)
        result->setProperty(key, value);
    return result;
}

juce::String request(int id, const juce::String& method, juce::var params = {}) {
    auto* message = new juce::DynamicObject();
    message->setProperty("jsonrpc", "2.0");
    message->setProperty("id", id);
    message->setProperty("method", method);
    if (params.getDynamicObject() != nullptr)
        message->setProperty("params", params);
    return juce::JSON::toString(juce::var(message), true);
}

/// `params` carrying the `_meta` a modern request must have.
juce::var modernParams(juce::var params = {}) {
    if (params.getDynamicObject() == nullptr)
        params = juce::var(new juce::DynamicObject());
    params.getDynamicObject()->setProperty(
        "_meta", object({{MCP_META_PROTOCOL_VERSION, "2026-07-28"},
                         {MCP_META_CLIENT_CAPABILITIES, juce::var(new juce::DynamicObject())}}));
    return params;
}

/// The record MAGDA publishes on startup, written by hand so the test controls
/// what the bridge will discover.
void writeRecord(const juce::File& dataDir, int port, const juce::String& token) {
    auto* payload = new juce::DynamicObject();
    payload->setProperty("port", port);
    payload->setProperty("token", token);
    payload->setProperty("mcpPort", port);
    payload->setProperty("mcpUrl", "http://127.0.0.1:" + juce::String(port) + "/mcp");
    payload->setProperty("pid", static_cast<juce::int64>(getpid()));

    dataDir.createDirectory();
    dataDir.getChildFile("remote-api-" + juce::String(static_cast<juce::int64>(getpid())) + ".json")
        .replaceWithText(juce::JSON::toString(juce::var(payload), true));
}

RemoteMcpServer::Options serverOptions() {
    RemoteMcpServer::Options options;
    options.bearerToken = kToken;
    // Without a registry the endpoint refuses everything, reads included
    // (#1860), and these tests are about whether the bridge relays a call at
    // all rather than about who may make it.
    options.clients = &magda::test::permissiveRegistry();
    return options;
}

}  // namespace

TEST_CASE("The bridge finds a running MAGDA and proxies a modern call", "[remote][mcp-bridge]") {
    MessageThreadRelaxation relax;
    MockMagdaApi api;
    api.project_.info.name = "Bridged";
    api.project_.info.tempo = 131.0;

    RemoteApiService service(api);
    RemoteMcpServer server(service, serverOptions());
    REQUIRE(server.start());

    juce::TemporaryFile scratch;
    const auto dataDir = scratch.getFile().getSiblingFile("magda-bridge-test");
    writeRecord(dataDir, server.boundPort(), kToken);

    BridgeProcess bridge(dataDir);

    // The host sends nothing but JSON-RPC; the URL, the token, and the mirrored
    // headers the endpoint requires are all the bridge's job.
    bridge.send(request(1, "server/discover", modernParams()));
    const auto discovered = bridge.receive();
    REQUIRE(discovered.getDynamicObject() != nullptr);
    REQUIRE(static_cast<int>(discovered["id"]) == 1);
    REQUIRE(discovered["error"].isVoid());
    REQUIRE((*discovered["result"]["supportedVersions"].getArray())[0].toString() == "2026-07-28");

    // tools/call needs an `Mcp-Name` header derived from the body. The endpoint
    // rejects a mismatch with -32020, so a reply at all proves the bridge built
    // it correctly.
    bridge.send(
        request(2, "tools/call",
                modernParams(object({{"name", "project.get"},
                                     {"arguments", juce::var(new juce::DynamicObject())}}))));
    const auto called = bridge.receive();
    REQUIRE(called.getDynamicObject() != nullptr);
    REQUIRE(static_cast<int>(called["id"]) == 2);
    REQUIRE(called["error"].isVoid());
    REQUIRE(static_cast<double>(called["result"]["structuredContent"]["tempo"]) == 131.0);

    bridge.close();
    dataDir.deleteRecursively();
}

TEST_CASE("The bridge carries a legacy session across requests", "[remote][mcp-bridge]") {
    MessageThreadRelaxation relax;
    MockMagdaApi api;
    RemoteApiService service(api);
    RemoteMcpServer server(service, serverOptions());
    REQUIRE(server.start());

    juce::TemporaryFile scratch;
    const auto dataDir = scratch.getFile().getSiblingFile("magda-bridge-legacy");
    writeRecord(dataDir, server.boundPort(), kToken);

    BridgeProcess bridge(dataDir);

    // A stdio host on the handshake era. `Mcp-Session-Id` is an HTTP header the
    // host never sees, so if the bridge did not remember it from the initialize
    // response, the next request would come back 404.
    bridge.send(request(1, "initialize", object({{"protocolVersion", "2025-11-25"}})));
    const auto initialized = bridge.receive();
    REQUIRE(initialized.getDynamicObject() != nullptr);
    REQUIRE(initialized["result"]["protocolVersion"].toString() == "2025-11-25");

    bridge.send(request(2, "tools/list"));
    const auto tools = bridge.receive();
    REQUIRE(tools.getDynamicObject() != nullptr);
    REQUIRE(tools["error"].isVoid());
    REQUIRE_FALSE(tools["result"]["tools"].getArray()->isEmpty());

    bridge.close();
    dataDir.deleteRecursively();
}

TEST_CASE("The bridge survives MAGDA restarting under it", "[remote][mcp-bridge]") {
    MessageThreadRelaxation relax;
    MockMagdaApi api;
    RemoteApiService service(api);

    juce::TemporaryFile scratch;
    const auto dataDir = scratch.getFile().getSiblingFile("magda-bridge-restart");

    auto first = std::make_unique<RemoteMcpServer>(service, serverOptions());
    REQUIRE(first->start());
    writeRecord(dataDir, first->boundPort(), kToken);

    BridgeProcess bridge(dataDir);

    bridge.send(request(1, "ping", modernParams()));
    REQUIRE(bridge.receive()["error"].isVoid());

    // A different port and a different token — exactly what a real restart
    // produces, and exactly what a static host config cannot follow. The bridge
    // re-reads the record when its cached endpoint stops answering.
    first->stop();
    first.reset();

    auto options = serverOptions();
    options.bearerToken = "bridge-token-rotated";
    RemoteMcpServer second(service, options);
    REQUIRE(second.start());
    REQUIRE(second.boundPort() != 0);
    writeRecord(dataDir, second.boundPort(), "bridge-token-rotated");

    bridge.send(request(2, "ping", modernParams()));
    const auto afterRestart = bridge.receive();
    REQUIRE(afterRestart.getDynamicObject() != nullptr);
    REQUIRE(static_cast<int>(afterRestart["id"]) == 2);
    REQUIRE(afterRestart["error"].isVoid());

    bridge.close();
    dataDir.deleteRecursively();
}

TEST_CASE("A legacy session survives MAGDA restarting under it", "[remote][mcp-bridge]") {
    MessageThreadRelaxation relax;
    MockMagdaApi api;
    RemoteApiService service(api);

    juce::TemporaryFile scratch;
    const auto dataDir = scratch.getFile().getSiblingFile("magda-bridge-session");

    auto first = std::make_unique<RemoteMcpServer>(service, serverOptions());
    REQUIRE(first->start());
    writeRecord(dataDir, first->boundPort(), kToken);

    BridgeProcess bridge(dataDir);

    bridge.send(request(1, "initialize", object({{"protocolVersion", "2025-11-25"}})));
    REQUIRE(bridge.receive()["result"]["protocolVersion"].toString() == "2025-11-25");

    // Restart. The new instance has an empty session table, so the id the bridge
    // is holding names nothing — which the host cannot know or fix, having sent
    // its one `initialize` at startup. Left alone this fails every call for the
    // life of the host, which is exactly the failure a real host hit.
    first->stop();
    first.reset();

    auto options = serverOptions();
    options.bearerToken = "session-token-rotated";
    RemoteMcpServer second(service, options);
    REQUIRE(second.start());
    writeRecord(dataDir, second.boundPort(), "session-token-rotated");

    bridge.send(request(2, "tools/list"));
    const auto tools = bridge.receive();
    REQUIRE(tools.getDynamicObject() != nullptr);
    REQUIRE(static_cast<int>(tools["id"]) == 2);
    // Answered, not 404: the bridge re-handshook on the host's behalf.
    REQUIRE(tools["error"].isVoid());
    REQUIRE_FALSE(tools["result"]["tools"].getArray()->isEmpty());

    bridge.close();
    dataDir.deleteRecursively();
}

TEST_CASE("The bridge absorbs rate-limit refusals with backoff", "[remote][mcp-bridge]") {
    // The endpoint's token bucket refuses a burst partway through (burst =
    // maxConcurrentRequests). A stdio host treats errors as terminal, so the
    // bridge retries 429s itself; every call in a modest burst must answer.
    const MessageThreadRelaxation relaxation;
    MockMagdaApi api;
    RemoteApiService service(api);

    auto options = serverOptions();
    options.maxConcurrentRequests = 2;    // burst of 2 tokens
    options.maxRequestsPerSecond = 20.0;  // one token back every 50ms
    RemoteMcpServer server(service, options);
    REQUIRE(server.start());

    juce::TemporaryFile scratch;
    const auto dataDir = scratch.getFile().getSiblingFile("magda-bridge-rate-limit");
    writeRecord(dataDir, server.boundPort(), kToken);
    BridgeProcess bridge(dataDir);

    for (int id = 1; id <= 8; ++id) {
        bridge.send(request(id, "ping", modernParams()));
        const auto response = bridge.receive();
        INFO("request " << id);
        REQUIRE(response.getDynamicObject() != nullptr);
        REQUIRE(static_cast<int>(response["id"]) == id);
        REQUIRE(response["error"].isVoid());
    }

    dataDir.deleteRecursively();
}

TEST_CASE("With no MAGDA running, the bridge says so rather than hanging", "[remote][mcp-bridge]") {
    juce::TemporaryFile scratch;
    const auto dataDir = scratch.getFile().getSiblingFile("magda-bridge-empty");
    dataDir.createDirectory();

    BridgeProcess bridge(dataDir);

    bridge.send(request(1, "tools/list", modernParams()));
    const auto reply = bridge.receive();

    // The host has to get an answer. A stdio server that silently swallows a
    // request leaves the host waiting on it forever, and the user with no
    // indication that MAGDA simply is not running.
    REQUIRE(reply.getDynamicObject() != nullptr);
    REQUIRE(static_cast<int>(reply["id"]) == 1);
    REQUIRE(reply["error"].getDynamicObject() != nullptr);
    REQUIRE(reply["error"]["message"].toString().containsIgnoreCase("MAGDA"));

    bridge.close();
    dataDir.deleteRecursively();
}

#endif  // !JUCE_WINDOWS
