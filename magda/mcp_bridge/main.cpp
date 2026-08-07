// magda-mcp — a stdio-to-HTTP bridge for MAGDA's MCP endpoint (#1858).
//
// An MCP host is configured once and expects that configuration to keep working.
// MAGDA's endpoint cannot be written into one directly: the port is ephemeral by
// default and the bearer token is regenerated on every launch and deleted on
// shutdown, so a static `url` + `Authorization` pair goes stale the first time
// the user restarts the DAW.
//
// This resolves that at run time instead. The host's config names one command
// and nothing else; on each request this finds the discovery record of a live
// MAGDA, and speaks Streamable HTTP to whatever port and token it currently
// advertises. Restarting MAGDA changes both, and nothing in the host's config
// has to change.
//
// It also reaches hosts that speak only stdio, which #1858 lists as the
// follow-on this turned out to be.
//
// Deliberately linked against juce_core and cpp-httplib alone. It is a proxy: it
// parses just enough of each message to route it, and never needs a DAW.

#if JUCE_WINDOWS
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
#endif

#include <httplib.h>
#include <juce_core/juce_core.h>

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "sse_parser.hpp"

#if JUCE_WINDOWS
    #include <windows.h>
#else
    #include <signal.h>
    #include <unistd.h>

    #include <cerrno>
#endif

namespace {

constexpr const char* kRecordPattern = "remote-api-*.json";

// JSON-RPC codes this bridge produces on its own behalf. Everything else is
// whatever MAGDA answered, passed through untouched.
constexpr int kInternalError = -32603;

juce::var makeObject() {
    return juce::var(new juce::DynamicObject());
}

void setProperty(juce::var& object, const char* name, const juce::var& value) {
    if (auto* dynamicObject = object.getDynamicObject())
        dynamicObject->setProperty(juce::Identifier(name), value);
}

bool isProcessAlive(juce::int64 processId) {
#if JUCE_WINDOWS
    auto* handle =
        OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(processId));
    if (handle == nullptr)
        return false;
    DWORD exitCode = 0;
    const auto running = GetExitCodeProcess(handle, &exitCode) != 0 && exitCode == STILL_ACTIVE;
    CloseHandle(handle);
    return running;
#else
    // Signal 0 runs the existence and permission checks without delivering
    // anything. EPERM means the process is there and belongs to someone else,
    // which still counts as alive.
    return kill(static_cast<pid_t>(processId), 0) == 0 || errno == EPERM;
#endif
}

juce::String envVar(const char* name) {
    if (const char* value = std::getenv(name); value != nullptr && value[0] != '\0')
        return juce::String::fromUTF8(value);
    return {};
}

/**
 * @brief Where MAGDA keeps its discovery records.
 *
 * Mirrors `magda::paths::dataDir()` rather than linking it: `config.json` itself
 * never moves, but the data directory it names can, so the same three-step
 * resolution has to happen here — environment override, then the configured
 * value, then the OS default.
 */
juce::File dataDir() {
    if (const auto fromEnv = envVar("MAGDA_DATA_DIR"); fromEnv.isNotEmpty())
        return juce::File(fromEnv);

    const auto osDefault = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                               .getChildFile("MAGDA");

    if (const auto config = osDefault.getChildFile("config.json"); config.existsAsFile()) {
        juce::var parsed;
        if (juce::JSON::parse(config.loadFileAsString(), parsed).wasOk()) {
            if (const auto configured = parsed["dataDir"].toString(); configured.isNotEmpty())
                return juce::File(configured);
        }
    }
    return osDefault;
}

/// Where a live MAGDA is listening, and what it will accept.
struct Endpoint {
    std::string origin;  ///< scheme://host:port
    std::string path;    ///< /mcp
    std::string token;
};

/**
 * @brief Find a running MAGDA that has an MCP endpoint open.
 *
 * Records are named after the process that wrote them, because MAGDA supports
 * more than one instance. A record whose process has gone is debris from a
 * crash and is skipped rather than trusted — its port may since have been
 * handed to something else entirely.
 *
 * The most recently written live record wins. That is the least surprising rule
 * when two instances are up: it is the one the user most likely just started.
 */
