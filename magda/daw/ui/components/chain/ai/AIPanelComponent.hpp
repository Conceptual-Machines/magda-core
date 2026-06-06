#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

#include "../../../../agents/coder_agent.hpp"
#include "../../../../agents/device_ai_agent.hpp"
#include "../../../../agents/sound_design_agent.hpp"
#include "core/ChainNodePath.hpp"

namespace magda::daw::ui {

/**
 * @brief Per-device docked AI panel — design presets from a prompt.
 *
 * Mirrors the docking pattern of MacroPanelComponent / ModsPanelComponent
 * inside NodeComponent: the panel is owned by the node, shown when the
 * "AI" header icon is toggled on, and bound to a specific ChainNodePath.
 * On submit, the panel runs the SoundDesignAgent registered for the
 * device's pluginId on a background thread and writes the result onto
 * the bound path. If no specialised agent exists for the device the
 * panel renders a one-line "not supported" message.
 *
 * Layout:
 * +------------------+
 * |       AI         |  <- header (drawn by NodeComponent::paintAIPanel)
 * |                  |
 * | <output area>    |  <- multiline read-only TextEditor
 * |                  |
 * +------------------+
 * | [prompt input]   |  <- single-line TextEditor, Enter to submit
 * +------------------+
 * | model name   [X] |  <- footer: model id + clear-chat button
 * +------------------+
 */
class AIPanelComponent : public juce::Component {
  public:
    AIPanelComponent();
    ~AIPanelComponent() override;

    // Bind the panel to the device path it sits on. Generations apply
    // to this path. Pass an empty path to clear binding.
    void setDevicePath(const ChainNodePath& path);

    // Tell the panel which pluginId is at `path` so it can pick the right
    // SoundDesignAgent and reflect "supported" / "not supported" in the UI.
    void setDevicePluginId(const juce::String& pluginId);

    void resized() override;
    void paint(juce::Graphics& g) override;

  private:
    void submitPrompt();
    void appendOutput(const juce::String& line);
    void appendStreamingToken(const juce::String& token);
    void onGenerationFinished(juce::String status, juce::String conversationJson);
    void setBusy(bool busy);
    // Mirror output_'s text onto the bound DeviceInfo so slot rebuilds restore it.
    void persistOutput();
    // Re-insert persisted text in coloured sections so the disclaimer keeps
    // its yellow tint after a rebuild.
    void restoreOutput(const juce::String& text);

    juce::String pluginId_;
    ChainNodePath path_;

    juce::TextEditor output_;  // multiline read-only
    juce::TextEditor input_;   // single-line, Enter = submit

    // Footer: model id (left) + clear-chat button (right).
    juce::Label modelLabel_;
    juce::DrawableButton clearButton_{"clear", juce::DrawableButton::ImageFitted};

    void clearChat();
    void refreshModelLabel();

    // Faust MCP status strip at the top (shown only for coder / Faust
    // devices): a dot + "Faust MCP off/on/connected" so it's clear the
    // compile-check backend is available when generating DSP.
    juce::Label mcpStatusLabel_;
    juce::Rectangle<int> mcpStripBounds_;
    bool mcpStripVisible_ = false;
    bool mcpEnabled_ = false;
    bool mcpRunning_ = false;
    void updateMcpStatus();

    // Track where the streaming response started so we can replace the
    // raw JSON the model emits with a clean status line on completion.
    int streamingStart_ = -1;

    // Background generation. juce::Thread + SafePointer pattern matches the
    // /design slash command flow in AIChatConsoleContent.
    class GenerateThread;
    std::unique_ptr<GenerateThread> thread_;
    std::unique_ptr<DeviceAIAgent> agent_;

    JUCE_DECLARE_WEAK_REFERENCEABLE(AIPanelComponent)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AIPanelComponent)
};

}  // namespace magda::daw::ui
