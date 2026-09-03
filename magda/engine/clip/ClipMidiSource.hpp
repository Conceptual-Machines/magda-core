#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <utility>
#include <vector>

#include "clip/ActiveNoteList.hpp"
#include "clip/ClipSnapshotFeed.hpp"
#include "clip/MidiEventList.hpp"
#include "core/TypeIds.hpp"
#include "exec/EngineDevice.hpp"
#include "launch/SessionLauncher.hpp"

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
 *
 * ## The two sections
 *
 * As on the audio side, one class plays both, chosen by the handle feed at
 * construction. A slot is the same lane walk over a block on the run's own axes
 * (SessionPlayback.hpp); the sample a beat lands on is unchanged, so emission
 * still writes into the callback's own buffer.
 *
 * That adds a sixth way a note ends: a slot not playing at the end of a block
 * owes an off for everything it started.
 */

namespace magda::engine {

struct MidiClipPlayback;

class ClipMidiSource final : public EngineMidiSource {
  public:
    /// The arrangement's source for @p trackId.
    ClipMidiSource(TrackId trackId, ClipSnapshotFeed& clips);

    /**
     * @brief The @p section's source for @p trackId, reading @p handles.
     *
     * Both sources of a track take the feed, and @p section says which of them
     * this is (#2302). A session source is positioned by the handles rather
     * than by the timeline; an arrangement source reads them only to know when
     * the session has taken the track off it, and where in the block, which is
     * where it owes note-offs for whatever it had sounding.
     *
     * @p handles outlives it.
     */
    ClipMidiSource(TrackId trackId, ClipSnapshotFeed& clips, LaunchHandleFeed& handles,
                   Section section);

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

    /**
     * @brief Blocks rendered against a map the snapshot was not compiled for.
     *
     * A snapshot carries the fingerprint of the tempo map its seconds were
     * derived through. One that is not the transport's was compiled against a
     * tempo that has since changed, so every second in it is wrong by however
     * much the map moved.
     *
     * Counted and then played anyway. Refusing to play it is a hole in the
     * middle of a set to report a bug that should not happen, and it buys
     * little, because stale spans usually stop overlapping the block in any
     * case: what that mostly converts is an accidental gap into a deliberate
     * one.
     *
     * Zero is the only right answer, and reaching it is the publish's job
     * rather than this one's: the map and the snapshot compiled for it are
     * meant to swap together (#2337). Non-zero says they did not, which is a
     * publish-ordering bug that would otherwise be inaudible until somebody
     * wondered why a clip was in the wrong place.
     */
    int staleSnapshots() const {
        return staleSnapshots_.load(std::memory_order_relaxed);
    }

  private:
    /// One note owed an off that would not fit in the block that owed it.
    struct OwedNote {
        int channel = 1;
        int note = 0;
    };

    /// Bits for every (channel, note), used to compare what is sounding against
    /// what a new snapshot says should be. On the stack: 256 bytes.
    class NoteMask {
      public:
        void set(int channel, int note) {
            const auto bit = index(channel, note);
            bits_[bit / 32] |= (1u << (bit % 32));
        }
        bool test(int channel, int note) const {
            const auto bit = index(channel, note);
            return (bits_[bit / 32] & (1u << (bit % 32))) != 0u;
        }

      private:
        static std::size_t index(int channel, int note) {
            return static_cast<std::size_t>(((channel - 1) * ActiveNoteList::kNotes) + note);
        }

        std::array<std::uint32_t, (ActiveNoteList::kChannels * ActiveNoteList::kNotes) / 32>
            bits_{};
    };

    /// Whether @p bytes more will fit, leaving room for every off still owed.
    bool fits(int bytes) const;

    void emit(juce::MidiBuffer& out, EventSample sample, const MidiClipEvent& event);

    /// Note-off now, or owed if it will not fit. Always leaves the note not
    /// sounding.
    void endNote(juce::MidiBuffer& out, EventSample sample, int channel, int note);

    /// End everything, or everything one clip owns.
    void endAll(juce::MidiBuffer& out, EventSample sample);
    void endClip(juce::MidiBuffer& out, EventSample sample, ClipId clipId);

    /// The notes of @p clip actually sounding at @p timelineBeat, into @ref
    /// scratch_. Grooved edges, not written ones, and one implementation for
    /// both the chase and the snapshot reconcile.
    void gatherSounding(const MidiClipPlayback& clip, const MidiFoldPass& pass,
                        double timelineBeat);

    /// The controller state and the notes hanging over @p beat, for one clip.
    void chaseClip(juce::MidiBuffer& out, const BlockInfo& block, const MidiClipPlayback& clip,
                   const MidiFoldPass& pass, double timelineBeat);

    /// Whether @p beat is inside one of @p clip's silenced spans.
    static bool inHole(const MidiClipPlayback& clip, double beat);

    void renderClip(juce::MidiBuffer& out, const BlockInfo& block, const MidiClipPlayback& clip,
                    double from, double to, bool jumped);

    /// Play the part of @p clips between @p from and @p to. One lane over one
    /// block: the track's clips for the arrangement, a slot's for the session.
    void playLane(juce::MidiBuffer& out, const BlockInfo& block,
                  const std::vector<MidiClipPlayback>& clips, double from, double to);

    /// What @p clips say should be sounding here, into @p expected, handing over
    /// any pitch the wrong clip holds. Split from the sweep because the session
    /// has several lanes to mark before anything may be ended.
    void expectLane(juce::MidiBuffer& out, const BlockInfo& block,
                    const std::vector<MidiClipPlayback>& clips, double from, double to,
                    NoteMask& expected);

    /// The other half: end everything sounding that @p expected does not name.
    void endUnexpected(juce::MidiBuffer& out, const NoteMask& expected);

    /// Every note one slot started, ended at @p sample. What a stop owes.
    void endSlot(juce::MidiBuffer& out, EventSample sample, const SessionSlotPlayback& slot);

    /// What a launched slot plays and what a stopped one owes. False when no
    /// handles have been published yet.
    bool renderSession(juce::MidiBuffer& out, const BlockInfo& block,
                       const TrackClipPlayback& track, bool reconcile);

    /// Start @p index of @p clip's list at @p timelineBeat.
    void startNote(juce::MidiBuffer& out, const BlockInfo& block, const MidiClipPlayback& clip,
                   std::int32_t index, double timelineBeat);

    TrackId trackId_;
    ClipSnapshotFeed& clips_;

    /// The section. Null is the arrangement, which needs no handles.
    LaunchHandleFeed* handles_ = nullptr;
    Section section_ = Section::Arrangement;

    /// Whether the session held this track when the last block ended, which is
    /// what makes the hand-over an edge rather than a state (#2302).
    bool wasHeld_ = false;

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
    std::atomic<int> staleSnapshots_{0};
};

}  // namespace magda::engine
