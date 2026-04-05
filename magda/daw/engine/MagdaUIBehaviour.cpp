#include "MagdaUIBehaviour.hpp"

#include <juce_gui_basics/juce_gui_basics.h>
#include <tracktion_engine/tracktion_engine.h>

namespace magda {

// =============================================================================
// runTaskWithProgressBar — required for TE track freeze rendering
// =============================================================================

namespace {

class TaskProgressWindow : public juce::ThreadWithProgressWindow {
  public:
    TaskProgressWindow(tracktion::ThreadPoolJobWithProgress& task)
        : juce::ThreadWithProgressWindow(task.getJobName(), task.canCancel(), true), task_(task) {}

    void run() override {
        while (!threadShouldExit()) {
            auto status = task_.runJob();
            setProgress(static_cast<double>(task_.getCurrentTaskProgress()));

            if (status == juce::ThreadPoolJob::jobHasFinished)
                break;

            if (status != juce::ThreadPoolJob::jobNeedsRunningAgain)
                break;

            juce::Thread::sleep(1);
        }
    }

  private:
    tracktion::ThreadPoolJobWithProgress& task_;
};

}  // namespace

void MagdaUIBehaviour::runTaskWithProgressBar(tracktion::ThreadPoolJobWithProgress& task) {
    DBG("MagdaUIBehaviour::runTaskWithProgressBar - starting task: " << task.getJobName());
    TaskProgressWindow window(task);
    window.runThread();
    auto progress = task.getCurrentTaskProgress();
    DBG("MagdaUIBehaviour::runTaskWithProgressBar - task finished, progress=" << progress);

    // Check for error message on RenderTask
    if (auto* renderTask = dynamic_cast<tracktion::Renderer::RenderTask*>(&task)) {
        if (renderTask->errorMessage.isNotEmpty()) {
            DBG("  -> Render error: " << renderTask->errorMessage);
        }
        DBG("  -> Dest file: " << renderTask->params.destFile.getFullPathName());
        DBG("  -> Dest file exists: " << (renderTask->params.destFile.existsAsFile() ? "yes"
                                                                                     : "no"));
        DBG("  -> Time range: " << renderTask->params.time.getStart().inSeconds() << " - "
                                << renderTask->params.time.getEnd().inSeconds());
        DBG("  -> Tracks to do bits: " << renderTask->params.tracksToDo.countNumberOfSetBits());
    }
}

void MagdaUIBehaviour::showWarningMessage(const juce::String& message) {
    // Suppress TE's internal warnings (e.g. "Converted to submix track"
    // when creating DrumGrid FolderTrack submixes). Log instead of popup.
    DBG("MagdaUIBehaviour: suppressed warning: " << message);
}

std::unique_ptr<juce::Component> MagdaUIBehaviour::createPluginWindow(
    tracktion::PluginWindowState& state) {
    // Cast to Plugin::WindowState to access the plugin
    auto* pluginState = dynamic_cast<tracktion::Plugin::WindowState*>(&state);
    if (!pluginState) {
        DBG("MagdaUIBehaviour::createPluginWindow - not a Plugin::WindowState");
        return nullptr;
    }

    auto& plugin = pluginState->plugin;
    DBG("MagdaUIBehaviour::createPluginWindow - creating window for: " << plugin.getName());

    // Create the window
    auto window = std::make_unique<PluginEditorWindow>(plugin, state);

    // Window might fail to create if plugin has no editor
    if (window->getContentComponent() == nullptr) {
        DBG("  -> Plugin has no editor component");
        return nullptr;
    }

    DBG("  -> Window created successfully");
    return window;
}

// =============================================================================
// PluginEditorWindow Implementation
// =============================================================================

PluginEditorWindow::PluginEditorWindow(tracktion::Plugin& plugin,
                                       tracktion::PluginWindowState& state)
    : DocumentWindow(plugin.getName(),
                     juce::LookAndFeel::getDefaultLookAndFeel().findColour(
                         juce::ResizableWindow::backgroundColourId),
                     DocumentWindow::minimiseButton | DocumentWindow::closeButton),
      plugin_(plugin),
      state_(state) {
    // IMPORTANT: Do NOT use native title bar!
    // With native title bar, macOS controls the close button behavior and may
    // try to close the window after closeButtonPressed() returns, conflicting
    // with Tracktion's window ownership. Using JUCE's title bar gives us
    // complete control - nothing happens after closeButtonPressed() unless we do it.
    setUsingNativeTitleBar(false);

    // Keep plugin window always on top so it doesn't go behind main window when user interacts with
    // parameters
    setAlwaysOnTop(true);

    // Try to create the plugin's editor
    std::unique_ptr<juce::Component> editor;

    // For external plugins, get the native editor
    if (auto* extPlugin = dynamic_cast<tracktion::ExternalPlugin*>(&plugin)) {
        if (auto* audioPluginInstance = extPlugin->getAudioPluginInstance()) {
            if (audioPluginInstance->hasEditor()) {
                editor.reset(audioPluginInstance->createEditorIfNeeded());
                DBG("PluginEditorWindow: Created native editor for: " << plugin.getName());
            }
        }
    }

    // If no native editor, try the plugin's generic editor
    if (!editor) {
        auto editorComponent = plugin.createEditor();
        if (editorComponent) {
            editor = std::move(editorComponent);
            DBG("PluginEditorWindow: Created generic editor for: " << plugin.getName());
        }
    }

    if (editor) {
        setContentOwned(editor.release(), true);

        // Check if editor is resizable (only AudioProcessorEditor has this property)
        bool isResizable = false;
        if (auto* processorEditor =
                dynamic_cast<juce::AudioProcessorEditor*>(getContentComponent())) {
            isResizable = processorEditor->isResizable();
        }
        setResizable(isResizable, false);

        // Set initial size from editor
        int width = getContentComponent()->getWidth();
        int height = getContentComponent()->getHeight();
        if (width > 0 && height > 0) {
            setSize(width, height);
        } else {
            setSize(400, 300);  // Default size
        }

        // Position window
        auto pos = state.choosePositionForPluginWindow();
        setTopLeftPosition(pos.x, pos.y);

        setVisible(true);
    } else {
        DBG("PluginEditorWindow: Failed to create editor for: " << plugin.getName());
    }
}

PluginEditorWindow::~PluginEditorWindow() {
    clearContentComponent();
}

void PluginEditorWindow::closeButtonPressed() {
    DBG("PluginEditorWindow::closeButtonPressed - scheduling deferred close");
    // IMPORTANT: We cannot call closeWindowExplicitly() directly here because it
    // deletes this window (via pluginWindow.reset()), but we're still inside
    // this window's member function. Deleting 'this' while in a member function
    // causes memory corruption when the function tries to return.
    //
    // Solution: Defer the close to happen after this method returns completely.
    // We capture a reference to state_ (which outlives the window) to call close later.
    auto& state = state_;
    juce::MessageManager::callAsync([&state]() {
        DBG("PluginEditorWindow - executing deferred close");
        state.closeWindowExplicitly();
    });
}

void PluginEditorWindow::moved() {
    // Save window position for next time
    if (state_.lastWindowBounds.has_value()) {
        state_.lastWindowBounds = getBounds();
    }
}

}  // namespace magda
