#include "AIChatConsoleContent.hpp"

#include <algorithm>
#include <atomic>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <tuple>

#include "../../../../agents/agent_tool_bridge.hpp"
#include "../../../../agents/automation_agent.hpp"
#include "../../../../agents/command_agent.hpp"
#include "../../../../agents/console_agent_orchestrator.hpp"
#include "../../../../agents/controller_profile_agent.hpp"
#include "../../../../agents/drummer_agent.hpp"
#include "../../../../agents/dsl_interpreter.hpp"
#include "../../../../agents/internal_plugins.hpp"
#include "../../../../agents/llama_model_manager.hpp"
#include "../../../../agents/llm_presets.hpp"
#include "../../../../agents/llm_tool_model.hpp"
#include "../../../../agents/midi_context.hpp"
#include "../../../../agents/mixing_agent.hpp"
#include "../../../../agents/music_agent.hpp"
#include "../../../../agents/theme_agent.hpp"
#include "../../../api/magda_api_live.hpp"
#include "../../../api/remote_api_host.hpp"
#include "../../../api/remote_service.hpp"
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
#include "../../code/SyntaxTheme.hpp"
#include "../../components/common/SvgButton.hpp"
#include "../../dialogs/settings/SettingsLink.hpp"
#include "../../state/TimelineController.hpp"
#include "../../themes/ActiveTheme.hpp"
#include "../../themes/FontManager.hpp"
#include "../../themes/SmallButtonLookAndFeel.hpp"
#include "../../themes/ThemePrompt.hpp"
#include "BinaryData.h"
#include "PluginBrowserContent.hpp"
#include "audio/plugins/DrumGridRoles.hpp"
#include "audio/plugins/MagdaSamplerPlugin.hpp"
#include "engine/AudioEngine.hpp"
#include "engine/PluginMetadataStore.hpp"

