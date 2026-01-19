#include "MenuManager.hpp"

#include "core/TrackManager.hpp"
#include "core/ViewModeController.hpp"

namespace magica {

MenuManager& MenuManager::getInstance() {
    static MenuManager instance;
    return instance;
}

void MenuManager::initialize(const MenuCallbacks& callbacks) {
    callbacks_ = callbacks;
}

void MenuManager::updateMenuStates(bool canUndo, bool canRedo, bool hasSelection,
                                   bool leftPanelVisible, bool rightPanelVisible,
                                   bool bottomPanelVisible, bool isPlaying, bool isRecording,
                                   bool isLooping) {
    canUndo_ = canUndo;
    canRedo_ = canRedo;
    hasSelection_ = hasSelection;
    leftPanelVisible_ = leftPanelVisible;
    rightPanelVisible_ = rightPanelVisible;
    bottomPanelVisible_ = bottomPanelVisible;
    isPlaying_ = isPlaying;
    isRecording_ = isRecording;
    isLooping_ = isLooping;

    // Trigger menu update
    menuItemsChanged();
}

juce::StringArray MenuManager::getMenuBarNames() {
    return {"File", "Edit", "View", "Transport", "Track", "Window", "Help"};
}

juce::PopupMenu MenuManager::getMenuForIndex(int topLevelMenuIndex, const juce::String& menuName) {
    juce::PopupMenu menu;

    if (menuName == "File") {
        menu.addItem(NewProject, "New Project", true, false);
        menu.addSeparator();
        menu.addItem(OpenProject, "Open Project...", true, false);
        menu.addSeparator();
        menu.addItem(SaveProject, "Save Project", true, false);
        menu.addItem(SaveProjectAs, "Save Project As...", true, false);
        menu.addSeparator();
        menu.addItem(ImportAudio, "Import Audio...", true, false);
        menu.addItem(ExportAudio, "Export Audio...", true, false);

#if !JUCE_MAC
        menu.addSeparator();
        menu.addItem(Quit, "Quit", true, false);
#endif
    } else if (menuName == "Edit") {
        menu.addItem(Undo, "Undo", canUndo_, false);
        menu.addItem(Redo, "Redo", canRedo_, false);
        menu.addSeparator();
        menu.addItem(Cut, "Cut", hasSelection_, false);
        menu.addItem(Copy, "Copy", hasSelection_, false);
        menu.addItem(Paste, "Paste", true, false);
        menu.addItem(Delete, "Delete", hasSelection_, false);
        menu.addSeparator();
        menu.addItem(SelectAll, "Select All", true, false);
        menu.addSeparator();
        menu.addItem(Preferences, "Preferences...", true, false);
    } else if (menuName == "View") {
        menu.addItem(ToggleLeftPanel, "Show Left Panel", true, leftPanelVisible_);
        menu.addItem(ToggleRightPanel, "Show Right Panel", true, rightPanelVisible_);
        menu.addItem(ToggleBottomPanel, "Show Bottom Panel", true, bottomPanelVisible_);
        menu.addSeparator();
        menu.addItem(ZoomIn, "Zoom In", true, false);
        menu.addItem(ZoomOut, "Zoom Out", true, false);
        menu.addItem(ZoomToFit, "Zoom to Fit", true, false);
        menu.addSeparator();
        menu.addItem(ToggleFullscreen, "Enter Full Screen", true, false);
    } else if (menuName == "Transport") {
        menu.addItem(Play, isPlaying_ ? "Pause" : "Play", true, false);
        menu.addItem(Stop, "Stop", true, false);
        menu.addItem(Record, "Record", true, isRecording_);
        menu.addSeparator();
        menu.addItem(ToggleLoop, "Loop", true, isLooping_);
        menu.addSeparator();
        menu.addItem(GoToStart, "Go to Start", true, false);
        menu.addItem(GoToEnd, "Go to End", true, false);
    } else if (menuName == "Track") {
#if JUCE_MAC
        menu.addItem(AddTrack, juce::String("Add Track") + juce::String::fromUTF8("\t\u2318T"),
                     true, false);
        menu.addItem(AddGroupTrack,
                     juce::String("Add Group Track") + juce::String::fromUTF8("\t\u21E7\u2318T"),
                     true, false);
        menu.addSeparator();
        menu.addItem(DeleteTrack, juce::String("Delete Track") + juce::String::fromUTF8("\t\u232B"),
                     true, false);
        menu.addItem(DuplicateTrack,
                     juce::String("Duplicate Track") + juce::String::fromUTF8("\t\u2318D"), true,
                     false);
#else
        menu.addItem(AddTrack, "Add Track\tCtrl+T", true, false);
        menu.addItem(AddGroupTrack, "Add Group Track\tCtrl+Shift+T", true, false);
        menu.addSeparator();
        menu.addItem(DeleteTrack, "Delete Track\tDelete", true, false);
        menu.addItem(DuplicateTrack, "Duplicate Track\tCtrl+D", true, false);
#endif
        menu.addSeparator();
        menu.addItem(MuteTrack, "Mute Track\tM", true, false);
        menu.addItem(SoloTrack, "Solo Track\tS", true, false);

        // Track visibility submenu
        menu.addSeparator();
        juce::PopupMenu visibilityMenu;
        auto currentMode = ViewModeController::getInstance().getViewMode();
        const auto& tracks = TrackManager::getInstance().getTracks();
        for (const auto& track : tracks) {
            bool isVisible = track.isVisibleIn(currentMode);
            visibilityMenu.addItem(TrackVisibilityBase + track.id, track.name, true, isVisible);
        }
        if (tracks.empty()) {
            visibilityMenu.addItem(-1, "(No tracks)", false, false);
        }
        menu.addSubMenu("Track Visibility", visibilityMenu);
    } else if (menuName == "Window") {
        menu.addItem(Minimize, "Minimize", true, false);
        menu.addItem(Zoom, "Zoom", true, false);
        menu.addSeparator();
        menu.addItem(BringAllToFront, "Bring All to Front", true, false);
    } else if (menuName == "Help") {
        menu.addItem(ShowHelp, "Magica DAW Help", true, false);
        menu.addSeparator();
        menu.addItem(About, "About Magica DAW", true, false);
    }

    return menu;
}

void MenuManager::menuItemSelected(int menuItemID, int topLevelMenuIndex) {
    switch (menuItemID) {
        // File menu
        case NewProject:
            if (callbacks_.onNewProject)
                callbacks_.onNewProject();
            break;
        case OpenProject:
            if (callbacks_.onOpenProject)
                callbacks_.onOpenProject();
            break;
        case SaveProject:
            if (callbacks_.onSaveProject)
                callbacks_.onSaveProject();
            break;
        case SaveProjectAs:
            if (callbacks_.onSaveProjectAs)
                callbacks_.onSaveProjectAs();
            break;
        case ImportAudio:
            if (callbacks_.onImportAudio)
                callbacks_.onImportAudio();
            break;
        case ExportAudio:
            if (callbacks_.onExportAudio)
                callbacks_.onExportAudio();
            break;
        case Quit:
            if (callbacks_.onQuit)
                callbacks_.onQuit();
            break;

        // Edit menu
        case Undo:
            if (callbacks_.onUndo)
                callbacks_.onUndo();
            break;
        case Redo:
            if (callbacks_.onRedo)
                callbacks_.onRedo();
            break;
        case Cut:
            if (callbacks_.onCut)
                callbacks_.onCut();
            break;
        case Copy:
            if (callbacks_.onCopy)
                callbacks_.onCopy();
            break;
        case Paste:
            if (callbacks_.onPaste)
                callbacks_.onPaste();
            break;
        case Delete:
            if (callbacks_.onDelete)
                callbacks_.onDelete();
            break;
        case SelectAll:
            if (callbacks_.onSelectAll)
                callbacks_.onSelectAll();
            break;
        case Preferences:
            if (callbacks_.onPreferences)
                callbacks_.onPreferences();
            break;

        // View menu
        case ToggleLeftPanel:
            if (callbacks_.onToggleLeftPanel)
                callbacks_.onToggleLeftPanel(!leftPanelVisible_);
            break;
        case ToggleRightPanel:
            if (callbacks_.onToggleRightPanel)
                callbacks_.onToggleRightPanel(!rightPanelVisible_);
            break;
        case ToggleBottomPanel:
            if (callbacks_.onToggleBottomPanel)
                callbacks_.onToggleBottomPanel(!bottomPanelVisible_);
            break;
        case ZoomIn:
            if (callbacks_.onZoomIn)
                callbacks_.onZoomIn();
            break;
        case ZoomOut:
            if (callbacks_.onZoomOut)
                callbacks_.onZoomOut();
            break;
        case ZoomToFit:
            if (callbacks_.onZoomToFit)
                callbacks_.onZoomToFit();
            break;
        case ToggleFullscreen:
            if (callbacks_.onToggleFullscreen)
                callbacks_.onToggleFullscreen();
            break;

        // Transport menu
        case Play:
            if (callbacks_.onPlay)
                callbacks_.onPlay();
            break;
        case Stop:
            if (callbacks_.onStop)
                callbacks_.onStop();
            break;
        case Record:
            if (callbacks_.onRecord)
                callbacks_.onRecord();
            break;
        case ToggleLoop:
            if (callbacks_.onToggleLoop)
                callbacks_.onToggleLoop();
            break;
        case GoToStart:
            if (callbacks_.onGoToStart)
                callbacks_.onGoToStart();
            break;
        case GoToEnd:
            if (callbacks_.onGoToEnd)
                callbacks_.onGoToEnd();
            break;

        // Track menu
        case AddTrack:
            if (callbacks_.onAddTrack)
                callbacks_.onAddTrack();
            break;
        case AddGroupTrack:
            if (callbacks_.onAddGroupTrack)
                callbacks_.onAddGroupTrack();
            break;
        case DeleteTrack:
            if (callbacks_.onDeleteTrack)
                callbacks_.onDeleteTrack();
            break;
        case DuplicateTrack:
            if (callbacks_.onDuplicateTrack)
                callbacks_.onDuplicateTrack();
            break;
        case MuteTrack:
            if (callbacks_.onMuteTrack)
                callbacks_.onMuteTrack();
            break;
        case SoloTrack:
            if (callbacks_.onSoloTrack)
                callbacks_.onSoloTrack();
            break;

        // Window menu
        case Minimize:
            if (callbacks_.onMinimize)
                callbacks_.onMinimize();
            break;
        case Zoom:
            if (callbacks_.onZoom)
                callbacks_.onZoom();
            break;
        case BringAllToFront:
            if (callbacks_.onBringAllToFront)
                callbacks_.onBringAllToFront();
            break;

        // Help menu
        case ShowHelp:
            if (callbacks_.onShowHelp)
                callbacks_.onShowHelp();
            break;
        case About:
            if (callbacks_.onAbout)
                callbacks_.onAbout();
            break;

        default:
            // Check if it's a track visibility toggle (IDs 550+)
            if (menuItemID >= TrackVisibilityBase && menuItemID < 600) {
                int trackId = menuItemID - TrackVisibilityBase;
                if (callbacks_.onToggleTrackVisibility)
                    callbacks_.onToggleTrackVisibility(trackId);
            }
            break;
    }
}

}  // namespace magica
