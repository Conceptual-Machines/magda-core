#pragma once

namespace magda {

/**
 * @brief Unique identifier for clips
 */
using ClipId = int;
constexpr ClipId INVALID_CLIP_ID = -1;

/**
 * @brief Clip types
 */
enum class ClipType {
    Audio,  // Audio clip from file
    MIDI    // MIDI note data
};

/**
 * @brief Which view the clip belongs to
 *
 * Arrangement and session are completely independent with separate playbacks.
 * A clip exists in EITHER arrangement OR session, never both.
 */
enum class ClipView {
    Arrangement,  // Timeline-based arrangement view (absolute time positioning)
    Session       // Scene-based session view (relative/beat-based positioning)
};

/**
 * @brief Get display name for clip type
 */
inline const char* getClipTypeName(ClipType type) {
    switch (type) {
        case ClipType::Audio:
            return "Audio";
        case ClipType::MIDI:
            return "MIDI";
    }
    return "Unknown";
}

/**
 * @brief Check if clip type can be stretched without pitch change
 */
inline bool supportsTimeStretch(ClipType type) {
    return type == ClipType::Audio;
}

/**
 * @brief Check if clip type contains note data
 */
inline bool hasNoteData(ClipType type) {
    return type == ClipType::MIDI;
}

}  // namespace magda
