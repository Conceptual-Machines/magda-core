#pragma once

#include <juce_core/juce_core.h>

#include <string>

namespace magda {

class MCPClient {
  public:
    MCPClient(const juce::String& command, const juce::StringArray& args);
    ~MCPClient();

    MCPClient(const MCPClient&) = delete;
    MCPClient& operator=(const MCPClient&) = delete;

    bool start();
    void stop();
    bool isRunning() const;

    struct ToolResult {
        bool success = false;
        juce::String content;
        juce::String error;
    };

    ToolResult callTool(const juce::String& toolName, const juce::var& arguments);

  private:
    bool writeToStdin(const juce::String& data);
    juce::String readLine(int timeoutMs = 30000);
    juce::String sendRpc(const juce::String& method, const juce::var& params);

    juce::String command_;
    juce::StringArray args_;
    int nextId_ = 0;
    bool initialized_ = false;

#if JUCE_WINDOWS
    void* processHandle_ = nullptr;
    void* stdinWrite_ = nullptr;
    void* stdoutRead_ = nullptr;
#else
    pid_t childPid_ = -1;
    int stdinWrite_ = -1;
    int stdoutRead_ = -1;
#endif

    juce::String readBuffer_;
};

}  // namespace magda
