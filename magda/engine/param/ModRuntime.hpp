#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "exec/RenderContext.hpp"
#include "param/ModLfo.hpp"
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
 * to say when. A note-triggered modifier is driven by the MIDI reaching the
 * scope it lives in, and an audio-triggered or cross-track one by the level
 * detector watching its source. Neither exists in the native engine yet, so
 * the trigger calls below are the shape those feeds will use rather than
 * something the executor calls today.
 *
 * The transport mode needs nothing: a timeline-locked modifier is a function
 * of where the block is, and that is wired. The rest arrive with the slice
 * that has to solve the same problem for the ADSR's gate and the envelope
 * follower's input (#2120), because those cannot be built without it.
 */

namespace magda::engine {

struct ParamTable;

/**
 * @brief Where one modifier has got to.
 *
 * One kind runs today. The rest are carried as a value the table published and
 * hold it, which is what every modifier did before this slice; their engines
 * arrive in #2120 and their state joins this struct there.
 */
struct ModState {
    LfoState lfo;
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
     * division ordinal depending on whether the modifier is tempo synced.
     *
     * A modifier of a kind that has no engine yet publishes the value the
     * table carries, which is the model's own reading of it.
     */
    void advance(int index, const ParamTable& table, const LfoRate& rate, const BlockInfo& block);

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
     * phase and, where the modifier is gated by its trigger, opens the gate
     * and counts the note as held.
     *
     * Refused when the modifier belongs to another track's monitor
     * (LfoSettings::skipNativeResync): a cross-track sidechain LFO follows its
     * source and must not be retriggered by whatever the track it ducks
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
     * What the source track's monitor does to a cross-track sidechain LFO, and
     * the one path skipNativeResync does not refuse: it is the trigger the
     * modifier exists to follow.
     *
     * @p forceZero publishes a zero for the trigger block, so the device sees
     * the gap a gated retrigger would have left. Never for a level curve,
     * where zero output is full level and forcing one would pop the gain up
     * for a block in the middle of a duck.
     */
    void trigger(int index, const ParamTable& table, bool forceZero = true);

    /// Shut or open the gate directly, which is what an audio trigger does
    /// between hits.
    void setGated(int index, bool gated);

  private:
    ModState* mutableState(int index);

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

    std::uint64_t fingerprint_ = 0;
    double sampleRate_ = 44100.0;
    ModTiming timing_;
    int carried_ = 0;
};

}  // namespace magda::engine
