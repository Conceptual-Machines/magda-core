#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <atomic>
#include <cstdint>
#include <utility>
#include <vector>

#include "clip/ActiveNoteList.hpp"
#include "clip/ClipSnapshotFeed.hpp"
#include "clip/MidiEventList.hpp"
#include "core/TypeIds.hpp"
#include "exec/EngineDevice.hpp"

/**
 * @file ClipMidiSource.hpp
 * @brief What a track's MIDI clips play, block by block.
 *
 * The source behind a `ClipMidi` op. It shares the snapshot, the span and the
 * interior silences with the audio side and shares nothing below that: no file,
 * no reader, no stretcher, no pool. What it has instead is a question audio
 * never has to answer, which is what a note that has already sounded is owed.
 *
 * ## The invariant
 *
 * **A note-off is never emitted for a note this source did not start, and never
 * withheld from one it did.** ActiveNoteList.hpp is what makes that structural.
 * Five things end a note and only the first falls out of the material:
 *
 * - a loop pass running out, or a clip's span ending, which is in the list
 *   because the fold clipped the note to the pass and the compile clipped it to
 *   the span;
 * - a locate or a loop wrap (`!BlockInfo::continuous`), off at sample zero;
 * - a stop (`!BlockInfo::playing`), the same, and then nothing;
 * - a snapshot swap that moved or deleted what was sounding, reconciled against
 *   what the new snapshot says should be sounding here, which is the fork's
 *   `shouldSendNoteOffsForNotesNoLongerPlaying` rule;
 * - destruction, which needs nothing: a source is destroyed when its track
 *   leaves the model and its output port leaves with it, so there is nowhere
 *   for a note-off to go and nothing downstream left to hang.
 *
 * ## Chase
 *
 * Every discontinuous block, after those note-offs: the value of each controller
 * as of the instant, then note-ons for the notes that instant is inside. Both
 * are the fork's behaviour (`createMessagesForTime`, `getNotesOnAtTime`) and
 * dropping either is audible in an ordinary way. Because the compile emits on
 * value change, the controller half is exactly right rather than nearly right:
 * if nothing was emitted since, nothing changed since.
 *
 * ## Holes
 *
 * An event whose instant lands in a silenced span is not emitted, so a note
 * starting inside a hole never sounds and one that started before it goes on
 * ringing through it: MIDI plays around a hole rather than being cut by it.
 * Note-offs are exempt unconditionally. A hole is a reason for a note not to
 * start and never a reason for one not to end.
 */

namespace magda::engine {

struct MidiClipPlayback;

class ClipMidiSource final : public EngineMidiSource {
  public:
    ClipMidiSource(TrackId trackId, ClipSnapshotFeed& clips);

    void prepare(const RenderContext& context) override;

    /// On the audio thread. @p out arrives cleared.
    void render(const BlockInfo& block, juce::MidiBuffer& out) override;

    /**
     * @brief Events this track had to leave out of a block.
     *
     * Counted rather than allowed to allocate, the way a track counts a clip it
     * had no voice for. Never a note-off: those are carried to the next block
     * instead, because a note-off late is a longer tail and a note-off missing
     * is a note that rings until the session ends.
     */
    int droppedEvents() const {
        return dropped_.load(std::memory_order_relaxed);
    }

    /// Loop passes a block could not fit (kMaxFoldPassesPerBlock). Reachable
    /// only by a loop shorter than a block, which the model does not forbid.
    int overflowedPasses() const {
        return overflowed_.load(std::memory_order_relaxed);
    }

  private:
    /// One note owed an off that would not fit in the block that owed it.
    struct OwedNote {
        int channel = 1;
        int note = 0;
    };

    /// Whether @p bytes more will fit, leaving room for every off still owed.
    bool fits(int bytes) const;

    void emit(juce::MidiBuffer& out, int sample, const MidiClipEvent& event);

    /// Note-off now, or owed if it will not fit. Always leaves the note not
    /// sounding.
    void endNote(juce::MidiBuffer& out, int sample, int channel, int note);

    /// End everything, or everything one clip owns.
    void endAll(juce::MidiBuffer& out, int sample);
    void endClip(juce::MidiBuffer& out, int sample, ClipId clipId);

    /// The controller state and the notes hanging over @p beat, for one clip.
    void chaseClip(juce::MidiBuffer& out, const BlockInfo& block, const MidiClipPlayback& clip,
                   const MidiFoldPass& pass, double timelineBeat);

    /// Whether @p beat is inside one of @p clip's silenced spans.
    static bool inHole(const MidiClipPlayback& clip, double beat);

    void renderClip(juce::MidiBuffer& out, const BlockInfo& block, const MidiClipPlayback& clip,
                    double from, double to, bool jumped);

    /// Start @p index of @p clip's list at @p timelineBeat.
    void startNote(juce::MidiBuffer& out, const BlockInfo& block, const MidiClipPlayback& clip,
                   std::int32_t index, double timelineBeat);

    TrackId trackId_;
    ClipSnapshotFeed& clips_;

    ActiveNoteList active_;
    int activeCount_ = 0;

    /// The snapshot the last block ran against, by identity. What a swap is
    /// detected by, and the only thing that makes the reconcile run.
    const ClipSnapshot* lastSnapshot_ = nullptr;

    int bytesUsed_ = 0;

    /// Reserved off the audio thread, never grown on it.
    std::vector<OwedNote> owed_;
    std::vector<std::int32_t> scratch_;

    std::atomic<int> dropped_{0};
    std::atomic<int> overflowed_{0};
};

}  // namespace magda::engine
