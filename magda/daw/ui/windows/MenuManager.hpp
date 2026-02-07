#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

#include "core/UndoManager.hpp"

namespace magda {

class MenuManager : public juce::MenuBarModel, public UndoManagerListener {
  public:
    // Menu callbacks
    struct MenuCallbacks {
        // File menu
        std::function<void()> onNewProject;
        std::function<void()> onOpenProject;
        std::function<void()> onCloseProject;
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
        std::function<void()> onDuplicate;
        std::function<void()> onDelete;
        std::function<void()> onSplitOrTrim;
        std::function<void()> onJoinClips;
        std::function<void()> onRenderClip;
        std::function<void()> onRenderTimeSelection;
        std::function<void()> onSelectAll;
        std::function<void()> onPreferences;

        // Settings menu
        std::function<void()> onAudioSettings;
        std::function<void()> onPluginScan;
        std::function<void()> onPluginClear;
        std::function<void()> onPluginOpenFolder;

        // View menu
        std::function<void(bool)> onToggleLeftPanel;
        std::function<void(bool)> onToggleRightPanel;
        std::function<void(bool)> onToggleBottomPanel;
        std::function<void()> onZoomIn;
        std::function<void()> onZoomOut;
        std::function<void()> onZoomToFit;
        std::function<void()> onToggleFullscreen;
        std::function<void()> onToggleScrollbarPosition;

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
        std::function<void()> onDuplicateTrackNoContent;
        std::function<void()> onMuteTrack;
        std::function<void()> onSoloTrack;

        // View menu
        std::function<void()> onShowTrackManager;

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
    void updateMenuStates(bool canUndo, bool canRedo, bool hasSelection, bool hasEditCursor,
                          bool leftPanelVisible, bool rightPanelVisible, bool bottomPanelVisible,
                          bool isPlaying, bool isRecording, bool isLooping);

    // Get the menu bar model
    juce::MenuBarModel* getMenuBarModel() {
        return this;
    }

    // UndoManagerListener
    void undoStateChanged() override {
        // Force menu to rebuild when undo state changes
        menuItemsChanged();
    }

  private:
    MenuManager();
    ~MenuManager();

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
        CloseProject,
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
        Duplicate,
        Delete,
        SplitOrTrim = 218,
        JoinClips,
        RenderClip,
        RenderTimeSelection,
        SelectAll = 225,
        Preferences = 299,

        // Settings menu (800-899)
        AudioSettings = 800,
        PluginScan = 810,
        PluginClear,
        PluginOpenFolder,

        // View menu (300-399)
        ToggleLeftPanel = 300,
        ToggleRightPanel,
        ToggleBottomPanel,
        ShowTrackManager = 305,
        ZoomIn = 310,
        ZoomOut,
        ZoomToFit,
        ToggleFullscreen = 320,
        ToggleScrollbarPosition = 325,

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
        DuplicateTrackNoContent,
        MuteTrack = 520,
        SoloTrack,

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
    bool hasEditCursor_ = false;  // For Split operation
    bool leftPanelVisible_ = true;
    bool rightPanelVisible_ = true;
    bool bottomPanelVisible_ = true;
    bool isPlaying_ = false;
    bool isRecording_ = false;
    bool isLooping_ = false;
};

}  // namespace magda
