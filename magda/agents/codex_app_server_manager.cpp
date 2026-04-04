#include "codex_app_server_manager.hpp"

#include "../../third_party/llama.cpp/vendor/cpp-httplib/httplib.h"
#include "version.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>

namespace magda {

namespace {

constexpr int kConnectTimeoutMs = 12000;
constexpr int kReadTimeoutSec = 300;

juce::var parseJsonOrVoid(const std::string& text) {
    juce::var parsed;
    if (juce::JSON::parse(juce::String::fromUTF8(text.c_str()), parsed).failed())
        return {};
    return parsed;
}

juce::String jsonToString(const juce::var& value) {
    return juce::JSON::toString(value, true);
}

juce::DynamicObject* asObject(const juce::var& value) {
    return value.getDynamicObject();
}

juce::String getStringProp(const juce::var& value, const juce::Identifier& key) {
    if (auto* obj = asObject(value))
        return obj->getProperty(key).toString();
    return {};
}

juce::var makeTextInput(const juce::String& text) {
    auto* obj = new juce::DynamicObject();
    obj->setProperty("type", "text");
    obj->setProperty("text", text);
    return juce::var(obj);
}

bool isTerminalTurnStatus(const juce::String& status) {
    return status == "completed" || status == "failed" || status == "cancelled";
}

juce::String normaliseWsUrl(juce::String wsUrl) {
    auto trimmed = wsUrl.trim();
    if (trimmed.isEmpty())
        trimmed = CodexAppServerManager::getDefaultWsUrl();

    auto schemePos = trimmed.indexOf("://");
    if (schemePos >= 0) {
        auto pathStart = trimmed.indexOf(schemePos + 3, "/");
        if (pathStart < 0)
            trimmed << "/";
    }

    return trimmed;
}

}  // namespace

struct CodexAppServerManager::Impl {
    struct PendingTurn {
        juce::String threadId;
        juce::String turnId;
        juce::String text;
        juce::String error;
        bool completed = false;
        bool success = false;
    };

    struct LoginState {
        juce::String loginId;
        bool finished = false;
        bool success = false;
        juce::String error;
    };

    std::unique_ptr<juce::ChildProcess> process;
    std::unique_ptr<httplib::ws::WebSocketClient> ws;
    juce::String wsUrl;
    int nextId = 1;
    bool initialized = false;
    LoginState loginState;

    bool ensureConnected(const juce::String& targetUrl, juce::String& error) {
        auto normalized = normaliseWsUrl(targetUrl);

        if (ws && ws->is_open() && wsUrl == normalized)
            return true;

        disconnect();

        if (!connectSocket(normalized, error)) {
            if (!startServer(normalized, error))
                return false;
            if (!connectSocket(normalized, error))
                return false;
        }

        wsUrl = normalized;
        return initialize(error);
    }

    void disconnect() {
        initialized = false;
        loginState = {};

        if (ws) {
            if (ws->is_open())
                ws->close();
            ws.reset();
        }

        if (process) {
            process->kill();
            process.reset();
        }

        wsUrl.clear();
    }

    bool connectSocket(const juce::String& targetUrl, juce::String& error) {
        auto client = std::make_unique<httplib::ws::WebSocketClient>(targetUrl.toStdString());
        client->set_connection_timeout(5, 0);
        client->set_read_timeout(kReadTimeoutSec, 0);
        client->set_write_timeout(10, 0);

        if (!client->is_valid() || !client->connect()) {
            error = "Failed to connect to Codex App Server at " + targetUrl;
            return false;
        }

        ws = std::move(client);
        error.clear();
        return true;
    }

    bool startServer(const juce::String& targetUrl, juce::String& error) {
        process = std::make_unique<juce::ChildProcess>();

       #if JUCE_WINDOWS
        juce::StringArray args{"cmd.exe", "/c", "codex", "app-server", "--listen", targetUrl};
       #else
        juce::StringArray args{"codex", "app-server", "--listen", targetUrl};
       #endif

        if (!process->start(args)) {
            error = "Failed to launch local Codex App Server. Ensure `codex` is installed.";
            process.reset();
            return false;
        }

        auto deadline = juce::Time::getMillisecondCounter() + kConnectTimeoutMs;
        while (juce::Time::getMillisecondCounter() < deadline) {
            juce::String connectError;
            if (connectSocket(targetUrl, connectError))
                return true;

            juce::Thread::sleep(150);
        }

        error = "Timed out waiting for local Codex App Server to start.";
        return false;
    }

