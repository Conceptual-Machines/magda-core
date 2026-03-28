#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

#include <atomic>
#include <memory>
#include <vector>

#include "../../../../agents/llama_model_manager.hpp"
#include "../../../core/Config.hpp"
#include "../../../core/SelectionManager.hpp"
#include "../../../project/ProjectManager.hpp"
#include "DSLTokeniser.hpp"
#include "PanelContent.hpp"

namespace magda {
class CommandAgent;
class DAWAgent;
class MusicAgent;
class RouterAgent;
class SvgButton;
}  // namespace magda

namespace magda::daw::ui {

/**
 * @brief AI Chat console panel content
 *
 * Chat interface for interacting with AI assistant.
 * Sends user messages to DAWAgent on a background thread.
 */
class AIChatConsoleContent : public PanelContent,
                             private juce::Timer,
                             private juce::KeyListener,
                             public magda::SelectionManagerListener,
                             public magda::ProjectManagerListener,
                             public magda::ConfigListener {
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

    // ConfigListener
    void configChanged() override;

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
    void cancelRequest();
    void restoreSendIcon();
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
    juce::DrawableButton clearButton_{"clear", juce::DrawableButton::ImageFitted};
    juce::DrawableButton copyButton_{"copy", juce::DrawableButton::ImageFitted};
    juce::Rectangle<int> bottomBarBounds_;
    juce::Rectangle<int> contextIconBounds_;
    juce::String contextText_;
    bool contextEnabled_ = true;

    void mouseUp(const juce::MouseEvent& event) override;

    // KeyListener — intercept arrow keys for autocomplete navigation
    using juce::Component::keyPressed;  // unhide 1-param overload
    bool keyPressed(const juce::KeyPress& key, juce::Component* originatingComponent) override;

    std::unique_ptr<magda::DAWAgent> agent_;  // kept for legacy DSL REPL
    std::unique_ptr<magda::RouterAgent> routerAgent_;
    std::unique_ptr<magda::CommandAgent> commandAgent_;
    std::unique_ptr<magda::MusicAgent> musicAgent_;
    std::unique_ptr<RequestThread> requestThread_;
    std::atomic<bool> shouldStop_{false};
    std::atomic<bool> processing_{false};
    juce::String pendingMessage_;
    int dotCount_{0};

    // Config status bar
    juce::Label configStatusLabel_;
    std::unique_ptr<magda::SvgButton> serverToggleButton_;
    void updateConfigStatus();
    bool isLocalPreset() const;

    // Plugin alias autocomplete
    struct AliasEntry {
        juce::String alias;       // e.g. "serum_2"
        juce::String pluginName;  // e.g. "Serum 2"
    };

    class AutocompletePopup;
    std::unique_ptr<AutocompletePopup> autocompletePopup_;
    std::vector<AliasEntry> allAliases_;

    void buildAliasList();
    juce::String resolveAliases(const juce::String& text);
    juce::String rewriteSlashCommand(const juce::String& text);

    // Slash command definitions
    struct SlashCommand {
        juce::String name;         // e.g. "groove"
        juce::String description;  // e.g. "Create or apply swing/groove templates"
    };
    std::vector<SlashCommand> slashCommands_;
    void buildSlashCommands();
    void showSlashAutocomplete(const juce::String& filter);
    void insertSlashCommand(const juce::String& command);
    void showAutocomplete(const juce::String& filter);
    void hideAutocomplete();
    void insertAlias(const juce::String& alias);

    // Tab switching: AI vs DSL
    enum class ConsoleTab { AI, DSL };
    ConsoleTab activeTab_ = ConsoleTab::AI;
    std::unique_ptr<magda::SvgButton> aiTabButton_;
    std::unique_ptr<magda::SvgButton> dslTabButton_;
    void switchTab(ConsoleTab tab);
    void setupTabButtons();

    // DSL tab components
    DSLTokeniser dslTokeniser_;
    juce::CodeDocument dslDocument_;
    std::unique_ptr<juce::CodeEditorComponent> dslEditor_;
    juce::TextEditor dslOutput_;
    juce::Label dslStatusLabel_;
    juce::StringArray dslHistory_;
    int dslHistoryIndex_ = -1;

    void executeDSL();
    void appendDSLOutput(const juce::String& text, juce::Colour colour);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AIChatConsoleContent)
};

}  // namespace magda::daw::ui
