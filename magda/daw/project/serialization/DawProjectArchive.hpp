#pragma once

#include <juce_core/juce_core.h>

#include "ProjectDocument.hpp"

namespace magda {

class DawProjectArchive {
  public:
    static juce::String toMetadataXml(const ProjectDocument& document);

    static bool writeToFile(const juce::File& file, const ProjectDocument& document,
                            juce::String& error);
    static bool readFromFile(const juce::File& file, ProjectDocument& outDocument,
                             juce::String& error);
};

}  // namespace magda
