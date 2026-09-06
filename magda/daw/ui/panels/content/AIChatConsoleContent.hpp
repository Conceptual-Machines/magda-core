#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_gui_extra/juce_gui_extra.h>

#include <atomic>
#include <cstddef>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <unordered_set>
#include <utility>
#include <vector>

#include "../../../../agents/conversation_store.hpp"
#include "../../../../agents/llama_model_manager.hpp"
#include "../../../../agents/mixing_agent.hpp"
#include "../../../core/Config.hpp"
#include "../../../core/MixAnalysisService.hpp"
#include "../../../core/PluginPreferences.hpp"
#include "../../../core/SelectionManager.hpp"
#include "../../../core/TrackMeasurementManager.hpp"
#include "../../../core/ViewModeController.hpp"
#include "../../../project/ProjectManager.hpp"
#include "../../code/ChatPromptTokeniser.hpp"
#include "../../code/DSLTokeniser.hpp"
#include "PanelContent.hpp"
#include "SlashCommands.hpp"

namespace magda {
namespace agent {
class ConsoleAgentOrchestrator;
}
class AutomationAgent;
class CommandAgent;
class ControllerProfileAgent;
class FourOscAgent;
class ThemeAgent;
class DrummerAgent;
class MagdaApi;
class MagdaApiLive;
class MusicAgent;
class SvgButton;
}  // namespace magda

