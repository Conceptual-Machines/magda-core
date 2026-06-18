#pragma once

#include <juce_core/juce_core.h>
#include <juce_graphics/juce_graphics.h>

#include <vector>

#include "../../core/AutomationInfo.hpp"
#include "../../core/ClipInfo.hpp"
#include "../../core/TrackInfo.hpp"
#include "../ProjectInfo.hpp"

namespace magda {

/**
 * Format-neutral project data used by import/export adapters.
 *
 * MAGDA's native .mgd remains the lossless project format. This document is the
 * interchange boundary: native JSON and DAWproject can both map through it while
 * preserving a place for native-only state.
 */
struct ProjectDocument {
    ProjectInfo info;
    std::vector<TrackInfo> tracks;
    std::vector<ClipInfo> clips;
    std::vector<AutomationLaneInfo> automationLanes;
    std::vector<AutomationClipInfo> automationClips;
    juce::var nativeExtensions;
};

}  // namespace magda
