#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_gui_extra/juce_gui_extra.h>

#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <unordered_set>
#include <utility>
#include <vector>

#include "../../../../agents/llama_model_manager.hpp"
#include "../../../core/Config.hpp"
#include "../../../core/SelectionManager.hpp"
#include "../../../project/ProjectManager.hpp"
#include "ChatPromptTokeniser.hpp"
#include "DSLTokeniser.hpp"
#include "PanelContent.hpp"
#include "SlashCommands.hpp"

namespace magda {
class AutomationAgent;
class CommandAgent;
class ControllerProfileAgent;
class FourOscAgent;
class DAWAgent;
class DrummerAgent;
class MagdaApi;
class MagdaApiLive;
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
                             private juce::CodeDocument::Listener,
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
    void multiClipSelectionChanged(const std::unordered_set<magda::ClipId>& clipIds) override;
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

    // Input box: CodeEditorComponent + ChatPromptTokeniser so @plugin and
    // /command syntax pick up colour automatically. inputDocument_ holds the
    // text; inputBox_ is the visible editor; we listen on the document for
    // text changes (the autocomplete trigger) and intercept Enter / Esc via
    // the KeyListener mixin already on this class.
    juce::CodeDocument inputDocument_;
    ChatPromptTokeniser inputTokeniser_;
    std::unique_ptr<juce::CodeEditorComponent> inputBox_;

    // CodeDocument::Listener — autocomplete trigger replaces TextEditor::onTextChange.
    void codeDocumentTextInserted(const juce::String& text, int insertIndex) override;
    void codeDocumentTextDeleted(int startIndex, int endIndex) override;
    void onInputChanged();  // shared body for both insert / delete callbacks

    // Bottom bar: context icon + label + send button
    enum class ContextIcon { None, Track, Clip, Device, Drummer };
    ContextIcon contextIcon_ = ContextIcon::None;
    std::unique_ptr<juce::Drawable> trackIconDrawable_;
    std::unique_ptr<juce::Drawable> clipIconDrawable_;
    std::unique_ptr<juce::Drawable> drumIconDrawable_;
    // True when the selected track's primary instrument carries a kit with at
    // least one role-tagged row. Drives the drummer auto-route in
    // RequestThread::run and the drum context icon below the chat.
    bool drummerModeActive_ = false;
    juce::Label contextLabel_;
    std::unique_ptr<juce::LookAndFeel_V4> selectedClipContextLookAndFeel_;
    juce::ToggleButton selectedClipContextToggle_{"Use context"};
    juce::DrawableButton sendButton_{"send", juce::DrawableButton::ImageFitted};
    juce::DrawableButton clearButton_{"clear", juce::DrawableButton::ImageFitted};
    juce::DrawableButton copyButton_{"copy", juce::DrawableButton::ImageFitted};
    juce::Rectangle<int> bottomBarBounds_;
    juce::Rectangle<int> contextIconBounds_;
    juce::String contextText_;
    bool contextEnabled_ = true;
    bool selectedClipContextAvailable_ = false;
    bool selectedClipContextEnabled_ = true;

    void mouseUp(const juce::MouseEvent& event) override;

    // KeyListener — intercept arrow keys for autocomplete navigation
    using juce::Component::keyPressed;  // unhide 1-param overload
    bool keyPressed(const juce::KeyPress& key, juce::Component* originatingComponent) override;

    // Live MagdaApi backing the agent layer. Normally borrowed from
    // TracktionEngineWrapper (the engine outlives this panel). When the
    // engine is unreachable (headless tests, init failure), we fall back
    // to owning a fresh MagdaApiLive in ownedApi_ so the pointer is never
    // null and downstream agents / executors can dereference unconditionally.
    std::unique_ptr<magda::MagdaApiLive> ownedApi_;
    magda::MagdaApi* magdaApi_ = nullptr;

