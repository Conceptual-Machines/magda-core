#include "groove_api_live.hpp"

#include "../music/GrooveLibrary.hpp"

namespace magda {

bool GrooveApiLive::upsertTemplate(const juce::String& name, int notesPerBeat, bool parameterized,
                                   const std::vector<float>& latenessProportions) {
    return GrooveLibrary::getInstance().upsert({.name = name,
                                                .notesPerBeat = notesPerBeat,
                                                .parameterized = parameterized,
                                                .latenessProportions = latenessProportions});
}

juce::StringArray GrooveApiLive::getTemplateNames() const {
    return GrooveLibrary::getInstance().names();
}

}  // namespace magda
