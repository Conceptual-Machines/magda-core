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
        std::function<void()> onProjectSettings;
        std::function<void()> onCollectFiles;
        std::function<void()> onExportAudio;
        std::function<void()> onExportMidi;
        std::function<void()> onImportDawProject;
        std::function<void()> onExportDawProject;
        std::function<void()> onQuit;
        std::function<void(const juce::String&)> onOpenRecentProject;

        // Edit menu
        std::function<void()> onUndo;
        std::function<void()> onRedo;
        std::function<void()> onCut;
        std::function<void()> onCopy;
        std::function<void()> onPaste;
        std::function<void()> onDuplicate;
        std::function<void()> onDuplicateClipWithAutomation;
        std::function<void()> onDuplicateClipWithoutAutomation;
        std::function<void()> onDuplicateClipAsGhost;
        std::function<void()> onMakeClipUnique;
        std::function<void()> onDelete;
        std::function<void()> onSplitOrTrim;
        std::function<void()> onJoinClips;
        std::function<void()> onRenderClip;
        std::function<void()> onRenderTimeSelection;
        std::function<void()> onInsertTime;
        std::function<void()> onDuplicateTimeRange;
        std::function<void()> onDuplicateLoopRange;
        std::function<void()> onSplitAllTracksAtCursor;
        std::function<void()> onCopyTimeRange;
        std::function<void()> onCutTimeRange;
        std::function<void()> onDeleteTimeRange;
        std::function<void()> onCopyLoopRange;
        std::function<void()> onCutLoopRange;
        std::function<void()> onDeleteLoopRange;
        std::function<void()> onPasteRipple;
        std::function<void()> onSelectAll;
        std::function<void()> onPreferences;

        // Settings menu
        std::function<void()> onAISettings;
        std::function<void()> onAudioSettings;
        std::function<void()> onPluginSettings;
        std::function<void()> onControllerSettings;
        std::function<void()> onConnectionSettings;

        // View menu
        std::function<void(bool)> onToggleLeftPanel;
        std::function<void(bool)> onToggleRightPanel;
        std::function<void(bool)> onToggleBottomPanel;
        std::function<void()> onZoomIn;
        std::function<void()> onZoomOut;
        std::function<void()> onZoomToFit;
        std::function<void()> onZoomLoopToFit;
        std::function<void()> onZoomSelectionToFit;
        std::function<void()> onToggleArrangeSession;
        std::function<void()> onToggleFullscreen;
        std::function<void()> onToggleScrollbarPosition;

        // Transport menu
        std::function<void()> onPlay;
        std::function<void()> onStop;
        std::function<void()> onRecord;
        std::function<void()> onToggleLoop;
        std::function<void()> onGoToStart;
        std::function<void()> onGoToEnd;
        std::function<void()> onAddMarker;
        std::function<void()> onGoToPreviousMarker;
        std::function<void()> onGoToNextMarker;

        // Track menu
        std::function<void()> onAddTrack;
        std::function<void()> onAddGroupTrack;
        std::function<void()> onAddAuxTrack;
        std::function<void()> onAddChordTrack;
        std::function<void()> onDeleteTrack;
        std::function<void()> onDuplicateTrack;
        std::function<void()> onDuplicateTrackNoContent;
        std::function<void()> onDuplicateTrackContentOnly;
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
        std::function<void()> onOpenManual;
        std::function<void()> onCheckForUpdates;
        std::function<void()> onAbout;
    };

    static MenuManager& getInstance();

    // Set up the menu bar
    void initialize(const MenuCallbacks& callbacks);

    // Invoke a command-backed menu action through the same callback used by a
    // menu click. Returns false when the command is not handled. A missing
    // callback for a known command is a programming error.
    bool invokeApplicationCommand(juce::CommandID commandID);

    // Source of truth for menu shortcut hints (#1352). When set, menu items
    // render the command's current key from the mapping set instead of a
    // hardcoded per-platform string, so remaps (#20) stay in sync. Owned by
    // MainComponent; cleared (nullptr) on its destruction.
    void setCommandManager(juce::ApplicationCommandManager* commandManager) {
        commandManager_ = commandManager;
    }

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

    // Tab-prefixed shortcut hint for a command's current key (platform-correct
    // via getTextDescriptionWithIcons), or "" if unbound / no command manager.
    juce::String keyHint(juce::CommandID commandID) const;

    juce::ApplicationCommandManager* commandManager_ = nullptr;

    // Menu IDs
    enum MenuIDs {
        // File menu (100-199)
        NewProject = 100,
        OpenProject,
        CloseProject,
        SaveProject,
        SaveProjectAs,
        ExportAudio = 111,
        ExportMidi,
        ImportDawProject = 113,
        ExportDawProject,
        CollectFiles = 115,
        ProjectSettings = 116,
        RecentProjectBase = 150,  // 150-159 reserved for recent projects
        Quit = 199,

        // Edit menu (200-299)
        Undo = 200,
        Redo,
        Cut = 210,
        Copy,
        Paste,
        Duplicate,
        DuplicateClipWithAutomation,
        DuplicateClipWithoutAutomation,
        Delete,
        SplitOrTrim = 218,
        JoinClips,
        RenderClip,
        RenderTimeSelection,
        InsertTime = 222,
        DuplicateTimeRange,
        DuplicateLoopRange,
        SelectAll = 225,
        SplitAllTracksAtCursor = 226,
        CopyTimeRange = 227,
        CutTimeRange,
        DeleteTimeRange,
        CopyLoopRange,
        CutLoopRange,
        DeleteLoopRange,
        PasteRipple,
        DuplicateClipAsGhost = 235,
        MakeClipUnique,
        Preferences = 299,

        // Settings menu (800-899)
        AISettings = 800,
        AudioSettings = 801,
        PluginSettings = 810,
        ControllerSettings = 811,
        ConnectionSettings = 812,

        // View menu (300-399)
        ToggleLeftPanel = 300,
        ToggleRightPanel,
        ToggleBottomPanel,
        ShowTrackManager = 305,
        ZoomIn = 310,
        ZoomOut,
        ZoomToFit,
        ZoomLoopToFit,
        ZoomSelectionToFit,
        ToggleFullscreen = 320,
        ToggleScrollbarPosition = 325,

        // Transport menu (400-499)
        Play = 400,
        Stop,
        Record,
        ToggleLoop = 410,
        GoToStart = 420,
        GoToEnd,
        AddMarker,
        GoToPreviousMarker,
        GoToNextMarker,

        // Track menu (500-599)
        AddTrack = 500,
        AddGroupTrack,
        AddAuxTrack,
        AddChordTrack,
        DeleteTrack = 510,
        DuplicateTrack,
        DuplicateTrackNoContent,
        DuplicateTrackContentOnly,
        MuteTrack = 520,
        SoloTrack,

        // Window menu (600-699)
        Minimize = 600,
        Zoom,
        BringAllToFront = 610,

        // Help menu (700-799)
        ShowHelp = 700,
        OpenManual,
        CheckForUpdates,
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
