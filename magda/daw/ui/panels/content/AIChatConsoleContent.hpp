#pragma once

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
class AIChatConsoleContent : public PanelContent {
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
    void sendMessage(const juce::String& text);
    void appendToChat(const juce::String& text);

    juce::Label titleLabel_;
    juce::TextEditor chatHistory_;
    juce::TextEditor inputBox_;

    std::unique_ptr<magda::DAWAgent> agent_;
    std::atomic<bool> processing_{false};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AIChatConsoleContent)
};

}  // namespace magda::daw::ui
