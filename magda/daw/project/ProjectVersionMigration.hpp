#pragma once

#include <juce_core/juce_core.h>

namespace magda::project_version {

inline int majorVersion(juce::String version) {
    version = version.trim();
    if (version.startsWithIgnoreCase("v"))
        version = version.substring(1);
    const auto major = version.upToFirstOccurrenceOf(".", false, false);
    if (major.isEmpty() || !major.containsOnly("0123456789"))
        return -1;
    return major.getIntValue();
}

inline bool needsV1Copy(const juce::String& savedVersion, const juce::String& runningVersion) {
    return majorVersion(savedVersion) == 0 && majorVersion(runningVersion) >= 1;
}

inline bool isSeparateProject(const juce::File& source, const juce::File& destination) {
    if (destination == source)
        return false;
    const auto parent = source.getParentDirectory();
    if (parent.getFileName() == source.getFileNameWithoutExtension())
        return destination.getParentDirectory() != parent && !destination.isAChildOf(parent);
    return !destination.isAChildOf(
        parent.getChildFile(source.getFileNameWithoutExtension() + "_Media"));
}

/// Unwrapped path: Save As creates the new project's folder.
inline juce::File newProjectFileFor(const juce::File& source, const juce::String& suffix) {
    if (source == juce::File{})
        return {};
    const auto parent = source.getParentDirectory();
    const auto projects = parent.getFileName() == source.getFileNameWithoutExtension()
                              ? parent.getParentDirectory()
                              : parent;
    if (!projects.isDirectory())
        return {};
    const auto folder = projects.getChildFile(source.getFileNameWithoutExtension() + suffix)
                            .getNonexistentSibling();
    return projects.getChildFile(folder.getFileName() + source.getFileExtension());
}

}  // namespace magda::project_version
