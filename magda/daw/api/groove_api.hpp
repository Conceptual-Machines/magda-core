#pragma once

#include <juce_core/juce_core.h>

#include <vector>

namespace magda {

/** Groove-template operations exposed without leaking the audio engine. */
class GrooveApi {
  public:
    virtual ~GrooveApi() = default;

    virtual bool upsertTemplate(const juce::String& name, int notesPerBeat, bool parameterized,
                                const std::vector<float>& latenessProportions) = 0;
    virtual juce::StringArray getTemplateNames() const = 0;
};

}  // namespace magda
