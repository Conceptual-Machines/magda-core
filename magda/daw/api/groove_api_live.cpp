#include "groove_api_live.hpp"

#include "../music/GrooveLibrary.hpp"
#include "../project/ProjectManager.hpp"

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

std::optional<sdk::sequencer::TimeBend> GrooveApiLive::timeBend(const juce::String& name,
                                                                float strength) const {
    return GrooveLibrary::getInstance().bend(
        name, ProjectManager::getInstance().getCurrentProjectInfo().beatsPerBar(), strength);
}

}  // namespace magda
