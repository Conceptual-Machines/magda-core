#include "groove_api_live.hpp"

#include "../core/TrackManager.hpp"
#include "../engine/AudioEngine.hpp"

namespace magda {

bool GrooveApiLive::upsertTemplate(const juce::String& name, int notesPerBeat, bool parameterized,
                                   const std::vector<float>& latenessProportions) {
    auto* engine = TrackManager::getInstance().getAudioEngine();
    if (engine == nullptr)
        return false;
    return engine->upsertGrooveTemplate({.name = name,
                                         .notesPerBeat = notesPerBeat,
                                         .parameterized = parameterized,
                                         .latenessProportions = latenessProportions});
}

juce::StringArray GrooveApiLive::getTemplateNames() const {
    if (auto* engine = TrackManager::getInstance().getAudioEngine())
        return engine->getGrooveTemplateNames();
    return {};
}

}  // namespace magda