std::optional<Endpoint> resolveEndpoint() {
    const auto directory = dataDir();
    if (!directory.isDirectory())
        return std::nullopt;

    std::optional<Endpoint> best;
    juce::int64 bestTime = 0;

    for (const auto& record :
         directory.findChildFiles(juce::File::findFiles, false, kRecordPattern)) {
        juce::var parsed;
        if (!juce::JSON::parse(record.loadFileAsString(), parsed).wasOk())
            continue;

        const auto pid = static_cast<juce::int64>(parsed["pid"]);
        if (pid <= 0 || !isProcessAlive(pid))
            continue;

        // Absent when MAGDA came up but its MCP listener did not. The record is
        // still valid for the WebSocket, and is simply not for us.
        const auto url = parsed["mcpUrl"].toString();
        const auto token = parsed["token"].toString();
        if (url.isEmpty() || token.isEmpty())
            continue;

        const auto separator = url.indexOf("://");
        if (separator < 0)
            continue;
        const auto afterScheme = url.indexOf(separator + 3, "/");
        if (afterScheme < 0)
            continue;

        const auto modified = record.getLastModificationTime().toMilliseconds();
        if (best.has_value() && modified <= bestTime)
            continue;

        bestTime = modified;
        best = Endpoint{url.substring(0, afterScheme).toStdString(),
                        url.substring(afterScheme).toStdString(), token.toStdString()};
    }
    return best;
}

/**
 * @brief The bridge itself.
 *
 * One thread reads stdin; each request gets its own thread so a long-lived
 * `subscriptions/listen` stream cannot block the ordinary calls arriving
 * alongside it. Everything written to stdout goes through one mutex, because
 * the stdio transport is a single stream of newline-delimited messages and two
 * threads interleaving inside one would corrupt both.
 */
class Bridge {
  public:
    int run() {
        std::string line;
        while (std::getline(std::cin, line)) {
            if (line.empty())
                continue;

            juce::var message;
            if (juce::JSON::parse(
                    juce::String::fromUTF8(line.data(), static_cast<int>(line.size())), message)
                    .failed() ||
                message.getDynamicObject() == nullptr) {
                // Not addressable — without an id there is nobody to answer, and
                // inventing one would invite a reply to a request that was never
                // made. Report it where the host can see it and move on.
                std::cerr << "magda-mcp: ignoring unparseable message\n";
                continue;
            }

            dispatch(message);
        }

        // stdin closed: the host is shutting us down. Stop every stream so its
        // thread unwinds, then wait for them.
        shutdown();
        return 0;
    }

  private:
    void dispatch(const juce::var& message) {
        const auto method = message["method"].toString();

        // Cancellation is the one message this bridge acts on rather than
        // forwards. On stdio it is how a host closes a `subscriptions/listen`
        // stream; on HTTP the equivalent is closing the response, which is
        // exactly what stopping that request's client does.
        if (method == "notifications/cancelled") {
            cancel(message["params"]["requestId"]);
            return;
        }

        auto finished = std::make_shared<std::atomic<bool>>(false);
        std::lock_guard<std::mutex> lock(threadsMutex_);

        // Reap before adding. A thread handle is a native resource, and a host
        // that stays open for hours sends thousands of requests — without this
        // the vector only ever grows, and nothing is released until stdin
        // closes.
        for (auto it = threads_.begin(); it != threads_.end();) {
            if (it->second->load()) {
                if (it->first.joinable())
                    it->first.join();
                it = threads_.erase(it);
            } else {
                ++it;
            }
        }

        threads_.emplace_back(std::thread([this, message, finished] {
                                  forward(message);
                                  // Last thing it does, so a reaper that sees
                                  // this flag knows the thread is about to be
                                  // joinable rather than mid-request.
                                  finished->store(true);
                              }),
                              finished);
    }

    void forward(const juce::var& message) {
        const auto id = message["id"];
        const auto method = message["method"].toString();
        const auto body = juce::JSON::toString(message, true).toStdString();

        // Kept so a session lost to a MAGDA restart can be rebuilt without
        // involving the host, which sends this exactly once at startup.
        if (method == "initialize")
            rememberHandshake(body);

        // Three attempts, because a MAGDA restart can invalidate three things in
        // sequence and each is only discovered by trying: the cached endpoint
        // (dead port), then the token (401), then the session (404). A budget of
        // two was enough to re-resolve but left nothing to spend on the
        // re-handshake that a legacy host needs afterwards — so the first call
        // after a restart failed, every time.
        for (int attempt = 0; attempt < 3; ++attempt) {
            const auto endpoint = currentEndpoint(attempt > 0);
            if (endpoint && send(*endpoint, id, method, body))
                return;
        }

        // Every path out of the loop that reaches here failed, and the host is
        // still waiting. Falling through silently would leave a request
        // outstanding forever with nothing to say why — the one outcome a stdio
        // server must never produce.
        fail(id, "Could not reach a running MAGDA. Start MAGDA and enable the remote API in its "
                 "settings.");
    }

