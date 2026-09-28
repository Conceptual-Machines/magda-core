#pragma once

#include <cmath>
#include <utility>

#include "../../engine/io/AudioFileMetadata.hpp"
#include "../project/ProjectInfo.hpp"
#include "version.hpp"

namespace magda {

/// Snapshot the few musical facts needed by a background writer.
inline engine::AudioFileMetadata renderFileMetadata(const ProjectInfo& project,
                                                    double durationSeconds,
                                                    juce::String description,
                                                    std::optional<bool> oneShot = std::nullopt) {
    engine::AudioFileMetadata facts;
    if (std::isfinite(project.tempo) && project.tempo > 0.0) {
        facts.tempo = project.tempo;
        if (durationSeconds > 0.0)
            facts.beats = durationSeconds * project.tempo / 60.0;
    }
    facts.numerator = project.timeSignatureNumerator;
    facts.denominator = project.timeSignatureDenominator;
    if (project.keyRoot >= 0 && project.keyRoot < 12) {
        facts.keyRoot = project.keyRoot;
        facts.keyQuality = project.keyQuality;
    }
    facts.oneShot = oneShot;
    facts.description = std::move(description);
    facts.originator = juce::String("MAGDA ") + MAGDA_VERSION;
    return facts;
}

}  // namespace magda
