#pragma once

#include <atomic>
#include <memory>

#include "../../../core/SelectionManager.hpp"
#include "../../../project/ProjectManager.hpp"
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
class AIChatConsoleContent : public PanelContent,
                             private juce::Timer,
                             public magda::SelectionManagerListener,
                             public magda::ProjectManagerListener {
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

    // ProjectManagerListener
    void projectOpened(const magda::ProjectInfo& info) override;

    // SelectionManagerListener
    void selectionTypeChanged(magda::SelectionType newType) override;
    void trackSelectionChanged(magda::TrackId trackId) override;
    void clipSelectionChanged(magda::ClipId clipId) override;
    void chainNodeSelectionChanged(const magda::ChainNodePath& path) override;

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
    void updateContextBar();

    // Timer callback for "Thinking..." animation
    void timerCallback() override;

    juce::TextEditor chatHistory_;
    juce::TextEditor inputBox_;

    // Bottom bar: context icon + label + send button
    enum class ContextIcon { None, Track, Clip, Device };
    ContextIcon contextIcon_ = ContextIcon::None;
    std::unique_ptr<juce::Drawable> trackIconDrawable_;
    std::unique_ptr<juce::Drawable> clipIconDrawable_;
    juce::Label contextLabel_;
    juce::DrawableButton sendButton_{"send", juce::DrawableButton::ImageFitted};
    juce::Rectangle<int> bottomBarBounds_;
    juce::Rectangle<int> contextIconBounds_;
    juce::String contextText_;
    bool contextEnabled_ = true;

    void mouseUp(const juce::MouseEvent& event) override;

    std::unique_ptr<magda::DAWAgent> agent_;
    std::unique_ptr<RequestThread> requestThread_;
    std::atomic<bool> shouldStop_{false};
    std::atomic<bool> processing_{false};
    juce::String pendingMessage_;
    int dotCount_{0};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AIChatConsoleContent)
};

}  // namespace magda::daw::ui