namespace magda::daw::ui {

namespace {
// Forward declaration so RequestThread::run can call the formatter — the
// definition lives further down in the file's other anon-namespace block,
// next to isDrummerTrack().
juce::String formatClipAsDrummerContext(magda::ClipId clipId);
juce::String formatSelectedClipsAsDrummerContext();

/**
 * The tool-calling console path (#2295): the surface's agent runs the bounded
 * observe-act runtime, and every call executes through RemoteApiService — the
 * same schema validation, scope enforcement, revision tracking, and audit an
 * MCP client gets, with the ConsoleRouting allowlist as its tool set.
 *
 * Preflight rather than commitment: returns nullopt when the loop is switched
 * off in config, no remote host exists, or the configured provider cannot
 * carry tool calls — and the caller falls through to the DSL workflows. Once
 * the run starts, its outcome is the response, success or not: mutations have
 * gone through the service, so falling back to a second execution path would
 * apply the request twice.
 */
std::optional<std::string> runAgentToolLoop(magda::AgentSurfaceId surfaceId,
                                            const std::string& message,
                                            const std::string& priorConversation,
                                            const magda::agent::CancellationToken& cancellation) {
    auto& config = magda::Config::getInstance();
    if (!config.getAgentToolLoopEnabled())
        return std::nullopt;
    // Read from the request thread: safe because the host outlives every
    // console request thread — it is created at startup and destroyed at
    // shutdown, after this panel (and its RequestThread) are gone.
    auto* host = magda::remote::activeHost();
    if (host == nullptr)
        return std::nullopt;

    const auto llmConfig = config.getAgentLLMConfig(magda::role::COMMAND);
    // The on-device command model is an intent tagger, not a tool-calling
    // model; the DSL path is the one that knows how to drive it. The embedded
    // llama client likewise refuses any request that carries tools, so
    // starting the loop with it could only end in a model error — preflight
    // both and let the DSL workflows take the request instead.
    if (llmConfig.provider == magda::provider::FAST_INFERENCE ||
        llmConfig.provider == magda::provider::LLAMA_LOCAL)
        return std::nullopt;
    auto client = magda::createLLMClient(llmConfig, "tool-loop");
    if (client == nullptr)
        return std::nullopt;

    const auto& surface = magda::agentSurface(surfaceId);
    auto tools = magda::agent::agentToolsForSurface(surface);
    if (tools.empty())
        return std::nullopt;

    magda::agent::LlmToolModel model(*client);
    magda::agent::RemoteAgentToolExecutor executor(host->service(), surface);
    // Approving here matches what the DSL executors do today — they apply
    // mutations without a per-step prompt. The hook is the seam where a real
    // approval UI lands; the surface's `approveMutations` policy is what it
    // will read.
    magda::agent::AgentRuntime runtime(model, executor, [](const magda::agent::ApprovalRequest&) {
        return magda::agent::ApprovalDecision{};
    });

    magda::agent::AgentDefinition definition;
    definition.id = "console-" + juce::String(surface.name);
    definition.systemPrompt = "You are MAGDA's " + juce::String(surface.name) + " agent. " +
                              juce::String(surface.responsibility);
    for (const auto& fragment : surface.promptFragments)
        definition.systemPrompt += "\n" + juce::String(fragment);
    definition.systemPrompt +=
        "\nAct by calling tools; inspect state before mutating it. When the work is done, "
        "reply with a short summary and no further tool calls.";
    definition.tools = std::move(tools);
    definition.budget.maxSteps = surface.runPolicy.maxSteps;
    definition.budget.maxMutations = surface.runPolicy.maxMutations;

    magda::agent::AgentRunInput input;
    input.userMessage = priorConversation.empty()
                            ? juce::String(message)
                            : juce::String(priorConversation) + "\nUser request: " + message;
    input.projectRevision = host->service().currentRevision();

    const auto result = runtime.run(definition, input, cancellation);

    std::string response = result.finalText.toStdString();
    if (result.state != magda::agent::RunState::Completed) {
        if (!response.empty())
            response += "\n";
        response += "[agent stopped: " + std::string(magda::agent::toString(result.reason));
        if (result.detail.isNotEmpty())
            response += ": " + result.detail.toStdString();
        response += "]";
    }
    if (result.mutations > 0)
        response += "\n[" + std::to_string(result.mutations) + " change(s) applied in " +
                    std::to_string(result.steps) + " step(s)]";
    return response;
}
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
                           ActiveTheme::getColour(ActiveTheme::SURFACE));
        listBox_.setColour(juce::ListBox::outlineColourId, ActiveTheme::getBorderColour());
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
            g.setColour(ActiveTheme::getColour(ActiveTheme::ACCENT_PRIMARY).withAlpha(0.3f));
            g.fillRect(0, 0, width, height);
        }

        if (mode_ == Mode::Alias) {
            const auto& entry = *filtered_[static_cast<size_t>(rowNumber)];
            g.setColour(ActiveTheme::getAccentColour());
            g.setFont(FontManager::getInstance().getMonoFont(11.0f));
            g.drawText("@" + entry.alias, 6, 0, width / 2, height,
                       juce::Justification::centredLeft);
            g.setColour(ActiveTheme::getSecondaryTextColour());
            g.setFont(FontManager::getInstance().getUIFont(10.0f));
            g.drawText(entry.pluginName, width / 2, 0, width / 2 - 6, height,
                       juce::Justification::centredRight);
        } else if (mode_ == Mode::SlashCommand) {
            const auto& cmd = *filteredCommands_[static_cast<size_t>(rowNumber)];
            g.setColour(ActiveTheme::getAccentColour());
            g.setFont(FontManager::getInstance().getMonoFont(11.0f));
            g.drawText("/" + cmd.name, 6, 0, width / 3, height, juce::Justification::centredLeft);
            g.setColour(ActiveTheme::getSecondaryTextColour());
            g.setFont(FontManager::getInstance().getUIFont(10.0f));
            g.drawText(cmd.description, width / 3, 0, width * 2 / 3 - 6, height,
                       juce::Justification::centredLeft);
        } else {
            const auto& entry = *filteredParams_[static_cast<size_t>(rowNumber)];
            g.setColour(ActiveTheme::getAccentColour());
            g.setFont(FontManager::getInstance().getMonoFont(11.0f));
            g.drawText("@" + entry.pluginAlias + "." + entry.paramAlias, 6, 0, width / 2, height,
                       juce::Justification::centredLeft);
            g.setColour(ActiveTheme::getSecondaryTextColour());
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
                              ActiveTheme::getColour(ActiveTheme::BUTTON_NORMAL));
            button->setColour(juce::TextButton::buttonOnColourId,
                              ActiveTheme::getColour(ActiveTheme::SURFACE_HOVER));
            button->setColour(juce::TextButton::textColourOffId, ActiveTheme::getTextColour());
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
                           ActiveTheme::getColour(ActiveTheme::SURFACE));
        listBox_.setColour(juce::ListBox::outlineColourId, ActiveTheme::getBorderColour());
        addAndMakeVisible(listBox_);

        buildRows();
        const int listHeight =
            juce::jlimit(kRowHeight, 320, static_cast<int>(rows_.size()) * kRowHeight);
        setSize(310, kToolbarHeight + listHeight + 16);
    }

    void paint(juce::Graphics& g) override {
        g.fillAll(ActiveTheme::getColour(ActiveTheme::SURFACE));
        g.setColour(ActiveTheme::getBorderColour());
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
            g.fillAll(ActiveTheme::getColour(ActiveTheme::SURFACE_HOVER));

        if (row.kind == Row::Kind::Empty) {
            g.setColour(ActiveTheme::getSecondaryTextColour());
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
                                           static_cast<float>(height - 13) / 2.0f, 13.0f, 13.0f);
        g.setColour(ActiveTheme::getBorderColour());
        g.drawRoundedRectangle(tick, 2.0f, 1.0f);
        if (checked || partial) {
            g.setColour(ActiveTheme::getAccentColour());
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
        g.setColour(row.kind == Row::Kind::Track ? ActiveTheme::getTextColour()
                                                 : ActiveTheme::getSecondaryTextColour());
        g.setFont(row.kind == Row::Kind::Track
                      ? FontManager::getInstance().getMonoFont(11.0f).boldened()
                      : FontManager::getInstance().getMonoFont(11.0f));
        g.drawText(row.label, textX, 0, width - textX - textRightPadding, height,
                   juce::Justification::centredLeft, true);

        if (row.detail.isNotEmpty()) {
            g.setColour(ActiveTheme::getSecondaryTextColour().withAlpha(0.65f));
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
                    clipRow.detail += juce::String::fromUTF8(" · live");
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
            std::scoped_lock lk(singleState->mu);
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
                std::scoped_lock lk(singleState->mu);
                chunk = std::move(singleState->pending);
                singleState->pending.clear();
            }
            if (chunk.isEmpty())
                return;
            auto text = safeThis->chatHistory_.getText();
            safeThis->chatHistory_.setText(text + chunk);
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

    // The fast-inference raw-message rule now lives in
    // ConsoleAgentOrchestrator, which owns prompt composition.
    // Step 2: Dispatch directly from the selected surface.
    std::string dslCode;                                   // DSL from command agent
    std::vector<magda::Instruction> musicInstructions;     // IR from music agent
    std::string musicDescription;                          // description from DSL music agent
    std::vector<magda::AutoInstruction> autoInstructions;  // IR from automation agent
    std::string mixAnalysis;                               // prose from the mix-analysis agent
    std::string error;

    auto agentStart = std::chrono::steady_clock::now();

    // Tool-calling path first (#2295): when enabled, the agent acts through
    // RemoteApiService with the surface allowlist as its tool set, and its
    // summary flows through the same prose channel the mix agent uses. A
    // nullopt is a preflight refusal, and the DSL workflows below run instead.
    if (auto toolLoopResponse = runAgentToolLoop(
            surfaceDecision.surface, message, priorContext,
            magda::agent::CancellationToken([this] { return threadShouldExit(); }))) {
        if (threadShouldExit())
            return;
        mixAnalysis = std::move(*toolLoopResponse);
    } else {
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
    }

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

            // Read before the move below; the caveat check further down used to
            // test `error` after it had been moved into `output`.
            const bool hadError = !error.empty();

            magda::agent::ConsoleRunOutput output{
                .dslCode = dsl,
                .musicInstructions = musicIR,
                .musicDescription = musicDesc,
                .automationInstructions = autoIR,
                .prose = mixAnalysis,
                .error = error,
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
            if (!mixAnalysis.empty() && !hadError) {
                safeThis->chatHistory_.appendDim(
                    juce::String(magda::MixAnalysisAgent::getUserCaveat()) + "\n\n");
            }

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

    auto monoFont = FontManager::getInstance().getMonoFont(13.0f);
    chatHistory_.setEmptyHint("Describe what you want to hear. MAGDA writes clips, edits "
                              "selections and arranges on the target track.");
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
    // of relying on transparency.
    inputBox_->setColour(juce::CodeEditorComponent::backgroundColourId,
                         ActiveTheme::getColour(ActiveTheme::BUTTON_NORMAL));
    inputBox_->setColour(juce::CodeEditorComponent::defaultTextColourId,
                         ActiveTheme::getTextColour());
    inputBox_->setColour(juce::CodeEditorComponent::lineNumberBackgroundId,
                         juce::Colours::transparentBlack);
    inputBox_->setColour(juce::CodeEditorComponent::highlightColourId,
                         ActiveTheme::getColour(ActiveTheme::ACCENT_PRIMARY).withAlpha(0.3f));
    inputBox_->setColour(juce::CaretComponent::caretColourId, ActiveTheme::getTextColour());
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
    contextLabel_.setColour(juce::Label::textColourId, ActiveTheme::getSecondaryTextColour());
    contextLabel_.setColour(juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    contextLabel_.setColour(juce::Label::outlineColourId, juce::Colours::transparentBlack);
    contextLabel_.setBorderSize(juce::BorderSize<int>(0, 2, 0, 4));
    contextLabel_.setInterceptsMouseClicks(true, false);
    contextLabel_.addMouseListener(this, false);
    contextLabel_.setMouseCursor(juce::MouseCursor::PointingHandCursor);
    contextLabel_.setTooltip("Choose MIDI clips to include as agent context");
    addAndMakeVisible(contextLabel_);

    outputMode_.setOptions({"New clip", "Revise last"});
    outputMode_.onChange = [this](int index) {
        if (index == 1 && isLastGeneratedMidiClipValid()) {
            midiOutputMode_ = MidiOutputMode::ReviseLast;
            contextEnabled_ = true;
            magda::dsl::Interpreter::setContextEnabled(true);
        } else {
            midiOutputMode_ = MidiOutputMode::NewClip;
        }
        updateOutputModeButton();
    };
    addAndMakeVisible(outputMode_);
    updateOutputModeButton();

    // Mix analysis is gathered from the mixer's Analyze button now (#886); this
    // panel observes MixAnalysisService so it can show a "mix analysis ready" chip
    // in mixer view and use the latest measurement as agent context.
    analysisChip_.setJustificationType(juce::Justification::centredLeft);
    analysisChip_.setFont(juce::Font(11.0f));
    analysisChip_.setColour(juce::Label::textColourId,
                            ActiveTheme::getColour(ActiveTheme::ACCENT_INFO));
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
    sendButton_.setAlpha(0.45f);
    sendButton_.setTooltip("Send");
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
        juce::SystemClipboard::copyTextToClipboard(chatHistory_.getPlainText());
    };
    addAndMakeVisible(copyButton_);

    // Config status bar
    configStatusLabel_.setFont(FontManager::getInstance().getMonoFont(10.0f));
    configStatusLabel_.setColour(juce::Label::textColourId,
                                 ActiveTheme::getSecondaryTextColour().withAlpha(0.6f));
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
    if (auto* engine = magda::TrackManager::getInstance().getAudioEngine()) {
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
    themeAgent_ = std::make_unique<magda::ThemeAgent>();
}

AIChatConsoleContent::~AIChatConsoleContent() {
    magda::MixAnalysisService::getInstance().removeListener(this);
    magda::ViewModeController::getInstance().removeListener(this);
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
    const auto aliasLength = [](const AliasEntry& entry) { return entry.alias.length(); };
    std::ranges::sort(sorted, std::ranges::greater{}, aliasLength);

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
    // Slash commands dispatch through the central registry. Returns
    // true when the message was fully consumed (intercepting commands like
    // /controller, /theme, or any meta-flag like --help). Returns false
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
    updateSendState();

    chatHistory_.noteNextUserTarget(contextEnabled_ ? contextText_ : juce::String());
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
    updateSendState();
}

void AIChatConsoleContent::updateSendState() {
    sendReady_ = processing_ || inputDocument_.getAllContent().trim().isNotEmpty();
    sendButton_.setAlpha(sendReady_ ? 1.0f : 0.45f);
    repaint(sendButton_.getBounds());
}

void AIChatConsoleContent::setThemedButtonIcon(juce::DrawableButton& button, const void* svgData,
                                               std::size_t svgDataSize) {
    auto icon = juce::Drawable::createFromImageData(svgData, svgDataSize);
    if (icon)
        ActiveTheme::applyToSvgIcon(*icon);
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
    if (inputBox_ != nullptr) {
        inputBox_->setColour(juce::CodeEditorComponent::backgroundColourId,
                             ActiveTheme::getColour(ActiveTheme::BUTTON_NORMAL));
        inputBox_->setColour(juce::CodeEditorComponent::defaultTextColourId,
                             ActiveTheme::getTextColour());
        inputBox_->setColour(juce::CodeEditorComponent::highlightColourId,
                             ActiveTheme::getColour(ActiveTheme::ACCENT_PRIMARY).withAlpha(0.3f));
        inputBox_->setColour(juce::CaretComponent::caretColourId, ActiveTheme::getTextColour());
        // A CodeEditorComponent caches the scheme it got from its tokeniser at
        // construction, so the @plugin / /command colours need re-installing.
        inputBox_->setColourScheme(inputTokeniser_.getDefaultColourScheme());
    }
    contextLabel_.setColour(juce::Label::textColourId, ActiveTheme::getSecondaryTextColour());
    analysisChip_.setColour(juce::Label::textColourId,
                            ActiveTheme::getColour(ActiveTheme::ACCENT_INFO));
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
    }
}

void AIChatConsoleContent::appendToChat(const juce::String& text) {
    chatHistory_.append(text + "\n");
}

void AIChatConsoleContent::paint(juce::Graphics& g) {
    g.fillAll(ActiveTheme::getColour(ActiveTheme::BACKGROUND));

    // Header: brand, model chip.
    g.setColour(ActiveTheme::getPanelBackgroundColour());
    g.fillRect(headerBounds_);
    g.setColour(ActiveTheme::getColour(ActiveTheme::SEPARATOR));
    g.drawHorizontalLine(headerBounds_.getBottom() - 1, 0.0f, static_cast<float>(getWidth()));

    auto brand = headerBounds_.reduced(12, 0);
    const auto mark = brand.removeFromLeft(10).toFloat().withSizeKeepingCentre(9.0f, 9.0f);
    juce::Path diamond;
    diamond.addQuadrilateral(mark.getCentreX(), mark.getY(), mark.getRight(), mark.getCentreY(),
                             mark.getCentreX(), mark.getBottom(), mark.getX(), mark.getCentreY());
    g.setColour(ActiveTheme::getAccentColour());
    g.fillPath(diamond);
    brand.removeFromLeft(8);
    g.setColour(ActiveTheme::getTextColour());
    g.setFont(
        FontManager::getInstance().getMonoFont(12.0f).boldened().withExtraKerningFactor(0.1f));
    if (showWordmark_)
        g.drawText("MAGDA", brand, juce::Justification::centredLeft, false);

    if (!modelChipBounds_.isEmpty()) {
        const auto chip = modelChipBounds_.toFloat();
        g.setColour(ActiveTheme::getColour(ActiveTheme::SURFACE));
        g.fillRoundedRectangle(chip, 5.0f);
        g.setColour(ActiveTheme::getBorderColour());
        g.drawRoundedRectangle(chip.reduced(0.5f), 5.0f, 1.0f);
        const bool live = !configStatusLabel_.getText().containsIgnoreCase("no model");
        g.setColour(live ? ActiveTheme::getColour(ActiveTheme::STATUS_SUCCESS)
                         : ActiveTheme::getColour(ActiveTheme::TEXT_DIM));
        g.fillEllipse(chip.getX() + 9.0f, chip.getCentreY() - 3.0f, 6.0f, 6.0f);
        const auto c = juce::Point<float>(chip.getRight() - 12.0f, chip.getCentreY());
        juce::Path caret;
        caret.addTriangle(c.x - 3.5f, c.y - 1.5f, c.x + 3.5f, c.y - 1.5f, c.x, c.y + 2.5f);
        g.setColour(ActiveTheme::getSecondaryTextColour());
        g.fillPath(caret);
    }

    // Composer: one field holding the prompt with its controls underneath.
    const auto composer = composerBounds_.toFloat();
    g.setColour(ActiveTheme::getColour(ActiveTheme::BUTTON_NORMAL));
    g.fillRoundedRectangle(composer, 6.0f);
    g.setColour(ActiveTheme::getBorderColour());
    g.drawRoundedRectangle(composer.reduced(0.5f), 6.0f, 1.0f);

    if (!contextChipBounds_.isEmpty()) {
        const auto chip = contextChipBounds_.toFloat();
        g.setColour(ActiveTheme::getColour(ActiveTheme::SURFACE));
        g.fillRoundedRectangle(chip, 5.0f);
        g.setColour(ActiveTheme::getBorderColour());
        g.drawRoundedRectangle(chip.reduced(0.5f), 5.0f, 1.0f);
    }

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
    if (icon != nullptr) {
        const auto colour = contextEnabled_ ? ActiveTheme::getAccentColour()
                                            : ActiveTheme::getSecondaryTextColour().withAlpha(0.3f);
        auto iconCopy = icon->createCopy();
        iconCopy->replaceColour(juce::Colour(0xFFB3B3B3), colour);
        iconCopy->replaceColour(juce::Colours::white, colour);
        // The footer view glyphs use fill="currentColor", which JUCE resolves to black.
        iconCopy->replaceColour(juce::Colours::black, colour);
        iconCopy->drawWithin(g, contextIconBounds_.toFloat().reduced(5.0f),
                             juce::RectanglePlacement::centred, 1.0f);
    }

    // Send: a filled accent square once there is something to send.
    const auto send = sendButton_.getBounds().toFloat();
    g.setColour(sendReady_ ? ActiveTheme::getAccentColour()
                           : ActiveTheme::getColour(ActiveTheme::SURFACE));
    g.fillRoundedRectangle(send, 5.0f);
    if (!sendReady_) {
        g.setColour(ActiveTheme::getBorderColour());
        g.drawRoundedRectangle(send.reduced(0.5f), 5.0f, 1.0f);
    }

    paintKeyHints(g);
}

void AIChatConsoleContent::paintKeyHints(juce::Graphics& g) const {
    if (hintsBounds_.isEmpty())
        return;
    const auto font = FontManager::getInstance().getMonoFont(10.5f);
    g.setFont(font);
    auto drawHints = [&](juce::Rectangle<int> area,
                         const std::vector<std::pair<juce::String, juce::String>>& hints,
                         bool fromRight) {
        auto row = area;
        for (size_t i = 0; i < hints.size(); ++i) {
            const auto& [key, label] = hints[fromRight ? hints.size() - 1 - i : i];
            const int keyW =
                juce::roundToInt(juce::GlyphArrangement::getStringWidth(font, key)) + 8;
            const int labelW =
                juce::roundToInt(juce::GlyphArrangement::getStringWidth(font, label)) + 2;
            auto slot = fromRight ? row.removeFromRight(keyW + 5 + labelW)
                                  : row.removeFromLeft(keyW + 5 + labelW);
            const auto keyBox = slot.removeFromLeft(keyW).withSizeKeepingCentre(keyW, 15).toFloat();
            g.setColour(ActiveTheme::getBorderColour());
            g.drawRoundedRectangle(keyBox.reduced(0.5f), 3.0f, 1.0f);
            g.setColour(ActiveTheme::getColour(ActiveTheme::TEXT_DIM));
            g.drawText(key, keyBox, juce::Justification::centred, false);
            slot.removeFromLeft(5);
            g.drawText(label, slot, juce::Justification::centredLeft, false);
            if (fromRight)
                row.removeFromRight(12);
            else
                row.removeFromLeft(12);
        }
    };
    const auto enter = juce::String::fromUTF8("\xe2\x86\xb5");
    const auto shift = juce::String::fromUTF8("\xe2\x87\xa7");
    auto area = hintsBounds_;
    drawHints(area.removeFromLeft(area.getWidth() / 2),
              {{enter, "send"}, {shift + enter, "newline"}}, false);
    drawHints(area, {{"@", "plugin"}, {"/", "command"}}, true);
}

void AIChatConsoleContent::resized() {
    auto bounds = getLocalBounds();

    // Header: MAGDA, the model chip (opens AI settings), copy and clear.
    headerBounds_ = bounds.removeFromTop(40);
    auto header = headerBounds_.reduced(12, 8);
    copyButton_.setBounds(header.removeFromRight(24));
    header.removeFromRight(4);
    clearButton_.setBounds(header.removeFromRight(24));
    // On a narrow console the wordmark goes, then the model chip's label (#3117).
    const bool showServer = serverToggleButton_ && serverToggleButton_->isVisible();
    const int serverWidth = showServer ? 30 : 0;
    constexpr int kWordmarkWidth = 78;  // diamond + MAGDA
    constexpr int kChipMinWidth = 44;
    showWordmark_ = header.getWidth() >= kWordmarkWidth + kChipMinWidth + serverWidth;
    header.removeFromLeft(showWordmark_ ? kWordmarkWidth : 20);
    const int labelWidth =
        juce::jmin(header.getWidth() - serverWidth - 60,
                   juce::roundToInt(juce::GlyphArrangement::getStringWidth(
                       configStatusLabel_.getFont(), configStatusLabel_.getText())) +
                       8);
    modelChipBounds_ = header.removeFromLeft(
        juce::jmin(juce::jmax(0, labelWidth) + 44, juce::jmax(0, header.getWidth() - serverWidth)));
    configStatusLabel_.setBounds(modelChipBounds_.withTrimmedLeft(20).withTrimmedRight(22));
    if (showServer) {
        header.removeFromLeft(6);
        serverToggleButton_->setBounds(header.removeFromLeft(24).reduced(2));
    }

    // Key hints along the bottom edge, the composer above them.
    auto footer = bounds.removeFromBottom(26).reduced(12, 0);
    hintsBounds_ = footer.withTrimmedBottom(6);
    composerBounds_ = bounds.removeFromBottom(112).reduced(10, 0);

    auto composer = composerBounds_.reduced(8, 8);
    auto controls = composer.removeFromBottom(28);
    bottomBarBounds_ = controls;
    composer.removeFromBottom(6);
    inputBox_->setBounds(composer.withTrimmedLeft(4));

    sendButton_.setBounds(controls.removeFromRight(28));
    controls.removeFromRight(8);
    if (analysisChip_.isVisible()) {
        // Leaves the context chip its 60px minimum on a narrow console (#3117).
        analysisChip_.setBounds(
            controls.removeFromRight(juce::jlimit(0, 120, controls.getWidth() - 76)));
        controls.removeFromRight(8);
    }

    const auto contextFont = contextLabel_.getFont();
    const int contextWidth = juce::jlimit(60, juce::jmax(60, controls.getWidth() / 2),
                                          juce::roundToInt(juce::GlyphArrangement::getStringWidth(
                                              contextFont, contextLabel_.getText())) +
                                              34);
    contextChipBounds_ = controls.removeFromLeft(contextWidth);
    auto chip = contextChipBounds_;
    contextIconBounds_ = chip.removeFromLeft(24);
    contextLabel_.setBounds(chip.withTrimmedRight(4));
    controls.removeFromLeft(8);
    if (outputMode_.isVisible())
        outputMode_.setBounds(
            controls.removeFromLeft(juce::jmin(controls.getWidth(), outputMode_.getIdealWidth())));

    bounds.removeFromBottom(10);
    chatHistory_.setBounds(bounds);
}

void AIChatConsoleContent::onActivated() {
    buildAliasList();
    updateConfigStatus();
    updateAnalysisChip();
    if (isShowing())
        inputBox_->grabKeyboardFocus();
}

void AIChatConsoleContent::onDeactivated() {
    // Could save chat history here
}

// ============================================================================
// ProjectManagerListener
// ============================================================================

void AIChatConsoleContent::projectOpened(const magda::ProjectInfo& /*info*/) {
    resetForProjectBoundary();
}

void AIChatConsoleContent::projectClosed() {
    resetForProjectBoundary();
}

void AIChatConsoleContent::resetForProjectBoundary() {
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
    constexpr auto hasRole = [](const auto& row) { return row.role.isNotEmpty(); };
    return std::ranges::any_of(device->kitRows, hasRole);
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
    const double lengthBeats = clip->getLengthInBeats();

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
                    juce::String::charToString(cells[static_cast<size_t>(bar) * kCellsPerBar + i]);
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

    const auto inArrangementOrder = [](magda::ClipId a, magda::ClipId b) {
        const auto* clipA = magda::ClipManager::getInstance().getClip(a);
        const auto* clipB = magda::ClipManager::getInstance().getClip(b);
        if (clipA == nullptr || clipB == nullptr)
            return a < b;
        return std::tuple{clipA->trackId, clipA->placement.startBeat, a} <
               std::tuple{clipB->trackId, clipB->placement.startBeat, b};
    };
    std::ranges::sort(ids, inArrangementOrder);

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
        contextText_ = std::move(trackName);
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
        contextText_ = std::move(trackName);
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

    const auto isInMidiContext = [this](magda::ClipId clipId) {
        return midiContextClipIds_.contains(clipId);
    };
    const bool allSelected = std::ranges::all_of(midiClipIds, isInMidiContext);
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
            return trackName + juce::String::fromUTF8(" · all MIDI");
        return trackName + juce::String::fromUTF8(" · ") +
               juce::String(static_cast<int>(midiContextClipIds_.size())) + " MIDI";
    }

    return juce::String(static_cast<int>(midiContextClipIds_.size())) +
           juce::String::fromUTF8(" MIDI · ") + juce::String(static_cast<int>(trackIds.size())) +
           " tracks";
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

    outputMode_.setSelectedIndex(midiOutputMode_ == MidiOutputMode::ReviseLast ? 1 : 0);
    outputMode_.setSegmentEnabled(1, haveLastResult);
    outputMode_.setTooltip(haveLastResult
                               ? "New clip, or replace the most recent AI-created MIDI clip"
                               : "Generate a MIDI clip before using Revise last");

    const bool shouldShow = currentViewMode_ == magda::ViewMode::Arrange;
    if (outputMode_.isVisible() != shouldShow) {
        outputMode_.setVisible(shouldShow);
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
                            contextEnabled_
                                ? ActiveTheme::getAccentColour()
                                : ActiveTheme::getSecondaryTextColour().withAlpha(0.3f));
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
        status += juce::String::fromUTF8(" \xc2\xb7 ") +
                  (serverModel.empty() ? juce::String("No model") : juce::String(serverModel));
    } else if (musicCfg.model.empty()) {
        status += juce::String::fromUTF8(" \xc2\xb7 Embedded");
    } else {
        status += juce::String::fromUTF8(" \xc2\xb7 ") + juce::String(musicCfg.model);
    }

    // If embedded local provider, show model status + toggle button
    if (isLocalPreset() && serverToggleButton_) {
        auto& mgr = magda::LlamaModelManager::getInstance();
        if (mgr.isLoaded()) {
            auto modelName = juce::File(mgr.getLoadedModelPath()).getFileName();
            status += juce::String::fromUTF8(" \xc2\xb7 ") + modelName;
            serverToggleButton_->updateSvgData(BinaryData::server_stop_svg,
                                               BinaryData::server_stop_svgSize);
            serverToggleButton_->setNormalColor(juce::Colours::limegreen);
            serverToggleButton_->setHoverColor(juce::Colours::limegreen.brighter(0.3f));
            serverToggleButton_->setVisible(true);
            configStatusLabel_.setColour(juce::Label::textColourId, juce::Colours::limegreen);
        } else {
            status += juce::String::fromUTF8(" \xc2\xb7 No model loaded");
            serverToggleButton_->updateSvgData(BinaryData::server_play_svg,
                                               BinaryData::server_play_svgSize);
            serverToggleButton_->setNormalColor(ActiveTheme::getSecondaryTextColour());
            serverToggleButton_->setHoverColor(ActiveTheme::getColour(ActiveTheme::TEXT_PRIMARY));
            serverToggleButton_->setVisible(true);
            configStatusLabel_.setColour(juce::Label::textColourId,
                                         ActiveTheme::getSecondaryTextColour());
        }
        serverToggleButton_->repaint();
    } else {
        if (serverToggleButton_)
            serverToggleButton_->setVisible(false);
        configStatusLabel_.setColour(juce::Label::textColourId,
                                     ActiveTheme::getSecondaryTextColour());
    }

    configStatusLabel_.setText(status, juce::dontSendNotification);
    resized();
}

