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
/// Supports both the new `path|reason|timestamp` format and the old plain-path format
/// (which gets reason="unknown").
std::vector<ExcludedPlugin> loadExclusionList(const juce::File& file);

/// Save exclusion entries to a file in `path|reason|timestamp` format (one per line).
void saveExclusionList(const juce::File& file, const std::vector<ExcludedPlugin>& entries);

}  // namespace magda
