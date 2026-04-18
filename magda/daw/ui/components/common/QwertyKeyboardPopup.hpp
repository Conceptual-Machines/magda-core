#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "../../../audio/QwertyMidiKeyboard.hpp"

namespace magda {

/**
 * Interactive layout hint for the QWERTY MIDI keyboard.
 *
 * Shows a two-octave piano with the QWERTY letters overlaid on each key,
 * reflects the current base octave (labels become "C3"/"C#3"/…), highlights
 * keys that are currently held down, and offers octave-shift buttons mirroring
 * the Z / X shortcuts.
 *
 * Designed to live inside a juce::CallOutBox attached to the transport's
 * QWERTY toggle. The caller instantiates it, sets the size, and hands it to
 * CallOutBox::launchAsynchronously().
 */
class QwertyKeyboardPopup : public juce::Component, private juce::Timer {
  public:
    explicit QwertyKeyboardPopup(QwertyMidiKeyboard& keyboard);
    ~QwertyKeyboardPopup() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

  private:
    void timerCallback() override;
    void shiftOctave(int delta);

    struct KeyCell {
        char letter;       // Displayed QWERTY letter
        int semitone;      // 0..11 inside octave
        int octaveOffset;  // 0 for lower row, 1 for upper row
        bool isBlack;      // Drawn as a black key when true
    };

    static constexpr std::array<KeyCell, 9> kWhiteKeys_ = {{
        {'A', 0, 0, false},
        {'S', 2, 0, false},
        {'D', 4, 0, false},
        {'F', 5, 0, false},
        {'G', 7, 0, false},
        {'H', 9, 0, false},
        {'J', 11, 0, false},
        {'K', 0, 1, false},
        {'L', 2, 1, false},
    }};

    // Black keys are positioned by their anchoring white-key index
    // (right edge of white[i] is the centre of the black key between i and i+1).
    struct BlackKey {
        char letter;
        int semitone;
        int octaveOffset;
        int anchorWhiteIndex;  // black key sits above the right edge of whites_[anchorWhiteIndex]
    };
    static constexpr std::array<BlackKey, 7> kBlackKeys_ = {{
        {'W', 1, 0, 0},   // C# between A(C) and S(D)
        {'E', 3, 0, 1},   // D# between S(D) and D(E)
        {'T', 6, 0, 3},   // F# between F(F) and G(G)
        {'Y', 8, 0, 4},   // G# between G(G) and H(A)
        {'U', 10, 0, 5},  // A# between H(A) and J(B)
        {'O', 1, 1, 7},   // C# (upper) between K(C) and L(D)
        {'P', 3, 1, 8},   // D# (upper) after L(D) — drawn against the next (implicit) white
    }};

    juce::Rectangle<float> whiteKeyBounds(size_t index) const;
    juce::Rectangle<float> blackKeyBounds(const BlackKey& bk) const;

    static juce::String noteName(int semitone, int octave);

    QwertyMidiKeyboard& keyboard_;
    juce::TextButton octaveDownButton_{"Z"};
    juce::TextButton octaveUpButton_{"X"};
    juce::Label octaveLabel_;
    juce::Label hintLabel_;

    // Cached for change-detection so we only repaint on state change.
    int lastBaseOctave_ = -1;
    std::unordered_set<int> lastHeldNotes_;

    juce::Rectangle<int> keyboardArea_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(QwertyKeyboardPopup)
};

}  // namespace magda
