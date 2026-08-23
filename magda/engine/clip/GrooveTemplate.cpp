#include "clip/GrooveTemplate.hpp"

#include <algorithm>
#include <cmath>

namespace magda::engine {

namespace {

/// Index of @p value in a pattern of @p length, for negative values too.
/// C++'s % answers negative for a negative left operand, and the fork's
/// juce::Array then answers zero for the negative index it is handed. Timeline
/// beats are never negative in MAGDA, so the two agree everywhere they can be
/// reached; folding properly is what makes that a property of this function
/// rather than of the caller.
int patternIndex(double value, int length) {
    const auto index = static_cast<long long>(std::llround(value)) % length;
    return static_cast<int>(index < 0 ? index + length : index);
}

}  // namespace

GrooveTemplate GrooveTemplate::compile(const std::vector<float>& latenesses, int numNotes,
                                       int notesPerBeat, bool parameterized, float strength) {
    GrooveTemplate groove;

    const auto notes = std::clamp(numNotes, kMinNotes, kMaxNotes);
    groove.notesPerBeat_ = std::clamp(notesPerBeat, kMinNotesPerBeat, kMaxNotesPerBeat);

    // Only a parameterized template answers to strength; the rest carry their
    // shape at full weight however the clip is set, which is the fork's
    // getLatenessProportion.
    const auto weight = parameterized ? strength : 1.0f;

    groove.latenesses_.assign(static_cast<std::size_t>(notes), 0.0f);
    auto largest = 0.0f;

    for (std::size_t i = 0; i < groove.latenesses_.size(); ++i) {
        const auto raw = i < latenesses.size() ? latenesses[i] : 0.0f;
        const auto value = std::clamp(raw, -1.0f, 1.0f) * weight;
        groove.latenesses_[i] = value;
        largest = std::max(largest, std::abs(value));
    }

    // Nothing to do is nothing carried: an all-zero table is the identity, and
    // saying so here is what makes every caller downstream groove-agnostic
    // rather than groove-aware-and-checking.
    if (largest <= 0.0f) {
        groove.latenesses_.clear();
        return groove;
    }

    // The displacement works out to half the lerp between two neighbouring
    // latenesses, over the grid: see groovyBeat below. Its bound is therefore
    // exact rather than sampled.
    groove.maxDisplacement_ = 0.5 * static_cast<double>(largest) / groove.notesPerBeat_;
    return groove;
}

double GrooveTemplate::groovyBeat(double beat) const {
    if (latenesses_.empty())
        return beat;

    const auto notes = static_cast<int>(latenesses_.size());
    const auto grid = static_cast<double>(notesPerBeat_);

    const auto step = std::floor(beat * grid);
    const auto offset = beat * grid - step;

    const auto index = patternIndex(step, notes);
    const auto lateness = static_cast<double>(latenesses_[static_cast<std::size_t>(index)]);
    const auto next =
        static_cast<double>(latenesses_[static_cast<std::size_t>((index + 1) % notes)]);

    // The fork's formula, unchanged: the step moves by half its own lateness and
    // the one after it by half of the next, and a position between them rides
    // the line between the two.
    const auto start = step + 0.5 * lateness;
    const auto span = 1.0 + 0.5 * (next - lateness);

    return (start + offset * span) / grid;
}

void GrooveTemplateSet::add(Entry entry) {
    entries_.push_back(std::move(entry));
}

bool GrooveTemplateSet::contains(const std::string& name) const {
    return std::any_of(entries_.begin(), entries_.end(),
                       [&](const Entry& entry) { return entry.name == name; });
}

GrooveTemplate GrooveTemplateSet::compile(const std::string& name, float strength) const {
    if (name.empty())
        return {};

    const auto found = std::find_if(entries_.begin(), entries_.end(),
                                    [&](const Entry& entry) { return entry.name == name; });

    if (found == entries_.end())
        return {};

    return GrooveTemplate::compile(found->latenesses, found->numNotes, found->notesPerBeat,
                                   found->parameterized, strength);
}

GrooveTemplateSet GrooveTemplateSet::parse(const juce::XmlElement& document) {
    GrooveTemplateSet set;

    for (const auto* node : document.getChildWithTagNameIterator("GROOVETEMPLATE")) {
        Entry entry;
        entry.name = node->getStringAttribute("name").toStdString();
        entry.numNotes = node->getIntAttribute("numberOfNotes", 16);
        entry.notesPerBeat = node->getIntAttribute("notesPerBeat", 2);
        entry.parameterized = node->getBoolAttribute("parameterized", false);

        for (const auto* shift : node->getChildWithTagNameIterator("SHIFT"))
            entry.latenesses.push_back(static_cast<float>(shift->getDoubleAttribute("delta", 0.0)));

        set.add(std::move(entry));
    }

    return set;
}

}  // namespace magda::engine
