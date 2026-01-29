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

    // Loop settings
    bool loopEnabled = false;
    double loopStart = 0.0;
    double loopEnd = 0.0;

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
