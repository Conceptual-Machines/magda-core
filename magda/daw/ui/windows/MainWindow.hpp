#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

#include "../../core/SelectionManager.hpp"
#include "../components/common/Toast.hpp"
#include "../dialogs/ExportAudioDialog.hpp"
#include "../dialogs/ExportMidiDialog.hpp"
#include "../layout/LayoutConfig.hpp"
#include "CommandIDs.hpp"
#include "MenuManager.hpp"
#include "core/TrackManager.hpp"
#include "core/ViewModeController.hpp"
#include "core/ViewModeState.hpp"
#include "core/controllers/MidiLearnCoordinator.hpp"
#include "project/ProjectManager.hpp"

namespace magda {

class TracktionEngineWrapper;
class TransportPanel;

class LeftPanel;
class RightPanel;
class MainView;
class SessionView;
class MixerView;
class BottomPanel;
class FooterBar;
class AudioEngine;
class QwertyMidiKeyboard;
class PlaybackPositionTimer;
class KeyMappingStore;

class MainWindow : public juce::DocumentWindow, public ProjectManagerListener {
  public:
    MainWindow(AudioEngine* audioEngine = nullptr);
    ~MainWindow() override;

    void closeButtonPressed() override;

    // ProjectManagerListener
    void projectOpened(const ProjectInfo& info) override;
    void projectSaved(const ProjectInfo& info) override;
    void projectClosed() override;
    void projectDirtyStateChanged(bool isDirty) override;

    /** Open a .mgd project file (used by menu, command line, and OS file association). */
    void openProjectFile(const juce::File& file);

    /** Import a .dawproject interchange archive as a new unsaved project. */
    void importDawProjectFile(const juce::File& file);

    /** Re-read panel visibility from Config and apply immediately. */
    void applyPanelVisibilityFromConfig();

    /** Re-read layout settings (e.g. headers side) from Config and apply. */
    void applyLayoutFromConfig();

    juce::ApplicationCommandManager& getCommandManager();

  private:
    void updateWindowTitle();
    class MainComponent;
    MainComponent* mainComponent = nullptr;       // Raw pointer - owned by DocumentWindow
    AudioEngine* externalAudioEngine_ = nullptr;  // Non-owning pointer to external engine

    // File chooser for async file import
    std::unique_ptr<juce::FileChooser> fileChooser_;

    void setupMenuBar();
    void setupMenuCallbacks();

    // Export helper methods
    void performExport(const ExportAudioDialog::Settings& settings, TracktionEngineWrapper* engine);
    void performMidiExport(const ExportMidiDialog::Settings& settings);
    juce::String getFileExtensionForFormat(const juce::String& format) const;
    int getBitDepthForFormat(const juce::String& format) const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainWindow)
};

