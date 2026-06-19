#pragma once

#include <juce_core/juce_core.h>

namespace magda {

class DawProjectValidator {
  public:
    static bool validateProjectXml(const juce::String& xml, juce::String& error);
    static bool validateMetadataXml(const juce::String& xml, juce::String& error);
    static bool validateXmlAgainstSchema(const juce::String& xml, const juce::File& schemaFile,
                                         juce::String& error);
};

}  // namespace magda
