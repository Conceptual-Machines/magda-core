#pragma once

namespace magda {

/**
 * @brief Track types
 *
 * All regular tracks are Audio (hybrid — can hold audio clips, MIDI clips,
 * instruments, and effects). The remaining types have genuine behavioral
 * differences (routing, hierarchy, output).
 */
enum class TrackType {
    Audio = 0,    // Regular hybrid track
    Group = 3,    // Contains child tracks, routing hub
    Aux = 4,      // Receives from sends
    Master = 5,   // Final output
    MultiOut = 6  // Output track for multi-out instrument pair
};

/**
 * @brief Get display name for track type
 */
inline const char* getTrackTypeName(TrackType type) {
    switch (type) {
        case TrackType::Audio:
            return "Audio";
        case TrackType::Group:
            return "Group";
        case TrackType::Aux:
            return "Aux";
        case TrackType::Master:
            return "Master";
        case TrackType::MultiOut:
            return "MultiOut";
    }
    return "Unknown";
}

/**
 * @brief Deserialize a track type from an integer, mapping legacy values.
 *
 * Old projects stored Instrument=1 and MIDI=2; both map to Audio now.
 */
inline TrackType trackTypeFromInt(int v) {
    switch (v) {
        case 0:
        case 1:
        case 2:
            return TrackType::Audio;
        case 3:
            return TrackType::Group;
        case 4:
            return TrackType::Aux;
        case 5:
            return TrackType::Master;
        case 6:
            return TrackType::MultiOut;
        default:
            return TrackType::Audio;
    }
}

/**
 * @brief Check if track type can have children
 */
inline bool canHaveChildren(TrackType type) {
    return type == TrackType::Group;
}

}  // namespace magda
