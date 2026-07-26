#include "AIChatConsoleContent.hpp"

#include <algorithm>
#include <atomic>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>

#include "../../../../agents/automation_agent.hpp"
#include "../../../../agents/command_agent.hpp"
#include "../../../../agents/console_agent_orchestrator.hpp"
#include "../../../../agents/controller_profile_agent.hpp"
#include "../../../../agents/drummer_agent.hpp"
#include "../../../../agents/dsl_interpreter.hpp"
#include "../../../../agents/four_osc_agent.hpp"
#include "../../../../agents/four_osc_apply.hpp"
#include "../../../../agents/internal_plugins.hpp"
#include "../../../../agents/llama_model_manager.hpp"
#include "../../../../agents/llm_presets.hpp"
#include "../../../../agents/midi_context.hpp"
#include "../../../../agents/mixing_agent.hpp"
#include "../../../../agents/music_agent.hpp"
#include "../../../../agents/theme_agent.hpp"
#include "../../../api/magda_api_live.hpp"
#include "../../../audio/analysis/OfflineMixAnalysis.hpp"
#include "../../../core/AppPaths.hpp"
#include "../../../core/ClipManager.hpp"
#include "../../../core/Config.hpp"
#include "../../../core/ConsoleRouting.hpp"
#include "../../../core/MixAnalysisService.hpp"
#include "../../../core/ParameterUtils.hpp"
#include "../../../core/PresetManager.hpp"
#include "../../../core/SelectionManager.hpp"
#include "../../../core/TrackManager.hpp"
#include "../../../core/aliases/AliasRegistry.hpp"
#include "../../../core/aliases/ParamNameNormalize.hpp"
#include "../../../core/controllers/BindingRegistry.hpp"
#include "../../../core/controllers/ControllerProfileRegistry.hpp"
#include "../../../core/controllers/ControllerRegistry.hpp"
#include "../../../project/ProjectManager.hpp"
#include "../../components/common/SvgButton.hpp"
#include "../../dialogs/AISettingsDialog.hpp"
#include "../../state/TimelineController.hpp"
#include "../../themes/DarkTheme.hpp"
#include "../../themes/FontManager.hpp"
#include "../../themes/SmallButtonLookAndFeel.hpp"
#include "../../themes/ThemePrompt.hpp"
#include "BinaryData.h"
#include "PluginBrowserContent.hpp"
#include "audio/AudioBridge.hpp"
#include "audio/plugins/DrumGridPlugin.hpp"
#include "audio/plugins/DrumGridRoles.hpp"
#include "audio/plugins/MagdaSamplerPlugin.hpp"
#include "engine/AudioEngine.hpp"
#include "engine/TracktionEngineWrapper.hpp"

namespace magda::daw::ui {

namespace {
// Forward declaration so RequestThread::run can call the formatter — the
// definition lives further down in the file's other anon-namespace block,
// next to isDrummerTrack().
juce::String formatClipAsDrummerContext(magda::ClipId clipId);
juce::String formatSelectedClipsAsDrummerContext();
}  // namespace

// ============================================================================
// AutocompletePopup
// ============================================================================

class AIChatConsoleContent::AutocompletePopup : public juce::Component, public juce::ListBoxModel {
  public:
    AutocompletePopup(AIChatConsoleContent& owner) : owner_(owner) {
        listBox_.setModel(this);
        listBox_.setRowHeight(22);
        listBox_.setColour(juce::ListBox::backgroundColourId,
                           DarkTheme::getColour(DarkTheme::SURFACE));
        listBox_.setColour(juce::ListBox::outlineColourId, DarkTheme::getBorderColour());
        addAndMakeVisible(listBox_);
    }

    enum class Mode { Alias, SlashCommand, Param };

    void updateFilter(const juce::String& filter) {
        mode_ = Mode::Alias;
        filter_ = filter.toLowerCase();
        filtered_.clear();
        filteredCommands_.clear();
        paramEntries_.clear();
        filteredParams_.clear();

        for (const auto& entry : owner_.allAliases_) {
            if (filter_.isEmpty() || entry.alias.toLowerCase().contains(filter_) ||
                entry.pluginName.toLowerCase().contains(filter_)) {
                filtered_.push_back(&entry);
            }
        }

        listBox_.updateContent();
        if (!filtered_.empty())
            listBox_.selectRow(0);

        int rows = juce::jmin(static_cast<int>(filtered_.size()), 8);
        setSize(getWidth(), rows * 22 + 2);
    }

    void updateSlashFilter(const juce::String& filter) {
        mode_ = Mode::SlashCommand;
        filter_ = filter.toLowerCase();
        filtered_.clear();
        filteredCommands_.clear();
        paramEntries_.clear();
        filteredParams_.clear();

        if (owner_.slashRegistry_) {
            for (const auto& cmd : owner_.slashRegistry_->all()) {
                if (filter_.isEmpty() || cmd.name.toLowerCase().startsWith(filter_))
                    filteredCommands_.push_back(&cmd);
            }
        }

        listBox_.updateContent();
        if (!filteredCommands_.empty())
            listBox_.selectRow(0);

        int rows = juce::jmin(static_cast<int>(filteredCommands_.size()), 8);
        setSize(getWidth(), rows * 22 + 2);
    }

    // Switch to param mode: the popup shows paramAlias entries belonging to
    // a single plugin alias. Caller supplies the source list (already scoped
    // by pluginAlias) and a sub-filter typed by the user after the dot.
    void updateParamFilter(std::vector<ParamAliasEntry> source, const juce::String& pluginAlias,
                           const juce::String& filter) {
        mode_ = Mode::Param;
        filter_ = filter.toLowerCase();
        currentPluginAlias_ = pluginAlias;
        filtered_.clear();
        filteredCommands_.clear();
        paramEntries_ = std::move(source);
        filteredParams_.clear();
        for (const auto& entry : paramEntries_) {
            if (filter_.isEmpty() || entry.paramAlias.toLowerCase().contains(filter_) ||
                entry.paramName.toLowerCase().contains(filter_)) {
                filteredParams_.push_back(&entry);
            }
        }
        listBox_.updateContent();
        if (!filteredParams_.empty())
            listBox_.selectRow(0);
        int rows = juce::jmin(static_cast<int>(filteredParams_.size()), 8);
        setSize(getWidth(), rows * 22 + 2);
    }

    bool isEmpty() const {
        switch (mode_) {
            case Mode::Alias:
                return filtered_.empty();
            case Mode::SlashCommand:
                return filteredCommands_.empty();
            case Mode::Param:
                return filteredParams_.empty();
        }
        return true;
    }

    Mode getMode() const {
        return mode_;
    }

    const AliasEntry* getSelectedEntry() const {
        int row = listBox_.getSelectedRow();
        if (mode_ == Mode::Alias && row >= 0 && row < static_cast<int>(filtered_.size()))
            return filtered_[static_cast<size_t>(row)];
        return nullptr;
    }

    const SlashCommand* getSelectedCommand() const {
        int row = listBox_.getSelectedRow();
        if (mode_ == Mode::SlashCommand && row >= 0 &&
            row < static_cast<int>(filteredCommands_.size()))
            return filteredCommands_[static_cast<size_t>(row)];
        return nullptr;
    }

    const ParamAliasEntry* getSelectedParamEntry() const {
        int row = listBox_.getSelectedRow();
        if (mode_ == Mode::Param && row >= 0 && row < static_cast<int>(filteredParams_.size()))
            return filteredParams_[static_cast<size_t>(row)];
        return nullptr;
    }

    void selectNext() {
        int current = listBox_.getSelectedRow();
        if (current < getNumRows() - 1)
            listBox_.selectRow(current + 1);
    }

    void selectPrevious() {
        int current = listBox_.getSelectedRow();
        if (current > 0)
            listBox_.selectRow(current - 1);
    }

    // ListBoxModel
    int getNumRows() override {
        switch (mode_) {
            case Mode::Alias:
                return static_cast<int>(filtered_.size());
            case Mode::SlashCommand:
                return static_cast<int>(filteredCommands_.size());
            case Mode::Param:
                return static_cast<int>(filteredParams_.size());
        }
        return 0;
    }

    void paintListBoxItem(int rowNumber, juce::Graphics& g, int width, int height,
                          bool rowIsSelected) override {
        if (rowNumber < 0 || rowNumber >= getNumRows())
            return;

        if (rowIsSelected) {
            g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_PRIMARY).withAlpha(0.3f));
            g.fillRect(0, 0, width, height);
        }

        if (mode_ == Mode::Alias) {
            const auto& entry = *filtered_[static_cast<size_t>(rowNumber)];
            g.setColour(DarkTheme::getAccentColour());
            g.setFont(FontManager::getInstance().getMonoFont(11.0f));
            g.drawText("@" + entry.alias, 6, 0, width / 2, height,
                       juce::Justification::centredLeft);
            g.setColour(DarkTheme::getSecondaryTextColour());
            g.setFont(FontManager::getInstance().getUIFont(10.0f));
            g.drawText(entry.pluginName, width / 2, 0, width / 2 - 6, height,
                       juce::Justification::centredRight);
        } else if (mode_ == Mode::SlashCommand) {
            const auto& cmd = *filteredCommands_[static_cast<size_t>(rowNumber)];
            g.setColour(DarkTheme::getAccentColour());
            g.setFont(FontManager::getInstance().getMonoFont(11.0f));
            g.drawText("/" + cmd.name, 6, 0, width / 3, height, juce::Justification::centredLeft);
            g.setColour(DarkTheme::getSecondaryTextColour());
            g.setFont(FontManager::getInstance().getUIFont(10.0f));
            g.drawText(cmd.description, width / 3, 0, width * 2 / 3 - 6, height,
                       juce::Justification::centredLeft);
        } else {
            const auto& entry = *filteredParams_[static_cast<size_t>(rowNumber)];
            g.setColour(DarkTheme::getAccentColour());
            g.setFont(FontManager::getInstance().getMonoFont(11.0f));
            g.drawText("@" + entry.pluginAlias + "." + entry.paramAlias, 6, 0, width / 2, height,
                       juce::Justification::centredLeft);
            g.setColour(DarkTheme::getSecondaryTextColour());
            g.setFont(FontManager::getInstance().getUIFont(10.0f));
            const auto& displayName =
                entry.paramName.isNotEmpty() ? entry.paramName : juce::String("parameter");
            g.drawText(displayName, width / 2, 0, width / 2 - 6, height,
                       juce::Justification::centredRight);
        }
    }

    void listBoxItemDoubleClicked(int row, const juce::MouseEvent&) override {
        if (mode_ == Mode::Alias && row >= 0 && row < static_cast<int>(filtered_.size())) {
            owner_.insertAlias(filtered_[static_cast<size_t>(row)]->alias);
        } else if (mode_ == Mode::SlashCommand && row >= 0 &&
                   row < static_cast<int>(filteredCommands_.size())) {
            owner_.insertSlashCommand(filteredCommands_[static_cast<size_t>(row)]->name);
        } else if (mode_ == Mode::Param && row >= 0 &&
                   row < static_cast<int>(filteredParams_.size())) {
            const auto& entry = *filteredParams_[static_cast<size_t>(row)];
            owner_.insertParamAlias(entry.pluginAlias, entry.paramAlias);
        }
    }

    void resized() override {
        listBox_.setBounds(getLocalBounds());
    }

  private:
    AIChatConsoleContent& owner_;
    juce::ListBox listBox_;
    juce::String filter_;
    Mode mode_ = Mode::Alias;
    std::vector<const AliasEntry*> filtered_;
    std::vector<const SlashCommand*> filteredCommands_;
    std::vector<ParamAliasEntry> paramEntries_;
    std::vector<const ParamAliasEntry*> filteredParams_;
    juce::String currentPluginAlias_;
};

// ============================================================================
// MidiContextPopup
// ============================================================================

class AIChatConsoleContent::MidiContextPopup : public juce::Component, private juce::ListBoxModel {
  public:
    explicit MidiContextPopup(AIChatConsoleContent& owner) : owner_(&owner) {
        useSelectionButton_.setButtonText("Use selection");
        clearButton_.setButtonText("Clear");
        for (auto* button : {&useSelectionButton_, &clearButton_}) {
            button->setColour(juce::TextButton::buttonColourId,
                              DarkTheme::getColour(DarkTheme::BUTTON_NORMAL));
            button->setColour(juce::TextButton::buttonOnColourId,
                              DarkTheme::getColour(DarkTheme::SURFACE_HOVER));
            button->setColour(juce::TextButton::textColourOffId, DarkTheme::getTextColour());
            addAndMakeVisible(*button);
        }

        useSelectionButton_.setTooltip(
            "Use the selected MIDI clip(s), or every MIDI clip on the selected track(s)");
        useSelectionButton_.onClick = [safeOwner = owner_]() {
            if (safeOwner != nullptr)
                safeOwner->useCurrentSelectionAsMidiContext();
        };
        clearButton_.onClick = [safeOwner = owner_]() {
            if (safeOwner != nullptr)
                safeOwner->clearMidiContext();
        };

        listBox_.setModel(this);
        listBox_.setRowHeight(kRowHeight);
        listBox_.setOutlineThickness(0);
        listBox_.setColour(juce::ListBox::backgroundColourId,
                           DarkTheme::getColour(DarkTheme::SURFACE));
        listBox_.setColour(juce::ListBox::outlineColourId, DarkTheme::getBorderColour());
        addAndMakeVisible(listBox_);

        buildRows();
        const int listHeight =
            juce::jlimit(kRowHeight, 320, static_cast<int>(rows_.size()) * kRowHeight);
        setSize(310, kToolbarHeight + listHeight + 16);
    }

    void paint(juce::Graphics& g) override {
        g.fillAll(DarkTheme::getColour(DarkTheme::SURFACE));
        g.setColour(DarkTheme::getBorderColour());
        g.drawRect(getLocalBounds(), 1);
    }

    void resized() override {
        auto area = getLocalBounds().reduced(6);
        auto toolbar = area.removeFromTop(kToolbarHeight);
        useSelectionButton_.setBounds(toolbar.removeFromLeft(112));
        toolbar.removeFromLeft(6);
        clearButton_.setBounds(toolbar.removeFromLeft(58));
        area.removeFromTop(4);
        listBox_.setBounds(area);
    }

  private:
    struct Row {
        enum class Kind { Track, Clip, Empty };
        Kind kind = Kind::Empty;
        magda::TrackId trackId = magda::INVALID_TRACK_ID;
        magda::ClipId clipId = magda::INVALID_CLIP_ID;
        std::vector<magda::ClipId> trackClipIds;
        juce::String label;
        juce::String detail;
    };

    static constexpr int kRowHeight = 24;
    static constexpr int kToolbarHeight = 24;

    int getNumRows() override {
        return static_cast<int>(rows_.size());
    }

    void paintListBoxItem(int rowNumber, juce::Graphics& g, int width, int height,
                          bool rowIsSelected) override {
        if (rowNumber < 0 || rowNumber >= static_cast<int>(rows_.size()))
            return;
        const auto& row = rows_[static_cast<std::size_t>(rowNumber)];

        if (rowIsSelected)
            g.fillAll(DarkTheme::getColour(DarkTheme::SURFACE_HOVER));

        if (row.kind == Row::Kind::Empty) {
            g.setColour(DarkTheme::getSecondaryTextColour());
            g.setFont(FontManager::getInstance().getMonoFont(11.0f));
            g.drawText(row.label, 8, 0, width - 16, height, juce::Justification::centredLeft);
            return;
        }

        bool checked = false;
        bool partial = false;
        if (owner_ != nullptr) {
            if (row.kind == Row::Kind::Clip) {
                checked = owner_->isMidiContextClipSelected(row.clipId);
            } else {
                int selected = 0;
                for (auto clipId : row.trackClipIds)
                    selected += owner_->isMidiContextClipSelected(clipId) ? 1 : 0;
                checked = selected == static_cast<int>(row.trackClipIds.size()) && selected > 0;
                partial = selected > 0 && !checked;
            }
        }

        const int indent = row.kind == Row::Kind::Clip ? 24 : 8;
        auto tick = juce::Rectangle<float>(static_cast<float>(indent),
                                           static_cast<float>((height - 13) / 2), 13.0f, 13.0f);
        g.setColour(DarkTheme::getBorderColour());
        g.drawRoundedRectangle(tick, 2.0f, 1.0f);
        if (checked || partial) {
            g.setColour(DarkTheme::getAccentColour());
            if (partial) {
                g.fillRect(tick.reduced(3.0f).withHeight(2.0f).withCentre(tick.getCentre()));
            } else {
                juce::Path check;
                check.startNewSubPath(tick.getX() + 2.5f, tick.getCentreY());
                check.lineTo(tick.getX() + 5.5f, tick.getBottom() - 3.0f);
                check.lineTo(tick.getRight() - 2.0f, tick.getY() + 3.0f);
                g.strokePath(check, juce::PathStrokeType(1.7f));
            }
        }

        const int textX = indent + 20;
        const int textRightPadding = row.detail.isNotEmpty() ? 92 : 8;
        g.setColour(row.kind == Row::Kind::Track ? DarkTheme::getTextColour()
                                                 : DarkTheme::getSecondaryTextColour());
        g.setFont(row.kind == Row::Kind::Track
                      ? FontManager::getInstance().getMonoFont(11.0f).boldened()
                      : FontManager::getInstance().getMonoFont(11.0f));
        g.drawText(row.label, textX, 0, width - textX - textRightPadding, height,
                   juce::Justification::centredLeft, true);

        if (row.detail.isNotEmpty()) {
            g.setColour(DarkTheme::getSecondaryTextColour().withAlpha(0.65f));
            g.setFont(FontManager::getInstance().getMonoFont(9.5f));
            const int detailWidth = 82;
            g.drawText(row.detail, width - detailWidth - 8, 0, detailWidth, height,
                       juce::Justification::centredRight);
        }
    }

    void listBoxItemClicked(int rowNumber, const juce::MouseEvent&) override {
        if (owner_ == nullptr || rowNumber < 0 || rowNumber >= static_cast<int>(rows_.size()))
            return;
        const auto& row = rows_[static_cast<std::size_t>(rowNumber)];
        if (row.kind == Row::Kind::Track)
            owner_->toggleMidiContextTrack(row.trackId);
        else if (row.kind == Row::Kind::Clip)
            owner_->toggleMidiContextClip(row.clipId);
        listBox_.deselectAllRows();
        listBox_.repaint();
    }