    /// @return true when the request was answered, false to re-resolve and retry.
    bool send(const Endpoint& endpoint, const juce::var& id, const juce::String& method,
              const std::string& body) {
        auto client = std::make_shared<httplib::Client>(endpoint.origin);
        const auto headers = headersFor(endpoint, method, body);

        // Two shapes, and the difference is not cosmetic. A streaming response
        // has to be parsed as it arrives, because it never completes — so it
        // needs a content receiver, and a content receiver means `result->body`
        // is never populated. An ordinary response is the opposite: it completes,
        // and reading it whole is both simpler and the only way to see its
        // status and content type before deciding what it was.
        //
        // Only `subscriptions/listen` streams. The transport permits any request
        // to be answered with SSE, so the non-streaming path below still handles
        // an SSE body it did not expect — it just gets to do it after the fact.
        if (method == "subscriptions/listen") {
            // A stream never completes on its own, so no read timeout may apply
            // to it. Cancellation and shutdown both stop the client instead,
            // which is prompt regardless.
            client->set_read_timeout(0, 0);
            registerStream(id, client);

            // Per request, not shared. Two concurrent listen streams both feed
            // this callback, and one buffer between them splices a partial frame
            // from one response onto the next — corrupting both.
            auto stream = std::make_shared<magda::mcp::SseParser>();

            auto result = client->Post(
                endpoint.path, headers, body, "application/json",
                [this, stream](const char* data, std::size_t length) {
                    stream->consume(std::string(data, length),
                                    [this](const std::string& message) { emitRaw(message); });
                    return true;
                });
            unregisterStream(id);

            // A stream that ended is not a failure to retry: the host cancelled
            // it, or the server closed it. Retrying would silently reopen a
            // subscription the host believes it closed.
            if (!result)
                return isCancelled(id);
            rememberSession(*result);
            if (result->status == 401)
                return false;

            // The request was refused rather than streamed — a 400 or a 503 with
            // an ordinary JSON-RPC error body. A content receiver is handed the
            // body for *every* status, so that body went into the SSE parser,
            // found no frames, and left `result->body` empty. Returning here as
            // though the request had been answered leaves the host waiting on
            // this id forever, so the raw bytes are emitted instead.
            if (!isEventStream(*result)) {
                const auto raw = stream->raw();
                if (!raw.empty())
                    emitRaw(raw);
                else
                    fail(id, "The server refused the subscription with an empty response");
            }
            return true;
        }

        auto result = client->Post(endpoint.path, headers, body, "application/json");

        if (!result) {
            // Connection refused or reset: MAGDA is gone or was restarted. Worth
            // one re-resolve, which is what returning false asks for.
            return false;
        }

        rememberSession(*result);

        // The token rotated under us — the same situation as a refused
        // connection, reached a different way.
        if (result->status == 401)
            return false;

        // The session died under us, which is what a MAGDA restart looks like to
        // a handshake-era host: the port and token are new, and so is an empty
        // session table.
        //
        // The host cannot fix this. It sent `initialize` once, at startup, and
        // has no reason to think anything has changed — so left alone this
        // fails every subsequent call for the life of the host process. Since
        // hiding a restart is the entire reason this bridge exists, it
        // re-handshakes on the host's behalf and retries.
        if (result->status == httplib::StatusCode::NotFound_404 && !currentSession().empty()) {
            clearSession();
            reestablishSession(endpoint);
            return false;
        }

        // A notification is answered with 202 and no body, and the stdio
        // transport expects nothing written back for it.
        if (result->status == 202 || result->body.empty())
            return true;

        if (isEventStream(*result)) {
            magda::mcp::SseParser complete;
            complete.consume(result->body, [this](const std::string& m) { emitRaw(m); });
            return true;
        }

        emitRaw(result->body);
        return true;
    }

    /// Whether this stream was stopped on purpose, which is the difference
    /// between "the host closed it" and "the connection broke".
    bool isCancelled(const juce::var& id) {
        std::lock_guard<std::mutex> lock(streamMutex_);
        return cancelled_.erase(juce::JSON::toString(id, true).toStdString()) > 0;
    }

