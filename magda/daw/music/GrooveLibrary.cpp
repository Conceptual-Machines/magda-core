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
    notifyChanged();
}

void GrooveLibrary::setStore(Reader reader, Writer writer) {
    reader_ = std::move(reader);
    writer_ = std::move(writer);
    if (reader_)
        import(reader_());
}

void GrooveLibrary::notifyChanged() const {
    if (onChanged_)
        onChanged_();
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

    if (writer_) {
        // The store persists the list, so a write it refuses is not in the library either.
        if (!writer_(groove))
            return false;

        // What it kept, not what it was asked for: a renamed or reflagged groove would
        // otherwise play one way here and another under the fork.
        if (reader_) {
            grooves_ = reader_();
            notifyChanged();
            return true;
        }
    }

    const auto existing = std::ranges::find(grooves_, groove.name, &GrooveTemplateData::name);
    if (existing != grooves_.end())
        *existing = groove;
    else
        grooves_.push_back(groove);

    notifyChanged();
    return true;
}

}  // namespace magda
