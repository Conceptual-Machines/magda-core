#pragma once

#include <juce_events/juce_events.h>

namespace magda {

/** @brief A song key as the MIDI editor uses it: a root pitch class and major or minor. */
struct KeyScale {
    int root = -1;    // 0 = C ... 11 = B; -1 = no key
    int quality = 0;  // 0 = major, 1 = minor

    bool valid() const {
        return root >= 0;
    }
    bool isRoot(int noteNumber) const {
        return valid() && ((noteNumber % 12) + 12) % 12 == root;
    }
    bool contains(int noteNumber) const {
        if (!valid())
            return true;
        static constexpr int kMajor[] = {1, 0, 1, 0, 1, 1, 0, 1, 0, 1, 0, 1};
        static constexpr int kMinor[] = {1, 0, 1, 1, 0, 1, 0, 1, 1, 0, 1, 0};
        const int degree = (((noteNumber - root) % 12) + 12) % 12;
        return (quality == 1 ? kMinor : kMajor)[degree] != 0;
    }
};

/**
 * @brief The key chip's lit state: scale highlighting and fold-to-key, one switch.
 *
 * The key itself is the project's song key; this only says whether the editor shows it.
 */
class MidiEditorKeyState : public juce::ChangeBroadcaster {
  public:
    static MidiEditorKeyState& getInstance();

    bool isLit() const {
        return lit_;
    }
    void setLit(bool lit);

    /** The project key, or no key when the chip is unlit. */
    KeyScale activeScale() const;
    /** The project key regardless of the chip. */
    static KeyScale songKey();

  private:
    bool lit_ = true;
};

}  // namespace magda
