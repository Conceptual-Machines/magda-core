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
    split = 0x1008,
    trim = 0x1009,

    // Transport menu
    play = 0x2000,
    stop = 0x2001,
    record = 0x2002,
    goToStart = 0x2003,
    goToEnd = 0x2004,

    // Track menu
    newAudioTrack = 0x3000,
    newMidiTrack = 0x3001,
    deleteTrack = 0x3002,

    // View menu
    zoom = 0x4000,
    toggleArrangeSession = 0x4001,

    // Help menu
    showHelp = 0x5000,
    about = 0x5001
};

}  // namespace CommandIDs
}  // namespace magda
