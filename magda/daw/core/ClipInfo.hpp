#pragma once

#include <juce_core/juce_core.h>
#include <juce_graphics/juce_graphics.h>

#include <vector>

#include "ClipTypes.hpp"
#include "TrackTypes.hpp"
#include "TypeIds.hpp"

namespace magda {

/**
 * @brief MIDI note data for MIDI clips
 */
struct MidiNote {
    int noteNumber = 60;       // MIDI note number (0-127)
    int velocity = 100;        // Note velocity (0-127)
    double startBeat = 0.0;    // Start position in beats within clip
    double lengthBeats = 1.0;  // Duration in beats
};

/**
 * @brief Clip data structure containing all clip properties
 */
struct ClipInfo {
    ClipId id = INVALID_CLIP_ID;
    TrackId trackId = INVALID_TRACK_ID;
    juce::String name;
    juce::Colour colour;
    ClipType type = ClipType::MIDI;
    ClipView view = ClipView::Arrangement;  // Which view this clip belongs to

    // Timeline position
    double startTime = 0.0;  // Position on timeline (seconds) - only for Arrangement view
    double length = 4.0;     // Duration (seconds)

    // Internal looping
    bool internalLoopEnabled = false;
    double internalLoopOffset = 0.0;  // Loop start offset in beats
    double internalLoopLength = 4.0;  // In beats

    // Audio-specific properties (flat model: one clip = one file reference)
    juce::String audioFilePath;       // Path to audio file
    double audioOffset = 0.0;         // File start offset for trimming (seconds)
    double audioStretchFactor = 1.0;  // Time stretch factor (1.0 = original speed)

    // MIDI-specific properties
    std::vector<MidiNote> midiNotes;
    double midiOffset = 0.0;  // Start offset in beats (for non-destructive trim)

    // Session view properties
    int sceneIndex = -1;     // -1 = not in session view (arrangement only)
    bool isPlaying = false;  // Currently playing in session
    bool isQueued = false;   // Queued to start

    // Session launch properties
    LaunchMode launchMode = LaunchMode::Trigger;
    LaunchQuantize launchQuantize = LaunchQuantize::None;

    // Helpers
    double getEndTime() const {
        return startTime + length;
    }

    bool containsTime(double time) const {
        return time >= startTime && time < getEndTime();
    }

    bool overlaps(double start, double end) const {
        return startTime < end && getEndTime() > start;
    }

    bool overlaps(const ClipInfo& other) const {
        return overlaps(other.startTime, other.getEndTime());
    }

    // Default clip colors (different palette from tracks)
    static inline const std::array<juce::uint32, 8> defaultColors = {
        0xFF6688CC,  // Light Blue
        0xFF66CCAA,  // Teal
        0xFFAACC66,  // Lime
        0xFFCCCC66,  // Yellow
        0xFFCCAA66,  // Orange
        0xFFCC6666,  // Red
        0xFFCC66CC,  // Pink
        0xFF8866CC,  // Purple
    };

    static juce::Colour getDefaultColor(int index) {
        return juce::Colour(defaultColors[index % defaultColors.size()]);
    }
};

}  // namespace magda
