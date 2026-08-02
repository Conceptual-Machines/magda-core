#pragma once

#include <juce_core/juce_core.h>

#include <vector>

namespace magda {

struct ExcludedPlugin {
    juce::String path;
    juce::String reason;     // "crash", "timeout", "scan_failed", "unknown"
    juce::String timestamp;  // ISO format from juce::Time::getCurrentTime()
};

/// Load exclusion entries from a file.
/// Supports tab-delimited `path\treason\ttimestamp`, legacy pipe-delimited, and plain-path formats.
std::vector<ExcludedPlugin> loadExclusionList(const juce::File& file);

}  // namespace magda