bool AIChatConsoleContent::isLocalPreset() {
    auto& config = magda::Config::getInstance();
    auto preset = config.getAIPreset();
    auto commandCfg = config.getAgentLLMConfig(magda::role::COMMAND);
    return commandCfg.provider == magda::provider::LLAMA_LOCAL ||
           preset == magda::preset::LOCAL_EMBEDDED || preset == "local";
}

void AIChatConsoleContent::mouseUp(const juce::MouseEvent& event) {
    if (event.originalComponent == &configStatusLabel_) {
        magda::openSettings(magda::SettingsSectionId::AI);
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
    std::map<juce::String, std::size_t> aliasIndex;

    // Internal plugins — single source of truth in internal_plugins.hpp,
    // shared with the DSL interpreter and InstructionExecutor so the autocomplete
    // dropdown lists exactly the aliases the agent layer accepts.
    for (const auto& entry : magda::getInternalPlugins()) {
        aliasIndex[entry.pluginId] = allAliases_.size();
        allAliases_.push_back({entry.primaryAlias, entry.displayName});
    }

    // External plugins come through the same MagdaApi boundary used by agents.
    const auto plugins = magdaApi_->plugins().getExternalPlugins();
    DBG("AIChatConsole: buildAliasList - plugin catalog has " << plugins.size() << " plugins");
    for (const auto& plugin : plugins) {
        auto alias = PluginBrowserInfo::generateAlias(plugin.name);
        aliasIndex[plugin.uniqueId] = allAliases_.size();
        allAliases_.push_back({alias, plugin.name});
    }

    try {
        for (const auto& [key, alias] :
             magda::PluginMetadataStore::defaultForCurrentThread().aliases()) {
            if (const auto it = aliasIndex.find(key); it != aliasIndex.end())
                allAliases_[it->second].alias = alias;
        }
    } catch (const std::exception& e) {
        DBG("AIChatConsole: failed to load plugin aliases: " << e.what());
    }

    std::ranges::sort(allAliases_, {}, &AliasEntry::alias);
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
    const juce::String& pluginAlias) {
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

    std::ranges::sort(out, {}, &ParamAliasEntry::paramAlias);
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
            juce::CodeDocument::Position(inputDocument_, atPos + inserted.length() + 1), false);
    }

    suppressAutocompleteForContent_ = inputDocument_.getAllContent();
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
            juce::CodeDocument::Position(inputDocument_, atPos + 1 + alias.length()), false);
    }

    suppressAutocompleteForContent_ = inputDocument_.getAllContent();
    hideAutocomplete();
    inputBox_->grabKeyboardFocus();
}

