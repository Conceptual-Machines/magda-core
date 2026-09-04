#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "exec/RenderContext.hpp"
#include "param/ModAdsr.hpp"
#include "param/ModFollower.hpp"
#include "param/ModLfo.hpp"
#include "param/ModRandom.hpp"
#include "param/ModSources.hpp"
#include "param/ParamKey.hpp"

/**
 * @file ModRuntime.hpp
 * @brief The modifiers of one plan, and where each of them has got to.
 *
 * The table says what a modifier is; this says where it is. Apart because
 * they live at different speeds: a table is republished on every knob move,
 * and an LFO restarting each time would never finish a cycle. State is
 * carried from one epoch to the next by the modifier's own address, shared
 * rather than copied -- the executor being replaced is still rendering while
 * the next one is prepared, so an LFO surviving a device insert keeps turning
 * through the swap rather than resuming from wherever it was read at.
 *
 * Prepared off the audio thread, advanced on it, once per modifier per block,
 * from inside the table's resolution order so anything a modifier reads has
 * already been resolved (ParamResolve.hpp).
 *
 * ## Triggers (#2120)
 *
 * Note-triggered: driven by MIDI reaching the modifier's scope, known before
 * the block resolves (PlanExecutor's MIDI prefix runs first), so the notes in
 * a block reach that block's modifiers.
 *
 * Audio-triggered: driven by the level of the track it watches, which is not
 * known before the block resolves since it's what that block's ops are about
 * to produce -- arrives exactly one block late, a property of resolving
 * parameters at the top of a block (ModFollower.hpp weighs the alternative).
 *
 * Transport: needs nothing, a timeline-locked modifier is a function of
 * where the block is.
 *
 * What a trigger means depends on the kind: an LFO restarts its phase (and
 * gates too if the model asked); an envelope is nothing but a gate, opened by
 * a trigger and shut when the last note lifts; a random walk restarts with no
 * gate; a follower has neither, since its input is a level with nothing to
 * retrigger.
 */

namespace magda::engine {

struct ParamTable;
struct ParamModifier;

/**
 * @brief Where one modifier has got to.
 *
 * All four kinds side by side, matching ParamModifier's four settings
 * structs: only one is ever live, so the padding buys a flat struct carried
 * by address with no discriminant to check.
 */
struct ModState {
    LfoState lfo;
    AdsrState adsr;
    RandomState random;
    FollowerState follower;
};

/**
 * @brief The modifier engines behind one plan.
 *
 * Indexed the way ParamTable::modifiers is: a link names a modifier by that
 * index and the block has no time to look one up.
 */
class ModRuntime {
  public:
    /**
     * @brief Size for @p table and adopt what @p previous already had.
     *
     * Off the audio thread, when a plan is prepared. A modifier @p previous
     * holds at the same address and kind keeps its state, shared rather than
     * copied; everything else starts fresh. @p previous may be null (a
     * session's first plan).
     */
    void prepare(const ParamTable& table, const RenderContext& context,
                 const ModRuntime* previous = nullptr);

    /// Forget everything. Off the audio thread; every prepare starts here.
    void reset();

    int size() const {
        return static_cast<int>(states_.size());
    }

    /// Modifiers carried over from the runtime this replaced rather than
    /// restarted -- every one is an LFO that didn't reset on the edit.
    int carried() const {
        return carried_;
    }

    /**
     * @brief Identity of the modifier list this was sized for.
     *
     * What ParamTable::layoutFingerprint is to the parameters: a table with a
     * different one puts different modifiers at the same indices, so state
     * kept here would be another LFO's phase.
     */
    std::uint64_t fingerprint() const {
        return fingerprint_;
    }

    /// Settle what this block is, before any modifier is advanced. On the
    /// audio thread, once per block.
    void beginBlock(const BlockInfo& block);

    /**
     * @brief Advance modifier @p index over the block and publish its output.
     *
     * On the audio thread, from the table's resolution order. @p rate is
     * what its Rate parameter resolved to (a frequency or division ordinal,
     * per tempo sync); only the two kinds with a rate read it.
     */
    void advance(int index, const ParamTable& table, const LfoRate& rate, const BlockInfo& block);

    /**
     * @brief Hand modifier @p index the block its source just rendered.
     *
     * On the audio thread, after the source's ops run and before the next
     * block resolves. Only a follower does anything with it, so a caller can
     * offer a track's output to every modifier listening to it without
     * asking what kind each is. @p mono is the source's block downmixed to
     * one channel; detection happens here because the buffer is only valid
     * until the next block overwrites it.
     */
    void detectSource(int index, const ParamTable& table, std::span<const float> mono);

    /// Every modifier listening to track @p track. Off the audio thread:
    /// built at prepare and read by index on the block path.
    std::span<const int> listenersOf(magda::TrackId track) const;

