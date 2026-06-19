#pragma once

#include <juce_core/juce_core.h>

#include "ProjectDocument.hpp"

namespace magda {

class DawProjectArchive {
  public:
    static juce::String toMetadataXml(const ProjectDocument& document);

    static bool writeToFile(const juce::File& file, const ProjectDocument& document,
                            juce::String& error);

    // Reads the archive and repoints audio clips at extracted, on-disk copies of
    // any embedded samples. audioExtractionDir is where those copies land; pass
    // the project's media directory so the audio persists with the project. When
    // empty, a session-temp directory is used (resolves for the session only).
    static bool readFromFile(const juce::File& file, ProjectDocument& outDocument,
                             juce::String& error, const juce::File& audioExtractionDir = {});
};

}  // namespace magda
