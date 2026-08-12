#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include "exec/RenderContext.hpp"

/**
 * @file MidiTap.hpp
 * @brief What a merged-MIDI op hands to whoever is watching that point.
 *
 * The sibling of LevelTap, and it exists for the same reason: the plan is
 * topology, so an op names a location and owns nothing, and a tap is a place in
 * the signal rather than a thing in the project. Bound by OpKey for exactly
 * that reason.
 *
 * The location worth naming is the one this was written for. A track's
 * TrackMidiInput op is the merge of everything feeding its chain: its clips, its
 * live input, and any MIDI routed in from another track. That is the answer to
 * "what does this track play", and it is a property of the track rather than of
 * whatever happens to be installed on it. Reading it from a device instead
 * confuses two different things, and then has to guess which device it meant
 * once a track has more than one, or one that consumes no MIDI, or none at all.
 *
 * Unbound is the ordinary case and never an error, as with a meter: an offline
 * render nobody is watching binds none of these.
 *
 * Called on whichever thread renders the block, so what a tap does with the
 * buffer is the binder's problem, the same contract EngineDevice has. Nothing
 * here is safe to do on the audio thread unless the implementation makes it so.
 */

namespace magda::engine {

class MidiTap {
  public:
    virtual ~MidiTap() = default;

    /**
     * @brief The op's merged output for this block.
     *
     * @param midi   every event feeding the chain, stamped within the block
     * @param block  where the block sits, so an event can be placed on the
     *               timeline rather than inside a callback
     */
    virtual void write(const juce::MidiBuffer& midi, const BlockInfo& block) = 0;
};

}  // namespace magda::engine
