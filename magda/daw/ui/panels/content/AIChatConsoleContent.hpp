#pragma once

#include <atomic>
#include <memory>

#include "PanelContent.hpp"

namespace magda {
class DAWAgent;
}

namespace magda::daw::ui {

/**
 * @brief AI Chat console panel content
 *
 * Chat interface for interacting with AI assistant.
 * Sends user messages to DAWAgent on a background thread.
 */
class AIChatConsoleContent : public PanelContent, private juce::Timer {
  public:
    AIChatConsoleContent();
    ~AIChatConsoleContent() override;

    PanelContentType getContentType() const override {
        return PanelContentType::AIChatConsole;
    }

    PanelContentInfo getContentInfo() const override {
        return {PanelContentType::AIChatConsole, "AI Chat", "AI assistant chat", "AIChat"};
    }

    void paint(juce::Graphics& g) override;
    void resized() override;

    void onActivated() override;
    void onDeactivated() override;

  private:
    // Background thread for AI requests
    class RequestThread : public juce::Thread {
      public:
        RequestThread(AIChatConsoleContent& owner);
        void run() override;

      private:
        AIChatConsoleContent& owner_;
    };

    void sendMessage(const juce::String& text);
    void appendToChat(const juce::String& text);

    // Timer callback for "Thinking..." animation
    void timerCallback() override;

    juce::Label titleLabel_;
    juce::TextEditor chatHistory_;
    juce::TextEditor inputBox_;

    std::unique_ptr<magda::DAWAgent> agent_;
    std::unique_ptr<RequestThread> requestThread_;
    std::atomic<bool> shouldStop_{false};
    std::atomic<bool> processing_{false};
    juce::String pendingMessage_;
    int dotCount_{0};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AIChatConsoleContent)
};

}  // namespace magda::daw::ui
