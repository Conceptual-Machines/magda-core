#pragma once

#include <vector>

#include "transport/TempoMap.hpp"

/**
 * @file AraMusicalContent.hpp
 * @brief The project's tempo and bar grid as an ARA musical context reads them (#1579).
 *
 * Plain structs rather than the SDK's, so the conversion is testable without the ARA headers.
 */

namespace magda::daw::audio::ara {

/** @brief One point of ARA's piecewise-linear seconds-to-quarters map. */
struct AraTempoEntry {
    double seconds = 0.0;
    double quarters = 0.0;

    bool operator==(const AraTempoEntry&) const = default;
};

/** @brief A signature from a bar line on, positioned in quarters. */
struct AraBarSignature {
    double quarters = 0.0;
    int numerator = 4;
    int denominator = 4;

    bool operator==(const AraBarSignature&) const = default;
};

/** @brief @p map's sections as tempo entries, plus one a beat past the last: ARA wants two. */
std::vector<AraTempoEntry> tempoEntriesFor(const engine::TempoMap& map);

/** @brief One entry per signature change in @p map, never empty. */
std::vector<AraBarSignature> barSignaturesFor(const engine::TempoMap& map);

}  // namespace magda::daw::audio::ara
