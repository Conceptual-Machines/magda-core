#pragma once

#include <juce_core/juce_core.h>

namespace magda {

/**
 * @brief Project metadata and settings
 *
 * Contains all project-level information including tempo, time signature,
 * loop settings, and file path.
 */
struct ProjectInfo {
    juce::String name;
    juce::String filePath;  // .mgd file path

    // Playback settings
    double tempo = 120.0;
    int timeSignatureNumerator = 4;
    int timeSignatureDenominator = 4;
    double projectLength = 240.0;  // seconds
    double sampleRate = 44100.0;

    // Key signature
    int keyRoot = -1;    // 0=C, 1=C#, ..., 11=B; -1=none
    int keyQuality = 0;  // 0=major, 1=minor

    // Loop settings (beats are authoritative, seconds derived from tempo)
    bool loopEnabled = false;
    double loopStartBeats = 0.0;
    double loopEndBeats = 0.0;

    // Version tracking
    juce::String version = "1.0.0";  // Magda version
    juce::Time lastModified;

    // Default constructor
    ProjectInfo() : lastModified(juce::Time::getCurrentTime()) {}

    // Helper to update modification time
    void touch() {
        lastModified = juce::Time::getCurrentTime();
    }
};

}  // namespace magda
