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
 * @brief Launch mode for session clips
 */
enum class LaunchMode {
    Trigger,  // Play from start, stop on re-trigger or stop command
    Toggle    // Click to start, click again to stop
};

/**
 * @brief Launch quantization for session clips
 */
enum class LaunchQuantize {
    None,        // Immediate
    OneBar,      // Snap to next bar
    HalfBar,     // Snap to next half bar
    QuarterBar,  // Snap to next beat
    EighthBar    // Snap to next eighth note
};

/**
 * @brief Get display name for launch mode
 */
inline const char* getLaunchModeName(LaunchMode mode) {
    switch (mode) {
        case LaunchMode::Trigger:
            return "Trigger";
        case LaunchMode::Toggle:
            return "Toggle";
    }
    return "Unknown";
}

/**
 * @brief Get display name for launch quantize
 */
inline const char* getLaunchQuantizeName(LaunchQuantize q) {
    switch (q) {
        case LaunchQuantize::None:
            return "None";
        case LaunchQuantize::OneBar:
            return "1 Bar";
        case LaunchQuantize::HalfBar:
            return "1/2";
        case LaunchQuantize::QuarterBar:
            return "1/4";
        case LaunchQuantize::EighthBar:
            return "1/8";
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
