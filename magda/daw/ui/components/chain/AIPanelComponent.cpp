#include "AIPanelComponent.hpp"

#include "../../themes/DarkTheme.hpp"
#include "../../themes/FontManager.hpp"
#include "core/TrackManager.hpp"

namespace magda::daw::ui {

class AIPanelComponent::GenerateThread : public juce::Thread {
  public:
    GenerateThread(AIPanelComponent& owner, std::unique_ptr<SoundDesignAgent> agent,
                   juce::String prompt, ChainNodePath path)
        : juce::Thread("MAGDA-SoundDesignAgent"),
          owner_(owner),
          agent_(std::move(agent)),
          prompt_(std::move(prompt)),
          path_(path) {}

    void run() override {
        auto safeOwner = juce::WeakReference<AIPanelComponent>(&owner_);
        if (threadShouldExit() || agent_ == nullptr) {
            postResult(safeOwner, "no agent");
            return;
        }

        // Per-token forwarder — runs on this worker thread, hops each token
        // to the message thread before mutating the panel's text editor.
        auto onToken = [safeOwner, this](const juce::String& token) -> bool {
            if (threadShouldExit())
                return false;
            juce::MessageManager::callAsync([safeOwner, token]() {
                if (auto* p = safeOwner.get())
                    p->appendStreamingToken(token);
            });
            return true;
        };

        auto status = agent_->generateAndApply(prompt_, path_, std::move(onToken));
        if (threadShouldExit())
            return;
        postResult(safeOwner, status);
    }

    void cancel() {
        if (agent_)
            agent_->requestCancel();
        signalThreadShouldExit();
    }

  private:
    static void postResult(juce::WeakReference<AIPanelComponent> safeOwner, juce::String status) {
        juce::MessageManager::callAsync([safeOwner, status]() {
            if (auto* p = safeOwner.get())
                p->onGenerationFinished(status);
        });
    }