    void buildRows() {
        rows_.clear();
        auto& tracks = magda::TrackManager::getInstance();
        auto& clips = magda::ClipManager::getInstance();

        for (const auto& track : tracks.getTracks()) {
            std::vector<magda::ClipId> midiClipIds;
            for (auto clipId : clips.getClipsOnTrack(track.id)) {
                const auto* clip = clips.getClip(clipId);
                if (clip != nullptr && clip->isMidi())
                    midiClipIds.push_back(clipId);
            }
            if (midiClipIds.empty())
                continue;

            Row trackRow;
            trackRow.kind = Row::Kind::Track;
            trackRow.trackId = track.id;
            trackRow.trackClipIds = midiClipIds;
            trackRow.label = track.name;
            trackRow.detail = juce::String(static_cast<int>(midiClipIds.size())) + " MIDI";
            rows_.push_back(std::move(trackRow));

            for (auto clipId : midiClipIds) {
                const auto* clip = clips.getClip(clipId);
                if (clip == nullptr)
                    continue;
                Row clipRow;
                clipRow.kind = Row::Kind::Clip;
                clipRow.trackId = track.id;
                clipRow.clipId = clipId;
                clipRow.label = clip->name.isNotEmpty() ? clip->name : "Untitled MIDI clip";
                clipRow.detail = juce::String(static_cast<int>(clip->midiNotes.size())) + " notes";
                if (clip->view == magda::ClipView::Session)
                    clipRow.detail += " · live";
                rows_.push_back(std::move(clipRow));
            }
        }

        if (rows_.empty()) {
            Row empty;
            empty.kind = Row::Kind::Empty;
            empty.label = "No MIDI clips in this project";
            rows_.push_back(std::move(empty));
        }
        listBox_.updateContent();
    }

    juce::Component::SafePointer<AIChatConsoleContent> owner_;
    juce::TextButton useSelectionButton_;
    juce::TextButton clearButton_;
    juce::ListBox listBox_;
    std::vector<Row> rows_;
};

// ============================================================================
// RequestThread
// ============================================================================

magda::ConversationStore::Channel AIChatConsoleContent::conversationChannel(magda::ViewMode mode) {
    switch (mode) {
        case magda::ViewMode::Live:
            return magda::ConversationStore::Channel::Session;
        case magda::ViewMode::Mix:
        case magda::ViewMode::Master:
            return magda::ConversationStore::Channel::Mixing;
        case magda::ViewMode::Arrange:
            break;
    }
    return magda::ConversationStore::Channel::Arrangement;
}

AIChatConsoleContent::RequestThread::RequestThread(AIChatConsoleContent& owner,
                                                   juce::String message, juce::String midiContext,
                                                   juce::String drummerContext,
                                                   magda::ClipId reviseTargetClipId)
    : juce::Thread("AI Chat Request"),
      owner_(owner),
      message_(std::move(message)),
      midiContext_(std::move(midiContext)),
      drummerContext_(std::move(drummerContext)),
      reviseTargetClipId_(reviseTargetClipId) {}

void AIChatConsoleContent::RequestThread::run() {
    auto message = message_.toStdString();
    auto safeThis = juce::Component::SafePointer<AIChatConsoleContent>(&owner_);

    auto totalStart = std::chrono::steady_clock::now();
    double agentMs = 0.0;

    // The active or explicitly invoked surface is the routing decision. No
    // model call or intent classifier sits between context and the workflow.
    auto invocation = magda::AgentInvocationSurface::ViewDefault;
    switch (magda::SelectionManager::getInstance().getSelectionType()) {
        case magda::SelectionType::AutomationLane:
        case magda::SelectionType::AutomationClip:
        case magda::SelectionType::AutomationPoint:
            invocation = magda::AgentInvocationSurface::AutomationEditor;
            break;
        case magda::SelectionType::Note:
            invocation = magda::AgentInvocationSurface::PianoRoll;
            break;
        case magda::SelectionType::Device:
        case magda::SelectionType::ChainNode:
        case magda::SelectionType::MultiChainNode:
        case magda::SelectionType::Param:
            invocation = magda::AgentInvocationSurface::DevicePanel;
            break;
        default:
            break;
    }
    const auto surfaceDecision = magda::resolveAgentSurface(
        {.view = owner_.currentViewMode_,
         .invocation = invocation,
         .explicitOverride = magda::explicitAgentOverrideFromMessage(message),
         .drummerModeActive =
             owner_.drummerModeActive_ && invocation == magda::AgentInvocationSurface::ViewDefault,
         .mixCaptureAttached = false});
    DBG("MAGDA Agent: routed to " + juce::String(magda::toSurfaceString(surfaceDecision.surface)) +
        " (" + juce::String(surfaceDecision.source) + ")");

    if (threadShouldExit())
        return;

    // streamAnchor marks the text position where streamed output begins,
    // so we can replace it with execution results later. Shared ownership so
    // callAsync lambdas keep it alive even if this thread exits before they
    // run (capturing by reference to a stack local would be UB).
    auto streamAnchor = std::make_shared<std::atomic<int>>(-1);

    // Helper: replace "Thinking..." with streaming output area
    auto startStreaming = [safeThis, streamAnchor]() {
        juce::MessageManager::callAsync([safeThis, streamAnchor]() {
            if (!safeThis)
                return;
            safeThis->stopTimer();
            auto text = safeThis->chatHistory_.getText();
            auto thinkingPos = text.lastIndexOf(juce::String::charToString(0x25C6) + " Thinking");
            if (thinkingPos >= 0) {
                auto lineEnd = text.indexOf(thinkingPos, "\n");
                if (lineEnd < 0)
                    lineEnd = text.length();
                text = text.substring(0, thinkingPos) + text.substring(lineEnd + 1);
            }
            streamAnchor->store(text.length());
            safeThis->chatHistory_.setText(text);
            safeThis->chatHistory_.moveCaretToEnd();
        });
    };

    // Coalesced token buffer for single-intent streaming. Every token would
    // otherwise post its own callAsync; on a fast stream that buries the
    // message queue and the final execute-callAsync sits behind hundreds of
    // stale text appends. We buffer tokens and keep at most one flush
    // callback in flight at a time.
    struct SingleStream {
        std::mutex mu;
        juce::String pending;
        std::atomic<bool> flushPending{false};
    };
    auto singleState = std::make_shared<SingleStream>();

    auto appendToken = [safeThis, singleState](const juce::String& token) {
        {
            std::lock_guard<std::mutex> lk(singleState->mu);
            singleState->pending += token;
        }
        bool expected = false;
        if (!singleState->flushPending.compare_exchange_strong(expected, true))
            return;
        juce::MessageManager::callAsync([safeThis, singleState]() {
            singleState->flushPending.store(false);
            if (!safeThis)
                return;
            juce::String chunk;
            {
                std::lock_guard<std::mutex> lk(singleState->mu);
                chunk = std::move(singleState->pending);
                singleState->pending.clear();
            }
            if (chunk.isEmpty())
                return;
            auto text = safeThis->chatHistory_.getText();
            safeThis->chatHistory_.setText(text + chunk);
            safeThis->chatHistory_.moveCaretToEnd();
        });
    };

    // Token callback for streaming — starts stream on first token
    bool streamStarted = false;
    auto onToken = [&](const juce::String& token) -> bool {
        if (threadShouldExit())
            return false;
        if (!streamStarted) {
            startStreaming();
            streamStarted = true;
            wait(16);
        }
        appendToken(token);
        return true;
    };

    // Centralised conversation memory (#1402): render the active view's thread
    // and prepend it so the agent builds on earlier turns. The original message
    // is recorded verbatim once the turn completes (see the callAsync below).
    const auto convoChannel = conversationChannel(owner_.currentViewMode_);
    const std::string priorContext = owner_.conversation_.render(convoChannel);

    // Step 2: Dispatch directly from the selected surface.
    std::string dslCode;                                   // DSL from command agent
    std::vector<magda::Instruction> musicInstructions;     // IR from music agent
    std::string musicDescription;                          // description from DSL music agent
    std::vector<magda::AutoInstruction> autoInstructions;  // IR from automation agent
    std::string mixAnalysis;                               // prose from the mix-analysis agent
    std::string error;

    auto agentStart = std::chrono::steady_clock::now();

    magda::agent::ConsoleRunRequest request{
        .surface = surfaceDecision.surface,
        .userMessage = message,
        .priorConversation = priorContext,
        .midiContext = midiContext_.toStdString(),
        .drummerContext = drummerContext_.toStdString(),
        .reviseTargetClipId = reviseTargetClipId_,
    };
    auto output = owner_.agentOrchestrator_->run(
        request,
        [&](const magda::agent::ConsoleRunEvent& event) {
            if (event.type == magda::agent::ConsoleRunEventType::Token)
                onToken(event.text);
        },
        magda::agent::CancellationToken([this] { return threadShouldExit(); }));
    if (output.cancelled || threadShouldExit())
        return;
    dslCode = std::move(output.dslCode);
    musicInstructions = std::move(output.musicInstructions);
    musicDescription = std::move(output.musicDescription);
    autoInstructions = std::move(output.automationInstructions);
    mixAnalysis = std::move(output.prose);
    error = std::move(output.error);

    agentMs =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - agentStart)
            .count();
    auto totalMs =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - totalStart)
            .count();

    DBG("MAGDA Timing: agent=" + juce::String(agentMs, 0) +
        "ms, total=" + juce::String(totalMs, 0) + "ms");

    if (threadShouldExit())
        return;

    int anchor = streamAnchor->load();

    // Step 3: Execute on message thread, replacing streamed output
    juce::MessageManager::callAsync(
        [safeThis, dsl = std::move(dslCode), musicIR = std::move(musicInstructions),
         musicDesc = std::move(musicDescription), autoIR = std::move(autoInstructions),
         mixAnalysis = std::move(mixAnalysis), error = std::move(error), userMsg = message,
         channel = convoChannel, reviseTargetClipId = reviseTargetClipId_, anchor, agentMs,
         totalMs]() {
            if (!safeThis)
                return;

            // Execution runs synchronously on the message thread; a multi-track
            // fan-out can take a beat. Show the busy cursor so there's feedback
            // even while the thread is occupied (a spinner couldn't animate).
            juce::MouseCursor::showWaitCursor();

            magda::agent::ConsoleRunOutput output{
                .dslCode = std::move(dsl),
                .musicInstructions = std::move(musicIR),
                .musicDescription = std::move(musicDesc),
                .automationInstructions = std::move(autoIR),
                .prose = mixAnalysis,
                .error = std::move(error),
            };
            magda::agent::ConsoleAgentResultExecutor executor(*safeThis->magdaApi_);
            auto execution = executor.execute(std::move(output), reviseTargetClipId);
            std::string response = std::move(execution.response);
            if (execution.generatedMidiClipId != magda::INVALID_CLIP_ID)
                safeThis->rememberGeneratedMidiClip(execution.generatedMidiClipId);

            // Record the exchange into the view's conversation thread before the
            // timing footer is added, so the stored reply is clean prose the next
            // turn can build on.
            safeThis->conversation_.record(channel, userMsg, response);

            // Append timing info
            auto formatMs = [](double ms) -> std::string {
                if (ms >= 1000.0)
                    return juce::String(ms / 1000.0, 1).toStdString() + "s";
                return juce::String(ms, 0).toStdString() + "ms";
            };
            response += "\n[" + formatMs(agentMs) + " agent, " + formatMs(totalMs) + " total]";

            safeThis->stopTimer();

            auto currentText = safeThis->chatHistory_.getText();

            // Replace streamed raw output with execution results
            if (anchor >= 0 && anchor <= currentText.length()) {
                currentText = currentText.substring(0, anchor);
            } else {
                // Streaming never started — remove "Thinking..." if present
                auto thinkingPos =
                    currentText.lastIndexOf(juce::String::charToString(0x25C6) + " Thinking");
                if (thinkingPos >= 0) {
                    auto lineEnd = currentText.indexOf(thinkingPos, "\n");
                    if (lineEnd < 0)
                        lineEnd = currentText.length();
                    currentText =
                        currentText.substring(0, thinkingPos) + currentText.substring(lineEnd + 1);
                }
            }

            juce::String formattedResponse(response);
            if (formattedResponse.startsWith("Error:") ||
                formattedResponse.startsWith("DSL execution error:")) {
                formattedResponse = "[!] " + formattedResponse;
            }

            currentText += juce::String::charToString(0x25C6) + " " + formattedResponse + "\n\n";
            safeThis->chatHistory_.setText(currentText);

            // Append the mixing-agent caveat in a dim secondary colour after the
            // main response. Inserted after setText so it does not enter the plain
            // text that gets stored in conversation history (display-only).
            if (!mixAnalysis.empty() && error.empty()) {
                safeThis->chatHistory_.moveCaretToEnd();
                safeThis->chatHistory_.setColour(
                    juce::TextEditor::textColourId,
                    DarkTheme::getSecondaryTextColour().withAlpha(0.5f));
                safeThis->chatHistory_.insertTextAtCaret(
                    juce::String(magda::MixAnalysisAgent::getUserCaveat()) + "\n\n");
                safeThis->chatHistory_.setColour(juce::TextEditor::textColourId,
                                                 DarkTheme::getSecondaryTextColour());
            }

            safeThis->chatHistory_.moveCaretToEnd();
            safeThis->inputBox_->setEnabled(true);
            safeThis->processing_ = false;
            safeThis->restoreSendIcon();
            safeThis->inputBox_->grabKeyboardFocus();
            juce::MouseCursor::hideWaitCursor();
        });
}

// ============================================================================
// AIChatConsoleContent
// ============================================================================

