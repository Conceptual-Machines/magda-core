// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

namespace magda {

/**
 * @brief View modes for the DAW
 *
 * Each mode optimizes the UI layout and audio engine for different workflows:
 * - Live: Real-time performance with lowest latency
 * - Arrange: Composing and editing with balanced settings
 * - Mix: Mixing and processing with higher buffer for plugins
 * - Master: Mastering with maximum quality settings
 */
enum class ViewMode { Live, Arrange, Mix, Master };

/**
 * @brief Audio engine optimization profile for each view mode
 *
 * These profiles allow the audio engine to be tuned for different use cases.
 */
struct AudioEngineProfile {
    int bufferSize;       // Buffer size in samples
    int latencyMs;        // Target latency in milliseconds
    bool lowLatencyMode;  // Prioritize responsiveness over quality
    bool multiThreaded;   // Use multiple processing threads

    static AudioEngineProfile getLiveProfile() {
        return {128, 3, true, false};
    }

    static AudioEngineProfile getArrangeProfile() {
        return {512, 12, false, true};
    }

    static AudioEngineProfile getMixProfile() {
        return {1024, 23, false, true};
    }

    static AudioEngineProfile getMasterProfile() {
        return {2048, 46, false, true};
    }

    static AudioEngineProfile getProfileForMode(ViewMode mode) {
        switch (mode) {
            case ViewMode::Live:
                return getLiveProfile();
            case ViewMode::Arrange:
                return getArrangeProfile();
            case ViewMode::Mix:
                return getMixProfile();
            case ViewMode::Master:
                return getMasterProfile();
        }
        return getArrangeProfile();  // Default
    }
};

/**
 * @brief Get a display name for a view mode
 */
inline const char* getViewModeName(ViewMode mode) {
    switch (mode) {
        case ViewMode::Live:
            return "Live";
        case ViewMode::Arrange:
            return "Arrange";
        case ViewMode::Mix:
            return "Mix";
        case ViewMode::Master:
            return "Master";
    }
    return "Unknown";
}

}  // namespace magda
