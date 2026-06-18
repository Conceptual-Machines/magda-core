#pragma once

#include <juce_core/juce_core.h>

#include "ProjectDocument.hpp"

namespace magda {

class DawProjectXmlAdapter {
  public:
    static juce::String toProjectXml(const ProjectDocument& document);
    static bool fromProjectXml(const juce::String& xml, ProjectDocument& outDocument,
                               juce::String& error);
};

}  // namespace magda