    AIPanelComponent& owner_;
    std::unique_ptr<SoundDesignAgent> agent_;
    juce::String prompt_;
    ChainNodePath path_;
};

AIPanelComponent::AIPanelComponent() {
    output_.setMultiLine(true, true);
    output_.setReadOnly(true);
    output_.setScrollbarsShown(true);
    output_.setCaretVisible(false);
    output_.setColour(juce::TextEditor::backgroundColourId,
                      DarkTheme::getColour(DarkTheme::BACKGROUND));
    output_.setColour(juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    output_.setColour(juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
    output_.setColour(juce::TextEditor::textColourId,
                      DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
    output_.setFont(FontManager::getInstance().getUIFont(11.0f));
    addAndMakeVisible(output_);

    input_.setMultiLine(false);
    input_.setReturnKeyStartsNewLine(false);
    input_.setTextToShowWhenEmpty("describe the sound...",
                                  DarkTheme::getColour(DarkTheme::TEXT_PRIMARY).withAlpha(0.4f));
    input_.setColour(juce::TextEditor::backgroundColourId,
                     DarkTheme::getColour(DarkTheme::BACKGROUND).brighter(0.05f));
    input_.setColour(juce::TextEditor::textColourId, DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
    input_.setFont(FontManager::getInstance().getUIFont(11.0f));
    input_.onReturnKey = [this]() { submitPrompt(); };
    addAndMakeVisible(input_);
}

AIPanelComponent::~AIPanelComponent() {
    if (thread_) {
        thread_->cancel();
        thread_->stopThread(2000);
    }
}

void AIPanelComponent::setDevicePath(const ChainNodePath& path) {
    path_ = path;
    // Restore any prior output for this device — DeviceInfo persists across
    // DeviceSlotComponent rebuilds (which happen on notifyTrackDevicesChanged
    // for plugin loads, preset apply, sidechain edits, etc.), so the user's
    // streamed result and prompt history survive a slot teardown.
    if (auto* dev = TrackManager::getInstance().getDeviceInChainByPath(path_)) {
        output_.setText(dev->aiPanelOutput, juce::dontSendNotification);
        output_.moveCaretToEnd();
    }
}

void AIPanelComponent::setDevicePluginId(const juce::String& pluginId) {
    if (pluginId == pluginId_)
        return;
    pluginId_ = pluginId;
    const bool supported = isSoundDesignSupported(pluginId_);
    input_.setEnabled(supported);
    if (supported) {
        input_.setTextToShowWhenEmpty(
            "describe the sound...", DarkTheme::getColour(DarkTheme::TEXT_PRIMARY).withAlpha(0.4f));
    } else {
        input_.setTextToShowWhenEmpty(
            "AI design not supported for this device",
            DarkTheme::getColour(DarkTheme::TEXT_PRIMARY).withAlpha(0.4f));
    }
}

void AIPanelComponent::resized() {
    auto bounds = getLocalBounds().reduced(4);
    auto inputArea = bounds.removeFromBottom(22);
    bounds.removeFromBottom(4);
    output_.setBounds(bounds);
    input_.setBounds(inputArea);
}

void AIPanelComponent::paint(juce::Graphics& g) {
    g.fillAll(DarkTheme::getColour(DarkTheme::PANEL_BACKGROUND));
}

void AIPanelComponent::submitPrompt() {
    auto prompt = input_.getText().trim();
    if (prompt.isEmpty())
        return;
    if (!isSoundDesignSupported(pluginId_)) {
        appendOutput("unsupported device");
        return;
    }

    // Cancel any in-flight generation before starting a new one — only one
    // active per panel; merging makes no sense for whole-preset generation.
    if (thread_) {
        thread_->cancel();
        thread_->stopThread(2000);
        thread_.reset();
    }

    appendOutput(juce::String::charToString(0x25CF) + " " + prompt);
    input_.clear();

    auto agent = createSoundDesignAgentFor(pluginId_);
    if (!agent) {
        appendOutput("no agent for " + pluginId_);
        return;
    }

    setBusy(true);

    // Mark where the streamed response starts so onGenerationFinished can
    // replace the raw JSON we showed during streaming with a clean status
    // line. Add a trailing newline so the first token lands on its own row.
    auto text = output_.getText();
    if (text.isNotEmpty() && !text.endsWithChar('\n'))
        text += "\n";
    streamingStart_ = text.length();
    output_.setText(text, juce::dontSendNotification);
    output_.moveCaretToEnd();
    persistOutput();

    thread_ = std::make_unique<GenerateThread>(*this, std::move(agent), prompt, path_);
    thread_->startThread();
}

void AIPanelComponent::appendStreamingToken(const juce::String& token) {
    // Strip JSON curly brackets so the streamed payload reads as content
    // rather than raw envelope syntax. Everything else (keys, values,
    // commas, quotes) flows through unchanged.
    auto cleaned = token.replaceCharacters("{}", "  ");
    // insertTextAtCaret is cheaper than full setText on every token, and
    // keeps the caret pinned to the end so the view auto-scrolls.
    output_.moveCaretToEnd();
    output_.insertTextAtCaret(cleaned);
    persistOutput();
}

void AIPanelComponent::onGenerationFinished(juce::String status) {
    // Keep the streamed response visible — append the status line after it.
    auto text = output_.getText();
    if (text.isNotEmpty() && !text.endsWithChar('\n'))
        text += "\n";
    text += juce::String(juce::CharPointer_UTF8("\xe2\x86\x92 ")) + status;
    output_.setText(text, juce::dontSendNotification);
    output_.moveCaretToEnd();
    streamingStart_ = -1;
    setBusy(false);
    persistOutput();
}

void AIPanelComponent::setBusy(bool busy) {
    input_.setEnabled(!busy);
}

void AIPanelComponent::appendOutput(const juce::String& line) {
    auto existing = output_.getText();
    if (existing.isNotEmpty())
        existing += "\n";
    existing += line;
    output_.setText(existing, juce::dontSendNotification);
    output_.moveCaretToEnd();
    persistOutput();
}

void AIPanelComponent::persistOutput() {
    // Mirror the panel's text into DeviceInfo so a slot rebuild can restore
    // it. DeviceInfo lives on TrackManager and outlives the slot; the field
    // is transient (not serialized to disk).
    if (auto* dev = TrackManager::getInstance().getDeviceInChainByPath(path_))
        dev->aiPanelOutput = output_.getText();
}

}  // namespace magda::daw::ui
