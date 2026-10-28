// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

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

    const auto& credits = project.metadata;
    facts.title = credits.title;
    facts.artist = credits.artist;
    facts.album = credits.album;
    facts.date = credits.year;
    facts.genre = credits.genre;
    facts.comment = credits.comment;
    return facts;
}

}  // namespace magda