namespace magda::daw::ui {

/**
 * @brief AI Chat console panel content
 *
 * Chat interface for interacting with AI assistant.
 * Routes user messages to the per-view agents on a background thread.
 */
class AIChatConsoleContent : public PanelContent,
                             private juce::Timer,
                             private juce::KeyListener,
                             private juce::CodeDocument::Listener,
                             public magda::SelectionManagerListener,
                             public magda::ProjectManagerListener,
                             public magda::ConfigListener,
                             public magda::ViewModeListener,
                             public magda::MixAnalysisService::Listener,
                             public magda::PluginPreferences::Listener {
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
    void resized() final;
    void lookAndFeelChanged() override;

    void onActivated() override;
    void onDeactivated() override;

    // ProjectManagerListener
    void projectOpened(const magda::ProjectInfo& info) override;

    // ConfigListener
    void configChanged() override;

    // ViewModeListener (#1402): swap the console's context glyph + scope routing.
    void viewModeChanged(magda::ViewMode mode, const magda::AudioEngineProfile& profile) override;

    // MixAnalysisService::Listener (#886): refresh the "mix analysis ready" chip.
    void mixAnalysisChanged() override;

    // PluginPreferences::Listener: keep @plugin autocomplete aligned with the
    // preferred AU/VST3 presentation list.
    void externalPluginFormatPreferenceChanged(magda::PluginFormat preference) override;

    // SelectionManagerListener
    void selectionTypeChanged(magda::SelectionType newType) override;
    void trackSelectionChanged(magda::TrackId trackId) final;
    void multiTrackSelectionChanged(const std::unordered_set<magda::TrackId>& trackIds) override;
    void clipSelectionChanged(magda::ClipId clipId) final;
    void multiClipSelectionChanged(const std::unordered_set<magda::ClipId>& clipIds) override;
    void chainNodeSelectionChanged(const magda::ChainNodePath& path) override;

  private:
    enum class MidiOutputMode { NewClip, ReviseLast };

    // Background thread for AI requests
    class RequestThread : public juce::Thread {
      public:
        RequestThread(AIChatConsoleContent& owner, juce::String message, juce::String midiContext,
                      juce::String drummerContext, magda::ClipId reviseTargetClipId);
        void run() override;

      private:
        AIChatConsoleContent& owner_;
        juce::String message_;
        juce::String midiContext_;
        juce::String drummerContext_;
        magda::ClipId reviseTargetClipId_ = magda::INVALID_CLIP_ID;
    };

    void sendMessage(const juce::String& text);
    void cancelRequest();
    void restoreSendIcon();
    void setThemedButtonIcon(juce::DrawableButton& button, const void* svgData,
                             std::size_t svgDataSize);
    void appendToChat(const juce::String& text);
    void updateContextBar();
    void showMidiContextPicker();
    void useCurrentSelectionAsMidiContext();
    void clearMidiContext();
    void toggleMidiContextClip(magda::ClipId clipId);
    void toggleMidiContextTrack(magda::TrackId trackId);
    void pruneMidiContextSelection();
    juce::String getMidiContextSummary() const;
    bool isMidiContextClipSelected(magda::ClipId clipId) const;
    void updateOutputModeButton();
    bool isLastGeneratedMidiClipValid() const;
    void rememberGeneratedMidiClip(magda::ClipId clipId);

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
    class MidiContextPopup;
    enum class ContextIcon { None, Track, Clip, Device, Drummer };
    ContextIcon contextIcon_ = ContextIcon::None;
    std::unique_ptr<juce::Drawable> trackIconDrawable_;
    std::unique_ptr<juce::Drawable> clipIconDrawable_;
    std::unique_ptr<juce::Drawable> drumIconDrawable_;
    // View-context routing (#1402): the bottom-left glyph reflects the active
    // view (session / arrangement / mixer), which scopes the console's agent.
    std::unique_ptr<juce::Drawable> sessionIconDrawable_;
    std::unique_ptr<juce::Drawable> arrangeIconDrawable_;
    std::unique_ptr<juce::Drawable> mixIconDrawable_;
    magda::ViewMode currentViewMode_ = magda::ViewMode::Arrange;
    // True when the selected track's primary instrument carries a kit with at
    // least one role-tagged row. Drives the drummer auto-route in
    // RequestThread::run and the drum context icon below the chat.
    bool drummerModeActive_ = false;
    juce::Label contextLabel_;
    juce::TextButton outputModeButton_{"New clip"};
    juce::DrawableButton sendButton_{"send", juce::DrawableButton::ImageFitted};
    juce::DrawableButton clearButton_{"clear", juce::DrawableButton::ImageFitted};
    juce::DrawableButton copyButton_{"copy", juce::DrawableButton::ImageFitted};
    juce::Rectangle<int> bottomBarBounds_;
    juce::Rectangle<int> contextIconBounds_;
    juce::String contextText_;
    bool contextEnabled_ = true;
    MidiOutputMode midiOutputMode_ = MidiOutputMode::NewClip;
    magda::ClipId lastGeneratedMidiClipId_ = magda::INVALID_CLIP_ID;
    // Explicit clip context chosen from the footer picker. This is deliberately
    // independent of the editor selection so users can move around the project
    // without silently changing what the next request will see.
    std::unordered_set<magda::ClipId> midiContextClipIds_;

    // Mix analysis is gathered by the mixer's Analyze button and held by
    // MixAnalysisService (#886). The console only surfaces a small "mix analysis
    // ready" chip in mixer view; sending a message in mixer view feeds the latest
    // measurement to the mixing agent as context (see RequestThread::run).
    juce::Label analysisChip_;
    void updateAnalysisChip();  // show/hide + label the chip from the service state

    void mouseUp(const juce::MouseEvent& event) override;

    // KeyListener — intercept arrow keys for autocomplete navigation
    using juce::Component::keyPressed;  // unhide 1-param overload
    bool keyPressed(const juce::KeyPress& key, juce::Component* originatingComponent) override;

    // Live MagdaApi backing the agent layer. Normally borrowed from
    // application audio engine (which outlives this panel). When the
    // engine is unreachable (headless tests, init failure), we fall back
    // to owning a fresh MagdaApiLive in ownedApi_ so the pointer is never
    // null and downstream agents / executors can dereference unconditionally.
    std::unique_ptr<magda::MagdaApiLive> ownedApi_;
    magda::MagdaApi* magdaApi_ = nullptr;

    // Centralised rolling conversation memory, one thread per view (#1402).
    // RequestThread renders the active view's thread into each agent prompt and
    // records the exchange when the turn completes. See conversationChannel().
    magda::ConversationStore conversation_;
    static magda::ConversationStore::Channel conversationChannel(magda::ViewMode mode);

    std::unique_ptr<magda::CommandAgent> commandAgent_;
    std::unique_ptr<magda::MusicAgent> musicAgent_;
    std::unique_ptr<magda::DrummerAgent> drummerAgent_;
    std::unique_ptr<magda::AutomationAgent> automationAgent_;
    std::unique_ptr<magda::agent::ConsoleAgentOrchestrator> agentOrchestrator_;
    std::unique_ptr<magda::ControllerProfileAgent> controllerAgent_;
    std::unique_ptr<magda::FourOscAgent> fourOscAgent_;
    std::unique_ptr<magda::ThemeAgent> themeAgent_;
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
    // long preset generation can't block the main agent
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

    // /theme <description> - kick the ThemeAgent on a background thread. On
    // success the generated JSON is written into paths::themesDir() and
    // selected via Config, so the app's existing apply + hot-reload path
    // renders it. Its own thread for the same reason as FourOsc.
    class ThemeRequestThread : public juce::Thread {
      public:
        ThemeRequestThread(AIChatConsoleContent& owner, juce::String description);
        void run() override;

      private:
        AIChatConsoleContent& owner_;
        juce::String description_;
    };
    std::unique_ptr<ThemeRequestThread> themeThread_;
    void startThemeGeneration(const juce::String& description);
    // streamAnchor is the text position where the streamed JSON began (or -1 if
    // nothing streamed); the streamed region is collapsed to the summary here.
    void finishThemeGeneration(bool success, const juce::String& jsonOrError, juce::String name,
                               juce::String base, int colourCount, int syntaxCount,
                               int streamAnchor);

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
    // Accepting a completion rewrites the document, which fires BOTH document
    // listeners (delete + insert) and so queues two deferred onInputChanged()
    // calls, each with the caret parked just after the freshly-inserted
    // @alias — matching it again and re-opening the popup we just closed.
    // Holds the exact text inserted, so every callback for that content is
    // absorbed and completion resumes as soon as the user types.
    juce::String suppressAutocompleteForContent_;
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