AIChatConsoleContent::AIChatConsoleContent() {
    setName("AI Chat");

    // Chat history area
    auto monoFont = FontManager::getInstance().getMonoFont(13.0f);
    chatHistory_.setMultiLine(true);
    chatHistory_.setReadOnly(true);
    chatHistory_.setFont(monoFont);
    chatHistory_.setColour(juce::TextEditor::backgroundColourId, juce::Colours::transparentBlack);
    chatHistory_.setColour(juce::TextEditor::textColourId, DarkTheme::getSecondaryTextColour());
    chatHistory_.setColour(juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    chatHistory_.setColour(juce::TextEditor::focusedOutlineColourId,
                           juce::Colours::transparentBlack);
    chatHistory_.setColour(juce::TextEditor::highlightColourId, juce::Colours::transparentBlack);
    chatHistory_.setText(juce::String::charToString(0x25C6) + " MAGDA\n\n");
    addAndMakeVisible(chatHistory_);

    // Input box: CodeEditorComponent driven by ChatPromptTokeniser so
    // @plugin / @plugin.param / /command get coloured automatically. Same
    // affordance the DSL panel uses, just with a different tokeniser. Enter
    // is intercepted in keyPressed() to send the message; the document
    // listener replaces TextEditor::onTextChange for autocomplete triggering.
    inputBox_ = std::make_unique<juce::CodeEditorComponent>(inputDocument_, &inputTokeniser_);
    inputBox_->setFont(monoFont);
    inputBox_->setLineNumbersShown(false);
    inputBox_->setScrollbarThickness(8);
    // CodeEditorComponent forces setOpaque(true) in its own constructor, so a
    // transparent fill here is undefined depending on how the platform clips
    // paint-behind-opaque-child (JUCE's TextEditor, by contrast, never
    // opts into opaque and composites fine with a transparent fill). Paint
    // the same solid colour as the panel drawn behind it in paint() instead
    // of relying on transparency — matches dslEditor_'s approach below.
    inputBox_->setColour(juce::CodeEditorComponent::backgroundColourId,
                         DarkTheme::getColour(DarkTheme::BUTTON_NORMAL));
    inputBox_->setColour(juce::CodeEditorComponent::defaultTextColourId,
                         DarkTheme::getTextColour());
    inputBox_->setColour(juce::CodeEditorComponent::lineNumberBackgroundId,
                         juce::Colours::transparentBlack);
    inputBox_->setColour(juce::CodeEditorComponent::highlightColourId,
                         DarkTheme::getColour(DarkTheme::ACCENT_PRIMARY).withAlpha(0.3f));
    inputBox_->setColour(juce::CaretComponent::caretColourId, DarkTheme::getTextColour());
    inputDocument_.addListener(this);
    addAndMakeVisible(*inputBox_);

    // Register key listener for autocomplete navigation + Enter/Esc handling.
    inputBox_->addKeyListener(this);

    // Load context icons
    trackIconDrawable_ =
        juce::Drawable::createFromImageData(BinaryData::track_svg, BinaryData::track_svgSize);
    clipIconDrawable_ =
        juce::Drawable::createFromImageData(BinaryData::clip_svg, BinaryData::clip_svgSize);
    drumIconDrawable_ = juce::Drawable::createFromImageData(BinaryData::drum_grid_svg,
                                                            BinaryData::drum_grid_svgSize);
    // View-context glyphs (#1402), matching the footer view switcher.
    sessionIconDrawable_ = juce::Drawable::createFromImageData(
        BinaryData::iconsessionboldm_svg, BinaryData::iconsessionboldm_svgSize);
    arrangeIconDrawable_ = juce::Drawable::createFromImageData(
        BinaryData::iconarrangementboldm_svg, BinaryData::iconarrangementboldm_svgSize);
    mixIconDrawable_ = juce::Drawable::createFromImageData(BinaryData::iconmixboldm_svg,
                                                           BinaryData::iconmixboldm_svgSize);
    currentViewMode_ = magda::ViewModeController::getInstance().getViewMode();
    magda::ViewModeController::getInstance().addListener(this);

    // Context label (always visible, inside bottom bar)
    contextLabel_.setFont(FontManager::getInstance().getMonoFont(11.0f));
    contextLabel_.setColour(juce::Label::textColourId, DarkTheme::getSecondaryTextColour());
    contextLabel_.setColour(juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    contextLabel_.setColour(juce::Label::outlineColourId, juce::Colours::transparentBlack);
    contextLabel_.setBorderSize(juce::BorderSize<int>(0, 2, 0, 4));
    contextLabel_.setInterceptsMouseClicks(true, false);
    contextLabel_.addMouseListener(this, false);
    contextLabel_.setMouseCursor(juce::MouseCursor::PointingHandCursor);
    contextLabel_.setTooltip("Choose MIDI clips to include as agent context");
    addAndMakeVisible(contextLabel_);

    outputModeButton_.setLookAndFeel(&SmallButtonLookAndFeel::getInstance());
    outputModeButton_.setColour(juce::TextButton::buttonColourId,
                                DarkTheme::getColour(DarkTheme::BUTTON_NORMAL));
    outputModeButton_.setColour(juce::TextButton::buttonOnColourId,
                                DarkTheme::getAccentColour().withAlpha(0.28f));
    outputModeButton_.setColour(juce::TextButton::textColourOffId,
                                DarkTheme::getSecondaryTextColour());
    outputModeButton_.setColour(juce::TextButton::textColourOnId, DarkTheme::getAccentColour());
    outputModeButton_.setMouseCursor(juce::MouseCursor::PointingHandCursor);
    outputModeButton_.onClick = [this]() {
        if (midiOutputMode_ == MidiOutputMode::ReviseLast) {
            midiOutputMode_ = MidiOutputMode::NewClip;
        } else if (isLastGeneratedMidiClipValid()) {
            midiOutputMode_ = MidiOutputMode::ReviseLast;
            contextEnabled_ = true;
            magda::dsl::Interpreter::setContextEnabled(true);
        }
        updateOutputModeButton();
    };
    addAndMakeVisible(outputModeButton_);
    updateOutputModeButton();

    // Mix analysis is gathered from the mixer's Analyze button now (#886); this
    // panel observes MixAnalysisService so it can show a "mix analysis ready" chip
    // in mixer view and use the latest measurement as agent context.
    analysisChip_.setJustificationType(juce::Justification::centredLeft);
    analysisChip_.setFont(juce::Font(11.0f));
    analysisChip_.setColour(juce::Label::textColourId,
                            DarkTheme::getColour(DarkTheme::ACCENT_INFO));
    analysisChip_.setInterceptsMouseClicks(false, false);
    addChildComponent(analysisChip_);
    magda::MixAnalysisService::getInstance().addListener(this);

    // Send button (embedded in bottom bar) — SVG icon
    setThemedButtonIcon(sendButton_, BinaryData::enter_svg, BinaryData::enter_svgSize);
    sendButton_.setEdgeIndent(5);
    sendButton_.setColour(juce::DrawableButton::backgroundColourId,
                          juce::Colours::transparentBlack);
    sendButton_.setColour(juce::DrawableButton::backgroundOnColourId,
                          juce::Colours::transparentBlack);
    sendButton_.setMouseCursor(juce::MouseCursor::PointingHandCursor);
    sendButton_.setAlpha(0.35f);
    sendButton_.onClick = [this]() {
        if (processing_) {
            cancelRequest();
            return;
        }
        auto text = inputDocument_.getAllContent().trim();
        if (text.isNotEmpty())
            sendMessage(text);
    };
    addAndMakeVisible(sendButton_);

    // Clear chat button
    setThemedButtonIcon(clearButton_, BinaryData::delete_svg, BinaryData::delete_svgSize);
    clearButton_.setEdgeIndent(4);
    clearButton_.setColour(juce::DrawableButton::backgroundColourId,
                           juce::Colours::transparentBlack);
    clearButton_.setColour(juce::DrawableButton::backgroundOnColourId,
                           juce::Colours::transparentBlack);
    clearButton_.setMouseCursor(juce::MouseCursor::PointingHandCursor);
    clearButton_.setTooltip("Clear chat");
    clearButton_.setAlpha(0.35f);
    clearButton_.onClick = [this]() {
        chatHistory_.setText(juce::String::charToString(0x25C6) + " MAGDA\n\n");
        // The visible chat is one shared buffer across views, so clearing it
        // wipes every view's conversation memory too.
        conversation_.clearAll();
    };
    addAndMakeVisible(clearButton_);

    // Copy chat button
    setThemedButtonIcon(copyButton_, BinaryData::copycontent_svg, BinaryData::copycontent_svgSize);
    copyButton_.setEdgeIndent(4);
    copyButton_.setColour(juce::DrawableButton::backgroundColourId,
                          juce::Colours::transparentBlack);
    copyButton_.setColour(juce::DrawableButton::backgroundOnColourId,
                          juce::Colours::transparentBlack);
    copyButton_.setMouseCursor(juce::MouseCursor::PointingHandCursor);
    copyButton_.setTooltip("Copy chat to clipboard");
    copyButton_.setAlpha(0.35f);
    copyButton_.onClick = [this]() {
        juce::SystemClipboard::copyTextToClipboard(chatHistory_.getText());
    };
    addAndMakeVisible(copyButton_);

    // Tab buttons (MAGDA flat tab style)
    setupTabButtons();

    // DSL output area
    dslOutput_.setMultiLine(true);
    dslOutput_.setReadOnly(true);
    dslOutput_.setFont(FontManager::getInstance().getMonoFont(12.0f));
    dslOutput_.setColour(juce::TextEditor::backgroundColourId, juce::Colours::transparentBlack);
    dslOutput_.setColour(juce::TextEditor::textColourId, juce::Colour(0xff88ff88));
    dslOutput_.setColour(juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    dslOutput_.setColour(juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
    dslOutput_.setText("MAGDA DSL Console\nCtrl+Enter to execute.\n\n");

    // DSL code editor
    dslEditor_ = std::make_unique<juce::CodeEditorComponent>(dslDocument_, &dslTokeniser_);
    dslEditor_->setFont(FontManager::getInstance().getMonoFont(13.0f));
    dslEditor_->setColour(juce::CodeEditorComponent::backgroundColourId,
                          DarkTheme::getColour(DarkTheme::BUTTON_NORMAL));
    dslEditor_->setColour(juce::CodeEditorComponent::lineNumberBackgroundId,
                          juce::Colour(0xff252526));
    dslEditor_->setColour(juce::CodeEditorComponent::lineNumberTextId, juce::Colour(0xff858585));
    dslEditor_->setColour(juce::CaretComponent::caretColourId, juce::Colour(0xff88ff88));
    dslEditor_->setColour(juce::CodeEditorComponent::highlightColourId, juce::Colour(0xff264f78));
    dslEditor_->setLineNumbersShown(true);
    dslEditor_->setTabSize(2, true);
    dslEditor_->setScrollbarThickness(8);
    dslEditor_->addKeyListener(this);

    // DSL status bar
    dslStatusLabel_.setFont(FontManager::getInstance().getUIFont(11.0f));
    dslStatusLabel_.setColour(juce::Label::backgroundColourId, juce::Colour(0xff007acc));
    dslStatusLabel_.setColour(juce::Label::textColourId, juce::Colours::white);
#if JUCE_MAC
    dslStatusLabel_.setText("  MAGDA DSL  |  Cmd+Enter: Run  |  Cmd+L: Clear",
                            juce::dontSendNotification);
#else
    dslStatusLabel_.setText("  MAGDA DSL  |  Ctrl+Enter: Run  |  Ctrl+L: Clear",
                            juce::dontSendNotification);
#endif

    // DSL components start hidden
    dslOutput_.setVisible(false);
    dslEditor_->setVisible(false);
    dslStatusLabel_.setVisible(false);
    addChildComponent(dslOutput_);
    addChildComponent(*dslEditor_);
    addChildComponent(dslStatusLabel_);

    // Config status bar
    configStatusLabel_.setFont(FontManager::getInstance().getMonoFont(10.0f));
    configStatusLabel_.setColour(juce::Label::textColourId,
                                 DarkTheme::getSecondaryTextColour().withAlpha(0.6f));
    configStatusLabel_.setColour(juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    configStatusLabel_.setJustificationType(juce::Justification::centredLeft);
    // Clickable: opens AI Settings so the footer both reports and edits config.
    configStatusLabel_.setInterceptsMouseClicks(true, false);
    configStatusLabel_.addMouseListener(this, false);
    configStatusLabel_.setMouseCursor(juce::MouseCursor::PointingHandCursor);
    configStatusLabel_.setTooltip("Open AI Settings");
    addAndMakeVisible(configStatusLabel_);

    // Model load/unload button (shown only for local_embedded preset)
    serverToggleButton_ = std::make_unique<magda::SvgButton>(
        "ModelToggle", BinaryData::server_play_svg, BinaryData::server_play_svgSize);
    serverToggleButton_->onClick = [this]() {
        auto& mgr = magda::LlamaModelManager::getInstance();
        if (mgr.isLoaded()) {
            mgr.unloadModel();
            updateConfigStatus();
        } else {
            auto& config = magda::Config::getInstance();
            auto modelPath = config.getLocalModelPath();
            if (modelPath.empty()) {
                configStatusLabel_.setText("No model path configured", juce::dontSendNotification);
                configStatusLabel_.setColour(juce::Label::textColourId, juce::Colours::red);
                return;
            }
            magda::LlamaModelManager::Config cfg;
            cfg.modelPath = modelPath;
            cfg.gpuLayers = config.getLocalLlamaGpuLayers();
            cfg.contextSize = config.getLocalLlamaContextSize();
            configStatusLabel_.setText("Loading model...", juce::dontSendNotification);
            configStatusLabel_.setColour(juce::Label::textColourId, juce::Colours::yellow);
            std::thread([this, cfg]() {
                std::string errorMessage;
                bool ok = magda::LlamaModelManager::getInstance().loadModel(cfg, &errorMessage);
                juce::MessageManager::callAsync([this, ok, errorMessage]() {
                    if (ok) {
                        updateConfigStatus();
                        return;
                    }

                    DBG("Console: failed to load local model");
                    configStatusLabel_.setText(errorMessage.empty()
                                                   ? juce::String("Failed to load local model")
                                                   : juce::String(errorMessage),
                                               juce::dontSendNotification);
                    configStatusLabel_.setColour(juce::Label::textColourId, juce::Colours::red);
                });
            }).detach();
        }
    };
    addChildComponent(*serverToggleButton_);  // hidden by default

    updateConfigStatus();

    // Register for selection changes and seed the context bar from the
    // currently-selected track/clip. Without this, opening the panel after
    // an existing selection leaves the context bar empty until the next
    // selection event fires.
    {
        auto& sm = magda::SelectionManager::getInstance();
        sm.addListener(this);
        if (auto clipId = sm.getSelectedClip(); clipId != magda::INVALID_CLIP_ID) {
            clipSelectionChanged(clipId);
        } else if (auto trackId = sm.getSelectedTrack(); trackId != magda::INVALID_TRACK_ID) {
            trackSelectionChanged(trackId);
        }
    }

    // Register for project lifecycle events
    magda::ProjectManager::getInstance().addListener(this);

    // Register for config changes (e.g. preset changed in settings dialog)
    magda::Config::getInstance().addListener(this);
    magda::PluginPreferences::getInstance().addListener(this);

    // Prefer the engine's MagdaApi (avoids a redundant facade). Fall back
    // to owning one if the engine is unreachable so magdaApi_ is never
    // null — every dereference site below ( *safeThis->magdaApi_ etc. )
    // assumes a live api.
    if (auto* engine = dynamic_cast<magda::TracktionEngineWrapper*>(
            magda::TrackManager::getInstance().getAudioEngine())) {
        magdaApi_ = &engine->getMagdaApi();
    } else {
        ownedApi_ = std::make_unique<magda::MagdaApiLive>();
        magdaApi_ = ownedApi_.get();
    }

    // Create agents
    commandAgent_ = std::make_unique<magda::CommandAgent>(*magdaApi_);
    musicAgent_ = std::make_unique<magda::MusicAgent>();
    drummerAgent_ = std::make_unique<magda::DrummerAgent>();
    automationAgent_ = std::make_unique<magda::AutomationAgent>(*magdaApi_);
    agentOrchestrator_ = std::make_unique<magda::agent::ConsoleAgentOrchestrator>(
        *commandAgent_, *musicAgent_, *automationAgent_, *drummerAgent_);
    controllerAgent_ = std::make_unique<magda::ControllerProfileAgent>();
    fourOscAgent_ = std::make_unique<magda::FourOscAgent>();
    themeAgent_ = std::make_unique<magda::ThemeAgent>();
}

AIChatConsoleContent::~AIChatConsoleContent() {
    magda::MixAnalysisService::getInstance().removeListener(this);
    magda::ViewModeController::getInstance().removeListener(this);
    outputModeButton_.setLookAndFeel(nullptr);
    if (dslEditor_)
        dslEditor_->removeKeyListener(this);
    if (inputBox_) {
        inputDocument_.removeListener(this);
        inputBox_->removeKeyListener(this);
    }
    autocompletePopup_.reset();
    magda::PluginPreferences::getInstance().removeListener(this);
    magda::Config::getInstance().removeListener(this);
    magda::ProjectManager::getInstance().removeListener(this);
    magda::SelectionManager::getInstance().removeListener(this);
    stopTimer();

    // Signal cancellation
    shouldStop_ = true;
    if (agentOrchestrator_)
        agentOrchestrator_->cancel();
    if (controllerAgent_)
        controllerAgent_->requestCancel();
    if (themeAgent_)
        themeAgent_->requestCancel();

    // Stop the background thread with a timeout
    if (requestThread_) {
        requestThread_->signalThreadShouldExit();
        if (!requestThread_->stopThread(5000))
            DBG("AIChatConsole: Warning - request thread did not stop within timeout");
        requestThread_.reset();
    }

    if (controllerThread_) {
        controllerThread_->signalThreadShouldExit();
        if (!controllerThread_->stopThread(5000))
            DBG("AIChatConsole: Warning - controller thread did not stop within timeout");
        controllerThread_.reset();
    }

    if (themeThread_) {
        themeThread_->signalThreadShouldExit();
        if (!themeThread_->stopThread(5000))
            DBG("AIChatConsole: Warning - theme thread did not stop within timeout");
        themeThread_.reset();
    }
}

juce::String AIChatConsoleContent::resolveAliases(const juce::String& text) {
    if (allAliases_.empty())
        buildAliasList();

    // Sort by alias length descending to avoid prefix collisions
    // (e.g. @pro matching inside @pro_q_3)
    auto sorted = allAliases_;
    std::sort(sorted.begin(), sorted.end(),
              [](const auto& a, const auto& b) { return a.alias.length() > b.alias.length(); });

    // Convert @alias to <alias> token format for the LLM — resolved at DSL execution time
    auto resolved = text;
    for (const auto& entry : sorted) {
        auto token = "@" + entry.alias;
        if (resolved.contains(token))
            resolved = resolved.replace(token, "<" + entry.alias + ">");
    }
    return resolved;
}

juce::String AIChatConsoleContent::rewriteSlashCommand(const juce::String& text) {
    auto trimmed = text.trimStart();

    // /agent <surface> <request> — retain an @surface prefix for deterministic
    // routing after the slash registry accepts this escape hatch.
    if (trimmed.startsWithIgnoreCase("/agent ")) {
        const auto invocation = magda::daw::ui::SlashCommandRegistry::parse(trimmed);
        auto tokens = juce::StringArray::fromTokens(invocation.positional, false);
        if (tokens.size() >= 2) {
            const auto surface = tokens[0];
            tokens.remove(0);
            return "@" + surface + " " + tokens.joinIntoString(" ");
        }
    }

    // /groove <request> — constrain LLM to groove/swing template operations only
    if (trimmed.startsWithIgnoreCase("/groove ")) {
        auto request = trimmed.substring(8).trim();
        return "[COMMAND: GROOVE] The user wants to create or apply a SWING/GROOVE TEMPLATE "
               "(timing feel that shifts note playback timing). "
               "You MUST use ONLY groove.new(), groove.set(), groove.extract(), or groove.list() "
               "commands. Do NOT create tracks, clips, or notes. "
               "User request: " +
               request;
    }

    return text;
}

void AIChatConsoleContent::sendMessage(const juce::String& text) {
    // Direct DSL execution — bypass AI entirely. Kept outside the slash
    // command registry because /dsl isn't a "command with --help" — the
    // payload is arbitrary DSL code, not a fixed argument set.
    if (text.trimStart().startsWith("/dsl ")) {
        auto dslCode = text.trimStart().substring(5).trim();
        appendToChat(juce::String::charToString(0x25CF) + " " + text);

        magda::dsl::Interpreter interpreter(*magdaApi_);
        bool success = interpreter.execute(dslCode.toRawUTF8());

        if (success) {
            auto results = interpreter.getResults();
            if (results.isEmpty())
                results = "OK";
            appendToChat(juce::String::charToString(0x25C6) + " " + results);
        } else {
            appendToChat(juce::String::charToString(0x25C6) +
                         " Error: " + juce::String(interpreter.getError()));
        }
        clearInput();
        return;
    }

    // Other slash commands — dispatch through the central registry. Returns
    // true when the message was fully consumed (intercepting commands like
    // /controller, /design, or any meta-flag like --help). Returns false
    // when the message should continue down the normal AI path (e.g.
    // /groove, whose handler returns false so rewriteSlashCommand below
    // can transform the prompt before sending it to the LLM).
    if (!slashRegistry_)
        buildSlashCommands();
    if (slashRegistry_->dispatch(text)) {
        clearInput();
        return;
    }

    // If a previous request thread is still around, stop it before starting a new one
    if (requestThread_ && requestThread_->isThreadRunning()) {
        if (agentOrchestrator_)
            agentOrchestrator_->cancel();
        requestThread_->signalThreadShouldExit();
        if (!requestThread_->stopThread(2000))
            DBG("AIChatConsole: Warning - previous request thread did not stop within timeout");
        requestThread_.reset();
    }

    // Resolve @alias mentions to real plugin names before sending to the LLM
    auto resolvedText = resolveAliases(text);

    // Slash-command prefixes: rewrite user message with LLM context hints
    resolvedText = rewriteSlashCommand(resolvedText);

    processing_ = true;
    clearInput();
    inputBox_->setEnabled(false);

    // Swap send button to stop icon
    setThemedButtonIcon(sendButton_, BinaryData::stop_svg, BinaryData::stop_svgSize);
    sendButton_.setAlpha(0.6f);

    appendToChat(juce::String::charToString(0x25CF) + " " + text);
    appendToChat(juce::String::charToString(0x25C6) + " Thinking");

    // Reset cancel state and start new request
    shouldStop_ = false;
    if (agentOrchestrator_)
        agentOrchestrator_->resetCancellation();

    pruneMidiContextSelection();
    updateOutputModeButton();
    const auto reviseTargetClipId =
        midiOutputMode_ == MidiOutputMode::ReviseLast && isLastGeneratedMidiClipValid()
            ? lastGeneratedMidiClipId_
            : magda::INVALID_CLIP_ID;

    std::unordered_set<magda::ClipId> requestContextClipIds;
    if (contextEnabled_) {
        requestContextClipIds = midiContextClipIds_;
    }
    if (reviseTargetClipId != magda::INVALID_CLIP_ID)
        requestContextClipIds.insert(reviseTargetClipId);

    const std::vector<magda::ClipId> requestContextClips(requestContextClipIds.begin(),
                                                         requestContextClipIds.end());
    auto midiContext = magda::buildMidiContext(*magdaApi_, requestContextClips);
    auto drummerContext = contextEnabled_ ? formatSelectedClipsAsDrummerContext() : juce::String{};

    dotCount_ = 0;
    startTimer(400);  // Animate dots every 400ms

    requestThread_ = std::make_unique<RequestThread>(*this, resolvedText, std::move(midiContext),
                                                     std::move(drummerContext), reviseTargetClipId);
    requestThread_->startThread();
}

void AIChatConsoleContent::cancelRequest() {
    if (!processing_)
        return;

    shouldStop_ = true;
    if (agentOrchestrator_)
        agentOrchestrator_->cancel();

    if (requestThread_ && requestThread_->isThreadRunning()) {
        requestThread_->signalThreadShouldExit();
        requestThread_->stopThread(3000);
        requestThread_.reset();
    }

    stopTimer();
    processing_ = false;

    appendToChat("[cancelled]\n");
    inputBox_->setEnabled(true);
    inputBox_->grabKeyboardFocus();
    restoreSendIcon();
}

void AIChatConsoleContent::restoreSendIcon() {
    setThemedButtonIcon(sendButton_, BinaryData::enter_svg, BinaryData::enter_svgSize);
    sendButton_.setAlpha(0.35f);
}

void AIChatConsoleContent::setThemedButtonIcon(juce::DrawableButton& button, const void* svgData,
                                               std::size_t svgDataSize) {
    auto icon = juce::Drawable::createFromImageData(svgData, svgDataSize);
    if (icon)
        DarkTheme::applyToSvgIcon(*icon);
    button.setImages(icon.get());
}

void AIChatConsoleContent::lookAndFeelChanged() {
    // DrawableButton retains its own copy of the supplied drawable. Recreate
    // those copies when the active palette changes so the persistent chat
    // controls follow the same code-side SVG mapping as SvgButton.
    setThemedButtonIcon(sendButton_, processing_ ? BinaryData::stop_svg : BinaryData::enter_svg,
                        processing_ ? BinaryData::stop_svgSize : BinaryData::enter_svgSize);
    setThemedButtonIcon(clearButton_, BinaryData::delete_svg, BinaryData::delete_svgSize);
    setThemedButtonIcon(copyButton_, BinaryData::copycontent_svg, BinaryData::copycontent_svgSize);

    // The chat/input editors and footer labels all capture a resolved
    // juce::Colour at construction, so re-apply them here or they keep the old
    // palette after a live theme change. updateConfigStatus() re-applies
    // configStatusLabel_'s state colour.
    chatHistory_.setColour(juce::TextEditor::textColourId, DarkTheme::getSecondaryTextColour());
    if (inputBox_ != nullptr) {
        inputBox_->setColour(juce::CodeEditorComponent::backgroundColourId,
                             DarkTheme::getColour(DarkTheme::BUTTON_NORMAL));
        inputBox_->setColour(juce::CodeEditorComponent::defaultTextColourId,
                             DarkTheme::getTextColour());
        inputBox_->setColour(juce::CodeEditorComponent::highlightColourId,
                             DarkTheme::getColour(DarkTheme::ACCENT_PRIMARY).withAlpha(0.3f));
        inputBox_->setColour(juce::CaretComponent::caretColourId, DarkTheme::getTextColour());
    }
    contextLabel_.setColour(juce::Label::textColourId, DarkTheme::getSecondaryTextColour());
    analysisChip_.setColour(juce::Label::textColourId,
                            DarkTheme::getColour(DarkTheme::ACCENT_INFO));
    updateConfigStatus();

    repaint();
}

void AIChatConsoleContent::timerCallback() {
    if (!processing_) {
        stopTimer();
        return;
    }

    dotCount_ = (dotCount_ % 3) + 1;
    juce::String dots;
    for (int i = 0; i < dotCount_; ++i)
        dots += ".";

    // Update the "Thinking" line in the chat history
    auto currentText = chatHistory_.getText();
    auto thinkingPos = currentText.lastIndexOf(juce::String::charToString(0x25C6) + " Thinking");
    if (thinkingPos >= 0) {
        auto lineEnd = currentText.indexOf(thinkingPos, "\n");
        if (lineEnd < 0)
            lineEnd = currentText.length();
        chatHistory_.setText(currentText.substring(0, thinkingPos) +
                             juce::String::charToString(0x25C6) + " Thinking" + dots +
                             currentText.substring(lineEnd));
        chatHistory_.moveCaretToEnd();
    }
}

void AIChatConsoleContent::appendToChat(const juce::String& text) {
    chatHistory_.moveCaretToEnd();
    chatHistory_.insertTextAtCaret(text + "\n");
}

void AIChatConsoleContent::paint(juce::Graphics& g) {
    g.fillAll(DarkTheme::getPanelBackgroundColour());

    if (activeTab_ == ConsoleTab::AI) {
        // Draw chat history + status footer as one rounded panel
        auto chatBounds = chatHistory_.getBounds().toFloat();
        auto statusBounds = configStatusLabel_.getBounds().toFloat();
        auto chatPanel = chatBounds.getUnion(statusBounds);
        g.setColour(DarkTheme::getColour(DarkTheme::BUTTON_NORMAL));
        g.fillRoundedRectangle(chatPanel, 4.0f);
        g.setColour(DarkTheme::getBorderColour());
        g.drawRoundedRectangle(chatPanel, 4.0f, 1.0f);

        // Separator between chat and status footer
        float sepY = chatBounds.getBottom();
        g.drawHorizontalLine(static_cast<int>(sepY), chatPanel.getX() + 1.0f,
                             chatPanel.getRight() - 1.0f);

        // Draw input box + bottom bar as one unified rounded rectangle
        auto inputBounds = inputBox_->getBounds();
        auto barBounds = bottomBarBounds_;
        auto combined = inputBounds.getUnion(barBounds).toFloat();

        g.setColour(DarkTheme::getColour(DarkTheme::BUTTON_NORMAL));
        g.fillRoundedRectangle(combined, 4.0f);
        g.setColour(DarkTheme::getBorderColour());
        g.drawRoundedRectangle(combined, 4.0f, 1.0f);

        // Thin horizontal border between input and bottom bar
        float separatorY = static_cast<float>(inputBounds.getBottom());
        g.drawHorizontalLine(static_cast<int>(separatorY), combined.getX() + 1.0f,
                             combined.getRight() - 1.0f);

        // Draw context icon
        {
            // #1402: the context glyph reflects the active view, not selection.
            juce::Drawable* icon = nullptr;
            switch (currentViewMode_) {
                case magda::ViewMode::Live:
                    icon = sessionIconDrawable_.get();
                    break;
                case magda::ViewMode::Arrange:
                    icon = arrangeIconDrawable_.get();
                    break;
                case magda::ViewMode::Mix:
                case magda::ViewMode::Master:
                    icon = mixIconDrawable_.get();
                    break;
            }

            if (icon) {
                auto iconBounds = contextIconBounds_.toFloat().reduced(6.0f);
                auto colour = contextEnabled_ ? DarkTheme::getAccentColour()
                                              : DarkTheme::getSecondaryTextColour().withAlpha(0.3f);
                static const auto svgGrey = juce::Colour(0xFFB3B3B3);
                static const auto svgWhite = juce::Colours::white;
                auto iconCopy = icon->createCopy();
                iconCopy->replaceColour(svgGrey, colour);
                iconCopy->replaceColour(svgWhite, colour);
                // The footer view glyphs use fill="currentColor", which JUCE
                // resolves to black. Recolour that too (matches SvgButton) or
                // the icon renders dark regardless of the grey/white swaps.
                iconCopy->replaceColour(juce::Colours::black, colour);
                iconCopy->replaceColour(juce::Colour(0xFF000000), colour);
                iconCopy->drawWithin(g, iconBounds, juce::RectanglePlacement::centred, 1.0f);
            }
        }

    } else {
        // Draw DSL output area as rounded panel
        auto outputBounds = dslOutput_.getBounds().toFloat();
        g.setColour(DarkTheme::getColour(DarkTheme::BUTTON_NORMAL));
        g.fillRoundedRectangle(outputBounds, 4.0f);
        g.setColour(DarkTheme::getBorderColour());
        g.drawRoundedRectangle(outputBounds, 4.0f, 1.0f);
    }
}

void AIChatConsoleContent::resized() {
    auto bounds = getLocalBounds().reduced(10);

    // Tab buttons at bottom
    auto tabBar = bounds.removeFromBottom(22);
    aiTabButton_->setBounds(tabBar.removeFromLeft(28));
    tabBar.removeFromLeft(2);
    dslTabButton_->setBounds(tabBar.removeFromLeft(28));
    bounds.removeFromBottom(4);  // Spacing above tabs

    if (activeTab_ == ConsoleTab::AI) {
        // Context bar above tabs
        auto bottomBar = bounds.removeFromBottom(26);
        bottomBarBounds_ = bottomBar;

        // Footer right edge, from the right: send, mix-analysis status, then
        // the MIDI result destination control in arrangement view.
        sendButton_.setBounds(bottomBar.removeFromRight(22));
        if (analysisChip_.isVisible()) {
            bottomBar.removeFromRight(6);
            analysisChip_.setBounds(bottomBar.removeFromRight(120));
        }
        if (outputModeButton_.isVisible()) {
            bottomBar.removeFromRight(6);
            // Keep the button clear of the unified panel's separator and
            // rounded bottom border, which are painted behind child controls.
            outputModeButton_.setBounds(bottomBar.removeFromRight(82).reduced(0, 3));
        }
        contextIconBounds_ = bottomBar.removeFromLeft(22);
        contextLabel_.setBounds(bottomBar);

        // Input box directly above context bar (no gap — unified shape)
        auto inputArea = bounds.removeFromBottom(80);
        inputBox_->setBounds(inputArea);

        bounds.removeFromBottom(8);  // Spacing

        // Config status footer inside chat panel (bottom strip)
        int statusH = 26;

        chatHistory_.setBounds(bounds.withTrimmedBottom(statusH));

        // Clear + copy buttons inside chat panel, top-right corner
        auto chatBounds = chatHistory_.getBounds();
        int btnSize = 20;
        int margin = 4;
        copyButton_.setBounds(chatBounds.getRight() - btnSize - margin, chatBounds.getY() + margin,
                              btnSize, btnSize);
        clearButton_.setBounds(chatBounds.getRight() - 2 * btnSize - margin - 2,
                               chatBounds.getY() + margin, btnSize, btnSize);
        clearButton_.toFront(false);
        copyButton_.toFront(false);

        // Status bar sits below chat history, inside the same visual panel
        auto statusBar = juce::Rectangle<int>(bounds.getX(), bounds.getBottom() - statusH,
                                              bounds.getWidth(), statusH);
        statusBar.reduce(6, 2);  // Padding
        if (serverToggleButton_ && serverToggleButton_->isVisible()) {
            statusBar.removeFromRight(2);
            serverToggleButton_->setBounds(statusBar.removeFromRight(20).reduced(1));
            serverToggleButton_->toFront(false);
        }
        configStatusLabel_.setBounds(statusBar);
        configStatusLabel_.toFront(false);
    } else {
        // DSL tab layout
        bounds.removeFromBottom(4);  // Spacing above status bar
        dslStatusLabel_.setBounds(bounds.removeFromBottom(20));
        bounds.removeFromBottom(2);  // Spacing above editor
        auto editorHeight = juce::jmax(60, bounds.getHeight() / 3);
        dslEditor_->setBounds(bounds.removeFromBottom(editorHeight));
        bounds.removeFromBottom(1);  // Separator
        dslOutput_.setBounds(bounds);
    }
}

void AIChatConsoleContent::onActivated() {
    buildAliasList();
    updateConfigStatus();
    updateAnalysisChip();
    if (isShowing()) {
        if (activeTab_ == ConsoleTab::AI)
            inputBox_->grabKeyboardFocus();
        else if (dslEditor_)
            dslEditor_->grabKeyboardFocus();
    }
}

void AIChatConsoleContent::onDeactivated() {
    // Could save chat history here
}

// ============================================================================
// Tab Switching
// ============================================================================

void AIChatConsoleContent::setupTabButtons() {
    aiTabButton_ =
        std::make_unique<magda::SvgButton>("AITab", BinaryData::ai_svg, BinaryData::ai_svgSize);
    aiTabButton_->setOriginalColor(juce::Colour(0xFFB3B3B3));
    // ICON_ON_ACCENT keeps the active glyph legible on the accent chip in
    // every theme; a literal white got funnelled to TEXT_BRIGHT and vanished
    // on themes whose bright text sits near the accent.
    aiTabButton_->setActiveColor(DarkTheme::ICON_ON_ACCENT);
    aiTabButton_->setNormalBackgroundColor(DarkTheme::SURFACE);
    aiTabButton_->setActiveBackgroundColor(DarkTheme::ACCENT_PRIMARY);
    aiTabButton_->setClickingTogglesState(true);
    aiTabButton_->setRadioGroupId(9001);
    aiTabButton_->setToggleState(true, juce::dontSendNotification);
    aiTabButton_->setTooltip("AI Chat");
    aiTabButton_->onClick = [this]() { switchTab(ConsoleTab::AI); };
    addAndMakeVisible(aiTabButton_.get());

    dslTabButton_ = std::make_unique<magda::SvgButton>("DSLTab", BinaryData::script_svg,
                                                       BinaryData::script_svgSize);
    dslTabButton_->setOriginalColor(juce::Colour(0xFFB3B3B3));
    dslTabButton_->setActiveColor(DarkTheme::ICON_ON_ACCENT);
    dslTabButton_->setNormalBackgroundColor(DarkTheme::SURFACE);
    dslTabButton_->setActiveBackgroundColor(DarkTheme::ACCENT_PRIMARY);
    dslTabButton_->setClickingTogglesState(true);
    dslTabButton_->setRadioGroupId(9001);
    dslTabButton_->setTooltip("DSL Console");
    dslTabButton_->onClick = [this]() { switchTab(ConsoleTab::DSL); };
    addAndMakeVisible(dslTabButton_.get());
}

void AIChatConsoleContent::switchTab(ConsoleTab tab) {
    if (activeTab_ == tab)
        return;
    activeTab_ = tab;

    bool isAI = (tab == ConsoleTab::AI);

    // AI components
    chatHistory_.setVisible(isAI);
    inputBox_->setVisible(isAI);
    sendButton_.setVisible(isAI);
    contextLabel_.setVisible(isAI);
    clearButton_.setVisible(isAI);
    copyButton_.setVisible(isAI);
    configStatusLabel_.setVisible(isAI);
    updateOutputModeButton();

    // DSL components
    dslOutput_.setVisible(!isAI);
    dslEditor_->setVisible(!isAI);
    dslStatusLabel_.setVisible(!isAI);

    resized();
    repaint();

    if (isAI)
        inputBox_->grabKeyboardFocus();
    else
        dslEditor_->grabKeyboardFocus();
}

void AIChatConsoleContent::executeDSL() {
    auto code = dslDocument_.getAllContent().trim();
    if (code.isEmpty())
        return;

    // History
    if (dslHistory_.isEmpty() || dslHistory_.strings.getLast() != code)
        dslHistory_.add(code);
    dslHistoryIndex_ = -1;

    // Echo
    appendDSLOutput("> " + code + "\n", juce::Colour(0xff88ff88));

    // Built-in commands
    if (code == "help") {
        appendDSLOutput("MAGDA DSL Commands:\n"
                        "  track(name=\"X\")              - Reference/create track\n"
                        "  track(id=1)                  - Reference track by index\n"
                        "  .clip.new(bar=1, length_bars=4) - Create MIDI clip\n"
                        "  .fx.add(name=\"reverb\")       - Add effect\n"
                        "  .notes.add(pitch=C4, beat=0) - Add note\n"
                        "  .notes.add_chord(root=C4, quality=major)\n"
                        "  filter(tracks, ...).delete()  - Bulk operations\n\n",
                        juce::Colour(0xff569cd6));
        dslDocument_.replaceAllContent({});
        return;
    }
    if (code == "clear") {
        dslOutput_.clear();
        dslOutput_.setText("Output cleared.\n\n");
        dslDocument_.replaceAllContent({});
        return;
    }

    // Execute
    magda::dsl::Interpreter interpreter(*magdaApi_);
    bool success = interpreter.execute(code.toRawUTF8());

    if (success) {
        auto results = interpreter.getResults();
        if (results.isEmpty())
            results = "OK";
        appendDSLOutput(results + "\n\n", juce::Colour(0xffd4d4d4));
    } else {
        appendDSLOutput("Error: " + juce::String(interpreter.getError()) + "\n\n",
                        juce::Colour(0xfff48771));
    }

    dslDocument_.replaceAllContent({});
}

void AIChatConsoleContent::appendDSLOutput(const juce::String& text, juce::Colour colour) {
    dslOutput_.setColour(juce::TextEditor::textColourId, colour);
    dslOutput_.moveCaretToEnd();
    dslOutput_.insertTextAtCaret(text);
    dslOutput_.moveCaretToEnd();
}

// ============================================================================
// ProjectManagerListener
// ============================================================================

void AIChatConsoleContent::projectOpened(const magda::ProjectInfo& /*info*/) {
    // Reset chat history
    chatHistory_.setText(juce::String::charToString(0x25C6) + " MAGDA\n\n");

    // A different project means a fresh conversation; drop all view memory.
    conversation_.clearAll();
    lastGeneratedMidiClipId_ = magda::INVALID_CLIP_ID;
    midiOutputMode_ = MidiOutputMode::NewClip;
    clearMidiContext();
    updateOutputModeButton();

    // Cancel any in-flight request
    cancelRequest();
}

// ============================================================================
// ConfigListener
// ============================================================================

void AIChatConsoleContent::configChanged() {
    updateConfigStatus();
}

void AIChatConsoleContent::viewModeChanged(magda::ViewMode mode, const magda::AudioEngineProfile&) {
    if (currentViewMode_ == mode)
        return;
    currentViewMode_ = mode;
    // Reflect the view in the context glyph. The active view also scopes agent
    // routing in RequestThread::run (mixer view -> mixing agent, #1402).
    updateContextBar();
    updateOutputModeButton();
    // The mix-analysis chip is mixer-view only, so refresh it on view change.
    updateAnalysisChip();
}

void AIChatConsoleContent::externalPluginFormatPreferenceChanged(magda::PluginFormat preference) {
    juce::ignoreUnused(preference);
    buildAliasList();
    hideAutocomplete();
}

// ============================================================================
// Mix analysis context (#886) — gathered by the mixer's Analyze button, held by
// MixAnalysisService; the console surfaces it as a chip and uses it as context.
// ============================================================================

void AIChatConsoleContent::mixAnalysisChanged() {
    updateAnalysisChip();
}

void AIChatConsoleContent::updateAnalysisChip() {
    const bool mixerView =
        (currentViewMode_ == magda::ViewMode::Mix || currentViewMode_ == magda::ViewMode::Master);
    const bool haveAnalysis = magda::MixAnalysisService::getInstance().latest().has_value();
    const bool show = mixerView && haveAnalysis;
    if (show)
        analysisChip_.setText(juce::String::charToString(0x25C6) + " mix analysis ready",
                              juce::dontSendNotification);
    if (analysisChip_.isVisible() != show) {
        analysisChip_.setVisible(show);
        resized();  // reflow the footer
    }
}

// ============================================================================
// SelectionManagerListener
// ============================================================================

void AIChatConsoleContent::selectionTypeChanged(magda::SelectionType newType) {
    if (newType == magda::SelectionType::None) {
        contextText_.clear();
        contextIcon_ = ContextIcon::None;
        updateContextBar();
    }
    updateAnalysisChip();
}

namespace {
// Track is drummer-targetable when its primary instrument carries a kit with
// at least one role-tagged row. A kit-less instrument or rows that only have
// labels don't qualify — the agent needs roles to address rows by symbol.
bool isDrummerTrack(magda::TrackId trackId) {
    if (trackId == magda::INVALID_TRACK_ID)
        return false;
    const auto* device = magda::TrackManager::getInstance().getPrimaryInstrument(trackId);
    if (device == nullptr)
        return false;
    for (const auto& row : device->kitRows) {
        if (row.role.isNotEmpty())
            return true;
    }
    return false;
}

// Format an existing drum clip's notes back into the grid grammar the agent
// emits, so we can hand the current pattern to the LLM as input context for
// follow-up prompts ("add a fill", "make the hats busier", etc.). Returns an
// empty string when there's no clip / no notes / no instrument with a kit.
//
// Resolution is hard-coded to 16ths in 4/4. Notes that don't land on a 16th
// cell are quantised to the nearest one; off-grid playing detail isn't
// preserved (it can't survive a round-trip into role-grid form anyway).
juce::String formatClipAsDrummerContext(magda::ClipId clipId) {
    if (clipId == magda::INVALID_CLIP_ID)
        return {};
    const auto* clip = magda::ClipManager::getInstance().getClip(clipId);
    if (clip == nullptr || !clip->isMidi() || clip->midiNotes.empty())
        return {};
    const auto* device = magda::TrackManager::getInstance().getPrimaryInstrument(clip->trackId);
    if (device == nullptr)
        return {};

    double tempo = 120.0;
    if (auto* controller = magda::TimelineController::getCurrent())
        tempo = controller->getState().tempo.bpm;
    const double lengthBeats = clip->getLengthInBeats(tempo);

    constexpr double kBarBeats = 4.0;
    constexpr int kCellsPerBar = 16;
    constexpr double kCellBeats = kBarBeats / static_cast<double>(kCellsPerBar);
    const int numBars = juce::jmax(1, static_cast<int>(std::ceil(lengthBeats / kBarBeats)));
    const int totalCells = numBars * kCellsPerBar;

    juce::String header = "Current pattern (" + juce::String(numBars) + " bar" +
                          (numBars > 1 ? juce::String("s") : juce::String()) + ", " +
                          juce::String(kCellsPerBar) + " cells per bar):\n";

    juce::String rows;
    for (const auto& roleInfo : magda::daw::audio::drum_grid_roles::kRoles) {
        const juce::String roleId(roleInfo.id);
        std::vector<char> cells(static_cast<size_t>(totalCells), '.');
        bool hasAny = false;
        for (const auto& note : clip->midiNotes) {
            // role lookup: which kit row covers this note's MIDI number
            juce::String noteRole;
            for (const auto& row : device->kitRows) {
                if (row.noteNumber == note.noteNumber) {
                    noteRole = row.role;
                    break;
                }
            }
            if (noteRole != roleId)
                continue;
            int cellIdx = static_cast<int>(std::floor(note.startBeat / kCellBeats + 0.5));
            if (cellIdx < 0 || cellIdx >= totalCells)
                continue;
            cells[static_cast<size_t>(cellIdx)] = (note.velocity >= 100) ? 'X' : 'x';
            hasAny = true;
        }
        if (!hasAny)
            continue;

        juce::String line(roleInfo.shortTag);
        line += " | ";
        for (int bar = 0; bar < numBars; ++bar) {
            if (bar > 0)
                line += " | ";
            for (int i = 0; i < kCellsPerBar; ++i) {
                if (i > 0)
                    line += " ";
                line +=
                    juce::String::charToString(cells[static_cast<size_t>(bar * kCellsPerBar + i)]);
            }
        }
        line += "\n";
        rows += line;
    }

    if (rows.isEmpty())
        return {};
    return header + rows;
}

std::vector<magda::ClipId> getSelectedDrummerContextClipIds() {
    auto& selection = magda::SelectionManager::getInstance();
    std::vector<magda::ClipId> ids;

    const auto& selectedClips = selection.getSelectedClips();
    ids.reserve(selectedClips.size());
    for (auto clipId : selectedClips) {
        const auto* clip = magda::ClipManager::getInstance().getClip(clipId);
        if (clip != nullptr && clip->isMidi() && isDrummerTrack(clip->trackId))
            ids.push_back(clipId);
    }

    if (ids.empty()) {
        auto clipId = selection.getSelectedClip();
        const auto* clip = magda::ClipManager::getInstance().getClip(clipId);
        if (clip != nullptr && clip->isMidi() && isDrummerTrack(clip->trackId))
            ids.push_back(clipId);
    }

    std::sort(ids.begin(), ids.end(), [](auto a, auto b) {
        const auto* clipA = magda::ClipManager::getInstance().getClip(a);
        const auto* clipB = magda::ClipManager::getInstance().getClip(b);
        if (clipA == nullptr || clipB == nullptr)
            return a < b;
        if (clipA->trackId != clipB->trackId)
            return clipA->trackId < clipB->trackId;
        if (clipA->placement.startBeat != clipB->placement.startBeat)
            return clipA->placement.startBeat < clipB->placement.startBeat;
        return a < b;
    });

    return ids;
}

juce::String formatSelectedClipsAsDrummerContext() {
    auto clipIds = getSelectedDrummerContextClipIds();
    juce::String context;

    int index = 1;
    for (auto clipId : clipIds) {
        auto clipContext = formatClipAsDrummerContext(clipId);
        if (clipContext.isEmpty())
            continue;

        const auto* clip = magda::ClipManager::getInstance().getClip(clipId);
        if (!context.isEmpty())
            context += "\n";
        if (clipIds.size() > 1 && clip != nullptr) {
            context += "Selected clip " + juce::String(index) + ": " + clip->name + "\n";
        }
        context += clipContext;
        ++index;
    }

    return context.trim();
}
}  // namespace

void AIChatConsoleContent::trackSelectionChanged(magda::TrackId trackId) {
    auto* track = magda::TrackManager::getInstance().getTrack(trackId);
    auto trackName = track != nullptr ? track->name : juce::String(trackId);
    drummerModeActive_ = isDrummerTrack(trackId);
    if (drummerModeActive_) {
        contextText_ = juce::String::fromUTF8("Drummer \xc2\xb7 ") + trackName;
        contextIcon_ = ContextIcon::Drummer;
    } else {
        contextText_ = trackName;
        contextIcon_ = ContextIcon::Track;
    }
    updateContextBar();
}

void AIChatConsoleContent::multiTrackSelectionChanged(
    const std::unordered_set<magda::TrackId>& trackIds) {
    drummerModeActive_ = false;
    contextText_ = juce::String(static_cast<int>(trackIds.size())) + " Tracks";
    contextIcon_ = ContextIcon::Track;
    updateContextBar();
}

void AIChatConsoleContent::clipSelectionChanged(magda::ClipId clipId) {
    auto* clip = magda::ClipManager::getInstance().getClip(clipId);
    magda::TrackId trackId = magda::INVALID_TRACK_ID;
    juce::String trackName;
    if (clip != nullptr) {
        trackId = clip->trackId;
        auto* track = magda::TrackManager::getInstance().getTrack(clip->trackId);
        trackName = track != nullptr ? track->name : juce::String(clip->trackId);
        contextText_ = trackName + " > " + clip->name;
    } else {
        contextText_ = juce::String(clipId);
    }
    drummerModeActive_ = isDrummerTrack(trackId);
    if (drummerModeActive_) {
        if (clip != nullptr)
            contextText_ =
                juce::String::fromUTF8("Drummer \xc2\xb7 ") + trackName + " > " + clip->name;
        contextIcon_ = ContextIcon::Drummer;
    } else {
        contextIcon_ = ContextIcon::Clip;
    }
    updateContextBar();
}

void AIChatConsoleContent::multiClipSelectionChanged(
    const std::unordered_set<magda::ClipId>& clipIds) {
    auto contextClipIds = getSelectedDrummerContextClipIds();
    drummerModeActive_ = !contextClipIds.empty();

    if (!contextClipIds.empty()) {
        const auto* firstClip = magda::ClipManager::getInstance().getClip(contextClipIds.front());
        juce::String trackName;
        if (firstClip != nullptr) {
            auto* track = magda::TrackManager::getInstance().getTrack(firstClip->trackId);
            trackName = track != nullptr ? track->name : juce::String(firstClip->trackId);
        }

        contextText_ = juce::String::fromUTF8("Drummer \xc2\xb7 ") + trackName + " > " +
                       juce::String(static_cast<int>(contextClipIds.size())) + " clips";
        contextIcon_ = ContextIcon::Drummer;
    } else {
        contextText_ = juce::String(static_cast<int>(clipIds.size())) + " clips";
        contextIcon_ = ContextIcon::Clip;
    }

    updateContextBar();
}

void AIChatConsoleContent::chainNodeSelectionChanged(const magda::ChainNodePath& path) {
    auto* track = magda::TrackManager::getInstance().getTrack(path.trackId);
    juce::String trackName = track != nullptr ? track->name : juce::String(path.trackId);

    auto deviceId = path.getDeviceId();
    if (deviceId != magda::INVALID_DEVICE_ID) {
        auto* device = magda::TrackManager::getInstance().getDevice(path.trackId, deviceId);
        if (device != nullptr)
            contextText_ = trackName + " > " + device->name;
        else
            contextText_ = trackName + " > " + juce::String(deviceId);
    } else {
        contextText_ = trackName;
    }
    contextIcon_ = ContextIcon::Device;
    updateContextBar();
}

void AIChatConsoleContent::showMidiContextPicker() {
    pruneMidiContextSelection();
    auto popup = std::make_unique<MidiContextPopup>(*this);
    juce::CallOutBox::launchAsynchronously(std::move(popup), contextLabel_.getScreenBounds(),
                                           nullptr);
}

bool AIChatConsoleContent::isMidiContextClipSelected(magda::ClipId clipId) const {
    return midiContextClipIds_.contains(clipId);
}

void AIChatConsoleContent::toggleMidiContextClip(magda::ClipId clipId) {
    const auto* clip = magda::ClipManager::getInstance().getClip(clipId);
    if (clip == nullptr || !clip->isMidi())
        return;
    if (midiContextClipIds_.contains(clipId))
        midiContextClipIds_.erase(clipId);
    else
        midiContextClipIds_.insert(clipId);
    contextEnabled_ = true;
    magda::dsl::Interpreter::setContextEnabled(true);
    updateContextBar();
}

void AIChatConsoleContent::toggleMidiContextTrack(magda::TrackId trackId) {
    auto& clips = magda::ClipManager::getInstance();
    std::vector<magda::ClipId> midiClipIds;
    for (auto clipId : clips.getClipsOnTrack(trackId)) {
        const auto* clip = clips.getClip(clipId);
        if (clip != nullptr && clip->isMidi())
            midiClipIds.push_back(clipId);
    }
    if (midiClipIds.empty())
        return;

    const bool allSelected =
        std::all_of(midiClipIds.begin(), midiClipIds.end(),
                    [this](auto clipId) { return midiContextClipIds_.contains(clipId); });
    for (auto clipId : midiClipIds) {
        if (allSelected)
            midiContextClipIds_.erase(clipId);
        else
            midiContextClipIds_.insert(clipId);
    }
    contextEnabled_ = true;
    magda::dsl::Interpreter::setContextEnabled(true);
    updateContextBar();
}

void AIChatConsoleContent::useCurrentSelectionAsMidiContext() {
    std::unordered_set<magda::ClipId> selectedMidi;
    auto& selection = magda::SelectionManager::getInstance();
    auto& clips = magda::ClipManager::getInstance();

    const auto& noteSelection = selection.getNoteSelection();
    if (noteSelection.isValid()) {
        const auto* clip = clips.getClip(noteSelection.clipId);
        if (clip != nullptr && clip->isMidi())
            selectedMidi.insert(noteSelection.clipId);
    }

    for (auto clipId : selection.getSelectedClips()) {
        const auto* clip = clips.getClip(clipId);
        if (clip != nullptr && clip->isMidi())
            selectedMidi.insert(clipId);
    }

    if (selectedMidi.empty()) {
        auto addTrackClips = [&selectedMidi, &clips](magda::TrackId trackId) {
            for (auto clipId : clips.getClipsOnTrack(trackId)) {
                const auto* clip = clips.getClip(clipId);
                if (clip != nullptr && clip->isMidi())
                    selectedMidi.insert(clipId);
            }
        };

        const auto& selectedTracks = selection.getSelectedTracks();
        if (!selectedTracks.empty()) {
            for (auto trackId : selectedTracks)
                addTrackClips(trackId);
        } else if (selection.getSelectedTrack() != magda::INVALID_TRACK_ID) {
            addTrackClips(selection.getSelectedTrack());
        }
    }

    midiContextClipIds_ = std::move(selectedMidi);
    if (!midiContextClipIds_.empty()) {
        contextEnabled_ = true;
        magda::dsl::Interpreter::setContextEnabled(true);
    }
    updateContextBar();
}

void AIChatConsoleContent::clearMidiContext() {
    midiContextClipIds_.clear();
    updateContextBar();
}

void AIChatConsoleContent::pruneMidiContextSelection() {
    auto& clips = magda::ClipManager::getInstance();
    std::erase_if(midiContextClipIds_, [&clips](auto clipId) {
        const auto* clip = clips.getClip(clipId);
        return clip == nullptr || !clip->isMidi();
    });
}

juce::String AIChatConsoleContent::getMidiContextSummary() const {
    if (midiContextClipIds_.empty())
        return {};

    auto& tracks = magda::TrackManager::getInstance();
    auto& clips = magda::ClipManager::getInstance();
    std::unordered_set<magda::TrackId> trackIds;
    const magda::ClipInfo* onlyClip = nullptr;
    for (auto clipId : midiContextClipIds_) {
        if (const auto* clip = clips.getClip(clipId); clip != nullptr && clip->isMidi()) {
            trackIds.insert(clip->trackId);
            onlyClip = clip;
        }
    }
    if (trackIds.empty())
        return {};

    if (midiContextClipIds_.size() == 1 && onlyClip != nullptr) {
        const auto* track = tracks.getTrack(onlyClip->trackId);
        const auto trackName = track != nullptr ? track->name : "Track";
        return trackName + " > " +
               (onlyClip->name.isNotEmpty() ? onlyClip->name : juce::String("MIDI clip"));
    }

    if (trackIds.size() == 1) {
        const auto trackId = *trackIds.begin();
        const auto* track = tracks.getTrack(trackId);
        const auto trackName = track != nullptr ? track->name : "Track";
        int midiClipsOnTrack = 0;
        for (auto clipId : clips.getClipsOnTrack(trackId)) {
            const auto* clip = clips.getClip(clipId);
            midiClipsOnTrack += clip != nullptr && clip->isMidi() ? 1 : 0;
        }
        if (midiClipsOnTrack == static_cast<int>(midiContextClipIds_.size()))
            return trackName + " · all MIDI";
        return trackName + " · " + juce::String(static_cast<int>(midiContextClipIds_.size())) +
               " MIDI";
    }

    return juce::String(static_cast<int>(midiContextClipIds_.size())) + " MIDI · " +
           juce::String(static_cast<int>(trackIds.size())) + " tracks";
}

bool AIChatConsoleContent::isLastGeneratedMidiClipValid() const {
    if (lastGeneratedMidiClipId_ == magda::INVALID_CLIP_ID)
        return false;
    const auto* clip = magda::ClipManager::getInstance().getClip(lastGeneratedMidiClipId_);
    return clip != nullptr && clip->isMidi();
}

void AIChatConsoleContent::rememberGeneratedMidiClip(magda::ClipId clipId) {
    const auto* clip = magda::ClipManager::getInstance().getClip(clipId);
    if (clip == nullptr || !clip->isMidi())
        return;
    lastGeneratedMidiClipId_ = clipId;
    updateOutputModeButton();
}

void AIChatConsoleContent::updateOutputModeButton() {
    const bool haveLastResult = isLastGeneratedMidiClipValid();
    if (!haveLastResult)
        midiOutputMode_ = MidiOutputMode::NewClip;

    const bool revising = midiOutputMode_ == MidiOutputMode::ReviseLast;
    outputModeButton_.setButtonText(revising ? "Revise last" : "New clip");
    outputModeButton_.setToggleState(revising, juce::dontSendNotification);
    outputModeButton_.setEnabled(haveLastResult);
    outputModeButton_.setTooltip(
        haveLastResult ? (revising ? "Replace the most recent AI-created MIDI clip"
                                   : "Create a new MIDI clip; click to revise the last AI result")
                       : "Generate a MIDI clip before using Revise last");

    const bool shouldShow =
        activeTab_ == ConsoleTab::AI && currentViewMode_ == magda::ViewMode::Arrange;
    if (outputModeButton_.isVisible() != shouldShow) {
        outputModeButton_.setVisible(shouldShow);
        resized();
    }
}

void AIChatConsoleContent::updateContextBar() {
    pruneMidiContextSelection();
    auto displayText = getMidiContextSummary();
    if (displayText.isEmpty())
        displayText = contextText_.isNotEmpty() ? contextText_ : "Add MIDI context";
    displayText += "  " + juce::String::charToString(0x25BE);
    contextLabel_.setText(displayText, juce::dontSendNotification);
    contextLabel_.setColour(juce::Label::textColourId,
                            contextEnabled_ ? DarkTheme::getAccentColour()
                                            : DarkTheme::getSecondaryTextColour().withAlpha(0.3f));
    updateOutputModeButton();
    resized();
    repaint();
}

void AIChatConsoleContent::updateConfigStatus() {
    auto& config = magda::Config::getInstance();
    auto preset = config.getAIPreset();
    auto musicCfg = config.getAgentLLMConfig(magda::role::MUSIC);

    juce::String status;

    // Show preset/provider + model
    if (preset == "custom")
        status = "Custom";
    else {
        status = juce::String(preset).replaceCharacter('_', ' ');
        if (status.isNotEmpty())
            status = status.substring(0, 1).toUpperCase() + status.substring(1);
    }

    if (preset == magda::preset::ADVANCED) {
        // Per-agent config: each surface selects its workflow directly.
        status += juce::String::fromUTF8(" \xc2\xb7 per-agent");
    } else if (preset == magda::preset::LOCAL_SERVER) {
        auto serverModel = config.getLocalServerModel();
        status +=
            " | " + (serverModel.empty() ? juce::String("No model") : juce::String(serverModel));
    } else if (musicCfg.model.empty()) {
        status += " | Embedded";
    } else {
        status += " | " + juce::String(musicCfg.model);
    }

    // If embedded local provider, show model status + toggle button
    if (isLocalPreset() && serverToggleButton_) {
        auto& mgr = magda::LlamaModelManager::getInstance();
        if (mgr.isLoaded()) {
            auto modelName = juce::File(mgr.getLoadedModelPath()).getFileName();
            status += " | " + modelName;
            serverToggleButton_->updateSvgData(BinaryData::server_stop_svg,
                                               BinaryData::server_stop_svgSize);
            serverToggleButton_->setNormalColor(juce::Colours::limegreen);
            serverToggleButton_->setHoverColor(juce::Colours::limegreen.brighter(0.3f));
            serverToggleButton_->setVisible(true);
            configStatusLabel_.setColour(juce::Label::textColourId, juce::Colours::limegreen);
        } else {
            status += " | No model loaded";
            serverToggleButton_->updateSvgData(BinaryData::server_play_svg,
                                               BinaryData::server_play_svgSize);
            serverToggleButton_->setNormalColor(DarkTheme::getSecondaryTextColour());
            serverToggleButton_->setHoverColor(DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
            serverToggleButton_->setVisible(true);
            configStatusLabel_.setColour(juce::Label::textColourId,
                                         DarkTheme::getSecondaryTextColour());
        }
        serverToggleButton_->repaint();
    } else {
        if (serverToggleButton_)
            serverToggleButton_->setVisible(false);
        configStatusLabel_.setColour(juce::Label::textColourId,
                                     DarkTheme::getSecondaryTextColour());
    }

    configStatusLabel_.setText(status, juce::dontSendNotification);
    resized();
}

bool AIChatConsoleContent::isLocalPreset() const {
    auto& config = magda::Config::getInstance();
    auto preset = config.getAIPreset();
    auto commandCfg = config.getAgentLLMConfig(magda::role::COMMAND);
    return commandCfg.provider == magda::provider::LLAMA_LOCAL ||
           preset == magda::preset::LOCAL_EMBEDDED || preset == "local";
}

void AIChatConsoleContent::mouseUp(const juce::MouseEvent& event) {
    if (event.originalComponent == &configStatusLabel_) {
        magda::AISettingsDialog::showDialog(this);
        return;
    }
    if (event.originalComponent == &contextLabel_) {
        showMidiContextPicker();
        return;
    }
    if (event.originalComponent == this && contextIconBounds_.contains(event.getPosition())) {
        contextEnabled_ = !contextEnabled_;
        magda::dsl::Interpreter::setContextEnabled(contextEnabled_);
        updateContextBar();
    }
}

// ============================================================================
// Plugin alias autocomplete
// ============================================================================

void AIChatConsoleContent::buildAliasList() {
    allAliases_.clear();

    // Internal plugins — single source of truth in internal_plugins.hpp,
    // shared with the DSL interpreter and InstructionExecutor so the autocomplete
    // dropdown lists exactly the aliases the agent layer accepts.
    for (const auto& entry : magda::getInternalPlugins()) {
        allAliases_.push_back({entry.primaryAlias, entry.displayName});
    }

    // External plugins from KnownPluginList
    if (auto* engine = dynamic_cast<magda::TracktionEngineWrapper*>(
            magda::TrackManager::getInstance().getAudioEngine())) {
        auto types = engine->getPreferredPluginTypes();
        DBG("AIChatConsole: buildAliasList - KnownPluginList has " << types.size() << " plugins");
        for (const auto& desc : types) {
            auto alias = PluginBrowserInfo::generateAlias(desc.name);
            allAliases_.push_back({alias, desc.name});
        }
    } else {
        DBG("AIChatConsole: buildAliasList - engine not available via TrackManager");
    }

    // Load custom alias overrides
    auto aliasFile = magda::paths::pluginAliasesFile();

    if (aliasFile.existsAsFile()) {
        if (auto xml = juce::parseXML(aliasFile)) {
            for (auto* elem : xml->getChildIterator()) {
                auto key = elem->getStringAttribute("key");
                auto alias = elem->getStringAttribute("alias");
                // Find and update the matching entry
                for (auto& entry : allAliases_) {
                    // Match by uniqueId or by plugin name
                    if (entry.pluginName == key ||
                        PluginBrowserInfo::generateAlias(entry.pluginName) ==
                            PluginBrowserInfo::generateAlias(key)) {
                        entry.alias = alias;
                        break;
                    }
                }
            }
        }
    }

    // Sort by alias
    std::sort(allAliases_.begin(), allAliases_.end(),
              [](const AliasEntry& a, const AliasEntry& b) { return a.alias < b.alias; });
}

void AIChatConsoleContent::showAutocomplete(const juce::String& filter) {
    if (allAliases_.empty())
        buildAliasList();

    DBG("AIChatConsole: showAutocomplete filter=\"" << filter
                                                    << "\", total aliases=" << allAliases_.size());

    if (!autocompletePopup_) {
        autocompletePopup_ = std::make_unique<AutocompletePopup>(*this);
        addAndMakeVisible(*autocompletePopup_);
    }

    auto inputBounds = inputBox_->getBounds();
    int popupWidth = inputBounds.getWidth();
    autocompletePopup_->setSize(popupWidth, 8 * 22 + 2);  // Initial size, updateFilter adjusts
    autocompletePopup_->updateFilter(filter);

    if (autocompletePopup_->isEmpty()) {
        hideAutocomplete();
        return;
    }

    // Position above the input box
    int popupHeight = autocompletePopup_->getHeight();
    autocompletePopup_->setBounds(inputBounds.getX(), inputBounds.getY() - popupHeight, popupWidth,
                                  popupHeight);
    autocompletePopup_->setVisible(true);
    autocompletePopup_->toFront(false);
}

void AIChatConsoleContent::hideAutocomplete() {
    if (autocompletePopup_)
        autocompletePopup_->setVisible(false);
}

std::vector<AIChatConsoleContent::ParamAliasEntry> AIChatConsoleContent::collectParamAliases(
    const juce::String& pluginAlias) const {
    std::vector<ParamAliasEntry> out;
    if (pluginAlias.isEmpty())
        return out;

    auto& registry = magda::AliasRegistry::getInstance();
    const juce::String prefix = pluginAlias + ".";
    std::set<juce::String> seenCanonical;
    auto walk = [&](magda::AliasLayer layer) {
        for (const auto& [canonicalName, alias] : registry.layerEntries(layer)) {
            if (!canonicalName.startsWith(prefix))
                continue;
            if (!seenCanonical.insert(canonicalName).second)
                continue;
            ParamAliasEntry e;
            e.pluginAlias = pluginAlias;
            e.paramAlias = canonicalName.substring(prefix.length());
            e.paramName = alias.paramNameAtSetTime;
            out.push_back(std::move(e));
        }
    };
    walk(magda::AliasLayer::UserProject);
    walk(magda::AliasLayer::UserGlobal);
    walk(magda::AliasLayer::Curated);
    walk(magda::AliasLayer::AutoGen);

    std::sort(out.begin(), out.end(), [](const ParamAliasEntry& a, const ParamAliasEntry& b) {
        return a.paramAlias < b.paramAlias;
    });
    return out;
}

void AIChatConsoleContent::showParamAutocomplete(const juce::String& pluginAlias,
                                                 const juce::String& filter) {
    auto entries = collectParamAliases(pluginAlias);
    if (entries.empty()) {
        // No aliases for this plugin (yet) — hide rather than show an empty
        // popup or stale content from an earlier plugin scope.
        hideAutocomplete();
        return;
    }

    if (!autocompletePopup_) {
        autocompletePopup_ = std::make_unique<AutocompletePopup>(*this);
        addAndMakeVisible(*autocompletePopup_);
    }

    auto inputBounds = inputBox_->getBounds();
    int popupWidth = inputBounds.getWidth();
    autocompletePopup_->setSize(popupWidth, 8 * 22 + 2);
    autocompletePopup_->updateParamFilter(std::move(entries), pluginAlias, filter);

    if (autocompletePopup_->isEmpty()) {
        hideAutocomplete();
        return;
    }

    int popupHeight = autocompletePopup_->getHeight();
    autocompletePopup_->setBounds(inputBounds.getX(), inputBounds.getY() - popupHeight, popupWidth,
                                  popupHeight);
    autocompletePopup_->setVisible(true);
    autocompletePopup_->toFront(false);
}

void AIChatConsoleContent::insertParamAlias(const juce::String& pluginAlias,
                                            const juce::String& paramAlias) {
    auto text = inputDocument_.getAllContent();
    int caretPos = inputBox_->getCaretPos().getPosition();

    int atPos = -1;
    for (int i = caretPos - 1; i >= 0; --i) {
        auto ch = text[i];
        if (ch == '@') {
            atPos = i;
            break;
        }
        if (ch == ' ' || ch == '\n')
            break;
    }

    if (atPos >= 0) {
        auto before = text.substring(0, atPos);
        auto after = text.substring(caretPos);
        auto inserted = "@" + pluginAlias + "." + paramAlias;
        auto newText = before + inserted + " " + after;
        inputDocument_.replaceAllContent(newText);
        inputBox_->moveCaretTo(
            juce::CodeDocument::Position(inputDocument_, atPos + (int)inserted.length() + 1),
            false);
    }

    hideAutocomplete();
    inputBox_->grabKeyboardFocus();
}

void AIChatConsoleContent::insertAlias(const juce::String& alias) {
    auto text = inputDocument_.getAllContent();
    int caretPos = inputBox_->getCaretPos().getPosition();

    // Find the @ that started this completion
    int atPos = -1;
    for (int i = caretPos - 1; i >= 0; --i) {
        auto ch = text[i];
        if (ch == '@') {
            atPos = i;
            break;
        }
        if (ch == ' ' || ch == '\n')
            break;
    }

    if (atPos >= 0) {
        // Replace @partial with @full_alias. No trailing space — the user
        // may want to chain "." into the param-alias popup, and a space
        // would break the @-token search loop in onTextChange.
        auto before = text.substring(0, atPos);
        auto after = text.substring(caretPos);
        auto newText = before + "@" + alias + after;
        inputDocument_.replaceAllContent(newText);
        inputBox_->moveCaretTo(
            juce::CodeDocument::Position(inputDocument_, atPos + 1 + (int)alias.length()), false);
    }

    hideAutocomplete();
    inputBox_->grabKeyboardFocus();
}

bool AIChatConsoleContent::keyPressed(const juce::KeyPress& key, juce::Component*) {
    // DSL tab key handling
    if (activeTab_ == ConsoleTab::DSL) {
        // Ctrl+Enter — execute DSL
        if (key.getModifiers().isCommandDown() && key.getKeyCode() == juce::KeyPress::returnKey) {
            executeDSL();
            return true;
        }
        // Ctrl+L — clear DSL output
        if (key.getModifiers().isCommandDown() && key.getKeyCode() == 'L') {
            dslOutput_.clear();
            dslOutput_.setText("Output cleared.\n\n");
            return true;
        }
        return false;
    }

    // AI tab — autocomplete navigation, plus the Enter/Esc handling that
    // used to live on TextEditor's onReturnKey/onEscapeKey callbacks before
    // we swapped the input box for a CodeEditorComponent.
    const bool popupVisible = autocompletePopup_ && autocompletePopup_->isVisible();

    if (key == juce::KeyPress::escapeKey) {
        if (popupVisible) {
            hideAutocomplete();
            return true;
        }
        return false;
    }

    // Enter (no shift) — accept the autocomplete entry if visible, otherwise
    // send the message. Shift+Enter falls through to the editor for newline.
    if (key == juce::KeyPress::returnKey && !key.getModifiers().isShiftDown()) {
        if (popupVisible) {
            switch (autocompletePopup_->getMode()) {
                case AutocompletePopup::Mode::SlashCommand:
                    if (auto* cmd = autocompletePopup_->getSelectedCommand()) {
                        insertSlashCommand(cmd->name);
                        return true;
                    }
                    break;
                case AutocompletePopup::Mode::Param:
                    if (auto* entry = autocompletePopup_->getSelectedParamEntry()) {
                        insertParamAlias(entry->pluginAlias, entry->paramAlias);
                        return true;
                    }
                    break;
                case AutocompletePopup::Mode::Alias:
                    if (auto* entry = autocompletePopup_->getSelectedEntry()) {
                        insertAlias(entry->alias);
                        return true;
                    }
                    break;
            }
        }
        auto text = inputDocument_.getAllContent().trim();
        if (text.isNotEmpty() && !processing_)
            sendMessage(text);
        return true;
    }

    if (!popupVisible)
        return false;

    if (key == juce::KeyPress::upKey) {
        autocompletePopup_->selectPrevious();
        return true;
    }
    if (key == juce::KeyPress::downKey) {
        autocompletePopup_->selectNext();
        return true;
    }
    if (key == juce::KeyPress::tabKey) {
        switch (autocompletePopup_->getMode()) {
            case AutocompletePopup::Mode::SlashCommand:
                if (auto* cmd = autocompletePopup_->getSelectedCommand()) {
                    insertSlashCommand(cmd->name);
                    return true;
                }
                break;
            case AutocompletePopup::Mode::Param:
                if (auto* entry = autocompletePopup_->getSelectedParamEntry()) {
                    insertParamAlias(entry->pluginAlias, entry->paramAlias);
                    return true;
                }
                break;
            case AutocompletePopup::Mode::Alias:
                if (auto* entry = autocompletePopup_->getSelectedEntry()) {
                    insertAlias(entry->alias);
                    return true;
                }
                break;
        }
    }

    return false;
}

void AIChatConsoleContent::codeDocumentTextInserted(const juce::String& /*inserted*/,
                                                    int /*insertIndex*/) {
    // Defer to after the editor finishes settling its caret. The listener
    // fires synchronously during the document mutation, while the editor
    // updates its caret position after returning — checking caret-pos here
    // would read a stale value and pick the wrong autocomplete branch.
    juce::Component::SafePointer<AIChatConsoleContent> self(this);
    juce::MessageManager::callAsync([self] {
        if (self != nullptr)
            self->onInputChanged();
    });
}

void AIChatConsoleContent::codeDocumentTextDeleted(int /*startIndex*/, int /*endIndex*/) {
    juce::Component::SafePointer<AIChatConsoleContent> self(this);
    juce::MessageManager::callAsync([self] {
        if (self != nullptr)
            self->onInputChanged();
    });
}

void AIChatConsoleContent::onInputChanged() {
    if (!inputBox_)
        return;
    auto text = inputDocument_.getAllContent();
    int caretPos = inputBox_->getCaretPos().getPosition();

    // Slash commands at start of input
    if (text.startsWith("/")) {
        int spacePos = text.indexOf(" ");
        if (spacePos < 0 || caretPos <= spacePos) {
            auto filter = text.substring(1, caretPos);
            showSlashAutocomplete(filter);
            return;
        }
    }

    // Walk back from the caret to find the start of the current @-token.
    int atPos = -1;
    for (int i = caretPos - 1; i >= 0; --i) {
        auto ch = text[i];
        if (ch == '@') {
            atPos = i;
            break;
        }
        if (ch == ' ' || ch == '\n')
            break;
    }

    if (atPos >= 0) {
        auto after = text.substring(atPos + 1, caretPos);
        // A '.' inside the @-token splits plugin alias from a parameter
        // filter; the popup pivots to param mode.
        int dotPos = after.indexOfChar('.');
        if (dotPos < 0) {
            showAutocomplete(after);
        } else {
            auto pluginAlias = after.substring(0, dotPos);
            auto paramFilter = after.substring(dotPos + 1);
            showParamAutocomplete(pluginAlias, paramFilter);
        }
    } else {
        hideAutocomplete();
    }
}

void AIChatConsoleContent::buildSlashCommands() {
    if (slashRegistry_)
        return;

    slashRegistry_ = std::make_unique<magda::daw::ui::SlashCommandRegistry>(
        [this](const juce::String& msg) { appendToChat(msg); });

    // /agent — explicit deterministic surface override. It falls through to
    // rewriteSlashCommand(), which converts it to @surface text for routing.
    SlashCommand agent;
    agent.name = "agent";
    agent.description = "Override the contextual AI surface for one request";
    agent.usage = "/agent <surface> <request>";
    agent.details =
        "Surfaces: arrangement, piano-roll, session, mixer, automation, device, master, drummer.";
    agent.handler = [](const juce::String&, const std::map<juce::String, juce::String>&,
                       const juce::String&) { return false; };
    slashRegistry_->add(std::move(agent));

    // /design — 4OSC sound design. The handler reads --category=<cat> if
    // present, stashes it as the override that finishPresetGeneration
    // applies, then kicks the agent thread.
    SlashCommand design;
    design.name = "design";
    design.description = "Design a 4OSC preset from a description";
    design.usage = "/design [--category=<cat>] <description>";
    design.details =
        "Generate a preset for the focused 4OSC device from a natural-language description.\n"
        "Focus a 4OSC device first; the preset applies directly to it.\n"
        "The result is a starting point - tweak by ear, then save from the device header.\n"
        "\n"
        "Flags:\n"
        "  --category=<Bass|Lead|Pad|Pluck|Keys|FX|Other>  override the agent's category pick";
    design.examples = {
        {"Bass",
         {"deep sub bass", "fat reese bass with movement", "acid bass with resonant filter",
          "808-style bass with sub and click", "dub-style bass with delay"}},
        {"Lead",
         {"fat detuned saw lead with octave layer", "trance supersaw lead",
          "bright square lead with chorus", "legato mono synth lead with portamento",
          "screaming acid lead"}},
        {"Pad",
         {"warm analog pad", "evolving ambient pad with slow filter", "string ensemble pad",
          "dark cinematic drone", "lush chord pad"}},
        {"Pluck", {"snappy saw pluck", "muted soft pluck for arpeggios", "FM-style bell pluck"}},
        {"Keys", {"electric piano with chorus", "synth keys with subtle vibrato"}},
        {"FX", {"rising white noise sweep", "impact hit with reverb tail", "alarm-style siren"}},
    };
    design.handler = [this](const juce::String& originalText,
                            const std::map<juce::String, juce::String>& flags,
                            const juce::String& positional) {
        appendToChat(juce::String::charToString(0x25CF) + " " + originalText);
        if (positional.isEmpty()) {
            appendToChat(juce::String::charToString(0x25C6) +
                         " Usage: /design <description>  (run /design --help for examples)");
            return true;
        }
        // Save the user's --category pick so finishPresetGeneration can
        // override whatever the agent chose. Cleared on completion.
        auto catIt = flags.find("category");
        pendingCategoryOverride_ = catIt != flags.end() ? catIt->second.trim() : juce::String();
        startPresetGeneration(positional);
        return true;
    };
    slashRegistry_->add(std::move(design));

    // /theme - generate a UI colour theme. The handler kicks the agent
    // thread; on success the theme is written to the Themes folder, selected,
    // and rendered live via the existing apply + hot-reload path.
    SlashCommand theme;
    theme.name = "theme";
    theme.description = "Design a UI colour theme from a description";
    theme.usage = "/theme <description>";
    theme.details =
        "Generate a custom colour theme from a natural-language description and apply it "
        "immediately.\n"
        "The theme is saved as an editable JSON file in Documents/MAGDA/Themes and stays "
        "selected; edit the file to tweak it live, or switch back under Preferences > "
        "Appearance.";
    theme.examples = {
        {"Mood",
         {"warm sunset, dark", "cold arctic blue", "forest green dark theme",
          "high-contrast light theme", "muted pastel light theme"}},
        {"Style",
         {"retro amber terminal", "cyberpunk neon on black", "sepia paper light theme",
          "monochrome slate"}},
    };
    theme.handler = [this](const juce::String& originalText,
                           const std::map<juce::String, juce::String>& /*flags*/,
                           const juce::String& positional) {
        appendToChat(juce::String::charToString(0x25CF) + " " + originalText);
        if (positional.isEmpty()) {
            appendToChat(juce::String::charToString(0x25C6) +
                         " Usage: /theme <description>  (run /theme --help for examples)");
            return true;
        }
        startThemeGeneration(positional);
        return true;
    };
    slashRegistry_->add(std::move(theme));

    // /controller — generate a hardware controller profile JSON.
    SlashCommand controller;
    controller.name = "controller";
    controller.description = "Generate a controller profile from a description";
    controller.usage = "/controller <device description>";
    controller.details = "Generate a hardware controller profile (encoder / pad / fader layout) "
                         "from a description.\n"
                         "The agent emits JSON describing the control surface; the result lands in "
                         "the profile picker.";
    controller.examples = {
        {"MIDI controllers",
         {"Akai MPK Mini with 8 pads and 8 knobs", "Novation Launchkey 25 with mod wheel",
          "Behringer X-Touch Mini with 8 encoders and faders"}},
    };
    controller.handler = [this](const juce::String& originalText,
                                const std::map<juce::String, juce::String>&,
                                const juce::String& positional) {
        appendToChat(juce::String::charToString(0x25CF) + " " + originalText);
        if (positional.isEmpty()) {
            appendToChat(juce::String::charToString(0x25C6) +
                         " Usage: /controller <device description>");
            return true;
        }
        startControllerGeneration(positional);
        return true;
    };
    slashRegistry_->add(std::move(controller));

    // /groove — rewrite the prompt to constrain the LLM to groove ops, then
    // fall through to the normal AI path. The handler returns false so the
    // dispatcher lets sendMessage continue (where rewriteSlashCommand
    // transforms the user text before it goes to the model).
    SlashCommand groove;
    groove.name = "groove";
    groove.description = "Create or apply swing/groove timing templates";
    groove.usage = "/groove <request>";
    groove.details =
        "Constrain the AI to swing/groove template operations only. Use this when you want to\n"
        "create a new groove, extract one from a clip, or apply one to a track without the AI\n"
        "scope-bleeding into note-creation territory.";
    groove.examples = {
        {"Apply", {"set the project groove to MPC 8th swing 60%", "apply funky 16ths to track 2"}},
        {"Create", {"make a shuffled 16th-note groove with subtle delay on the off-beats"}},
        {"Extract", {"extract the timing feel from the selected clip into a new groove"}},
    };
    // Return false → fall through to the AI path. /groove's text reaches
    // the LLM via rewriteSlashCommand which adds the constraint preamble.
    // No echo here — sendMessage's AI path echoes the user's text further
    // down, so doing it here would double-echo.
    groove.handler = [](const juce::String&, const std::map<juce::String, juce::String>&,
                        const juce::String&) { return false; };
    slashRegistry_->add(std::move(groove));
}

void AIChatConsoleContent::showSlashAutocomplete(const juce::String& filter) {
    if (!slashRegistry_)
        buildSlashCommands();

    if (!autocompletePopup_) {
        autocompletePopup_ = std::make_unique<AutocompletePopup>(*this);
        addAndMakeVisible(*autocompletePopup_);
    }

    auto inputBounds = inputBox_->getBounds();
    int popupWidth = inputBounds.getWidth();
    autocompletePopup_->setSize(popupWidth, 8 * 22 + 2);
    autocompletePopup_->updateSlashFilter(filter);

    if (autocompletePopup_->isEmpty()) {
        hideAutocomplete();
        return;
    }

    int popupHeight = autocompletePopup_->getHeight();
    autocompletePopup_->setBounds(inputBounds.getX(), inputBounds.getY() - popupHeight, popupWidth,
                                  popupHeight);
    autocompletePopup_->setVisible(true);
    autocompletePopup_->toFront(false);
}

void AIChatConsoleContent::insertSlashCommand(const juce::String& command) {
    auto text = inputDocument_.getAllContent();
    // Find the end of the /command token
    int spacePos = text.indexOf(" ");
    auto after = (spacePos >= 0) ? text.substring(spacePos) : "";
    auto newText = "/" + command + " " + after.trimStart();
    inputDocument_.replaceAllContent(newText);
    inputBox_->moveCaretTo(juce::CodeDocument::Position(inputDocument_, newText.length()), false);

    hideAutocomplete();
    inputBox_->grabKeyboardFocus();
}

// ============================================================================
// /controller → /enable two-step flow
// ============================================================================

AIChatConsoleContent::ControllerRequestThread::ControllerRequestThread(
    AIChatConsoleContent& owner, juce::String description, std::vector<std::string> livePortNames)
    : juce::Thread("MAGDA-ControllerAgent"),
      owner_(owner),
      description_(std::move(description)),
      livePortNames_(std::move(livePortNames)) {}

void AIChatConsoleContent::ControllerRequestThread::run() {
    auto safeThis = juce::Component::SafePointer<AIChatConsoleContent>(&owner_);

    if (threadShouldExit() || !owner_.controllerAgent_)
        return;

    auto result = owner_.controllerAgent_->generate(description_.toStdString(), livePortNames_);

    if (threadShouldExit())
        return;

    // Copy result pieces into message-thread-friendly values before callAsync.
    bool success = !result.hasError && result.profile.has_value();
    juce::String errorOrJson = success ? result.rawJson : juce::String(result.error);
    juce::String profileId = success ? result.profile->id : juce::String();
    juce::String profileName =
        success ? (result.profile->vendor.isEmpty()
                       ? result.profile->name
                       : result.profile->vendor + " \xc2\xb7 " + result.profile->name)
                : juce::String();

    juce::MessageManager::callAsync([safeThis, success, errorOrJson, profileId, profileName]() {
        if (!safeThis)
            return;
        safeThis->finishControllerGeneration(success, errorOrJson, profileId, profileName);
    });
}

void AIChatConsoleContent::startControllerGeneration(const juce::String& description) {
    // Cancel any previous controller thread
    if (controllerThread_ && controllerThread_->isThreadRunning()) {
        if (controllerAgent_)
            controllerAgent_->requestCancel();
        controllerThread_->signalThreadShouldExit();
        controllerThread_->stopThread(2000);
        controllerThread_.reset();
    }
    if (controllerAgent_)
        controllerAgent_->resetCancel();

    appendToChat(juce::String::charToString(0x25C6) + " Generating controller profile...");

    // Snapshot live MIDI input names on the message thread — JUCE asserts
    // getAvailableDevices() elsewhere on some platforms.
    auto liveInputs = juce::MidiInput::getAvailableDevices();
    std::vector<std::string> portNames;
    portNames.reserve(static_cast<size_t>(liveInputs.size()));
    for (const auto& dev : liveInputs)
        portNames.push_back(dev.name.toStdString());

    controllerThread_ =
        std::make_unique<ControllerRequestThread>(*this, description, std::move(portNames));
    controllerThread_->startThread();
}

namespace {

/** Return baseId if free in registry, else baseId-2, baseId-3, ... */
juce::String findUniqueProfileId(const juce::String& baseId) {
    auto& reg = magda::ControllerProfileRegistry::getInstance();
    if (!reg.findById(baseId).has_value())
        return baseId;
    for (int i = 2; i < 1000; ++i) {
        auto candidate = baseId + "-" + juce::String(i);
        if (!reg.findById(candidate).has_value())
            return candidate;
    }
    return baseId + "-" + juce::Uuid().toDashedString().substring(0, 8);
}

/** Replace the top-level "id" field in a JSON profile string. */
juce::String rewriteProfileIdInJson(const juce::String& json, const juce::String& newId) {
    auto parsed = juce::JSON::parse(json);
    if (auto* obj = parsed.getDynamicObject()) {
        obj->setProperty("id", newId);
        return juce::JSON::toString(parsed, true);
    }
    return json;
}

}  // namespace

void AIChatConsoleContent::finishControllerGeneration(bool success, const juce::String& errorOrJson,
                                                      juce::String profileId,
                                                      juce::String profileName) {
    if (!success) {
        appendToChat(juce::String::charToString(0x25C6) + " Error: " + errorOrJson);
        return;
    }

    auto userDir = magda::ControllerProfileRegistry::userControllersDirectory();
    if (!userDir.isDirectory()) {
        if (auto res = userDir.createDirectory(); res.failed()) {
            appendToChat(juce::String::charToString(0x25C6) +
                         " Error: could not create user controllers dir (" + res.getErrorMessage() +
                         ")");
            return;
        }
    }

    auto safeThis = juce::Component::SafePointer<AIChatConsoleContent>(this);

    // Reusable continuation: write JSON at the resolved id, reload, prompt for port.
    auto writeAndPromptPort = [safeThis, userDir](juce::String finalJson, juce::String finalId,
                                                  juce::String displayName) {
        if (!safeThis)
            return;
        auto destFile =
            userDir.getChildFile(magda::ControllerProfileRegistry::filenameForProfileId(finalId));
        if (!destFile.replaceWithText(finalJson)) {
            safeThis->appendToChat(juce::String::charToString(0x25C6) +
                                   " Error: could not write profile file " +
                                   destFile.getFullPathName());
            return;
        }

        magda::ControllerProfileRegistry::getInstance().load();

        safeThis->appendToChat(juce::String::charToString(0x25C6) + " Generated: " + displayName +
                               " (" + finalId + ")");
        safeThis->appendToChat("    " + destFile.getFullPathName());

        // Print a readable mapping summary so the user can verify what the LLM produced.
        if (auto profileOpt = magda::ControllerProfileRegistry::getInstance().findById(finalId)) {
            juce::String summary = "    Controls:\n";
            for (const auto& ctrl : profileOpt->controls) {
                juce::String line = "      " + ctrl.controlId.paddedRight(' ', 12) + ctrl.kind +
                                    " CC " + juce::String(ctrl.cc) + " ch" +
                                    juce::String(ctrl.channel);
                // Find default binding for this control
                for (const auto& db : profileOpt->defaultBindings) {
                    if (db.controlId == ctrl.controlId) {
                        line += "  -> " + db.resolverKind;
                        auto keys = db.args.getAllKeys();
                        for (int k = 0; k < keys.size(); ++k)
                            line += " " + keys[k] + "=" + db.args.getAllValues()[k];
                        break;
                    }
                }
                summary += line + "\n";
            }
            safeThis->appendToChat(summary.trimEnd());
        }

        auto ports = juce::MidiInput::getAvailableDevices();
        if (ports.isEmpty()) {
            safeThis->appendToChat(
                juce::String::charToString(0x25C6) +
                " No MIDI inputs connected. Open the Controllers dialog to enable it later.");
            return;
        }

        juce::PopupMenu menu;
        menu.addSectionHeader("Enable " + displayName + " on:");
        for (int i = 0; i < ports.size(); ++i)
            menu.addItem(i + 1, ports[i].name);
        menu.addSeparator();
        menu.addItem(9999, "Skip");

        menu.showMenuAsync(
            juce::PopupMenu::Options().withTargetComponent(safeThis->inputBox_.get()),
            [safeThis, finalId, displayName, ports](int result) {
                if (!safeThis || result <= 0 || result == 9999)
                    return;
                int idx = result - 1;
                if (idx < 0 || idx >= ports.size())
                    return;

                auto profileOpt = magda::ControllerProfileRegistry::getInstance().findById(finalId);
                if (!profileOpt.has_value()) {
                    safeThis->appendToChat(juce::String::charToString(0x25C6) +
                                           " Profile disappeared — cannot enable.");
                    return;
                }

                const auto& dev = ports[idx];

                // One enabled controller per port: any existing row on this
                // port stays registered but loses its bindings, leaving the
                // newly-added one as the active controller.
                auto& controllerReg = magda::ControllerRegistry::getInstance();
                auto& bindingReg = magda::BindingRegistry::getInstance();
                for (const auto& existing : controllerReg.all()) {
                    if (existing.inputPort == dev.identifier) {
                        bindingReg.removeAllForController(magda::BindingScope::Global, existing.id);
                        bindingReg.removeAllForController(magda::BindingScope::Project,
                                                          existing.id);
                    }
                }

                auto mat = magda::materialiseControllerFromProfile(*profileOpt, dev.identifier, {},
                                                                   dev.name);

                controllerReg.add(mat.controller);
                for (const auto& b : mat.bindings)
                    bindingReg.add(magda::BindingScope::Global, b);

                auto& cfg = magda::Config::getInstance();
                cfg.setControllers(magda::ControllerRegistry::getInstance().saveToConfig());
                cfg.setGlobalBindings(magda::BindingRegistry::getInstance().saveGlobal());
                cfg.save();

                safeThis->appendToChat(juce::String::charToString(0x25C6) + " Enabled " +
                                       displayName + " on " + dev.name);
            });
    };

    // Collision: ask the user whether to replace, create as a unique variant, or cancel.
    if (magda::ControllerProfileRegistry::getInstance().findById(profileId).has_value()) {
        juce::PopupMenu menu;
        menu.addSectionHeader("Profile '" + profileId + "' already exists");
        menu.addItem(1, "Replace existing");
        menu.addItem(2, "Create as new (" + findUniqueProfileId(profileId) + ")");
        menu.addSeparator();
        menu.addItem(9999, "Cancel");

        auto rawJson = errorOrJson;  // contains the JSON when success == true
        auto baseId = profileId;
        auto displayName = profileName;

        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(inputBox_.get()),
                           [safeThis, writeAndPromptPort, rawJson, baseId, displayName](int r) {
                               if (!safeThis || r <= 0 || r == 9999) {
                                   if (safeThis)
                                       safeThis->appendToChat(juce::String::charToString(0x25C6) +
                                                              " Cancelled.");
                                   return;
                               }
                               if (r == 1) {
                                   writeAndPromptPort(rawJson, baseId, displayName);
                               } else if (r == 2) {
                                   auto newId = findUniqueProfileId(baseId);
                                   auto rewritten = rewriteProfileIdInJson(rawJson, newId);
                                   writeAndPromptPort(rewritten, newId, displayName);
                               }
                           });
        return;
    }

    writeAndPromptPort(errorOrJson, profileId, profileName);
}

// ============================================================================
// /design — 4OSC sound design (JSON only for now; apply / save coming next)
// ============================================================================

AIChatConsoleContent::FourOscRequestThread::FourOscRequestThread(AIChatConsoleContent& owner,
                                                                 juce::String description)
    : juce::Thread("MAGDA-FourOscAgent"), owner_(owner), description_(std::move(description)) {}

void AIChatConsoleContent::FourOscRequestThread::run() {
    auto safeThis = juce::Component::SafePointer<AIChatConsoleContent>(&owner_);

    if (threadShouldExit() || !owner_.fourOscAgent_)
        return;

    auto result = owner_.fourOscAgent_->generate(description_.toStdString());
    if (threadShouldExit())
        return;

    bool success = !result.hasError;
    juce::String pretty;
    juce::String presetName;
    if (success) {
        // Re-encode the parsed Preset so what the chat shows is what the
        // executor will see — sidesteps any markdown fences / stray prose
        // the model emits and gives us a stable schema to render.
        auto* obj = new juce::DynamicObject();
        obj->setProperty("name", juce::String(result.preset.name));
        if (!result.preset.category.empty())
            obj->setProperty("category", juce::String(result.preset.category));
        obj->setProperty("description", juce::String(result.preset.description));
        auto* waves = new juce::DynamicObject();
        for (const auto& [n, name] : result.preset.waves)
            waves->setProperty(juce::Identifier(juce::String(n)), juce::String(name));
        obj->setProperty("waves", juce::var(waves));
        if (!result.preset.filterType.empty())
            obj->setProperty("filter_type", juce::String(result.preset.filterType));
        if (!result.preset.voiceMode.empty())
            obj->setProperty("voice_mode", juce::String(result.preset.voiceMode));
        if (!result.preset.fx.empty()) {
            auto* fx = new juce::DynamicObject();
            for (const auto& [k, v] : result.preset.fx)
                fx->setProperty(juce::Identifier(juce::String(k)), v);
            obj->setProperty("fx", juce::var(fx));
        }
        auto* params = new juce::DynamicObject();
        for (const auto& [k, v] : result.preset.params)
            params->setProperty(juce::Identifier(juce::String(k)), v);
        obj->setProperty("params", juce::var(params));
        pretty = juce::JSON::toString(juce::var(obj), false /*allOnOneLine=false → pretty*/);
        presetName = juce::String(result.preset.name);
    } else {
        pretty = juce::String(result.error);
    }

    juce::MessageManager::callAsync([safeThis, success, pretty, presetName]() {
        if (!safeThis)
            return;
        safeThis->finishPresetGeneration(success, pretty, presetName);
    });
}

void AIChatConsoleContent::startPresetGeneration(const juce::String& description) {
    if (fourOscThread_ && fourOscThread_->isThreadRunning()) {
        if (fourOscAgent_)
            fourOscAgent_->requestCancel();
        fourOscThread_->signalThreadShouldExit();
        fourOscThread_->stopThread(2000);
        fourOscThread_.reset();
    }
    if (fourOscAgent_)
        fourOscAgent_->resetCancel();

    appendToChat(juce::String::charToString(0x25C6) + " Designing 4OSC preset...");

    fourOscThread_ = std::make_unique<FourOscRequestThread>(*this, description);
    fourOscThread_->startThread();
}

AIChatConsoleContent::ThemeRequestThread::ThemeRequestThread(AIChatConsoleContent& owner,
                                                             juce::String description)
    : juce::Thread("MAGDA-ThemeAgent"), owner_(owner), description_(std::move(description)) {}

void AIChatConsoleContent::ThemeRequestThread::run() {
    auto safeThis = juce::Component::SafePointer<AIChatConsoleContent>(&owner_);

    if (threadShouldExit() || !owner_.themeAgent_)
        return;

    // The themes layer owns the theme schema: it builds the prompt, the JSON
    // schema for provider-side structured output, and later validates the
    // result. The agent only does the LLM round-trip.
    const auto systemPrompt = magda::buildThemeSystemPrompt().toStdString();
    const auto schema = magda::buildThemeSchema();

    // Stream the JSON into an anchored region. The first flush drops the
    // "Designing theme..." placeholder and records the anchor; finishThemeGeneration
    // collapses everything from the anchor onward into the summary. Tokens are
    // coalesced so a fast stream doesn't bury the message queue.
    auto anchor = std::make_shared<std::atomic<int>>(-1);

    struct StreamBuf {
        std::mutex mu;
        juce::String pending;
        std::atomic<bool> flushPending{false};
    };
    auto buf = std::make_shared<StreamBuf>();

    auto onToken = [this, safeThis, anchor, buf](const juce::String& token) -> bool {
        if (threadShouldExit())
            return false;
        {
            std::lock_guard<std::mutex> lk(buf->mu);
            buf->pending += token;
        }
        bool expected = false;
        if (!buf->flushPending.compare_exchange_strong(expected, true))
            return true;  // a flush is already queued; it will read the buffer
        juce::MessageManager::callAsync([safeThis, anchor, buf]() {
            buf->flushPending.store(false);
            if (!safeThis)
                return;
            juce::String chunk;
            {
                std::lock_guard<std::mutex> lk(buf->mu);
                chunk = std::move(buf->pending);
                buf->pending.clear();
            }
            if (chunk.isEmpty())
                return;
            auto text = safeThis->chatHistory_.getText();
            if (anchor->load() < 0) {
                auto pos =
                    text.lastIndexOf(juce::String::charToString(0x25C6) + " Designing theme");
                if (pos >= 0) {
                    auto lineEnd = text.indexOf(pos, "\n");
                    if (lineEnd < 0)
                        lineEnd = text.length();
                    text = text.substring(0, pos) + text.substring(lineEnd + 1);
                }
                anchor->store(text.length());
            }
            safeThis->chatHistory_.setText(text + chunk);
            safeThis->chatHistory_.moveCaretToEnd();
        });
        return true;
    };

    auto result = owner_.themeAgent_->generateStreaming(systemPrompt, description_.toStdString(),
                                                        schema, onToken);
    if (threadShouldExit())
        return;

    bool success = !result.hasError;
    juce::String jsonOrError;
    juce::String name;
    juce::String base;
    int colourCount = 0;

    if (success) {
        std::string validationError;
        if (auto theme =
                magda::validateGeneratedTheme(juce::String(result.text), validationError)) {
            jsonOrError = juce::String(theme->json);
            name = juce::String(theme->name);
            base = juce::String(theme->base);
            colourCount = theme->colourCount;
        } else {
            success = false;
            jsonOrError = juce::String(validationError);
        }
    } else {
        jsonOrError = juce::String(result.error);
        if (jsonOrError.isEmpty())
            jsonOrError = "provider returned an error with no message";
    }

    // Read the anchor on the message thread (inside the callback), after the
    // queued token flushes have run, so it reflects the streamed region.
    juce::MessageManager::callAsync(
        [safeThis, success, jsonOrError, name, base, colourCount, anchor]() {
            if (!safeThis)
                return;
            safeThis->finishThemeGeneration(success, jsonOrError, name, base, colourCount,
                                            anchor->load());
        });
}

void AIChatConsoleContent::startThemeGeneration(const juce::String& description) {
    if (themeThread_ && themeThread_->isThreadRunning()) {
        if (themeAgent_)
            themeAgent_->requestCancel();
        themeThread_->signalThreadShouldExit();
        themeThread_->stopThread(2000);
        themeThread_.reset();
    }
    if (themeAgent_)
        themeAgent_->resetCancel();

    appendToChat(juce::String::charToString(0x25C6) + " Designing theme...");

    themeThread_ = std::make_unique<ThemeRequestThread>(*this, description);
    themeThread_->startThread();
}

void AIChatConsoleContent::finishThemeGeneration(bool success, const juce::String& jsonOrError,
                                                 juce::String name, juce::String base,
                                                 int colourCount, int streamAnchor) {
    // Collapse the streamed JSON (or, if nothing streamed, the "Designing
    // theme..." placeholder) so only the final summary/error remains.
    if (streamAnchor >= 0) {
        auto text = chatHistory_.getText();
        if (streamAnchor <= text.length())
            chatHistory_.setText(text.substring(0, streamAnchor));
    } else {
        auto text = chatHistory_.getText();
        auto pos = text.lastIndexOf(juce::String::charToString(0x25C6) + " Designing theme");
        if (pos >= 0) {
            auto lineEnd = text.indexOf(pos, "\n");
            if (lineEnd < 0)
                lineEnd = text.length();
            chatHistory_.setText(text.substring(0, pos) + text.substring(lineEnd + 1));
        }
    }

    if (!success) {
        appendToChat(juce::String::charToString(0x25C6) + " Error: " + jsonOrError);
        return;
    }

    auto dir = magda::paths::themesDir();
    dir.createDirectory();

    const juce::String safeName =
        juce::File::createLegalFileName(name.isEmpty() ? juce::String("AI Theme") : name);
    auto file = dir.getChildFile(safeName + ".json");
    if (!file.replaceWithText(jsonOrError)) {
        appendToChat(juce::String::charToString(0x25C6) + " Error: could not write theme file to " +
                     file.getFullPathName());
        return;
    }

    // Select the new theme. Config::save() notifies MainWindow, which swaps
    // the palette and arms hot-reload. If this id was already the active theme
    // the config guard skips re-apply, but the file rewrite is caught by the
    // watcher instead - so the edit shows up either way.
    auto& config = magda::Config::getInstance();
    config.setTheme(file.getFileNameWithoutExtension().toStdString());
    config.save();

    juce::String body = juce::String::charToString(0x25C6) + " " +
                        (name.isEmpty() ? juce::String("Theme") : name) + "\n";
    body << "  " << juce::String(colourCount) << " colours over the " << base << " base\n";
    body << "  saved to " << file.getFullPathName() << "\n";
    body << "  applied - edit the file to tweak it live, or switch under "
            "Preferences > Appearance";
    appendToChat(body);
}

// Format a seconds value as a compact human-readable string ("5ms",
// "150ms", "1.2s", "12s"). Used for ADSR display in the pretty-print.
static juce::String formatSeconds(float s) {
    if (s < 0.0f)
        s = 0.0f;
    if (s < 1.0f)
        return juce::String(static_cast<int>(std::round(s * 1000.0f))) + "ms";
    if (s < 10.0f)
        return juce::String(s, 1) + "s";
    return juce::String(static_cast<int>(std::round(s))) + "s";
}

// Format a normalized 0..1 value as 2-decimal text.
static juce::String formatNorm(float v) {
    return juce::String(juce::jlimit(0.0f, 1.0f, v), 2);
}

// Render a parsed preset as a categorized multi-line summary suitable
// for the chat. Hides empty/zero categories. Time params (ADSR) are
// formatted as ms/s; everything else as 2-decimal normalized values.
static juce::String prettyPrintPreset(const magda::FourOscAgent::Preset& preset) {
    // Lookup helper: preset.params is keyed on the alias suffix
    // ("amp_attack", "tune_1", …). Returns a sentinel when absent so
    // callers can decide whether to print or skip.
    auto get = [&](const std::string& key, float fallback = -1.0f) {
        auto it = preset.params.find(key);
        return it != preset.params.end() ? it->second : fallback;
    };
    auto has = [&](const std::string& key) { return preset.params.count(key) > 0; };

    juce::String out;
    if (!preset.description.empty())
        out << "  " << juce::String(preset.description) << "\n";
    out << "\n";

    // Oscillators (only print rows where wave != "none"). Show level,
    // tune, detune, pan, pulse-width, spread if any are set.
    for (int i = 1; i <= 4; ++i) {
        auto wIt = preset.waves.find(i);
        const bool hasWave = wIt != preset.waves.end() && wIt->second != "none";
        const auto suffix = std::to_string(i);
        const bool anyParam = has("level_" + suffix) || has("tune_" + suffix) ||
                              has("detune_" + suffix) || has("pan_" + suffix) ||
                              has("pulse_width_" + suffix) || has("spread_" + suffix);
        if (!hasWave && !anyParam)
            continue;
        out << "  osc " << i << "  ";
        out << (hasWave ? juce::String(wIt->second) : juce::String("(unchanged)"));
        if (has("level_" + suffix))
            out << "  lvl " << formatNorm(get("level_" + suffix));
        if (has("tune_" + suffix)) {
            const int st = static_cast<int>(std::round(get("tune_" + suffix)));
            out << "  tune " << (st >= 0 ? "+" : "") << st << "st";
        }
        if (has("fine_tune_" + suffix)) {
            const int c = static_cast<int>(std::round(get("fine_tune_" + suffix)));
            out << "  fine " << (c >= 0 ? "+" : "") << c << "c";
        }
        if (has("detune_" + suffix))
            out << "  det " << formatNorm(get("detune_" + suffix));
        if (has("pan_" + suffix))
            out << "  pan " << formatNorm(get("pan_" + suffix));
        if (has("pulse_width_" + suffix))
            out << "  pw " << formatNorm(get("pulse_width_" + suffix));
        if (has("spread_" + suffix))
            out << "  spr " << formatNorm(get("spread_" + suffix));
        out << "\n";
    }

    // Filter row
    if (!preset.filterType.empty() || has("filter_freq") || has("filter_resonance") ||
        has("filter_amount") || has("filter_attack") || has("filter_decay") ||
        has("filter_sustain") || has("filter_release")) {
        out << "  filter  ";
        out << (preset.filterType.empty() ? juce::String("(unchanged)")
                                          : juce::String(preset.filterType));
        if (has("filter_freq"))
            out << "  freq " << formatNorm(get("filter_freq"));
        if (has("filter_resonance"))
            out << "  res " << formatNorm(get("filter_resonance"));
        if (has("filter_amount"))
            out << "  amt " << formatNorm(get("filter_amount"));
        out << "\n";
        if (has("filter_attack") || has("filter_decay") || has("filter_sustain") ||
            has("filter_release")) {
            out << "  filter env  ";
            if (has("filter_attack"))
                out << "A " << formatSeconds(get("filter_attack")) << "  ";
            if (has("filter_decay"))
                out << "D " << formatSeconds(get("filter_decay")) << "  ";
            if (has("filter_sustain"))
                out << "S " << formatNorm(get("filter_sustain")) << "  ";
            if (has("filter_release"))
                out << "R " << formatSeconds(get("filter_release"));
            out << "\n";
        }
    }

    // Amp envelope row
    if (has("amp_attack") || has("amp_decay") || has("amp_sustain") || has("amp_release") ||
        has("amp_velocity")) {
        out << "  amp env  ";
        if (has("amp_attack"))
            out << "A " << formatSeconds(get("amp_attack")) << "  ";
        if (has("amp_decay"))
            out << "D " << formatSeconds(get("amp_decay")) << "  ";
        if (has("amp_sustain"))
            out << "S " << formatNorm(get("amp_sustain")) << "  ";
        if (has("amp_release"))
            out << "R " << formatSeconds(get("amp_release"));
        if (has("amp_velocity"))
            out << "  vel " << formatNorm(get("amp_velocity"));
        out << "\n";
    }

    // Voice mode + legato row (only print if either is set)
    if (!preset.voiceMode.empty() || has("legato")) {
        out << "  voice  ";
        if (!preset.voiceMode.empty())
            out << juce::String(preset.voiceMode);
        if (has("legato"))
            out << "  legato " << formatNorm(get("legato"));
        out << "\n";
    }

    // Modulators (LFO rate + depth)
    for (int i = 1; i <= 2; ++i) {
        const auto suffix = std::to_string(i);
        if (has("rate_" + suffix) || has("depth_" + suffix)) {
            out << "  mod " << i << "  ";
            if (has("rate_" + suffix))
                out << "rate " << formatNorm(get("rate_" + suffix)) << "  ";
            if (has("depth_" + suffix))
                out << "depth " << formatNorm(get("depth_" + suffix));
            out << "\n";
        }
    }

    // FX / global. Only print if at least one is set.
    juce::String fxLine;
    auto addFx = [&](const char* label, const std::string& key) {
        if (has(key))
            fxLine << label << " " << formatNorm(get(key)) << "  ";
    };
    addFx("level", "level");
    addFx("dist", "distortion");
    addFx("mix", "mix");
    addFx("size", "size");
    addFx("speed", "speed");
    addFx("feedback", "feedback");
    addFx("width", "width");
    if (fxLine.isNotEmpty())
        out << "  master  " << fxLine.trim() << "\n";

    // FX gate state — show which FX blocks are actually enabled. Without
    // this row the user can't tell at a glance whether a `dist 0.4` or
    // `size 0.7` is audible or sitting behind a closed gate.
    if (!preset.fx.empty()) {
        juce::String gates;
        auto addGate = [&](const char* label, const std::string& key) {
            auto it = preset.fx.find(key);
            if (it != preset.fx.end())
                gates << label << " " << (it->second ? "on" : "off") << "  ";
        };
        addGate("dist", "distortion");
        addGate("reverb", "reverb");
        addGate("delay", "delay");
        addGate("chorus", "chorus");
        if (gates.isNotEmpty())
            out << "  fx      " << gates.trim() << "\n";
    }

    return out;
}

// Apply a parsed preset to the currently focused 4OSC device. If there
// isn't one (no selection, selection isn't a device, or focused device
// is something other than 4OSC), spin up a new track with a fresh 4OSC
// instance and apply there — wasting the LLM's output to "no device
// focused" was just bad UX. Returns a one-line status string for the
// chat. Delegates the actual write to magda::applyFourOscPresetToPath.
static juce::String applyFourOscPresetToFocusedDevice(const magda::FourOscAgent::Preset& preset) {
    auto& sel = magda::SelectionManager::getInstance();
    auto& tm = magda::TrackManager::getInstance();

    magda::ChainNodePath path;
    magda::DeviceInfo* device = nullptr;
    if (sel.hasChainNodeSelection()) {
        path = sel.getSelectedChainNode();
        if (auto* d = tm.getDeviceInChainByPath(path);
            d != nullptr && d->pluginId.equalsIgnoreCase("4osc"))
            device = d;
    }

    juce::String preamble;
    if (device == nullptr) {
        magda::DeviceInfo newDevice;
        const juce::String trackName =
            preset.name.empty() ? juce::String("4OSC") : juce::String(preset.name);
        newDevice.name = "4OSC";
        newDevice.manufacturer = "MAGDA";
        newDevice.pluginId = "4osc";
        newDevice.uniqueId = "4osc";
        newDevice.fileOrIdentifier = "4osc";
        newDevice.isInstrument = true;
        newDevice.deviceType = magda::DeviceType::Instrument;
        newDevice.format = magda::PluginFormat::Internal;

        const auto trackId = tm.createTrack(trackName, magda::TrackType::Audio);
        if (trackId == magda::INVALID_TRACK_ID)
            return "(could not create track for preset)";
        const auto deviceId = tm.addDeviceToTrack(trackId, newDevice);
        if (deviceId == magda::INVALID_DEVICE_ID)
            return "(could not add 4OSC to new track)";

        path = magda::ChainNodePath{};
        path.trackId = trackId;
        path.topLevelDeviceId = deviceId;
        device = tm.getDeviceInChainByPath(path);
        if (device == nullptr)
            return "(created 4OSC but could not resolve its path)";

        sel.selectChainNode(path);
        preamble = "created 4OSC on '" + trackName + "', ";
    }

    return preamble + magda::applyFourOscPresetToPath(preset, path);
}

void AIChatConsoleContent::finishPresetGeneration(bool success, const juce::String& errorOrPretty,
                                                  juce::String presetName) {
    if (!success) {
        appendToChat(juce::String::charToString(0x25C6) + " Error: " + errorOrPretty);
        return;
    }
    // Re-parse the JSON we just rendered to recover a typed Preset for the
    // applier. (We could thread the original Preset through callAsync, but
    // that means promoting the agent header into the .hpp — re-parse keeps
    // this layer slimmer; the JSON is small.)
    auto parsed = juce::JSON::parse(errorOrPretty);
    auto* obj = parsed.getDynamicObject();
    if (obj == nullptr)
        return;
    magda::FourOscAgent::Preset preset;
    if (auto n = obj->getProperty("name"); n.isString())
        preset.name = n.toString().toStdString();
    if (auto c = obj->getProperty("category"); c.isString())
        preset.category = c.toString().toStdString();
    if (auto d = obj->getProperty("description"); d.isString())
        preset.description = d.toString().toStdString();
    if (auto* params = obj->getProperty("params").getDynamicObject()) {
        for (const auto& kv : params->getProperties()) {
            if (kv.value.isDouble() || kv.value.isInt())
                preset.params.emplace(kv.name.toString().toStdString(),
                                      static_cast<float>(static_cast<double>(kv.value)));
        }
    }
    if (auto* waves = obj->getProperty("waves").getDynamicObject()) {
        for (const auto& kv : waves->getProperties()) {
            if (!kv.value.isString())
                continue;
            const int oscNum = kv.name.toString().getIntValue();
            if (oscNum < 1 || oscNum > 4)
                continue;
            preset.waves.emplace(oscNum, kv.value.toString().toStdString());
        }
    }
    if (auto ft = obj->getProperty("filter_type"); ft.isString())
        preset.filterType = ft.toString().toStdString();
    if (auto vm = obj->getProperty("voice_mode"); vm.isString())
        preset.voiceMode = vm.toString().toStdString();
    if (auto* fx = obj->getProperty("fx").getDynamicObject()) {
        for (const auto& kv : fx->getProperties()) {
            if (kv.value.isBool() || kv.value.isInt() || kv.value.isDouble())
                preset.fx.emplace(kv.name.toString().toStdString(), static_cast<bool>(kv.value));
        }
    }
    // /design --category=<cat> wins over whatever the agent picked. Cleared
    // here so a subsequent /design without the flag falls back to inference.
    if (pendingCategoryOverride_.isNotEmpty()) {
        preset.category = pendingCategoryOverride_.toStdString();
        pendingCategoryOverride_.clear();
    }

    // Render the preset itself (header + categorized summary), then the
    // apply status as the tail line. Done together so the chat shows
    // one block per /design call instead of split JSON + status.
    auto header = presetName.isEmpty() ? juce::String("Preset") : presetName;
    juce::String body = juce::String::charToString(0x25C6) + " " + header + "\n";
    body << prettyPrintPreset(preset);
    body << "\n  " << applyFourOscPresetToFocusedDevice(preset);
    body << "\n  starting point - tweak by ear, then save from the device header";
    appendToChat(body);
}

void AIChatConsoleContent::clearInput() {
    inputDocument_.replaceAllContent({});
    // Force a repaint — replaceAllContent fires document listeners but
    // CodeEditorComponent caches its glyph layer per-line and on macOS
    // (with our custom fonts) sometimes leaves stale pixels where the
    // previous content was rendered. Repaint nukes the cache.
    if (inputBox_) {
        inputBox_->moveCaretToTop(false);
        inputBox_->repaint();
    }
}

}  // namespace magda::daw::ui