class MainWindow::MainComponent : public juce::Component,
                                  public juce::DragAndDropContainer,
                                  public juce::ApplicationCommandTarget,
                                  public ViewModeListener,
                                  public SelectionManagerListener,
                                  public TrackManagerListener,
                                  public magda::MidiLearnCoordinatorListener {
  public:
    MainComponent(AudioEngine* externalEngine = nullptr);
    ~MainComponent() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    // Keyboard handling
    bool keyPressed(const juce::KeyPress& key) override;

    // ApplicationCommandTarget
    ApplicationCommandTarget* getNextCommandTarget() override;
    void getAllCommands(juce::Array<juce::CommandID>& commands) override;
    void getCommandInfo(juce::CommandID commandID, juce::ApplicationCommandInfo& result) override;
    bool perform(const InvocationInfo& info) override;

    // ViewModeListener
    void viewModeChanged(ViewMode mode, const AudioEngineProfile& profile) override;

    // SelectionManagerListener
    void selectionTypeChanged(SelectionType newType) override;

    // TrackManagerListener
    void tracksChanged() override {}
    void trackPropertyChanged(int trackId) override;

    // MidiLearnCoordinatorListener
    void midiLearnStateChanged(const magda::ChainNodePath& path, int paramIndex,
                               magda::ControlTarget::Kind owner, bool learning) override;
    void midiLearnCompleted(const magda::ChainNodePath& path, int paramIndex,
                            magda::ControlTarget::Kind owner,
                            const magda::Binding& binding) override;
    void midiLearnCleared(const magda::ChainNodePath& path, int paramIndex,
                          magda::ControlTarget::Kind owner, int numRemoved) override;

    // Command manager access
    juce::ApplicationCommandManager& getCommandManager() {
        return commandManager;
    }

    // Make these public so MainWindow can access them
    bool leftPanelVisible = true;
    bool rightPanelVisible = true;
    bool bottomPanelVisible = true;

    // Collapsed state (panel shows thin bar with expand button)
    bool leftPanelCollapsed = false;
    bool rightPanelCollapsed = false;
    bool bottomPanelCollapsed = false;

    std::unique_ptr<TransportPanel> transportPanel;
    std::unique_ptr<MainView> mainView;
    std::unique_ptr<SessionView> sessionView;
    std::unique_ptr<MixerView> mixerView;
    std::unique_ptr<FooterBar> footerBar;

    QwertyMidiKeyboard* getQwertyKeyboard() {
        return qwertyKeyboard_.get();
    }

    // Access to audio engine for settings dialog
    AudioEngine* getAudioEngine() {
        // Return external engine if provided, otherwise return owned engine
        return externalAudioEngine_ ? externalAudioEngine_ : audioEngine_.get();
    }

    // Loading overlay control (for async project loading)
    void showLoadingMessage(const juce::String& message);
    void hideLoadingMessage();

    // Toggle MIDI editor fullscreen mode: expand bottomPanel to fill the
    // area between transport and footer. Restores the previous height on
    // exit. Applies to piano roll and drum grid (issue #1282).
    void toggleEditorFullscreen();
    bool isEditorFullscreen() const {
        return editorFullscreen_;
    }

  private:
    // Command manager for keyboard shortcuts and menu commands
    juce::ApplicationCommandManager commandManager;

    // Persists user keyboard-shortcut remaps (#20). Created after the commands
    // are registered; loads saved overrides and re-saves on any change.
    std::unique_ptr<KeyMappingStore> keyMappingStore_;

    // Current view mode
    ViewMode currentViewMode = ViewMode::Arrange;

    // Audio engine (either owned or external reference)
    std::unique_ptr<AudioEngine> audioEngine_;    // Owned engine (if no external engine)
    AudioEngine* externalAudioEngine_ = nullptr;  // Non-owning pointer to external engine
    std::unique_ptr<PlaybackPositionTimer> positionTimer_;
    std::unique_ptr<QwertyMidiKeyboard> qwertyKeyboard_;

    // Main layout panels
    std::unique_ptr<LeftPanel> leftPanel;
    std::unique_ptr<RightPanel> rightPanel;
    std::unique_ptr<BottomPanel> bottomPanel;

    // Panel sizing (initialized from LayoutConfig)
    int transportHeight;
    int leftPanelWidth;
    int rightPanelWidth;
    int bottomPanelHeight;

    // MIDI editor fullscreen state (issue #1282): when on, bottomPanel
    // expands to the maximum height that still leaves the resizer
    // reachable. prevBottomPanelHeight_ snapshots the height to restore.
    bool editorFullscreen_ = false;
    int prevBottomPanelHeight_ = 0;
    bool prevBottomPanelVisible_ = true;
    bool prevBottomPanelCollapsed_ = false;

    // Resize handles
    class ResizeHandle;
    std::unique_ptr<ResizeHandle> transportResizer;
    std::unique_ptr<ResizeHandle> leftResizer;
    std::unique_ptr<ResizeHandle> rightResizer;
    std::unique_ptr<ResizeHandle> bottomResizer;

    // Loading overlay (shown during device initialization)
    class LoadingOverlay;
    std::unique_ptr<LoadingOverlay> loadingOverlay_;

    // Tooltip support — enabled via Config::getShowTooltips()
    std::unique_ptr<juce::TooltipWindow> tooltipWindow_;

    // Toast notification overlay (top-right corner)
    std::unique_ptr<daw::ui::Toast> toast_;

    // Setup helpers
    void setupResizeHandles();
    void setupViewModeListener();
    void setupAudioEngineCallbacks(AudioEngine* engine);
    void setupDeviceLoadingCallback();

    // Layout helpers
    void layoutTransportArea(juce::Rectangle<int>& bounds);
    void layoutFooterArea(juce::Rectangle<int>& bounds);
    void layoutSidePanels(juce::Rectangle<int>& bounds);
    void layoutBottomPanel(juce::Rectangle<int>& bounds);
    void layoutContentArea(juce::Rectangle<int>& bounds);

    // View switching helper
    void switchToView(ViewMode mode);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};

}  // namespace magda
