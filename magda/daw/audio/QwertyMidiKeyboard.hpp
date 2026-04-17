#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <unordered_set>

namespace magda {

class AudioBridge;

/**
 * @brief Maps QWERTY keyboard keys to MIDI notes.
 *
 * When enabled, intercepts raw key events before they reach the command
 * manager or any other handler. Modifier combos (Cmd/Ctrl/Alt) and
 * spacebar always pass through.
 *
 * Key layout:
 *   Black keys: W E   T Y U     O P
 *   White keys: A S D F G H J   K L
 *   Octave:     Z (down) / X (up)
 */
class QwertyMidiKeyboard : public juce::KeyListener {
  public:
    explicit QwertyMidiKeyboard(AudioBridge& bridge);
    ~QwertyMidiKeyboard() override;

    void setEnabled(bool enabled);
    bool isEnabled() const {
        return enabled_;
    }

    int getBaseOctave() const {
        return baseOctave_;
    }
    void setBaseOctave(int octave) {
        baseOctave_ = juce::jlimit(0, 8, octave);
    }

    void setVelocity(int vel) {
        velocity_ = juce::jlimit(1, 127, vel);
    }
    int getVelocity() const {
        return velocity_;
    }

    // juce::KeyListener
    bool keyPressed(const juce::KeyPress& key, juce::Component* originatingComponent) override;
    bool keyStateChanged(bool isKeyDown, juce::Component* originatingComponent) override;

  private:
    int keyToNote(int keyCode) const;
    void sendNoteOn(int note);
    void sendNoteOff(int note);
    void allNotesOff();

    AudioBridge& bridge_;
    bool enabled_ = false;
    int baseOctave_ = 3;
    int velocity_ = 100;
    std::unordered_set<int> heldNotes_;
};

}  // namespace magda
