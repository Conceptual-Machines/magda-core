#pragma once

#include <juce_core/juce_core.h>

#include "version.hpp"

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

    // Zoom/scroll state
    double horizontalZoom = -1.0;  // Pixels per beat (-1 = use default)
    double verticalZoom = 1.0;     // Track height multiplier
    int scrollX = 0;               // Horizontal scroll position
    int scrollY = 0;               // Vertical scroll position

    // Active view (0=Live/Session, 1=Arrange, 2=Mix, 3=Master)
    int activeView = 1;  // Default to Arrange

    // Version tracking
    juce::String version = MAGDA_VERSION;  // Magda version
    juce::Time lastModified;

    // Default constructor
    ProjectInfo() : lastModified(juce::Time::getCurrentTime()) {}

    // Helper to update modification time
    void touch() {
        lastModified = juce::Time::getCurrentTime();
    }
};

}  // namespace magda
