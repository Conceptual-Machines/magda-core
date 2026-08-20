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
 * The table says what a modifier is; this says where it is. The two are apart
 * because they live at different speeds: a table is republished every time a
 * knob moves, and an LFO that restarted on each of those would never finish a
 * cycle. So the settings travel with the values and the phase stays here,
 * carried from one epoch to the next by the modifier's own address.
 *
 * Carried by sharing rather than by copying. The executor being replaced is
 * still rendering while the next one is prepared, so its states are taken by
 * reference the way a delay line is: an LFO whose modifier survives a device
 * insert goes on turning through the swap rather than resuming from wherever
 * it was read at.
 *
 * Prepared off the audio thread, advanced on it, once per modifier per block,
 * from inside the table's resolution order so that anything a modifier reads
 * has already been resolved when it runs (ParamResolve.hpp).
 *
 * ## Where a trigger comes from
 *
 * Two of the three trigger modes need something outside the parameter system
 * to say when, and both feeds are wired here (#2120).
 *
 * A note-triggered modifier is driven by the MIDI reaching the scope it lives
 * in, and that MIDI is known before the block resolves: the ops that produce it
 * read clips and input queues rather than audio, so the executor renders them
 * first and the notes in a block reach the modifiers of that same block
 * (PlanExecutor's MIDI prefix).
 *
 * An audio-triggered one is driven by the level of the track it watches, and
 * that is not known before the block resolves, because the level is what the
 * ops are about to produce. It arrives one block late, always exactly one, and
 * that is a property of resolving parameters at the top of a block rather than
 * an accident (ModFollower.hpp weighs the alternative).
 *
 * The transport mode needs nothing: a timeline-locked modifier is a function
 * of where the block is.
 *
 * ## What a trigger means depends on the kind
 *
 * Four engines and three answers. An LFO restarts its phase, and gates as well
 * if the model asked it to. An envelope is nothing but a gate, so a trigger
 * opens it and the last note lifting shuts it. A random walk restarts and has
 * no gate at all, which is the fork's own shape. A follower has neither: its
 * input is a level, and a level has nothing to retrigger.
 */

namespace magda::engine {

struct ParamTable;
struct ParamModifier;

/**
 * @brief Where one modifier has got to.
 *
 * All four kinds, side by side, for the reason ParamModifier holds four
 * settings structs: exactly one is live and a state is a few dozen bytes, so
 * the padding buys a flat struct that carries by address without a discriminant
 * to check first.
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
 * Indexed the way ParamTable::modifiers is, because a link names a modifier by
 * that index and the block has no time to look one up.
 */
class ModRuntime {
  public:
    /**
     * @brief Size for @p table and adopt what @p previous already had.
     *
     * Off the audio thread, when a plan is prepared. A modifier the previous
     * runtime holds at the same address and of the same kind keeps its state,
     * shared rather than copied; everything else starts fresh.
     *
     * @p previous may be null, which is a session's first plan.
     */
    void prepare(const ParamTable& table, const RenderContext& context,
                 const ModRuntime* previous = nullptr);

    /// Forget everything. Off the audio thread; every prepare starts here.
    void reset();

    int size() const {
        return static_cast<int>(states_.size());
    }

    /// Modifiers this runtime took over from the one it replaced, rather than
    /// starting again. What the address-keyed carry bought, in other words:
    /// every one of these is an LFO that did not restart on the edit.
    int carried() const {
        return carried_;
    }

    /**
     * @brief Identity of the modifier list this was sized for.
     *
     * What ParamTable::layoutFingerprint is to the parameters. A table with
     * another one puts different modifiers at the same indices, so the state
     * kept here would be another LFO's phase and a link would read another
     * modifier's output.
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
     * On the audio thread, from the table's resolution order. @p rate is what
     * its Rate parameter resolved to this block, which is a frequency or a
     * division ordinal depending on whether the modifier is tempo synced. Only
     * the two kinds that have a rate read it.
     */
    void advance(int index, const ParamTable& table, const LfoRate& rate, const BlockInfo& block);

    /**
     * @brief Hand modifier @p index the block its source just rendered.
     *
     * On the audio thread, after the source's ops have run and before the next
     * block resolves. Only a follower has anything to do with it; everything
     * else ignores it, so the caller can offer a track's output to every
     * modifier listening to that track without asking what kind each is.
     *
     * @p mono is the source's block downmixed to one channel. The detection
     * happens here rather than at the next resolve because the audio is only
     * valid until the next block overwrites the buffer it is in.
     */
    void detectSource(int index, const ParamTable& table, std::span<const float> mono);

    /// Every modifier listening to track @p track, for the executor to offer
    /// that track's signal to. Off the audio thread: it is built at prepare and
    /// read by index on the block path.
    std::span<const int> listenersOf(magda::TrackId track) const;

    /// The tracks anything in this runtime listens to.
    std::span<const magda::TrackId> listenedTracks() const {
        return listened_;
    }

    /// What modifier @p index listens to its source for, so a note reaches the
    /// modifiers waiting for one and a level reaches the modifiers waiting for
    /// that. One list of listeners per track and this to sort them, rather than
    /// three lists that could disagree about who is on which.
    ModListen listensFor(int index, const ParamTable& table) const;

    /// What modifier @p index published this block, or the model's own value
    /// for one nothing has advanced.
    float value(int index) const;

    /// Where modifier @p index has got to, for the read-back taps and the
    /// tests. Null for an index this runtime does not have.
    const ModState* state(int index) const;

    /// The modifier at @p key, or -1. Off the audio thread: it is a scan, and
    /// what asks is a caller holding an address rather than an index.
    int indexOf(const ParamKey& key) const;

    /**
     * @brief A trigger from where the modifier lives.
     *
     * A note-on on the stream the modifier's own scope plays. Restarts the
     * phase, and opens the gate where the modifier has one that its trigger
     * owns: an LFO the model asked to be gated, and an envelope, whose gate is
     * the whole of what it is. A random walk restarts and is not gated; a
     * follower does neither.
     *
     * Refused when the modifier belongs to another track's monitor
     * (LfoSettings::skipNativeResync): a cross-track sidechain modifier follows
     * its source and must not be retriggered by whatever the track it ducks
     * happens to be playing.
     */
    void noteOn(int index, const ParamTable& table);

    /// The other half of it: the gate shuts when the last held note lifts.
    /// Refused on the same terms, because a gate that only one half of the
    /// pair could reach would latch open on the first note it heard.
    void noteOff(int index, const ParamTable& table);

    /// Every held note at once, which is what an all-notes-off is. Refused on
    /// the same terms again.
    void allNotesOff(int index, const ParamTable& table);

    /**
     * @brief A trigger from somewhere else.
     *
     * What the source track's level detector does to a cross-track sidechain
     * modifier, and the one path skipNativeResync does not refuse: it is the
     * trigger the modifier exists to follow.
     *
     * @p forceZero publishes a zero for the trigger block, so the device sees
     * the gap a gated retrigger would have left. Never for a level curve,
     * where zero output is full level and forcing one would pop the gain up
     * for a block in the middle of a duck.
     */
    void trigger(int index, const ParamTable& table, bool forceZero = true);

    /// Shut or open the gate directly, which is what an audio trigger does
    /// between hits. Ignored by the kinds that have no gate.
    void setGated(int index, const ParamTable& table, bool gated);

  private:
    ModState* mutableState(int index);

    /// Work out which modifiers listen to which track, once per prepare.
    void buildListenerRouting(const ParamTable& table);

    /// Send @p state back to the top, whatever kind it is. @p fromZero is what
    /// a cross-track trigger asks for, and only the envelope has anywhere to
    /// put it: an LFO's gap is a latch and a walk has none at all.
    static void restart(ModState& state, const ParamModifier& modifier, bool fromZero);

    /// Shared rather than owned outright, so an epoch being replaced and the
    /// one replacing it are the same LFO rather than two copies of it. Only
    /// one epoch renders at a time, so there is an owner to outlive and no
    /// concurrent use to guard against.
    std::vector<std::shared_ptr<ModState>> states_;

    /// What each modifier published this block. Filled by advance(), and by
    /// the table's own value for a kind with no engine yet.
    std::vector<float> values_;

    /// The addresses the states are held at, for the carry and for indexOf.
    std::vector<ParamKey> keys_;
    std::vector<ModKind> kinds_;

    /// Which modifiers listen to which track: listeners_[listenerOffsets_[t],
    /// listenerOffsets_[t + 1]) are the indices listening to listened_[t]. One
    /// arena and a parallel list of tracks, because almost no project has a
    /// listening modifier at all and a map keyed by track would be a lookup per
    /// block for it.
    std::vector<magda::TrackId> listened_;
    std::vector<int> listeners_;
    std::vector<int> listenerOffsets_;

    /// Room for one block of band-limited detection, shared by every follower
    /// because they run one at a time. Sized at prepare, so the block path
    /// never allocates.
    std::vector<float> detectScratch_;

    std::uint64_t fingerprint_ = 0;
    double sampleRate_ = 44100.0;
    ModTiming timing_;
    int carried_ = 0;
};

}  // namespace magda::engine
