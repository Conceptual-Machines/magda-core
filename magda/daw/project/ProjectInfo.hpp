#pragma once

#include <juce_core/juce_core.h>

#include <cstdint>
#include <vector>

#include "../core/TempoUtils.hpp"
#include "version.hpp"

namespace magda {

struct ProjectTimelineMarker {
    int id = 0;
    double positionBeats = 0.0;
    juce::String name;
    std::uint32_t colourArgb = 0xFFFFC857;
};

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
    double tempo = DEFAULT_BPM;
    int timeSignatureNumerator = DEFAULT_TIME_SIGNATURE_NUMERATOR;
    int timeSignatureDenominator = DEFAULT_TIME_SIGNATURE_DENOMINATOR;
    double projectLength = 240.0;  // seconds (legacy; derived from timelineLengthBars)
    double sampleRate = 44100.0;   // project working/render sample rate

    // Total timeline length (per-project; seeded from Config default for new projects)
    int timelineLengthBars = 256;

    // Render / bounce settings (per-project)
    int renderBitDepth = 24;  // 16, 24, 32
    int bounceBitDepth = 32;  // 16, 24, 32 (default 32-bit float for internal bounces)

    // Key signature
    int keyRoot = -1;    // 0=C, 1=C#, ..., 11=B; -1=none
    int keyQuality = 0;  // 0=major, 1=minor

    // Loop settings (beats are authoritative, seconds derived from tempo)
    bool loopEnabled = false;
    double loopStartBeats = 0.0;
    double loopEndBeats = 0.0;

    // Named timeline markers (positions are stored in beats)
    std::vector<ProjectTimelineMarker> markers;

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

    // Parameter aliases (UserProject layer, opaque JSON blob managed by AliasRegistry)
    juce::var paramAliases;

    // Project-scope bindings (opaque JSON blob managed by BindingRegistry)
    juce::var projectBindings;

    // Default constructor
    ProjectInfo() : lastModified(juce::Time::getCurrentTime()) {}

    // Helper to update modification time
    void touch() {
        lastModified = juce::Time::getCurrentTime();
    }
};

}  // namespace magda
