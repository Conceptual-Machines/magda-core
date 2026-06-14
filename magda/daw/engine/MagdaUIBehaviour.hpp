#pragma once

#include <tracktion_engine/tracktion_engine.h>

#include "../ui/components/common/FloatingHostWindow.hpp"

namespace tracktion {
inline namespace engine {
class Plugin;
struct PluginWindowState;
}  // namespace engine
}  // namespace tracktion

namespace magda {

/**
 * @brief Custom UIBehaviour implementation for MAGDA
 *
 * Provides plugin window creation for external plugins.
 * This is required for Tracktion Engine to display native plugin UIs.
 */
class MagdaUIBehaviour : public tracktion::UIBehaviour {
  public:
    MagdaUIBehaviour() = default;
    ~MagdaUIBehaviour() override = default;

    /**
     * @brief Create a plugin window for the given plugin state
     * @param state The PluginWindowState containing the plugin to display
     * @return A unique_ptr to the created window component, or nullptr if failed
     */
    std::unique_ptr<juce::Component> createPluginWindow(
        tracktion::PluginWindowState& state) override;

    /**
     * @brief Run a background task with a progress bar, blocking until complete.
     *
     * Required by TE for track freezing and other rendering operations.
     */
    void runTaskWithProgressBar(tracktion::ThreadPoolJobWithProgress& task) override;

    /**
     * @brief Suppress TE's internal warning popups (e.g. "Converted to submix track").
     */
    void showWarningMessage(const juce::String& message) override;
};

/**
 * @brief Window component that displays a plugin's native editor UI
 *
 * This is a DocumentWindow subclass that wraps the plugin's AudioProcessorEditor
 * and manages its lifecycle. Uses JUCE's title bar (not native) so we have complete
 * control over window close behavior.
 */
class PluginEditorWindow final : public daw::ui::FloatingHostWindow {
  public:
    PluginEditorWindow(tracktion::Plugin& plugin, tracktion::PluginWindowState& state);
    ~PluginEditorWindow() override;

    // Injected by the UI layer (MainWindow) so plugin editor windows can route
    // keys they don't consume (Space = play/stop, etc.) to the app command
    // manager. This engine library can't depend on the UI's command manager, so
    // it's set externally and cleared on shutdown.
    static juce::ApplicationCommandManager* appCommandManager;

    void closeButtonPressed() override;
    void moved() override;
    // Forwards unconsumed keys to the app command manager (transport shortcuts).
    bool keyPressed(const juce::KeyPress& key) override;

  private:
    tracktion::Plugin& plugin_;
    tracktion::PluginWindowState& state_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginEditorWindow)
};

}  // namespace magda