    /// The tracks anything in this runtime listens to.
    std::span<const magda::TrackId> listenedTracks() const {
        return listened_;
    }

    /// What modifier @p index listens to its source for, so a note reaches
    /// note-waiting modifiers and a level reaches level-waiting ones. One
    /// listener list per track plus this to sort them, rather than three
    /// lists that could disagree about who is on which.
    ModListen listensFor(int index, const ParamTable& table) const;

    /**
     * @brief Whether modifier @p index is driven by a track other than its own.
     *
     * Two kinds of listener take a trigger through different doors: one
     * living on the source hears its own notes and counts them (gate shuts
     * on the last one); one living elsewhere is following that track rather
     * than playing it, so the note-counting path refuses it and @ref trigger
     * is the way in -- the fork's arrangement too (SidechainMonitorPlugin
     * fires triggerSidechain and ignores note-off). Exposed because the
     * executor holds the tap and cannot otherwise tell the two apart.
     */
    bool drivenFromElsewhere(int index, const ParamTable& table) const;

    /// What modifier @p index published this block, or the model's own
    /// value for one nothing has advanced.
    float value(int index) const;

    /// Where modifier @p index has got to, for read-back taps and tests.
    /// Null for an index this runtime does not have.
    const ModState* state(int index) const;

    /// The modifier at @p key, or -1. Off the audio thread: a scan, for a
    /// caller holding an address rather than an index.
    int indexOf(const ParamKey& key) const;

    /**
     * @brief A trigger from where the modifier lives.
     *
     * A note-on on the stream the modifier's own scope plays. Restarts the
     * phase and opens the gate where the modifier has one that its trigger
     * owns (a gated LFO, an envelope); a random walk restarts ungated, a
     * follower does neither.
     *
     * Refused when the modifier belongs to another track's monitor
     * (LfoSettings::skipNativeResync): a cross-track sidechain modifier
     * follows its source and must not be retriggered by whatever the track
     * it ducks happens to be playing.
     */
    void noteOn(int index, const ParamTable& table);

    /// The other half: the gate shuts when the last held note lifts.
    /// Refused on the same terms, or a gate only one half could reach would
    /// latch open on the first note it heard.
    void noteOff(int index, const ParamTable& table);

    /// Every held note at once, i.e. an all-notes-off. Refused on the same terms.
    void allNotesOff(int index, const ParamTable& table);

    /**
     * @brief A trigger from somewhere else.
     *
     * What the source track's level detector does to a cross-track
     * sidechain modifier -- the one path skipNativeResync does not refuse,
     * since it is the trigger the modifier exists to follow.
     *
     * @p forceZero publishes a zero for the trigger block so the device sees
     * the gap a gated retrigger would have left. Never for a level curve,
     * where zero output is full level and forcing one would pop the gain up
     * mid-duck.
     */
    void trigger(int index, const ParamTable& table, bool forceZero = true);

    /// Shut or open the gate directly, as an audio trigger does between
    /// hits. Ignored by kinds with no gate.
    void setGated(int index, const ParamTable& table, bool gated);

  private:
    ModState* mutableState(int index);

    /// Work out which modifiers listen to which track, once per prepare.
    void buildListenerRouting(const ParamTable& table);

    /// Send @p state back to the top, whatever kind it is. @p fromZero is
    /// what a cross-track trigger asks for; only the envelope has anywhere
    /// to put it (an LFO's gap is a latch, a walk has none).
    static void restart(ModState& state, const ParamModifier& modifier, bool fromZero);

    /// Shared rather than owned outright, so an epoch being replaced and the
    /// one replacing it are the same LFO, not two copies. Only one epoch
    /// renders at a time, so there's one owner and no concurrent use to guard.
    std::vector<std::shared_ptr<ModState>> states_;

    /// What each modifier published this block. Filled by advance(), or by
    /// the table's own value for a kind with no engine yet.
    std::vector<float> values_;

    /// The addresses the states are held at, for the carry and for indexOf.
    std::vector<ParamKey> keys_;
    std::vector<ModKind> kinds_;

    /// Which modifiers listen to which track: listeners_[listenerOffsets_[t],
    /// listenerOffsets_[t + 1]) listen to listened_[t]. One arena plus a
    /// parallel track list, since almost no project has a listening modifier
    /// at all and a map keyed by track would cost a lookup per block.
    std::vector<magda::TrackId> listened_;
    std::vector<int> listeners_;
    std::vector<int> listenerOffsets_;

    /// Room for one block of band-limited detection, shared by every
    /// follower since they run one at a time. Sized at prepare, so the block
    /// path never allocates.
    std::vector<float> detectScratch_;

    std::uint64_t fingerprint_ = 0;
    double sampleRate_ = 44100.0;
    ModTiming timing_;
    int carried_ = 0;
};

}  // namespace magda::engine
