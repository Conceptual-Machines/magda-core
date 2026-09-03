#pragma once

#include <algorithm>
#include <array>
#include <cstddef>

namespace magda::daw::audio::sequencer {

/**
 * @brief The step patterns the sequencers play, as plain data.
 *
 * Nothing here knows about devices, ValueTrees or an audio engine (#2313): a
 * pattern is a value the model authors, the sequencers read, and the clip
 * export walks. Serialization to the persisted `STEP` / `NOTE` trees is an
 * adapter that lives with the model schema, not with these types.
 */

/// Steps a pattern can hold. Frozen: saved patterns index into it.
inline constexpr int kMaxSteps = 32;

/// Notes one polyphonic step can hold.
inline constexpr int kMaxNotesPerStep = 8;

/// One step of the 303-style monophonic sequencer.
struct MonoStep {
    int noteNumber = 60;  ///< MIDI note (C4 default)
    int octaveShift = 0;  ///< -2 to +2, added as octaves to noteNumber
    bool gate = true;     ///< false = rest
    bool accent = false;  ///< play at the accent velocity
    bool glide = false;   ///< hold the full step so the next note slides in
    bool tie = false;     ///< extend the previous note instead of retriggering

    bool operator==(const MonoStep&) const = default;
};

/// A monophonic pattern: its steps and how many of them play.
struct MonoPattern {
    std::array<MonoStep, kMaxSteps> steps{};
    int length = 16;

    bool operator==(const MonoPattern&) const = default;

    /// The step at @p index, or a default rest-free step when out of range.
    const MonoStep& step(int index) const {
        static const MonoStep fallback{};
        if (index < 0 || index >= kMaxSteps)
            return fallback;
        return steps[static_cast<size_t>(index)];
    }

    /// Steps that actually play, clamped into the pattern.
    int playingLength() const {
        return std::clamp(length, 1, kMaxSteps);
    }
};

/// One note of a polyphonic step.
struct PolyNote {
    int noteNumber = 60;  ///< MIDI note 0-127
    int velocity = 0;     ///< 0 = use the step velocity, 1-127 = per-note override

    bool operator==(const PolyNote&) const = default;
};

/// One step of the polyphonic sequencer: a chord, or a rest.
struct PolyStep {
    bool gate = true;          ///< false = rest
    bool tie = false;          ///< extend the previous step's notes
    float probability = 1.0f;  ///< 0-1, rolled once each time the step fires
    int velocity = 100;        ///< step-level velocity (1-127)
    int noteCount = 0;
    std::array<PolyNote, kMaxNotesPerStep> notes{};

    bool operator==(const PolyStep&) const = default;
};

/// A polyphonic pattern: its steps and how many of them play.
struct PolyPattern {
    std::array<PolyStep, kMaxSteps> steps{};
    int length = 16;

    bool operator==(const PolyPattern&) const = default;

    const PolyStep& step(int index) const {
        static const PolyStep fallback{};
        if (index < 0 || index >= kMaxSteps)
            return fallback;
        return steps[static_cast<size_t>(index)];
    }

    int playingLength() const {
        return std::clamp(length, 1, kMaxSteps);
    }
};

}  // namespace magda::daw::audio::sequencer
