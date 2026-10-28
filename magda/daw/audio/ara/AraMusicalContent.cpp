// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include "AraMusicalContent.hpp"

namespace magda::daw::audio::ara {

std::vector<AraTempoEntry> tempoEntriesFor(const engine::TempoMap& map) {
    std::vector<AraTempoEntry> entries;
    for (const auto& section : map.sectionStarts()) {
        // ARA wants both faces strictly increasing; a tempo step is two sections at one beat.
        if (!entries.empty() && section.beat <= entries.back().quarters)
            continue;
        entries.push_back({.seconds = section.seconds, .quarters = section.beat});
    }

    const double lastBeat = entries.empty() ? 0.0 : entries.back().quarters;
    if (entries.empty())
        entries.push_back({.seconds = map.beatToTime(0.0), .quarters = 0.0});
    entries.push_back({.seconds = map.beatToTime(lastBeat + 1.0), .quarters = lastBeat + 1.0});
    return entries;
}

std::vector<AraBarSignature> barSignaturesFor(const engine::TempoMap& map) {
    std::vector<AraBarSignature> signatures;
    for (const auto& section : map.sectionStarts()) {
        if (!signatures.empty() && signatures.back().numerator == section.numerator &&
            signatures.back().denominator == section.denominator)
            continue;
        // A change always opens a bar, so the section that carries it starts on one.
        signatures.push_back({.quarters = section.beat,
                              .numerator = section.numerator,
                              .denominator = section.denominator});
    }

    if (signatures.empty())
        signatures.push_back({});
    return signatures;
}

}  // namespace magda::daw::audio::ara
