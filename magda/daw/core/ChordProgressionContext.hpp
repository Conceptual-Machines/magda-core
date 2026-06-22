#pragma once

#include <juce_core/juce_core.h>

#include <algorithm>
#include <vector>

#include "ClipManager.hpp"
#include "TrackManager.hpp"

namespace magda {

/// One chord on the chord-track timeline, in absolute song beats.
struct ProgressionChord {
    double startBeat = 0.0;
    double lengthBeats = 0.0;
    juce::String name;
};

/**
 * @brief Read-only view of the chord-track progression.
 *
 * The chord clip's annotations are the single source of truth; this pulls them
 * on demand (always current, no cached copy) so the chord engine, inspector, and
 * AI context can share the same progression. Message-thread only.
 */
class ChordProgressionContext {
  public:
    static std::vector<ProgressionChord> current() {
        std::vector<ProgressionChord> out;
        const auto trackId = TrackManager::getInstance().getChordTrackId();
        if (trackId == INVALID_TRACK_ID)
            return out;

        auto& cm = ClipManager::getInstance();
        for (const auto clipId : cm.getClipsOnTrack(trackId)) {
            const auto* clip = cm.getClip(clipId);
            if (clip == nullptr)
                continue;
            const double base = clip->placement.startBeat;
            for (const auto& a : clip->chordAnnotations)
                out.push_back({base + a.beatPosition, a.lengthBeats, a.chordName});
        }

        std::sort(out.begin(), out.end(), [](const ProgressionChord& x, const ProgressionChord& y) {
            return x.startBeat < y.startBeat;
        });
        return out;
    }

    /// Compact "C - Am - F - G" summary for prompts/labels (empty if none).
    static juce::String summary() {
        juce::String s;
        for (const auto& c : current()) {
            if (s.isNotEmpty())
                s += " - ";
            s += c.name;
        }
        return s;
    }

    static bool isEmpty() {
        return current().empty();
    }
};

}  // namespace magda