    bool initialize(juce::String& error) {
        if (initialized)
            return true;

        auto* params = new juce::DynamicObject();
        auto* clientInfo = new juce::DynamicObject();
        clientInfo->setProperty("name", "magda");
        clientInfo->setProperty("version", MAGDA_VERSION);
        params->setProperty("clientInfo", juce::var(clientInfo));

        auto* capabilities = new juce::DynamicObject();
        capabilities->setProperty("experimentalApi", false);
        params->setProperty("capabilities", juce::var(capabilities));

        juce::var result;
        if (!sendRequest("initialize", juce::var(params), result, error))
            return false;

        auto* initObj = new juce::DynamicObject();
        initObj->setProperty("method", "initialized");
        initObj->setProperty("params", juce::var(new juce::DynamicObject()));
        if (!sendNotification(juce::var(initObj), error))
            return false;

        initialized = true;
        return true;
    }

    bool sendNotification(const juce::var& message, juce::String& error) {
        if (!ws || !ws->is_open()) {
            error = "Codex App Server socket is not open.";
            return false;
        }

        auto json = juce::JSON::toString(message, false).toStdString();
        if (!ws->send(json)) {
            error = "Failed to send Codex App Server notification.";
            return false;
        }

        return true;
    }

    bool sendRequest(const juce::String& method,
                     const juce::var& params,
                     juce::var& result,
                     juce::String& error,
                     PendingTurn* pendingTurn = nullptr,
                     int timeoutMs = kConnectTimeoutMs) {
        if (!ws || !ws->is_open()) {
            error = "Codex App Server socket is not open.";
            return false;
        }

        const int id = nextId++;
        auto* request = new juce::DynamicObject();
        request->setProperty("id", id);
        request->setProperty("method", method);
        request->setProperty("params", params);

        auto json = juce::JSON::toString(juce::var(request), false).toStdString();
        if (!ws->send(json)) {
            error = "Failed to send Codex App Server request.";
            return false;
        }

        auto deadline = juce::Time::getMillisecondCounter() + timeoutMs;
        while (juce::Time::getMillisecondCounter() < deadline) {
            juce::var msg;
            if (!readMessage(msg, error, pendingTurn))
                return false;

            if (auto* obj = asObject(msg)) {
                if (obj->hasProperty("id")) {
                    auto responseId = static_cast<int>(obj->getProperty("id"));
                    if (responseId != id)
                        continue;

                    if (obj->hasProperty("error")) {
                        auto errValue = obj->getProperty("error");
                        if (auto* errObj = asObject(errValue))
                            error = errObj->getProperty("message").toString();
                        if (error.isEmpty())
                            error = jsonToString(errValue);
                        return false;
                    }

                    result = obj->getProperty("result");
                    return true;
                }
            }
        }

        error = "Timed out waiting for Codex App Server response to " + method;
        return false;
    }

    bool readMessage(juce::var& msg, juce::String& error, PendingTurn* pendingTurn) {
        std::string payload;
        auto readResult = ws->read(payload);
        if (readResult == httplib::ws::ReadResult::Fail) {
            error = "Codex App Server connection closed unexpectedly.";
            return false;
        }

        msg = parseJsonOrVoid(payload);
        if (msg.isVoid()) {
            error = "Received invalid JSON from Codex App Server.";
            return false;
        }

        handleNotification(msg, pendingTurn);
        return true;
    }

