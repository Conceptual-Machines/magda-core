#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <map>
#include <string>
#include <vector>

#include "NullDiffCase.hpp"
#include "NullDiffCompare.hpp"

/**
 * @file NullDiffTeLeg.hpp
 * @brief A null-diff case rendered through the incumbent (#2040).
 *
 * What the app does today: a te::Edit, the model tracks mapped through
 * TrackController, every clip pushed through ClipSynchronizer, and the render
 * parameters the offline-render helper builds. Not a second sync written for
 * the harness, because the sync layer is part of what is being validated: a
 * clip that reaches Tracktion wrong plays wrong for exactly the same user.
 *
 * Two things this leg can do that would poison the corpus, and both are handled
 * here rather than left to chance. They matter because neither looks like a
 * harness fault from the outside; both look like the engine being wrong.
 *
 * **It can render at the wrong depth.** Renderer::Parameters::bitDepth defaults
 * to 16, which puts quantisation noise around -96 dBFS, twenty-four decibels
 * above the corpus floor. Every case would then report the same residual and
 * that residual would be the file format. So this renders float, and checks
 * what it read back really is float rather than trusting the parameter.
 *
 * **It can render before its proxies exist.** Stretch, reverse and warp all
 * reach playback through a proxy file rendered on a background pool, and a
 * render started before that job finishes plays silence, intermittently, for
 * precisely the cases the corpus was built for. So it waits, and a wait that
 * times out fails the case as unmeasurable rather than handing back a
 * full-scale residual that is really a race.
 */

namespace magda::nulldiff {

struct IncumbentRender {
    juce::AudioBuffer<float> audio;

    /// Every capture's events, in timeline order. What the report prints.
    MidiStream midi;

    /// The same events, kept apart by the track that received them, so that a
    /// capture landing on the wrong track is a finding rather than an aggregate
    /// that happens to contain the same bytes. See NullDiffNativeLeg.hpp.
    std::map<TrackId, MidiStream> midiByTrack;

    /// Set when the case could not be rendered at all. Never reported as a
    /// residual: a race described as a parity failure costs somebody a day
    /// inside the engine looking for a bug that is not there.
    std::string failure;

    /// True when the buffer came back from a floating-point file, checked
    /// rather than assumed.
    bool renderedInFloat = false;

    /// Clips whose proxy had to be waited for, and how long the wait took. Not
    /// an assertion, a number: a wait that grows is worth seeing before it
    /// turns into a timeout.
    int proxiesWaitedFor = 0;
    int waitMilliseconds = 0;
};

/// Render @p value through Tracktion, over its own beat range.
IncumbentRender renderIncumbent(const Case& value);

}  // namespace magda::nulldiff
