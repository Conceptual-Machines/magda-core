#include "PluginExclusions.hpp"

namespace magda {

std::vector<ExcludedPlugin> loadExclusionList(const juce::File& file) {
    std::vector<ExcludedPlugin> entries;

    // Migration fallback: if the new file doesn't exist, try the old filename
    juce::File fileToLoad = file;
    if (!fileToLoad.existsAsFile()) {
        auto oldFile = file.getParentDirectory().getChildFile("plugin_blacklist.txt");
        if (oldFile.existsAsFile())
            fileToLoad = oldFile;
        else
            return entries;
    }

    juce::StringArray lines;
    fileToLoad.readLines(lines);

    for (const auto& line : lines) {
        auto trimmed = line.trim();
        if (trimmed.isEmpty())
            continue;

        // Current format: path\treason\ttimestamp (tab-delimited)
        // Also supports legacy pipe-delimited format and plain paths
        juce::String delimiter;
        if (trimmed.contains("\t"))
            delimiter = "\t";
        else if (trimmed.contains("|"))
            delimiter = "|";

        if (delimiter.isNotEmpty()) {
            juce::StringArray parts;
            parts.addTokens(trimmed, delimiter, "");

            ExcludedPlugin entry;
            entry.path = parts[0].trim();
            entry.reason = parts.size() > 1 ? parts[1].trim() : "unknown";
            entry.timestamp = parts.size() > 2 ? parts[2].trim() : "";
            entries.push_back(entry);
        } else {
            // Old format: plain path — backward compatibility
            entries.push_back({trimmed, "unknown", ""});
        }
    }

    return entries;
}

void saveExclusionList(const juce::File& file, const std::vector<ExcludedPlugin>& entries) {
    (void)file.getParentDirectory().createDirectory();

    juce::String content;
    for (const auto& entry : entries) {
        content += entry.path + "\t" + entry.reason + "\t" + entry.timestamp + "\n";
    }

    (void)file.replaceWithText(content);
}

}  // namespace magda