    std::unique_ptr<magda::DAWAgent> agent_;  // kept for legacy DSL REPL
    std::unique_ptr<magda::RouterAgent> routerAgent_;
    std::unique_ptr<magda::CommandAgent> commandAgent_;
    std::unique_ptr<magda::MusicAgent> musicAgent_;
    std::unique_ptr<magda::DrummerAgent> drummerAgent_;
    std::unique_ptr<magda::AutomationAgent> automationAgent_;
    std::unique_ptr<magda::ControllerProfileAgent> controllerAgent_;
    std::unique_ptr<magda::FourOscAgent> fourOscAgent_;
    std::unique_ptr<RequestThread> requestThread_;

    // Dedicated thread for the controller profile agent (kept separate from
    // the main RequestThread so the two flows don't interfere).
    class ControllerRequestThread : public juce::Thread {
      public:
        ControllerRequestThread(AIChatConsoleContent& owner, juce::String description,
                                std::vector<std::string> livePortNames);
        void run() override;

      private:
        AIChatConsoleContent& owner_;
        juce::String description_;
        std::vector<std::string> livePortNames_;
    };
    std::unique_ptr<ControllerRequestThread> controllerThread_;

    void startControllerGeneration(const juce::String& description);
    void finishControllerGeneration(bool success, const juce::String& errorOrRawJson,
                                    juce::String profileId, juce::String profileName);

    // /design <description> — kick the FourOscAgent on a background thread
    // and dump the parsed JSON into chat. Kept on its own thread so a
    // long preset generation can't block the main router/command/music
    // pipeline running in requestThread_.
    class FourOscRequestThread : public juce::Thread {
      public:
        FourOscRequestThread(AIChatConsoleContent& owner, juce::String description);
        void run() override;

      private:
        AIChatConsoleContent& owner_;
        juce::String description_;
    };
    std::unique_ptr<FourOscRequestThread> fourOscThread_;
    void startPresetGeneration(const juce::String& description);
    void finishPresetGeneration(bool success, const juce::String& errorOrPretty,
                                juce::String presetName);

    // Optional category override set by `/design --category=<cat>`. When
    // non-empty, finishPresetGeneration substitutes this value for the
    // category the agent picked (so the saved preset folders match what
    // the user asked for). Cleared after the design completes.
    juce::String pendingCategoryOverride_;

    // Clear the input box's text AND force a repaint. Document mutations
    // alone don't always invalidate the CodeEditorComponent's glyph
    // cache (especially on macOS with our custom fonts), leaving stale
    // pixels under where the previous text rendered.
    void clearInput();
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

    // Parameter alias entry shown after the user types '@plugin.' in the input.
    // Sourced from AliasRegistry across all layers (UserProject, UserGlobal,
    // Curated, AutoGen). The autocomplete pivots from plugin mode → param mode
    // when a '.' is typed inside an @-token, and back to plugin mode when the
    // '.' is deleted.
    struct ParamAliasEntry {
        juce::String pluginAlias;  // e.g. "eq" / "equaliser" — the bit before the dot
        juce::String paramAlias;   // e.g. "low_shelf_freq"   — the bit after the dot
        juce::String paramName;    // Display string from paramNameAtSetTime (may be empty)
    };

    class AutocompletePopup;
    std::unique_ptr<AutocompletePopup> autocompletePopup_;
    std::vector<AliasEntry> allAliases_;

    void buildAliasList();
    std::vector<ParamAliasEntry> collectParamAliases(const juce::String& pluginAlias) const;
    juce::String resolveAliases(const juce::String& text);
    juce::String rewriteSlashCommand(const juce::String& text);

    // Slash commands live in their own module (SlashCommands.{hpp,cpp})
    // so they can be tested without standing up the full chat panel. The
    // autocomplete reads commands via slashRegistry_->all().
    using SlashCommand = magda::daw::ui::SlashCommand;
    std::unique_ptr<magda::daw::ui::SlashCommandRegistry> slashRegistry_;
    void buildSlashCommands();
    void showSlashAutocomplete(const juce::String& filter);
    void insertSlashCommand(const juce::String& command);
    void showAutocomplete(const juce::String& filter);
    void showParamAutocomplete(const juce::String& pluginAlias, const juce::String& filter);
    void hideAutocomplete();
    void insertAlias(const juce::String& alias);
    void insertParamAlias(const juce::String& pluginAlias, const juce::String& paramAlias);

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
