#include "MCPClient.hpp"

#if JUCE_WINDOWS
#include <windows.h>
#else
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#endif

namespace magda {

MCPClient::MCPClient(const juce::String& command, const juce::StringArray& args)
    : command_(command), args_(args) {}

MCPClient::~MCPClient() {
    stop();
}

#if !JUCE_WINDOWS

bool MCPClient::start() {
    if (initialized_)
        return true;

    int toChild[2];
    int fromChild[2];

    if (pipe(toChild) != 0 || pipe(fromChild) != 0) {
        DBG("MCPClient: pipe() failed");
        return false;
    }

    pid_t pid = fork();
    if (pid < 0) {
        DBG("MCPClient: fork() failed");
        close(toChild[0]);
        close(toChild[1]);
        close(fromChild[0]);
        close(fromChild[1]);
        return false;
    }

    if (pid == 0) {
        // Child process
        close(toChild[1]);
        close(fromChild[0]);

        dup2(toChild[0], STDIN_FILENO);
        dup2(fromChild[1], STDOUT_FILENO);

        close(toChild[0]);
        close(fromChild[1]);

        // Redirect stderr to /dev/null so it doesn't pollute stdout
        int devNull = open("/dev/null", O_WRONLY);
        if (devNull >= 0) {
            dup2(devNull, STDERR_FILENO);
            close(devNull);
        }

        std::vector<std::string> argStrings;
        argStrings.push_back(command_.toStdString());
        for (const auto& a : args_)
            argStrings.push_back(a.toStdString());

        std::vector<char*> argv;
        for (auto& s : argStrings)
            argv.push_back(s.data());
        argv.push_back(nullptr);

        execvp(argv[0], argv.data());
        _exit(127);
    }

    // Parent process
    close(toChild[0]);
    close(fromChild[1]);

    stdinWrite_ = toChild[1];
    stdoutRead_ = fromChild[0];
    childPid_ = pid;

    // MCP initialize handshake
    auto* initParams = new juce::DynamicObject();
    initParams->setProperty("protocolVersion", "2024-11-05");
    initParams->setProperty("capabilities", juce::var(new juce::DynamicObject()));

    auto* clientInfo = new juce::DynamicObject();
    clientInfo->setProperty("name", "MAGDA");
    clientInfo->setProperty("version", "0.8.0");
    initParams->setProperty("clientInfo", juce::var(clientInfo));

    auto response = sendRpc("initialize", juce::var(initParams));
    if (response.isEmpty()) {
        DBG("MCPClient: initialize handshake failed");
        stop();
        return false;
    }

    // Send initialized notification
    auto* notifObj = new juce::DynamicObject();
    notifObj->setProperty("jsonrpc", "2.0");
    notifObj->setProperty("method", "notifications/initialized");
    auto notifJson = juce::JSON::toString(juce::var(notifObj), true);
    writeToStdin(notifJson + "\n");

    initialized_ = true;
    DBG("MCPClient: connected to " + command_);
    return true;
}

void MCPClient::stop() {
    if (childPid_ > 0) {
        if (stdinWrite_ >= 0) {
            close(stdinWrite_);
            stdinWrite_ = -1;
        }

        int status;
        if (waitpid(childPid_, &status, WNOHANG) == 0) {
            kill(childPid_, SIGTERM);
            waitpid(childPid_, &status, 0);
        }

        childPid_ = -1;
    }

    if (stdoutRead_ >= 0) {
        close(stdoutRead_);
        stdoutRead_ = -1;
    }

    initialized_ = false;
    readBuffer_.clear();
}

bool MCPClient::isRunning() const {
    if (childPid_ <= 0)
        return false;
    int status;
    return waitpid(childPid_, &status, WNOHANG) == 0;
}

bool MCPClient::writeToStdin(const juce::String& data) {
    if (stdinWrite_ < 0)
        return false;
    auto utf8 = data.toStdString();
    auto bytesWritten = write(stdinWrite_, utf8.data(), utf8.size());
    return bytesWritten == static_cast<ssize_t>(utf8.size());
}

juce::String MCPClient::readLine(int timeoutMs) {
    auto deadline =
        juce::Time::getMillisecondCounter() + static_cast<juce::uint32>(timeoutMs);

    while (juce::Time::getMillisecondCounter() < deadline) {
        auto newlineIdx = readBuffer_.indexOf("\n");
        if (newlineIdx >= 0) {
            auto line = readBuffer_.substring(0, newlineIdx).trim();
            readBuffer_ = readBuffer_.substring(newlineIdx + 1);
            if (line.isNotEmpty())
                return line;
            continue;
        }

        if (stdoutRead_ < 0)
            return {};

        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(stdoutRead_, &fds);

        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 50000;  // 50ms poll

        int ready = select(stdoutRead_ + 1, &fds, nullptr, nullptr, &tv);
        if (ready > 0) {
            char buf[4096];
            auto n = read(stdoutRead_, buf, sizeof(buf));
            if (n > 0)
                readBuffer_ += juce::String::fromUTF8(buf, static_cast<int>(n));
            else if (n == 0)
                return {};  // EOF
        }
    }

    DBG("MCPClient: read timeout");
    return {};
}

#else  // JUCE_WINDOWS

bool MCPClient::start() {
    // TODO: Windows CreateProcess with pipes
    DBG("MCPClient: Windows not yet implemented");
    return false;
}

void MCPClient::stop() {
    initialized_ = false;
}

bool MCPClient::isRunning() const {
    return false;
}

bool MCPClient::writeToStdin(const juce::String&) {
    return false;
}

juce::String MCPClient::readLine(int) {
    return {};
}

#endif

MCPClient::ToolResult MCPClient::callTool(const juce::String& toolName,
                                          const juce::var& arguments) {
    ToolResult result;

    if (!initialized_) {
        result.error = "MCP client not initialized";
        return result;
    }

    auto* params = new juce::DynamicObject();
    params->setProperty("name", toolName);
    params->setProperty("arguments", arguments);

    auto responseJson = sendRpc("tools/call", juce::var(params));
    if (responseJson.isEmpty()) {
        result.error = "No response from MCP server";
        return result;
    }

    auto parsed = juce::JSON::parse(responseJson);
    auto* responseObj = parsed.getDynamicObject();
    if (responseObj == nullptr) {
        result.error = "Invalid JSON response";
        return result;
    }

    if (responseObj->hasProperty("error")) {
        auto errorVar = responseObj->getProperty("error");
        if (auto* errObj = errorVar.getDynamicObject())
            result.error = errObj->getProperty("message").toString();
        else
            result.error = errorVar.toString();
        return result;
    }

    auto resultVar = responseObj->getProperty("result");
    auto* resultObj = resultVar.getDynamicObject();
    if (resultObj == nullptr) {
        result.error = "Missing result in response";
        return result;
    }

    bool isError = resultObj->getProperty("isError");
    auto contentArray = resultObj->getProperty("content");

    juce::String combinedText;
    if (auto* arr = contentArray.getArray()) {
        for (const auto& item : *arr) {
            if (auto* itemObj = item.getDynamicObject()) {
                if (itemObj->getProperty("type").toString() == "text")
                    combinedText += itemObj->getProperty("text").toString();
            }
        }
    }

    if (isError) {
        result.error = combinedText;
    } else {
        result.success = true;
        result.content = combinedText;
    }
    return result;
}

juce::String MCPClient::sendRpc(const juce::String& method, const juce::var& params) {
    int id = nextId_++;

    auto* request = new juce::DynamicObject();
    request->setProperty("jsonrpc", "2.0");
    request->setProperty("id", id);
    request->setProperty("method", method);
    request->setProperty("params", params);

    auto json = juce::JSON::toString(juce::var(request), true);

    if (!writeToStdin(json + "\n"))
        return {};

    auto line = readLine();
    if (line.isEmpty())
        return {};

    // Wrap back as full JSON for the caller to parse
    return line;
}

}  // namespace magda
