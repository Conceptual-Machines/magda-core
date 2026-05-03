#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

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

    juce::String pluginId_;
    ChainNodePath path_;

    juce::TextEditor output_;  // multiline read-only
    juce::TextEditor input_;   // single-line, Enter = submit

    // Background generation. juce::Thread + SafePointer pattern matches the
    // /design slash command flow in AIChatConsoleContent.
    class GenerateThread;
    std::unique_ptr<GenerateThread> thread_;
    std::unique_ptr<SoundDesignAgent> agent_;

    JUCE_DECLARE_WEAK_REFERENCEABLE(AIPanelComponent)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AIPanelComponent)
};

}  // namespace magda::daw::ui
