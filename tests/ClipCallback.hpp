#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

#include "clip/ClipAudioSource.hpp"
#include "clip/ClipMidiSource.hpp"
#include "clip/ClipSnapshotFeed.hpp"
#include "clip/SessionPlayback.hpp"

/**
 * @file ClipCallback.hpp
 * @brief One block through a clip source, the way a callback delivers one.
 *
 * A callback pins the clips for the block and resolves every track's section
 * before anything renders (#2490, EngineSession::process); a source rendered
 * outside that reads no snapshot at all. That is a protocol rather than a call,
 * so it lives here instead of in every rig that drives a source directly.
 */

namespace magda::test {

/// The clips pinned and the sections resolved, for as long as this exists.
/// Every source rendered inside one plays the same publish.
class ClipBlock {
  public:
    ClipBlock(engine::ClipSnapshotFeed& clips, const engine::BlockInfo& block,
              engine::LaunchHandleFeed* handles = nullptr)
        : pinned_(clips) {
        engine::advanceTrackSections(clips.sections(), clips.live(), handles, block);
    }

  private:
    engine::ClipSnapshotFeed::BlockScope pinned_;
};

/// One audio block, for a test with nothing else to do inside the callback.
inline void renderBlock(engine::ClipAudioSource& source, engine::ClipSnapshotFeed& clips,
                        const engine::BlockInfo& block, juce::dsp::AudioBlock<float> out,
                        engine::LaunchHandleFeed* handles = nullptr) {
    const ClipBlock pinned(clips, block, handles);
    source.render(block, out);
}

/// The same, for MIDI.
inline void renderBlock(engine::ClipMidiSource& source, engine::ClipSnapshotFeed& clips,
                        const engine::BlockInfo& block, juce::MidiBuffer& out,
                        engine::LaunchHandleFeed* handles = nullptr) {
    const ClipBlock pinned(clips, block, handles);
    source.render(block, out);
}

}  // namespace magda::test
