#include "QwertyMidiKeyboard.hpp"

#include <tracktion_engine/tracktion_engine.h>

#include "../core/SelectionManager.hpp"
#include "../core/TrackManager.hpp"
#include "AudioBridge.hpp"

namespace magda {

QwertyMidiKeyboard::QwertyMidiKeyboard(AudioBridge& bridge) : bridge_(bridge) {}

QwertyMidiKeyboard::~QwertyMidiKeyboard() {
    allNotesOff();
}

void QwertyMidiKeyboard::setEnabled(bool enabled) {
    if (enabled_ == enabled)
        return;
    if (!enabled)
        allNotesOff();
    enabled_ = enabled;
}

// Key mapping: returns a MIDI note number for the configured base octave, or -1
// if the key is not mapped.
// Base octave (octaveOffset = 0): A=C, S=D, D=E, F=F, G=G, H=A, J=B,
// with black keys W=C#, E=D#, T=F#, Y=G#, U=A#.
// Upper extension (octaveOffset = 1): K=C, L=D, with black keys O=C#, P=D#.
int QwertyMidiKeyboard::keyToNote(int keyCode) const {
    int semitone = -1;
    int octaveOffset = 0;

    switch (keyCode) {
        // Lower row: white keys (C..B)
        case 'A':
            semitone = 0;
            break;  // C
        case 'S':
            semitone = 2;
            break;  // D
        case 'D':
            semitone = 4;
            break;  // E
        case 'F':
            semitone = 5;
            break;  // F
        case 'G':
            semitone = 7;
            break;  // G
        case 'H':
            semitone = 9;
            break;  // A
        case 'J':
            semitone = 11;
            break;  // B
        // Lower row: black keys
        case 'W':
            semitone = 1;
            break;  // C#
        case 'E':
            semitone = 3;
            break;  // D#
        case 'T':
            semitone = 6;
            break;  // F#
        case 'Y':
            semitone = 8;
            break;  // G#
        case 'U':
            semitone = 10;
            break;  // A#

        // Upper row: white keys (one octave up)
        case 'K':
            semitone = 0;
            octaveOffset = 1;
            break;  // C
        case 'L':
            semitone = 2;
            octaveOffset = 1;
            break;  // D
        // Upper row: black keys
        case 'O':
            semitone = 1;
            octaveOffset = 1;
            break;  // C#
        case 'P':
            semitone = 3;
            octaveOffset = 1;
            break;  // D#

        default:
            return -1;
    }

    int note = (baseOctave_ + octaveOffset) * 12 + semitone;
    return juce::jlimit(0, 127, note);
}

void QwertyMidiKeyboard::sendNoteOn(int note) {
    auto* vmd = bridge_.getQwertyMidiDevice();
    if (!vmd)
        return;
    auto msg = juce::MidiMessage::noteOn(1, note, static_cast<juce::uint8>(velocity_));
    vmd->handleIncomingMidiMessage(msg, te::MPESourceID{});

    auto trackId = SelectionManager::getInstance().getSelectedTrack();
    if (trackId != INVALID_TRACK_ID) {
        bridge_.triggerMidiActivity(trackId);
        TrackManager::getInstance().triggerMidiNoteOn(trackId);
    }
}

void QwertyMidiKeyboard::sendNoteOff(int note) {
    auto* vmd = bridge_.getQwertyMidiDevice();
    if (!vmd)
        return;
    auto msg = juce::MidiMessage::noteOff(1, note);
    vmd->handleIncomingMidiMessage(msg, te::MPESourceID{});

    auto trackId = SelectionManager::getInstance().getSelectedTrack();
    if (trackId != INVALID_TRACK_ID)
        TrackManager::getInstance().triggerMidiNoteOff(trackId);
}

void QwertyMidiKeyboard::allNotesOff() {
    for (int note : heldNotes_)
        sendNoteOff(note);
    heldNotes_.clear();
}

bool QwertyMidiKeyboard::keyPressed(const juce::KeyPress& key, juce::Component*) {
    if (!enabled_)
        return false;

    // Always pass through modifier combos
    auto mods = key.getModifiers();
    if (mods.isCommandDown() || mods.isCtrlDown() || mods.isAltDown())
        return false;

    // Octave shift: Z down, X up
    int keyCode = key.getKeyCode();
    if (keyCode == 'Z') {
        setBaseOctave(baseOctave_ - 1);
        return true;
    }
    if (keyCode == 'X') {
        setBaseOctave(baseOctave_ + 1);
        return true;
    }

    // Spacebar passes through for transport
    if (keyCode == juce::KeyPress::spaceKey)
        return false;

    int note = keyToNote(keyCode);
    if (note < 0)
        return false;

    if (heldNotes_.find(note) == heldNotes_.end()) {
        heldNotes_.insert(note);
        sendNoteOn(note);
    }
    return true;
}

bool QwertyMidiKeyboard::keyStateChanged(bool /*isKeyDown*/, juce::Component*) {
    if (!enabled_)
        return false;

    // Check which held notes are no longer pressed and send note-off.
    // JUCE doesn't give us the specific key in keyStateChanged, so we
    // poll all held notes against KeyPress::isCurrentlyDown().
    std::vector<int> released;
    for (int note : heldNotes_) {
        // Reverse-map note to all possible key codes to check if still held.
        // This is a bit brute-force but the set is tiny (max ~10 keys).
        bool stillDown = false;

        // Build reverse map inline — check each key that maps to this note
        for (int kc :
             {'A', 'S', 'D', 'F', 'G', 'H', 'J', 'W', 'E', 'T', 'Y', 'U', 'K', 'L', 'O', 'P'}) {
            if (keyToNote(kc) == note && juce::KeyPress::isKeyCurrentlyDown(kc)) {
                stillDown = true;
                break;
            }
        }
        if (!stillDown)
            released.push_back(note);
    }

    for (int note : released) {
        heldNotes_.erase(note);
        sendNoteOff(note);
    }

    return !released.empty();
}

}  // namespace magda
