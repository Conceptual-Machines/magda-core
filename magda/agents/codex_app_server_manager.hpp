#pragma once

#include <juce_core/juce_core.h>
#include <juce_llm/juce_llm.h>

#include <memory>
#include <mutex>

namespace magda {

class CodexAppServerManager {
  public:
    struct AccountStatus {
        bool connected = false;
        bool loggedIn = false;
        bool requiresOpenaiAuth = false;
        juce::String email;
        juce::String planType;
        juce::String error;
    };

    struct LoginResult {
        bool success = false;
        juce::String authUrl;
        juce::String error;
    };

    static CodexAppServerManager& getInstance();

    static juce::String getDefaultWsUrl();
    static juce::String getDefaultModel();

    AccountStatus getAccountStatus(const juce::String& wsUrl, bool refreshToken);
    LoginResult loginWithChatGPT(const juce::String& wsUrl, int timeoutMs);
    llm::Response runPrompt(const juce::String& wsUrl,
                            const juce::String& model,
                            const llm::Request& request,
                            llm::StreamCallback onToken = {}) ;

  private:
    CodexAppServerManager() = default;
    ~CodexAppServerManager();

    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::mutex mutex_;
};

}  // namespace magda
