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

juce::ApplicationCommandManager* PluginEditorWindow::appCommandManager = nullptr;

PluginEditorWindow::PluginEditorWindow(tracktion::Plugin& plugin,
                                       tracktion::PluginWindowState& state)
    : daw::ui::FloatingHostWindow(plugin.getName()), plugin_(plugin), state_(state) {
    // Chrome (themed non-native title bar + always-on-top) comes from
    // FloatingHostWindow. The non-native bar is essential here: native macOS close
    // can try to close the window after closeButtonPressed() returns, racing
    // Tracktion's window ownership; JUCE's bar gives us full control.

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

        // Restore the user's last size/position for this plugin if we have it.
        // lastWindowBounds lives on the PluginWindowState, which outlives the
        // window, so a resize persists across close/reopen within the session.
        // We saved getBounds() (the whole window, title bar included), so
        // setBounds() round-trips exactly and does not shrink. Only honour a
        // saved *size* for resizable editors — fixed-size editors must keep their
        // intrinsic size; we still restore their position below.
        if (isResizable && state.lastWindowBounds.has_value()) {
            setBounds(*state.lastWindowBounds);
        } else {
            // No saved size. setContentOwned(..., true) above already sized the
            // window to fit the editor PLUS our non-native title bar (via
            // childBoundsChanged). Do NOT setSize() with the editor's content
            // dimensions here: setSize sets the whole window (title bar included),
            // squeezing the editor down by the title-bar height on every open. A
            // resizable plugin editor persists that shrunk size and reports it on
            // the next createEditorIfNeeded(), so each open/close cycle shaved off
            // another title-bar height -- the "ext UI window keeps shrinking on
            // repeated clicks" bug. Only fall back to a default when the editor
            // reports no size of its own.
            if (getContentComponent()->getWidth() <= 0 ||
                getContentComponent()->getHeight() <= 0) {
                setSize(400, 300);  // Default size for editors with no intrinsic size
            }

            // Position window (restores last position if known, else cascades)
            auto pos = state.choosePositionForPluginWindow();
            setTopLeftPosition(pos.x, pos.y);
        }

        // Seed the saved bounds and start tracking, so subsequent user moves and
        // resizes are remembered for next time.
        state.lastWindowBounds = getBounds();
        trackingBounds_ = true;

        setVisible(true);
    } else {
        DBG("PluginEditorWindow: Failed to create editor for: " << plugin.getName());
    }

    // Key routing for transport shortcuts is handled in keyPressed() (forwarded
    // at event time), not via a persistent KeyListener — see that override.
}

bool PluginEditorWindow::keyPressed(const juce::KeyPress& key) {
    // This is a separate top-level window, outside MainWindow's key chain, so
    // without this the app's shortcuts (Space = play/stop, etc.) go dead while a
    // plugin editor has focus. Forward keys the editor didn't consume to the app
    // command manager at event time. Checking the injected pointer each press
    // (and clearing it on shutdown) avoids holding a persistent KeyListener that
    // could dangle once the command manager is destroyed.
    if (appCommandManager != nullptr)
        return appCommandManager->getKeyMappings()->keyPressed(key, this);
    return false;
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
    if (trackingBounds_) {
        state_.lastWindowBounds = getBounds();
    }
}

void PluginEditorWindow::resized() {
    // Keep DocumentWindow's title-bar + content layout.
    daw::ui::FloatingHostWindow::resized();
    // Save window size for next time
    if (trackingBounds_) {
        state_.lastWindowBounds = getBounds();
    }
}

}  // namespace magda
