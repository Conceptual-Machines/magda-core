#pragma once

namespace magda {
namespace CommandIDs {

enum {
    // File menu
    newProject = 0x0001,
    openProject = 0x0002,
    saveProject = 0x0003,
    saveProjectAs = 0x0004,
    exportAudio = 0x0005,

    // Edit menu
    undo = 0x1000,
    redo = 0x1001,
    cut = 0x1002,
    copy = 0x1003,
    paste = 0x1004,
    duplicate = 0x1005,
    deleteCmd = 0x1006,  // 'delete' is a keyword
    selectAll = 0x1007,
    splitOrTrim = 0x1008,  // Cmd+E: split at cursor, or trim to selection if time selection exists
    joinClips = 0x1009,    // Cmd+J: join two adjacent clips into one
    renderClip = 0x100A,   // Cmd+B: render selected clips
    renderTimeSelection = 0x100B,  // Cmd+Shift+B: consolidate time selection to audio
    setLoopFromClip = 0x100C,      // Cmd+Shift+L: set loop region to selected clip bounds
    toggleClipLoop = 0x100D,       // Cmd+L: toggle loop on/off for selected clip
    duplicateClipWithAutomation = 0x100E,
    duplicateClipWithoutAutomation = 0x100F,
    escapeAction = 0x1010,        // Esc: exit link mode / clear edit cursor
    insertTime = 0x1011,          // Insert empty time at the selection, ripple later content right
    duplicateTimeRange = 0x1012,  // Ripple-duplicate the time selection after itself
    duplicateLoopRange =
        0x1013,  // Trim both loop ends, then ripple-duplicate the loop (all tracks)
    splitAllTracksAtCursor = 0x1014,  // Split every clip crossing the edit cursor, all tracks
    copyTimeRange = 0x1015,           // Copy the time selection's content to the clipboard
    cutTimeRange = 0x1016,            // Copy the time selection, then ripple-delete it
    deleteTimeRange = 0x1017,         // Ripple-delete the time selection (close the gap)
    copyLoopRange = 0x1018,           // Copy the loop region's content (all tracks)
    cutLoopRange = 0x1019,            // Copy the loop region, then ripple-delete it (all tracks)
    deleteLoopRange = 0x101A,         // Ripple-delete the loop region (all tracks)
    pasteRipple = 0x101B,             // Ripple-insert the clipboard span, then paste into it
    duplicateClipAsGhost = 0x101C,    // Copy joins the source's link group (mirrors content)
    makeClipUnique = 0x101D,          // Detach selected ghost clips from their link groups
    toggleClipEnabled = 0x101E,       // 0: enable/disable selected clips (disabled = no playback)

    // Transport menu
    play = 0x2000,  // Space: toggles play/stop (perform() already toggles)
    stop = 0x2001,
    record = 0x2002,
    goToStart = 0x2003,
    goToEnd = 0x2004,
    addMarker = 0x2005,
    goToPreviousMarker = 0x2006,
    goToNextMarker = 0x2007,
    goToLoopStart = 0x2008,
    goToLoopEnd = 0x2009,
    goToSelectionStart = 0x200A,
    goToSelectionEnd = 0x200B,

    // Track menu
    newAudioTrack = 0x3000,
    newMidiTrack = 0x3001,
    deleteTrack = 0x3002,
    duplicateTrackNoContent = 0x3003,    // Cmd/Ctrl+Shift+D: header only, no clips
    duplicateTrackContentOnly = 0x3004,  // Cmd/Ctrl+Alt+D: clips only, no FX chain
    toggleMuteSelectedTracks = 0x3005,   // M
    toggleSoloSelectedTracks = 0x3006,   // Shift+S
    audioLevelTest = 0x3007,             // Cmd/Ctrl+Shift+A: create two -12dB tone tracks

    // View menu
    zoom = 0x4000,
    toggleArrangeSession = 0x4001,
    uiScaleUp = 0x4002,    // Cmd+= / Cmd++: increase global UI scale
    uiScaleDown = 0x4003,  // Cmd+- / Cmd+_: decrease global UI scale
    cycleViewForward = 0x4004,
    cycleViewBackward = 0x4005,
    togglePianoRollFullscreen = 0x4006,  // Cmd/Ctrl+Shift+P

    // Help menu
    showHelp = 0x5000,
    about = 0x5001
};

}  // namespace CommandIDs
}  // namespace magda