bool AIChatConsoleContent::keyPressed(const juce::KeyPress& key, juce::Component*) {
    // Autocomplete navigation, plus the Enter/Esc handling that
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
                    if (const auto* cmd = autocompletePopup_->getSelectedCommand()) {
                        insertSlashCommand(cmd->name);
                        return true;
                    }
                    break;
                case AutocompletePopup::Mode::Param:
                    if (const auto* entry = autocompletePopup_->getSelectedParamEntry()) {
                        insertParamAlias(entry->pluginAlias, entry->paramAlias);
                        return true;
                    }
                    break;
                case AutocompletePopup::Mode::Alias:
                    if (const auto* entry = autocompletePopup_->getSelectedEntry()) {
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
                if (const auto* cmd = autocompletePopup_->getSelectedCommand()) {
                    insertSlashCommand(cmd->name);
                    return true;
                }
                break;
            case AutocompletePopup::Mode::Param:
                if (const auto* entry = autocompletePopup_->getSelectedParamEntry()) {
                    insertParamAlias(entry->pluginAlias, entry->paramAlias);
                    return true;
                }
                break;
            case AutocompletePopup::Mode::Alias:
                if (const auto* entry = autocompletePopup_->getSelectedEntry()) {
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
    updateSendState();
    auto text = inputDocument_.getAllContent();

    // Suppress by CONTENT, not by a one-shot flag. replaceAllContent() fires
    // BOTH document listeners — a delete of the old text and an insert of the
    // new — so accepting a completion queues two deferred onInputChanged()
    // calls. A one-shot is consumed by the first and the second re-opens the
    // popup we just closed, which is what left it stuck on screen with Tab
    // unable to dismiss it. Keyed on the text we inserted, any number of
    // callbacks are absorbed, and the moment the user types anything the
    // content differs and completion resumes.
    if (suppressAutocompleteForContent_.isNotEmpty()) {
        if (text == suppressAutocompleteForContent_) {
            hideAutocomplete();
            return;
        }
        suppressAutocompleteForContent_.clear();
    }
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

    // /dsl — run MAGDA DSL directly, no model involved. The handler reads the raw text
    // rather than the parsed positional, so code keeps its whitespace and newlines.
    SlashCommand dsl;
    dsl.name = "dsl";
    dsl.description = "Run MAGDA DSL code directly";
    dsl.usage = "/dsl <code>";
    dsl.details = "Runs the code against the project without the AI. Shift+Enter adds a line.\n"
                  "  track(name=\"X\")                  reference or create a track\n"
                  "  track(id=1)                        reference a track by index\n"
                  "  .clip.new(bar=1, length_bars=4)    create a MIDI clip\n"
                  "  .fx.add(name=\"reverb\")           add an effect\n"
                  "  .notes.add(pitch=C4, beat=0)       add a note\n"
                  "  .notes.add_chord(root=C4, quality=major)\n"
                  "  filter(tracks, ...).delete()       bulk operations";
    dsl.examples = {
        {"Tracks", {"track(name=\"Bass\")", "track(id=1).fx.add(name=\"reverb\")"}},
        {"Clips",
         {"track(id=1).clip.new(bar=1, length_bars=4)",
          "track(id=1).clip.new(bar=1, length_bars=1).notes.add_chord(root=C4, quality=major)"}},
    };
    dsl.handler = [this](const juce::String& originalText,
                         const std::map<juce::String, juce::String>& /*flags*/,
                         const juce::String& /*positional*/) {
        appendToChat(juce::String::charToString(0x25CF) + " " + originalText);
        const auto code =
            originalText.trimStart().fromFirstOccurrenceOf("/dsl", false, false).trim();
        magda::dsl::Interpreter interpreter(*magdaApi_);
        if (interpreter.execute(code.toRawUTF8())) {
            auto results = interpreter.getResults();
            appendToChat(juce::String::charToString(0x25C6) + " " +
                         (results.isEmpty() ? juce::String("OK") : results));
        } else {
            appendToChat(juce::String::charToString(0x25C6) +
                         " Error: " + juce::String(interpreter.getError()));
        }
        return true;
    };
    slashRegistry_->add(std::move(dsl));

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

    suppressAutocompleteForContent_ = inputDocument_.getAllContent();
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
                    safeThis->appendToChat(
                        juce::String::charToString(0x25C6) +
                        juce::String::fromUTF8(" Profile disappeared — cannot enable."));
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

        const auto& rawJson = errorOrJson;  // contains the JSON when success == true
        const auto& baseId = profileId;
        const auto& displayName = profileName;

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
            std::scoped_lock lk(buf->mu);
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
                std::scoped_lock lk(buf->mu);
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
    int syntaxCount = 0;

    if (success) {
        std::string validationError;
        if (auto theme =
                magda::validateGeneratedTheme(juce::String(result.text), validationError)) {
            jsonOrError = juce::String(theme->json);
            name = juce::String(theme->name);
            base = juce::String(theme->base);
            colourCount = theme->colourCount;
            syntaxCount = theme->syntaxCount;
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
        [safeThis, success, jsonOrError, name, base, colourCount, syntaxCount, anchor]() {
            if (!safeThis)
                return;
            safeThis->finishThemeGeneration(success, jsonOrError, name, base, colourCount,
                                            syntaxCount, anchor->load());
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
                                                 int colourCount, int syntaxCount,
                                                 int streamAnchor) {
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
    body << "  " << juce::String(colourCount) << " colours";
    if (syntaxCount > 0)
        body << " + " << juce::String(syntaxCount) << " syntax colours";
    body << " over the " << base << " base\n";
    body << "  saved to " << file.getFullPathName() << "\n";
    body << "  applied - edit the file to tweak it live, or switch under "
            "Preferences > Appearance";
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