    httplib::Headers headersFor(const Endpoint& endpoint, const juce::String& method,
                                const std::string& body) {
        juce::var parsed;
        juce::JSON::parse(juce::String::fromUTF8(body.data(), static_cast<int>(body.size())),
                          parsed);

        httplib::Headers headers;
        headers.emplace("Authorization", "Bearer " + endpoint.token);
        // Required by the transport, and both are required together: a server
        // may answer with either shape.
        headers.emplace("Accept", "application/json, text/event-stream");
        headers.emplace("Mcp-Method", method.toStdString());

        // The mirrored headers a modern request must carry. They are derived
        // from the body rather than invented, which is the only way they can
        // agree with it — a server that finds them disagreeing rejects the
        // request, and rightly.
        const auto params = parsed["params"];
        const auto version = params["_meta"]["io.modelcontextprotocol/protocolVersion"].toString();
        if (version.isNotEmpty())
            headers.emplace("MCP-Protocol-Version", version.toStdString());

        const juce::String name =
            method == "resources/read" ? params["uri"].toString() : params["name"].toString();
        if (name.isNotEmpty() &&
            (method == "tools/call" || method == "resources/read" || method == "prompts/get"))
            headers.emplace("Mcp-Name", encodeHeaderValue(name));

        // Carried across requests for a host that speaks the handshake era. A
        // modern server ignores it, as that revision requires, so sending it
        // unconditionally costs nothing.
        if (const auto session = currentSession(); !session.empty())
            headers.emplace("Mcp-Session-Id", session);

        return headers;
    }

    /**
     * @brief A header value the way the specification says to send it.
     *
     * HTTP field values are visible ASCII. A tool name or resource URI outside
     * that has to travel base64-wrapped in the `=?base64?…?=` sentinel, and a
     * plain value that happens to look like the sentinel has to be wrapped too,
     * or the server could not tell the two apart.
     */
    static std::string encodeHeaderValue(const juce::String& value) {
        const auto utf8 = value.toStdString();
        const auto needsEncoding = value.startsWith("=?base64?") ||
                                   std::any_of(utf8.begin(), utf8.end(), [](unsigned char c) {
                                       return c < 0x21 || c > 0x7E;
                                   });

        if (!needsEncoding)
            return utf8;
        return "=?base64?" + juce::Base64::toBase64(value).toStdString() + "?=";
    }

    static bool isEventStream(const httplib::Response& response) {
        return response.get_header_value("Content-Type").rfind("text/event-stream", 0) == 0;
    }

    /**
     * @brief Write one message to the host.
     *
     * Re-serialized rather than forwarded verbatim, because the stdio transport
     * requires one message per line with no embedded newlines and the server is
     * under no obligation to have formatted it that way.
     */
    void emitRaw(const std::string& json) {
        juce::var parsed;
        const auto text =
            juce::JSON::parse(juce::String::fromUTF8(json.data(), static_cast<int>(json.size())),
                              parsed)
                    .wasOk()
                ? juce::JSON::toString(parsed, true).toStdString()
                : json;

        std::lock_guard<std::mutex> lock(outputMutex_);
        std::cout << text << '\n' << std::flush;
    }

    void fail(const juce::var& id, const juce::String& message) {
        // Nothing to answer: a notification that could not be delivered has no
        // reply slot, so say it where a human debugging this will see it.
        if (id.isVoid() || id.isUndefined()) {
            std::cerr << "magda-mcp: " << message << '\n';
            return;
        }

        auto error = makeObject();
        setProperty(error, "code", kInternalError);
        setProperty(error, "message", message);

        auto reply = makeObject();
        setProperty(reply, "jsonrpc", "2.0");
        setProperty(reply, "id", id);
        setProperty(reply, "error", error);

        std::lock_guard<std::mutex> lock(outputMutex_);
        std::cout << juce::JSON::toString(reply, true).toStdString() << '\n' << std::flush;
    }

    // -----------------------------------------------------------------------
    // Cached state
    // -----------------------------------------------------------------------

    std::optional<Endpoint> currentEndpoint(bool forceRefresh) {
        std::lock_guard<std::mutex> lock(endpointMutex_);
        if (forceRefresh || !endpoint_)
            endpoint_ = resolveEndpoint();
        return endpoint_;
    }

    void rememberSession(const httplib::Response& response) {
        if (!response.has_header("Mcp-Session-Id"))
            return;
        std::lock_guard<std::mutex> lock(sessionMutex_);
        session_ = response.get_header_value("Mcp-Session-Id");
    }

    std::string currentSession() const {
        std::lock_guard<std::mutex> lock(sessionMutex_);
        return session_;
    }

