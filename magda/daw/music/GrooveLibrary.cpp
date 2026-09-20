#include "GrooveLibrary.hpp"

#include <algorithm>
#include <ranges>
#include <utility>

namespace magda {

GrooveLibrary& GrooveLibrary::getInstance() {
    static GrooveLibrary library;
    return library;
}

void GrooveLibrary::import(std::vector<GrooveTemplateData> grooves) {
    grooves_ = std::move(grooves);
}

juce::StringArray GrooveLibrary::names() const {
    juce::StringArray names;
    for (const auto& groove : grooves_)
        names.add(groove.name);
    return names;
}

const GrooveTemplateData* GrooveLibrary::find(const juce::String& name) const {
    const auto match = std::ranges::find(grooves_, name, &GrooveTemplateData::name);
    return match == grooves_.end() ? nullptr : &*match;
}

bool GrooveLibrary::upsert(const GrooveTemplateData& groove) {
    if (groove.name.isEmpty() || groove.latenessProportions.empty())
        return false;

    // The fork persists the list, so a write it refuses is not in the library either.
    if (writer_ && !writer_(groove))
        return false;

    const auto existing = std::ranges::find(grooves_, groove.name, &GrooveTemplateData::name);
    if (existing != grooves_.end())
        *existing = groove;
    else
        grooves_.push_back(groove);

    return true;
}

}  // namespace magda
