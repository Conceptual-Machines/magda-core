#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <optional>

#include "core/ClipInfo.hpp"

namespace magda::daw::ui {

/**
 * @brief How to render a clip's audio waveform into a rectangle.
 *
 * Shared by the arrangement ClipComponent and the automation clip editor's
 * track ghost so the two can never drift. Handles the warp, loop-tiling,
 * and plain single-pass paths identically to the arrangement.
 */
struct ClipWaveformSpec {
    double tempo = 120.0;
    juce::Colour colour = juce::Colours::black;
    bool thick = false;  // selected-clip stroke weight

    // Resize-drag preview state (ClipComponent mid-ResizeLeft). When set,
    // these replace the clip's committed offset / loop phase.
    std::optional<double> previewOffset;
    std::optional<double> previewLoopStart;
};

/**
 * @brief Draw a clip's audio waveform (warp-aware, loop-tiled) into
 *        waveformArea, whose width represents clipDisplayLength seconds.
 */
void paintClipWaveform(juce::Graphics& g, const ClipInfo& clip, ClipId clipId,
                       juce::Rectangle<int> waveformArea, double clipDisplayLength,
                       const ClipWaveformSpec& spec);

}  // namespace magda::daw::ui
