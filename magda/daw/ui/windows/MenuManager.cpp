#include "MenuManager.hpp"

#include "Config.hpp"
#include "core/StringTable.hpp"
#include "core/UndoManager.hpp"

namespace magda {

MenuManager& MenuManager::getInstance() {
    static MenuManager instance;
    return instance;
}

MenuManager::MenuManager() {
    // Register as UndoManager listener to refresh menu when undo state changes
    UndoManager::getInstance().addListener(this);
}

MenuManager::~MenuManager() {
    UndoManager::getInstance().removeListener(this);
}

void MenuManager::initialize(const MenuCallbacks& callbacks) {
    callbacks_ = callbacks;
}

void MenuManager::updateMenuStates(bool canUndo, bool canRedo, bool hasSelection,
                                   bool hasEditCursor, bool leftPanelVisible,
                                   bool rightPanelVisible, bool bottomPanelVisible, bool isPlaying,
                                   bool isRecording, bool isLooping) {
    canUndo_ = canUndo;
    canRedo_ = canRedo;
    hasSelection_ = hasSelection;
    hasEditCursor_ = hasEditCursor;
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
    return {tr("menu.file"),  tr("menu.edit"),     tr("menu.view"),   tr("menu.transport"),
            tr("menu.track"), tr("menu.settings"), tr("menu.window"), tr("menu.help")};
}

juce::PopupMenu MenuManager::getMenuForIndex(int topLevelMenuIndex,
                                             const juce::String& /*menuName*/) {
    juce::PopupMenu menu;

    switch (topLevelMenuIndex) {
        case 0:  // File
        {
            menu.addItem(NewProject, tr("menu.file.new_project"), true, false);
            menu.addSeparator();
            menu.addItem(OpenProject, tr("menu.file.open_project"), true, false);

            // Open Recent submenu
            {
                juce::PopupMenu recentMenu;
                auto recentPaths = Config::getInstance().getRecentProjects();
                if (recentPaths.empty()) {
                    recentMenu.addItem(0, tr("menu.file.no_recent"), false, false);
                } else {
                    int idx = 0;
                    for (const auto& path : recentPaths) {
                        if (idx >= 10)
                            break;
                        auto name = juce::File(juce::String(path)).getFileNameWithoutExtension();
                        recentMenu.addItem(RecentProjectBase + idx, name, true, false);
                        ++idx;
                    }
                    recentMenu.addSeparator();
                    recentMenu.addItem(RecentProjectBase + 10, tr("menu.file.clear_recent"), true,
                                       false);
                }
                menu.addSubMenu(tr("menu.file.open_recent"), recentMenu);
            }

            menu.addItem(CloseProject, tr("menu.file.close_project"), true, false);
            menu.addSeparator();
            menu.addItem(SaveProject, tr("menu.file.save_project"), true, false);
            menu.addItem(SaveProjectAs, tr("menu.file.save_project_as"), true, false);
            menu.addSeparator();
            menu.addItem(ExportAudio, tr("menu.file.export_audio"), true, false);
            menu.addItem(ExportMidi, tr("menu.file.export_midi"), true, false);

#if !JUCE_MAC
            menu.addSeparator();
            menu.addItem(Quit, tr("menu.file.quit"), true, false);
#endif
            break;
        }

        case 1:  // Edit
        {
            // Get undo/redo state directly from UndoManager for accurate descriptions
            auto& undoManager = UndoManager::getInstance();
            bool canUndo = undoManager.canUndo();
            bool canRedo = undoManager.canRedo();

            // Build undo menu item with description
            juce::String undoText = tr("menu.edit.undo");
            if (canUndo) {
                juce::String desc = undoManager.getUndoDescription();
                if (desc.isNotEmpty()) {
                    undoText = tr("menu.edit.undo") + " " + desc;
                }
            }

            // Build redo menu item with description
            juce::String redoText = tr("menu.edit.redo");
            if (canRedo) {
                juce::String desc = undoManager.getRedoDescription();
                if (desc.isNotEmpty()) {
                    redoText = tr("menu.edit.redo") + " " + desc;
                }
            }

#if JUCE_MAC
            menu.addItem(Undo, undoText + juce::String::fromUTF8("\t\u2318Z"), canUndo, false);
            menu.addItem(Redo, redoText + juce::String::fromUTF8("\t\u21E7\u2318Z"), canRedo,
                         false);
            menu.addSeparator();
            menu.addItem(Cut, tr("menu.edit.cut") + juce::String::fromUTF8("\t\u2318X"),
                         hasSelection_, false);
            menu.addItem(Copy, tr("menu.edit.copy") + juce::String::fromUTF8("\t\u2318C"),
                         hasSelection_, false);
            menu.addItem(Paste, tr("menu.edit.paste") + juce::String::fromUTF8("\t\u2318V"), true,
                         false);
            menu.addItem(Duplicate, tr("menu.edit.duplicate") + juce::String::fromUTF8("\t\u2318D"),
                         hasSelection_, false);
            menu.addItem(Delete, tr("menu.edit.delete") + juce::String::fromUTF8("\t\u232B"),
                         hasSelection_, false);
            menu.addSeparator();
            menu.addItem(SplitOrTrim,
                         tr("menu.edit.split_trim") + juce::String::fromUTF8("\t\u2318E"), true,
                         false);
            menu.addItem(JoinClips,
                         tr("menu.edit.join_clips") + juce::String::fromUTF8("\t\u2318J"),
                         hasSelection_, false);
            menu.addSeparator();
            menu.addItem(RenderClip,
                         tr("menu.edit.render_clip") + juce::String::fromUTF8("\t\u2318B"),
                         hasSelection_, false);
            menu.addItem(RenderTimeSelection,
                         tr("menu.edit.render_time_selection") +
                             juce::String::fromUTF8("\t\u21E7\u2318B"),
                         true, false);
            menu.addSeparator();
            menu.addItem(SelectAll,
                         tr("menu.edit.select_all") + juce::String::fromUTF8("\t\u2318A"), true,
                         false);
#else
            menu.addItem(Undo, undoText + "\tCtrl+Z", canUndo, false);
            menu.addItem(Redo, redoText + "\tCtrl+Shift+Z", canRedo, false);
            menu.addSeparator();
            menu.addItem(Cut, tr("menu.edit.cut") + "\tCtrl+X", hasSelection_, false);
            menu.addItem(Copy, tr("menu.edit.copy") + "\tCtrl+C", hasSelection_, false);
            menu.addItem(Paste, tr("menu.edit.paste") + "\tCtrl+V", true, false);
            menu.addItem(Duplicate, tr("menu.edit.duplicate") + "\tCtrl+D", hasSelection_, false);
            menu.addItem(Delete, tr("menu.edit.delete") + "\tDelete", hasSelection_, false);
            menu.addSeparator();
            menu.addItem(SplitOrTrim, tr("menu.edit.split_trim") + "\tCtrl+E", true, false);
            menu.addItem(JoinClips, tr("menu.edit.join_clips") + "\tCtrl+J", hasSelection_, false);
            menu.addSeparator();
            menu.addItem(RenderClip, tr("menu.edit.render_clip") + "\tCtrl+B", hasSelection_,
                         false);
            menu.addItem(RenderTimeSelection,
                         tr("menu.edit.render_time_selection") + "\tCtrl+Shift+B", true, false);
            menu.addSeparator();
            menu.addItem(SelectAll, tr("menu.edit.select_all") + "\tCtrl+A", true, false);
#endif
#if !JUCE_MAC
            menu.addSeparator();
            menu.addItem(Preferences, tr("menu.settings.preferences"), true, false);
#endif
            break;
        }

        case 2:  // View
        {
            menu.addItem(ShowTrackManager, tr("menu.view.track_manager"), true, false);
            menu.addSeparator();
            bool headersOnRight = Config::getInstance().getScrollbarOnLeft();
            menu.addItem(ToggleScrollbarPosition, tr("menu.view.headers_right"), true,
                         headersOnRight);
            menu.addSeparator();
            menu.addItem(ZoomIn, tr("menu.view.zoom_in"), true, false);
            menu.addItem(ZoomOut, tr("menu.view.zoom_out"), true, false);
            menu.addItem(ZoomToFit, tr("menu.view.zoom_to_fit"), true, false);
            menu.addItem(ZoomLoopToFit, tr("menu.view.zoom_loop"), true, false);
            menu.addItem(ZoomSelectionToFit, tr("menu.view.zoom_selection"), true, false);
            menu.addSeparator();
            menu.addItem(ToggleFullscreen, tr("menu.view.fullscreen"), true, false);
            break;
        }

        case 3:  // Transport
        {
            menu.addItem(Play, isPlaying_ ? tr("menu.transport.pause") : tr("menu.transport.play"),
                         true, false);
            menu.addItem(Stop, tr("menu.transport.stop"), true, false);
            menu.addItem(Record, tr("menu.transport.record"), true, isRecording_);
            menu.addSeparator();
            menu.addItem(ToggleLoop, tr("menu.transport.loop"), true, isLooping_);
            menu.addSeparator();
            menu.addItem(GoToStart, tr("menu.transport.go_to_start"), true, false);
            menu.addItem(GoToEnd, tr("menu.transport.go_to_end"), true, false);
            break;
        }

        case 4:  // Track
        {
#if JUCE_MAC
            menu.addItem(AddTrack, tr("menu.track.add_track") + juce::String::fromUTF8("\t\u2318T"),
                         true, false);
            menu.addItem(AddGroupTrack,
                         tr("menu.track.add_group") + juce::String::fromUTF8("\t\u21E7\u2318T"),
                         true, false);
            menu.addItem(AddAuxTrack, tr("menu.track.add_aux"), true, false);
            menu.addSeparator();
            menu.addItem(DeleteTrack, tr("menu.track.delete") + juce::String::fromUTF8("\t\u232B"),
                         true, false);
            menu.addItem(DuplicateTrack,
                         tr("menu.track.duplicate") + juce::String::fromUTF8("\t\u2318D"), true,
                         false);
            menu.addItem(DuplicateTrackNoContent,
                         tr("menu.track.duplicate_no_content") +
                             juce::String::fromUTF8("\t\u21E7\u2318D"),
                         true, false);
#else
            menu.addItem(AddTrack, tr("menu.track.add_track") + "\tCtrl+T", true, false);
            menu.addItem(AddGroupTrack, tr("menu.track.add_group") + "\tCtrl+Shift+T", true, false);
            menu.addItem(AddAuxTrack, tr("menu.track.add_aux"), true, false);
            menu.addSeparator();
            menu.addItem(DeleteTrack, tr("menu.track.delete") + "\tDelete", true, false);
            menu.addItem(DuplicateTrack, tr("menu.track.duplicate") + "\tCtrl+D", true, false);
            menu.addItem(DuplicateTrackNoContent,
                         tr("menu.track.duplicate_no_content") + "\tCtrl+Shift+D", true, false);
#endif
            menu.addSeparator();
            menu.addItem(MuteTrack, tr("menu.track.mute") + "\tM", true, false);
            menu.addItem(SoloTrack, tr("menu.track.solo") + "\tS", true, false);
            break;
        }

        case 5:  // Settings
        {
            menu.addItem(Preferences, tr("menu.settings.preferences"), true, false);
            menu.addSeparator();
            menu.addItem(AISettings, tr("menu.settings.ai"), true, false);
            menu.addSeparator();
            menu.addItem(AudioSettings, tr("menu.settings.audio_midi"), true, false);
            menu.addSeparator();
            menu.addItem(PluginSettings, tr("menu.settings.plugins"), true, false);
            break;
        }

        case 6:  // Window
        {
            menu.addItem(Minimize, tr("menu.window.minimize"), true, false);
            menu.addItem(Zoom, tr("menu.window.zoom"), true, false);
            menu.addSeparator();
            menu.addItem(BringAllToFront, tr("menu.window.bring_to_front"), true, false);
            break;
        }

        case 7:  // Help
        {
            menu.addItem(OpenManual, tr("menu.help.manual"), true, false);
            menu.addSeparator();
            menu.addItem(About, tr("menu.help.about"), true, false);
            break;
        }

        default:
            break;
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
        case CloseProject:
            if (callbacks_.onCloseProject)
                callbacks_.onCloseProject();
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
        case ExportMidi:
            if (callbacks_.onExportMidi)
                callbacks_.onExportMidi();
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
        case Duplicate:
            if (callbacks_.onDuplicate)
                callbacks_.onDuplicate();
            break;
        case Delete:
            if (callbacks_.onDelete)
                callbacks_.onDelete();
            break;
        case SplitOrTrim:
            if (callbacks_.onSplitOrTrim)
                callbacks_.onSplitOrTrim();
            break;
        case JoinClips:
            if (callbacks_.onJoinClips)
                callbacks_.onJoinClips();
            break;
        case RenderClip:
            if (callbacks_.onRenderClip)
                callbacks_.onRenderClip();
            break;
        case RenderTimeSelection:
            if (callbacks_.onRenderTimeSelection)
                callbacks_.onRenderTimeSelection();
            break;
        case SelectAll:
            if (callbacks_.onSelectAll)
                callbacks_.onSelectAll();
            break;
        case Preferences:
            if (callbacks_.onPreferences)
                callbacks_.onPreferences();
            break;
        // Settings menu
        case AISettings:
            if (callbacks_.onAISettings)
                callbacks_.onAISettings();
            break;
        case AudioSettings:
            if (callbacks_.onAudioSettings)
                callbacks_.onAudioSettings();
            break;
        case PluginSettings:
            if (callbacks_.onPluginSettings)
                callbacks_.onPluginSettings();
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
        case ZoomLoopToFit:
            if (callbacks_.onZoomLoopToFit)
                callbacks_.onZoomLoopToFit();
            break;
        case ZoomSelectionToFit:
            if (callbacks_.onZoomSelectionToFit)
                callbacks_.onZoomSelectionToFit();
            break;
        case ToggleFullscreen:
            if (callbacks_.onToggleFullscreen)
                callbacks_.onToggleFullscreen();
            break;
        case ShowTrackManager:
            if (callbacks_.onShowTrackManager)
                callbacks_.onShowTrackManager();
            break;
        case ToggleScrollbarPosition:
            if (callbacks_.onToggleScrollbarPosition)
                callbacks_.onToggleScrollbarPosition();
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
        case AddAuxTrack:
            if (callbacks_.onAddAuxTrack)
                callbacks_.onAddAuxTrack();
            break;
        case DeleteTrack:
            if (callbacks_.onDeleteTrack)
                callbacks_.onDeleteTrack();
            break;
        case DuplicateTrack:
            if (callbacks_.onDuplicateTrack)
                callbacks_.onDuplicateTrack();
            break;
        case DuplicateTrackNoContent:
            if (callbacks_.onDuplicateTrackNoContent)
                callbacks_.onDuplicateTrackNoContent();
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
        case OpenManual:
            if (callbacks_.onOpenManual)
                callbacks_.onOpenManual();
            break;
        case About:
            if (callbacks_.onAbout)
                callbacks_.onAbout();
            break;

        default:
            // Recent projects (IDs 150-159)
            if (menuItemID >= RecentProjectBase && menuItemID < RecentProjectBase + 10) {
                int idx = menuItemID - RecentProjectBase;
                auto recentPaths = Config::getInstance().getRecentProjects();
                if (idx < static_cast<int>(recentPaths.size()) && callbacks_.onOpenRecentProject) {
                    callbacks_.onOpenRecentProject(juce::String(recentPaths[idx]));
                }
            }
            // Clear Recent Projects (ID 160)
            else if (menuItemID == RecentProjectBase + 10) {
                Config::getInstance().clearRecentProjects();
                Config::getInstance().save();
                menuItemsChanged();
            }
            break;
    }
}

}  // namespace magda
