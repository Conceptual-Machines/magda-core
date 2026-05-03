#include "AIPanelComponent.hpp"

#include "../../themes/DarkTheme.hpp"
#include "../../themes/FontManager.hpp"

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
            postResult(safeOwner, "(no agent)");
            return;
        }

        auto status = agent_->generateAndApply(prompt_, path_);
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
                p->appendOutput(status);
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
    input_.setTextToShowWhenEmpty("describe the sound…",
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
}

void AIPanelComponent::setDevicePluginId(const juce::String& pluginId) {
    if (pluginId == pluginId_)
        return;
    pluginId_ = pluginId;
    const bool supported = isSoundDesignSupported(pluginId_);
    input_.setEnabled(supported);
    if (supported) {
        input_.setTextToShowWhenEmpty(
            "describe the sound…", DarkTheme::getColour(DarkTheme::TEXT_PRIMARY).withAlpha(0.4f));
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
        appendOutput("(unsupported device)");
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
        appendOutput("(no agent for " + pluginId_ + ")");
        return;
    }

    thread_ = std::make_unique<GenerateThread>(*this, std::move(agent), prompt, path_);
    thread_->startThread();
}

void AIPanelComponent::appendOutput(const juce::String& line) {
    auto existing = output_.getText();
    if (existing.isNotEmpty())
        existing += "\n";
    existing += line;
    output_.setText(existing, juce::dontSendNotification);
    output_.moveCaretToEnd();
}

}  // namespace magda::daw::ui
