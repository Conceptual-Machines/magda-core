#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace magica {

class MenuManager : public juce::MenuBarModel {
  public:
    // Menu callbacks
    struct MenuCallbacks {
        // File menu
        std::function<void()> onNewProject;
        std::function<void()> onOpenProject;
        std::function<void()> onSaveProject;
        std::function<void()> onSaveProjectAs;
        std::function<void()> onImportAudio;
        std::function<void()> onExportAudio;
        std::function<void()> onQuit;

        // Edit menu
        std::function<void()> onUndo;
        std::function<void()> onRedo;
        std::function<void()> onCut;
        std::function<void()> onCopy;
        std::function<void()> onPaste;
        std::function<void()> onDelete;
        std::function<void()> onSelectAll;
        std::function<void()> onPreferences;

        // View menu
        std::function<void(bool)> onToggleLeftPanel;
        std::function<void(bool)> onToggleRightPanel;
        std::function<void(bool)> onToggleBottomPanel;
        std::function<void()> onZoomIn;
        std::function<void()> onZoomOut;
        std::function<void()> onZoomToFit;
        std::function<void()> onToggleFullscreen;

        // Transport menu
        std::function<void()> onPlay;
        std::function<void()> onStop;
        std::function<void()> onRecord;
        std::function<void()> onToggleLoop;
        std::function<void()> onGoToStart;
        std::function<void()> onGoToEnd;

        // Track menu
        std::function<void()> onAddTrack;
        std::function<void()> onAddGroupTrack;
        std::function<void()> onDeleteTrack;
        std::function<void()> onDuplicateTrack;
        std::function<void()> onMuteTrack;
        std::function<void()> onSoloTrack;
        std::function<void(int)> onToggleTrackVisibility;  // Toggle visibility for track ID

        // Window menu
        std::function<void()> onMinimize;
        std::function<void()> onZoom;
        std::function<void()> onBringAllToFront;

        // Help menu
        std::function<void()> onShowHelp;
        std::function<void()> onAbout;
    };

    static MenuManager& getInstance();

    // Set up the menu bar
    void initialize(const MenuCallbacks& callbacks);

    // Update menu item states
    void updateMenuStates(bool canUndo, bool canRedo, bool hasSelection, bool leftPanelVisible,
                          bool rightPanelVisible, bool bottomPanelVisible, bool isPlaying,
                          bool isRecording, bool isLooping);

    // Get the menu bar model
    juce::MenuBarModel* getMenuBarModel() {
        return this;
    }

  private:
    MenuManager() = default;
    ~MenuManager() = default;

    // Non-copyable
    MenuManager(const MenuManager&) = delete;
    MenuManager& operator=(const MenuManager&) = delete;

    // MenuBarModel implementation
    juce::StringArray getMenuBarNames() override;
    juce::PopupMenu getMenuForIndex(int topLevelMenuIndex, const juce::String& menuName) override;
    void menuItemSelected(int menuItemID, int topLevelMenuIndex) override;

    // Menu IDs
    enum MenuIDs {
        // File menu (100-199)
        NewProject = 100,
        OpenProject,
        SaveProject,
        SaveProjectAs,
        ImportAudio = 110,
        ExportAudio,
        Quit = 199,

        // Edit menu (200-299)
        Undo = 200,
        Redo,
        Cut = 210,
        Copy,
        Paste,
        Delete,
        SelectAll = 220,
        Preferences = 299,

        // View menu (300-399)
        ToggleLeftPanel = 300,
        ToggleRightPanel,
        ToggleBottomPanel,
        ZoomIn = 310,
        ZoomOut,
        ZoomToFit,
        ToggleFullscreen = 320,

        // Transport menu (400-499)
        Play = 400,
        Stop,
        Record,
        ToggleLoop = 410,
        GoToStart = 420,
        GoToEnd,

        // Track menu (500-599)
        AddTrack = 500,
        AddGroupTrack,
        DeleteTrack = 510,
        DuplicateTrack,
        MuteTrack = 520,
        SoloTrack,
        // Track visibility toggles start at 550 (550 + trackId)
        TrackVisibilityBase = 550,

        // Window menu (600-699)
        Minimize = 600,
        Zoom,
        BringAllToFront = 610,

        // Help menu (700-799)
        ShowHelp = 700,
        About = 799
    };

    MenuCallbacks callbacks_;

    // Menu state
    bool canUndo_ = false;
    bool canRedo_ = false;
    bool hasSelection_ = false;
    bool leftPanelVisible_ = true;
    bool rightPanelVisible_ = true;
    bool bottomPanelVisible_ = true;
    bool isPlaying_ = false;
    bool isRecording_ = false;
    bool isLooping_ = false;
};

}  // namespace magica