    void handleNotification(const juce::var& msg, PendingTurn* pendingTurn) {
        auto method = getStringProp(msg, "method");
        if (method.isEmpty())
            return;

        auto params = asObject(msg)->getProperty("params");

        if (method == "account/login/completed") {
            loginState.finished = true;
            loginState.success = static_cast<bool>(asObject(params)->getProperty("success"));
            loginState.error = getStringProp(params, "error");
            if (loginState.error.isEmpty() && !loginState.success)
                loginState.error = "ChatGPT login failed.";
            return;
        }

        if (pendingTurn == nullptr)
            return;

        if (method == "item/agentMessage/delta") {
            if (getStringProp(params, "turnId") == pendingTurn->turnId) {
                auto delta = getStringProp(params, "delta");
                pendingTurn->text += delta;
            }
            return;
        }

        if (method == "error") {
            if (getStringProp(params, "turnId") == pendingTurn->turnId) {
                pendingTurn->error = getStringProp(asObject(params)->getProperty("error"), "message");
                pendingTurn->completed = true;
                pendingTurn->success = false;
            }
            return;
        }

        if (method == "turn/completed") {
            if (getStringProp(params, "threadId") == pendingTurn->threadId) {
                auto turn = asObject(params)->getProperty("turn");
                if (getStringProp(turn, "id") == pendingTurn->turnId) {
                    auto status = getStringProp(turn, "status");
                    pendingTurn->success = (status == "completed");
                    if (!pendingTurn->success && pendingTurn->error.isEmpty()) {
                        auto turnError = asObject(turn)->getProperty("error");
                        pendingTurn->error = getStringProp(turnError, "message");
                        if (pendingTurn->error.isEmpty())
                            pendingTurn->error = "Codex turn failed with status " + status;
                    }
                    pendingTurn->completed = true;
                }
            }
        }
    }

    AccountStatus getAccountStatus(const juce::String& targetUrl, bool refreshToken) {
        AccountStatus status;
        juce::String error;
        if (!ensureConnected(targetUrl, error)) {
            status.error = error;
            return status;
        }

        auto* params = new juce::DynamicObject();
        params->setProperty("refreshToken", refreshToken);

        juce::var result;
        if (!sendRequest("account/read", juce::var(params), result, error)) {
            status.connected = true;
            status.error = error;
            return status;
        }

        status.connected = true;
        auto* obj = asObject(result);
        status.requiresOpenaiAuth = static_cast<bool>(obj->getProperty("requiresOpenaiAuth"));
        auto account = obj->getProperty("account");
        if (auto* accountObj = asObject(account)) {
            status.loggedIn = true;
            status.email = accountObj->getProperty("email").toString();
            status.planType = accountObj->getProperty("planType").toString();
        }
        return status;
    }

    LoginResult loginWithChatGPT(const juce::String& targetUrl, int timeoutMs) {
        LoginResult out;
        juce::String error;
        if (!ensureConnected(targetUrl, error)) {
            out.error = error;
            return out;
        }

        auto account = getAccountStatus(targetUrl, true);
        if (account.loggedIn) {
            out.success = true;
            return out;
        }

        loginState = {};
        auto* params = new juce::DynamicObject();
        params->setProperty("type", "chatgpt");

        juce::var result;
        if (!sendRequest("account/login/start", juce::var(params), result, error)) {
            out.error = error;
            return out;
        }

        auto* obj = asObject(result);
        loginState.loginId = obj->getProperty("loginId").toString();
        out.authUrl = obj->getProperty("authUrl").toString();

        if (out.authUrl.isEmpty()) {
            out.error = "Codex App Server returned no login URL.";
            return out;
        }

        juce::URL(out.authUrl).launchInDefaultBrowser();

        auto deadline = juce::Time::getMillisecondCounter() + timeoutMs;
        while (juce::Time::getMillisecondCounter() < deadline) {
            juce::var msg;
            if (!readMessage(msg, error, nullptr)) {
                out.error = error;
                return out;
            }

            if (loginState.finished) {
                out.success = loginState.success;
                out.error = loginState.error;
                return out;
            }
        }

        out.error = "Timed out waiting for ChatGPT login to complete.";
        return out;
    }

