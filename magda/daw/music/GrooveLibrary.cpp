#include "GrooveLibrary.hpp"

#include <algorithm>
#include <cmath>
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

void GrooveLibrary::setStore(Reader reader, Writer writer) {
    reader_ = std::move(reader);
    writer_ = std::move(writer);
    if (reader_)
        import(reader_());
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
        // otherwise differ between the library and the store.
        if (reader_) {
            grooves_ = reader_();
            return true;
        }
    }

    const auto existing = std::ranges::find(grooves_, groove.name, &GrooveTemplateData::name);
    if (existing != grooves_.end())
        *existing = groove;
    else
        grooves_.push_back(groove);
    return true;
}

std::optional<sdk::sequencer::TimeBend> GrooveLibrary::bend(const juce::String& name,
                                                            double beatsPerBar,
                                                            float strength) const {
    const auto* groove = find(name);
    if (groove == nullptr)
        return std::nullopt;
    return grooveTimeBend(*groove, beatsPerBar, groove->parameterized ? strength : 1.0f);
}

std::optional<sdk::sequencer::TimeBend> grooveTimeBend(const GrooveTemplateData& groove,
                                                       double beatsPerBar, float amount) {
    using sdk::sequencer::kMaxTimeBendCycles;
    using sdk::sequencer::kMaxTimeBendPoints;

    const auto steps = static_cast<int>(groove.latenessProportions.size());
    if (steps <= 0 || groove.notesPerBeat <= 0 || beatsPerBar <= 0.0)
        return std::nullopt;
    const double period = static_cast<double>(steps) / groove.notesPerBeat;

    // The fewest repeats of the groove that make a cycle the span divides into.
    for (int repeats = 1; steps * repeats + 2 <= kMaxTimeBendPoints; ++repeats) {
        const double cycle = period * repeats;
        for (const int bars : {1, 2, 4}) {
            const double cycles = bars * beatsPerBar / cycle;
            const auto whole = std::lround(cycles);
            if (whole < 1 || whole > kMaxTimeBendCycles || std::abs(cycles - whole) > 1.0e-9)
                continue;

            const int n = steps * repeats;
            const auto lateness = [&](int k) {
                const auto l = std::clamp(
                    groove.latenessProportions[static_cast<size_t>(k % steps)], -1.0f, 1.0f);
                return static_cast<double>(k % steps == 0 ? std::max(l, 0.0f) : l);
            };
            std::vector<sdk::CurvePointData> points;
            points.push_back({.phase = 0.0f, .value = 0.0f});
            for (int k = 0; k < n; ++k) {
                const double x = (k + 0.5 * lateness(k)) / n;
                if (k > 0 || x > 0.0)
                    points.push_back({.phase = static_cast<float>(x),
                                      .value = static_cast<float>(static_cast<double>(k) / n)});
            }
            // The downbeat after the cycle sounds at 1 + l0 / 2n; the head reaches the cycle's end
            // short of it.
            const double lastX = (n - 1 + 0.5 * lateness(n - 1)) / n;
            const double nextX = 1.0 + 0.5 * lateness(0) / n;
            const double endY = (n - 1.0) / n + (1.0 - lastX) / (nextX - lastX) / n;
            points.push_back({.phase = 1.0f, .value = static_cast<float>(endY)});

            sdk::sequencer::TimeBend bend;
            bend.on = true;
            bend.amount = std::clamp(amount, 0.0f, 1.0f);
            bend.bars = bars;
            bend.cycles = static_cast<int>(whole);
            bend.setCurve(points);
            return bend;
        }
    }
    return std::nullopt;
}

}  // namespace magda