    void clearSession() {
        std::lock_guard<std::mutex> lock(sessionMutex_);
        session_.clear();
    }

    /// The `initialize` the host sent, kept so a session can be rebuilt without
    /// it. Empty when the host speaks the modern era and never sent one.
    void rememberHandshake(const std::string& body) {
        std::lock_guard<std::mutex> lock(sessionMutex_);
        handshake_ = body;
    }

    std::string currentHandshake() const {
        std::lock_guard<std::mutex> lock(sessionMutex_);
        return handshake_;
    }

    /**
     * @brief Replay the host's `initialize` to obtain a fresh session.
     *
     * The reply is deliberately discarded: the host was answered the first time
     * and is not expecting a second. Only the `Mcp-Session-Id` header matters,
     * and `rememberSession` takes it.
     */
    void reestablishSession(const Endpoint& endpoint) {
        const auto handshake = currentHandshake();
        if (handshake.empty())
            return;

        httplib::Client client(endpoint.origin);
        auto result = client.Post(endpoint.path, headersFor(endpoint, "initialize", handshake),
                                  handshake, "application/json");
        if (result)
            rememberSession(*result);
    }

    void registerStream(const juce::var& id, std::shared_ptr<httplib::Client> client) {
        std::lock_guard<std::mutex> lock(streamMutex_);
        streams_[juce::JSON::toString(id, true).toStdString()] = std::move(client);
    }

    void unregisterStream(const juce::var& id) {
        std::lock_guard<std::mutex> lock(streamMutex_);
        streams_.erase(juce::JSON::toString(id, true).toStdString());
    }

    void cancel(const juce::var& requestId) {
        std::shared_ptr<httplib::Client> client;
        {
            std::lock_guard<std::mutex> lock(streamMutex_);
            const auto key = juce::JSON::toString(requestId, true).toStdString();
            const auto it = streams_.find(key);
            if (it == streams_.end())
                return;
            client = it->second;
            // Recorded before stopping, so the thread that wakes up can tell
            // this apart from the connection having broken under it.
            cancelled_.insert(key);
        }
        // Aborts the blocked POST; its thread then unwinds and deregisters.
        client->stop();
    }

    void shutdown() {
        {
            std::lock_guard<std::mutex> lock(streamMutex_);
            for (auto& [id, client] : streams_) {
                cancelled_.insert(id);
                client->stop();
            }
        }
        std::lock_guard<std::mutex> lock(threadsMutex_);
        for (auto& [thread, finished] : threads_)
            if (thread.joinable())
                thread.join();
        threads_.clear();
    }

    mutable std::mutex endpointMutex_;
    std::optional<Endpoint> endpoint_;

    mutable std::mutex sessionMutex_;
    std::string session_;
    std::string handshake_;

    std::mutex streamMutex_;
    std::map<std::string, std::shared_ptr<httplib::Client>> streams_;
    std::set<std::string> cancelled_;

    std::mutex threadsMutex_;
    /// Each worker with the flag it sets on the way out, so finished ones can
    /// be joined and dropped rather than accumulating for the process's life.
    std::vector<std::pair<std::thread, std::shared_ptr<std::atomic<bool>>>> threads_;

    std::mutex outputMutex_;
};

}  // namespace

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        const juce::String argument(argv[i]);
        if (argument == "--help" || argument == "-h") {
            std::cout
                // Written straight to std::cout, so the bytes reach the
                // terminal as authored, never through juce::String. utf8-ok
                << "magda-mcp — bridges an MCP host's stdio transport to the MAGDA MCP "
                   "endpoint.\n\n"
                   "Takes no options. It finds a running MAGDA through the discovery record it\n"
                   "writes on startup, so the port and token it uses are whatever that instance\n"
                   "currently advertises, and restarting MAGDA needs no reconfiguration here.\n\n"
                   "MAGDA must be running with the remote API enabled.\n\n"
                   "Register it with a host, for example:\n"
                   "  claude mcp add magda -- magda-mcp\n\n"
                   "Environment:\n"
                   "  MAGDA_DATA_DIR   where to look for discovery records, overriding config\n";
            return 0;
        }
        std::cerr << "magda-mcp: unrecognised argument '" << argument << "'; try --help\n";
        return 2;
    }

    // The stdio transport is a byte protocol on stdout. Anything JUCE or a
    // library writes there would land inside a JSON-RPC message; diagnostics go
    // to stderr, which the host is explicitly told may carry them.
    Bridge bridge;
    return bridge.run();
}