    llm::Response runPrompt(const juce::String& targetUrl,
                            const juce::String& requestedModel,
                            const llm::Request& request,
                            llm::StreamCallback onToken) {
        llm::Response response;
        auto started = juce::Time::getMillisecondCounterHiRes();

        auto account = getAccountStatus(targetUrl, true);
        if (!account.connected) {
            response.error = account.error;
            return response;
        }
        if (!account.loggedIn) {
            response.error = account.error.isNotEmpty() ? account.error
                                                        : "Codex App Server is not logged in.";
            return response;
        }

        juce::String error;

        auto* threadParams = new juce::DynamicObject();
        threadParams->setProperty("ephemeral", true);
        threadParams->setProperty("approvalPolicy", "never");
        threadParams->setProperty("sandbox", "read-only");
        threadParams->setProperty("personality", "pragmatic");
        threadParams->setProperty("model", requestedModel.isEmpty() ? getDefaultModel() : requestedModel);
        if (request.systemPrompt.isNotEmpty())
            threadParams->setProperty("developerInstructions", request.systemPrompt);

        juce::var threadResult;
        if (!sendRequest("thread/start", juce::var(threadParams), threadResult, error)) {
            response.error = error;
            return response;
        }

        auto thread = asObject(threadResult)->getProperty("thread");
        auto threadId = getStringProp(thread, "id");
        if (threadId.isEmpty()) {
            response.error = "Codex App Server did not return a thread id.";
            return response;
        }

        auto* turnParams = new juce::DynamicObject();
        turnParams->setProperty("threadId", threadId);
        turnParams->setProperty("model", requestedModel.isEmpty() ? getDefaultModel() : requestedModel);

        juce::Array<juce::var> input;
        input.add(makeTextInput(request.userMessage));
        turnParams->setProperty("input", input);

        juce::var turnResult;
        if (!sendRequest("turn/start", juce::var(turnParams), turnResult, error)) {
            response.error = error;
            return response;
        }

        PendingTurn pending;
        pending.threadId = threadId;
        pending.turnId = getStringProp(asObject(turnResult)->getProperty("turn"), "id");
        if (pending.turnId.isEmpty()) {
            response.error = "Codex App Server did not return a turn id.";
            return response;
        }

        while (!pending.completed) {
            juce::var msg;
            if (!readMessage(msg, error, &pending)) {
                response.error = error;
                return response;
            }

            if (onToken && pending.text.isNotEmpty()) {
                auto chunk = pending.text;
                pending.text.clear();
                if (!onToken(chunk)) {
                    response.error = "Cancelled";
                    return response;
                }
                response.text += chunk;
            }
        }

        if (!pending.success) {
            response.error = pending.error;
            if (response.error.isEmpty())
                response.error = "Codex App Server turn failed.";
            return response;
        }

        if (!pending.text.isEmpty()) {
            if (onToken) {
                if (!onToken(pending.text)) {
                    response.error = "Cancelled";
                    return response;
                }
            }
            response.text += pending.text;
        }

        response.success = true;
        response.wallSeconds = (juce::Time::getMillisecondCounterHiRes() - started) / 1000.0;
        return response;
    }
};

CodexAppServerManager::~CodexAppServerManager() = default;

CodexAppServerManager& CodexAppServerManager::getInstance() {
    static CodexAppServerManager instance;
    return instance;
}

juce::String CodexAppServerManager::getDefaultWsUrl() {
    return "ws://127.0.0.1:8765/";
}

juce::String CodexAppServerManager::getDefaultModel() {
    return "gpt-5-codex";
}

CodexAppServerManager::AccountStatus CodexAppServerManager::getAccountStatus(const juce::String& wsUrl,
                                                                             bool refreshToken) {
    std::scoped_lock lock(mutex_);
    if (!impl_)
        impl_ = std::make_unique<Impl>();
    return impl_->getAccountStatus(wsUrl, refreshToken);
}

CodexAppServerManager::LoginResult CodexAppServerManager::loginWithChatGPT(const juce::String& wsUrl,
                                                                           int timeoutMs) {
    std::scoped_lock lock(mutex_);
    if (!impl_)
        impl_ = std::make_unique<Impl>();
    return impl_->loginWithChatGPT(wsUrl, timeoutMs);
}

llm::Response CodexAppServerManager::runPrompt(const juce::String& wsUrl,
                                               const juce::String& model,
                                               const llm::Request& request,
                                               llm::StreamCallback onToken) {
    std::scoped_lock lock(mutex_);
    if (!impl_)
        impl_ = std::make_unique<Impl>();
    return impl_->runPrompt(wsUrl, model, request, std::move(onToken));
}

}  // namespace magda
