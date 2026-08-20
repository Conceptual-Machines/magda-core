#include "param/ModRuntime.hpp"

#include <algorithm>
#include <unordered_map>

#include "param/ParamTable.hpp"

namespace magda::engine {

namespace {

/// What the model said triggers this modifier. One field under four names,
/// because each kind's settings carry their own copy of it.
magda::LFOTriggerMode triggerOf(const ParamModifier& modifier) {
    switch (modifier.kind) {
        case ModKind::Lfo:
            return modifier.lfo.trigger;
        case ModKind::Adsr:
            return modifier.adsr.trigger;
        case ModKind::Random:
            return modifier.random.trigger;
        case ModKind::Follower:
            break;
    }
    return magda::LFOTriggerMode::Free;
}

/// Whether a trigger reaching this modifier owns a gate as well as a phase.
///
/// Three answers over four kinds. An LFO is gated only if the model asked for
/// it, because a free-running one that a note silenced between hits would be an
/// LFO nobody wrote. An envelope is always gated by its trigger while it is in
/// note mode: the gate is the whole of what an envelope is, and in the other
/// modes the gate comes from somewhere a trigger cannot reach. A random walk
/// and a follower have no gate at all.
bool gatedByTrigger(const ParamModifier& modifier) {
    switch (modifier.kind) {
        case ModKind::Lfo:
            return modifier.lfo.gateOnTrigger;
        case ModKind::Adsr:
            return modifier.adsr.sync == ModSync::Note;
        case ModKind::Random:
        case ModKind::Follower:
            return false;
    }
    return false;
}

/// Whether this modifier belongs to another track's detector, and therefore
/// hears nothing from where it lives. One flag under four names, because each
/// kind's settings carry their own copy of what the model said.
bool drivenFromElsewhere(const ParamModifier& modifier) {
    switch (modifier.kind) {
        case ModKind::Lfo:
            return modifier.lfo.skipNativeResync;
        case ModKind::Adsr:
            return modifier.adsr.skipNativeResync;
        case ModKind::Random:
            return modifier.random.skipNativeResync;
        case ModKind::Follower:
            // A follower is not triggered at all, so there is nothing for the
            // flag to suppress. Refusing here as well would be the same answer
            // by a longer route.
            return false;
    }
    return false;
}

/// Notes are counted on whichever state this kind keeps its gate in, so the
/// gate shuts when the last one lifts rather than on the first note off.
int* heldNotesOf(ModState& state, const ParamModifier& modifier) {
    switch (modifier.kind) {
        case ModKind::Lfo:
            return &state.lfo.heldNotes;
        case ModKind::Adsr:
            return &state.adsr.heldNotes;
        case ModKind::Random:
        case ModKind::Follower:
            return nullptr;
    }
    return nullptr;
}

/// Shut or open whichever gate this kind has.
void applyGate(ModState& state, const ParamModifier& modifier, bool gated) {
    switch (modifier.kind) {
        case ModKind::Lfo:
            state.lfo.gated = gated;
            state.lfo.started = true;
            break;
        case ModKind::Adsr:
            state.adsr.gated = gated;
            state.adsr.started = true;
            break;
        case ModKind::Random:
        case ModKind::Follower:
            break;
    }
}

}  // namespace

void ModRuntime::reset() {
    states_.clear();
    values_.clear();
    keys_.clear();
    kinds_.clear();
    listened_.clear();
    listeners_.clear();
    listenerOffsets_.clear();
    detectScratch_.clear();
    fingerprint_ = 0;
    sampleRate_ = 44100.0;
    timing_ = ModTiming{};
    carried_ = 0;
}

void ModRuntime::prepare(const ParamTable& table, const RenderContext& context,
                         const ModRuntime* previous) {
    reset();

    sampleRate_ = context.sampleRate > 0.0 ? context.sampleRate : 44100.0;
    fingerprint_ = table.modifierFingerprint;
    timing_.sampleRate = sampleRate_;

    const auto count = table.modifiers.size();
    states_.reserve(count);
    values_.reserve(count);
    keys_.reserve(count);
    kinds_.reserve(count);

    // What the previous epoch holds, by address. Built rather than scanned
    // because a project with sixteen modifiers per scope has plenty of them,
    // and this runs once per prepare rather than once per block.
    std::unordered_map<ParamKey, std::size_t, ParamKeyHash> adoptable;
    if (previous != nullptr) {
        adoptable.reserve(previous->keys_.size());
        for (std::size_t i = 0; i < previous->keys_.size(); ++i)
            adoptable.emplace(previous->keys_[i], i);
    }

    for (const auto& modifier : table.modifiers) {
        std::shared_ptr<ModState> state;

        if (previous != nullptr) {
            const auto found = adoptable.find(modifier.key);

            // The same address and the same kind. A modifier the user has
            // switched from one type to another is a different engine at that
            // address, and the phase of the one it replaced means nothing to
            // it.
            if (found != adoptable.end() && previous->kinds_[found->second] == modifier.kind) {
                state = previous->states_[found->second];
                ++carried_;
            }
        }

        if (state == nullptr) {
            state = std::make_shared<ModState>();

            // A walk's generator is seeded from the modifier's own address, so
            // the numbers a project draws are the same on every run of it and
            // two modifiers never draw the same sequence. Only on a fresh
            // state: a carried one is mid-walk and reseeding would restart it.
            seedRandom(state->random, ParamKeyHash{}(modifier.key));
        }

        states_.push_back(std::move(state));
        values_.push_back(modifier.value);
        keys_.push_back(modifier.key);
        kinds_.push_back(modifier.kind);
    }

    buildListenerRouting(table);

    // One block of room for band-limited detection, which is the only place a
    // modifier touches a buffer at all.
    detectScratch_.assign(static_cast<std::size_t>(std::max(context.maxBlockSize, 0)), 0.0f);
}

void ModRuntime::buildListenerRouting(const ParamTable& table) {
    // The tracks, deduplicated and in order, then the modifiers listening to
    // each. Two passes over a list that is almost always empty, off the audio
    // thread, so the block path is an index into a flat arena.
    for (const auto& modifier : table.modifiers) {
        if (modifier.source == magda::INVALID_TRACK_ID)
            continue;

        if (std::find(listened_.begin(), listened_.end(), modifier.source) == listened_.end())
            listened_.push_back(modifier.source);
    }

    if (listened_.empty())
        return;

    listenerOffsets_.assign(listened_.size() + 1, 0);

    for (std::size_t t = 0; t < listened_.size(); ++t) {
        listenerOffsets_[t] = static_cast<int>(listeners_.size());

        for (std::size_t i = 0; i < table.modifiers.size(); ++i)
            if (table.modifiers[i].source == listened_[t])
                listeners_.push_back(static_cast<int>(i));
    }

    listenerOffsets_.back() = static_cast<int>(listeners_.size());
}

std::span<const int> ModRuntime::listenersOf(magda::TrackId track) const {
    const auto found = std::find(listened_.begin(), listened_.end(), track);
    if (found == listened_.end())
        return {};

    const auto t = static_cast<std::size_t>(std::distance(listened_.begin(), found));
    const auto first = static_cast<std::size_t>(listenerOffsets_[t]);
    const auto last = static_cast<std::size_t>(listenerOffsets_[t + 1]);

    return std::span<const int>{listeners_}.subspan(first, last - first);
}

ModListen ModRuntime::listensFor(int index, const ParamTable& table) const {
    if (index < 0 || index >= static_cast<int>(table.modifiers.size()))
        return ModListen::Nothing;

    const auto& modifier = table.modifiers[static_cast<std::size_t>(index)];
    if (!modifier.enabled || modifier.source == magda::INVALID_TRACK_ID)
        return ModListen::Nothing;

    // A follower is its source and nothing else, whatever its trigger mode
    // says: the trigger fields belong to the kinds that have a phase.
    if (modifier.kind == ModKind::Follower)
        return ModListen::Audio;

    switch (triggerOf(modifier)) {
        case magda::LFOTriggerMode::MIDI:
            return ModListen::Midi;
        case magda::LFOTriggerMode::Audio:
            return ModListen::Audio;
        case magda::LFOTriggerMode::Free:
        case magda::LFOTriggerMode::Transport:
            break;
    }

    return ModListen::Nothing;
}

void ModRuntime::beginBlock(const BlockInfo& block) {
    timing_ = modTimingFor(block, sampleRate_);
}

void ModRuntime::advance(int index, const ParamTable& table, const LfoRate& rate,
                         const BlockInfo& block) {
    auto* state = mutableState(index);
    if (state == nullptr || index >= static_cast<int>(table.modifiers.size()))
        return;

    const auto& modifier = table.modifiers[static_cast<std::size_t>(index)];

    // A modifier the model has switched off is not a modifier that outputs
    // nothing: it is a modifier that is not there. Its links are not carried
    // (ParamTableCompiler), so what this publishes is only ever read by the
    // taps, and zero is what an absent modifier reads as.
    if (!modifier.enabled) {
        values_[static_cast<std::size_t>(index)] = 0.0f;
        return;
    }

    auto& out = values_[static_cast<std::size_t>(index)];

    switch (modifier.kind) {
        case ModKind::Lfo: {
            auto settings = modifier.lfo;
            settings.rate = rate;
            out = advanceLfo(state->lfo, settings, table.modCurveFor(index), block, timing_);
            break;
        }

        case ModKind::Adsr:
            // No rate. An envelope's stage lengths are times rather than a
            // frequency, and the one lane a modifier exposes describes a
            // frequency, so there is nothing here for it to mean.
            out = advanceAdsr(state->adsr, modifier.adsr, block, timing_);
            break;

        case ModKind::Random: {
            auto settings = modifier.random;
            settings.rate = rate;
            out = advanceRandom(state->random, settings, block, timing_);
            break;
        }

        case ModKind::Follower:
            // The peak its detector left on the last block. No rate either:
            // what a follower runs at is whatever its source is doing.
            out = advanceFollower(state->follower, modifier.follower, block, timing_);
            break;
    }
}

void ModRuntime::detectSource(int index, const ParamTable& table, std::span<const float> mono) {
    auto* state = mutableState(index);
    if (state == nullptr || index >= static_cast<int>(table.modifiers.size()))
        return;

    const auto& modifier = table.modifiers[static_cast<std::size_t>(index)];

    // Only a follower listens, and only one the model has switched on: a
    // disabled modifier is a modifier that is not there, and detecting for it
    // would leave an envelope primed for the block it is switched back on in.
    if (modifier.kind != ModKind::Follower || !modifier.enabled)
        return;

    detectFollowerSource(state->follower, modifier.follower, mono, sampleRate_, detectScratch_);
}

float ModRuntime::value(int index) const {
    if (index < 0 || index >= static_cast<int>(values_.size()))
        return 0.0f;

    return values_[static_cast<std::size_t>(index)];
}

const ModState* ModRuntime::state(int index) const {
    if (index < 0 || index >= static_cast<int>(states_.size()))
        return nullptr;

    return states_[static_cast<std::size_t>(index)].get();
}

ModState* ModRuntime::mutableState(int index) {
    if (index < 0 || index >= static_cast<int>(states_.size()))
        return nullptr;

    return states_[static_cast<std::size_t>(index)].get();
}

int ModRuntime::indexOf(const ParamKey& key) const {
    const auto found = std::find(keys_.begin(), keys_.end(), key);
    return found == keys_.end() ? -1 : static_cast<int>(std::distance(keys_.begin(), found));
}

void ModRuntime::restart(ModState& state, const ParamModifier& modifier, bool fromZero) {
    switch (modifier.kind) {
        case ModKind::Lfo:
            restartLfo(state.lfo, modifier.lfo);
            break;
        case ModKind::Adsr:
            restartAdsr(state.adsr, modifier.adsr, fromZero);
            break;
        case ModKind::Random:
            restartRandom(state.random, modifier.random);
            break;
        case ModKind::Follower:
            // Nothing to restart. A follower's output is a level, and a level
            // has no phase a trigger could move it to.
            break;
    }
}

void ModRuntime::noteOn(int index, const ParamTable& table) {
    auto* state = mutableState(index);
    if (state == nullptr || index >= static_cast<int>(table.modifiers.size()))
        return;

    const auto& modifier = table.modifiers[static_cast<std::size_t>(index)];

    // A cross-track sidechain modifier follows its source track and nothing
    // else: not its phase, and not its gate either. Retriggering it from the
    // track it ducks is the bug the fork's flag exists to prevent, and so is
    // gating it from there, which flaps on every note the destination plays.
    //
    // The fork suppresses the two separately, because its flags are set apart
    // and the combination is prevented rather than refused. Here the policy is
    // the guard: a modifier driven from elsewhere does not hear this at all.
    if (drivenFromElsewhere(modifier))
        return;

    // From where it is rather than from zero. A gap is what a cross-track
    // trigger asks for and this is not one: a note arriving under a held note
    // should not silence the envelope for a block on the way up.
    restart(*state, modifier, false);

    if (gatedByTrigger(modifier)) {
        if (auto* held = heldNotesOf(*state, modifier))
            ++*held;

        applyGate(*state, modifier, false);
    }
}

void ModRuntime::noteOff(int index, const ParamTable& table) {
    auto* state = mutableState(index);
    if (state == nullptr || index >= static_cast<int>(table.modifiers.size()))
        return;

    const auto& modifier = table.modifiers[static_cast<std::size_t>(index)];
    if (drivenFromElsewhere(modifier) || !gatedByTrigger(modifier))
        return;

    auto* held = heldNotesOf(*state, modifier);
    if (held == nullptr)
        return;

    *held = std::max(0, *held - 1);
    if (*held == 0)
        applyGate(*state, modifier, true);
}

void ModRuntime::allNotesOff(int index, const ParamTable& table) {
    auto* state = mutableState(index);
    if (state == nullptr || index >= static_cast<int>(table.modifiers.size()))
        return;

    const auto& modifier = table.modifiers[static_cast<std::size_t>(index)];
    if (drivenFromElsewhere(modifier) || !gatedByTrigger(modifier))
        return;

    if (auto* held = heldNotesOf(*state, modifier))
        *held = 0;

    applyGate(*state, modifier, true);
}

void ModRuntime::trigger(int index, const ParamTable& table, bool forceZero) {
    auto* state = mutableState(index);
    if (state == nullptr || index >= static_cast<int>(table.modifiers.size()))
        return;

    const auto& modifier = table.modifiers[static_cast<std::size_t>(index)];

    // The forced zero stands in for the gap a gated retrigger leaves, so the
    // device sees a transition rather than a continuation. Never for a level
    // curve: there, zero output is full level, and forcing one in the middle
    // of a duck pops the gain up for a block, which is a click on every hit.
    const bool gap = forceZero && !(modifier.kind == ModKind::Lfo && modifier.lfo.invertOutput);

    applyGate(*state, modifier, false);
    restart(*state, modifier, gap);

    // Latched rather than published. A trigger arrives inside a block whose
    // parameters have already been resolved, so a value written here would be
    // overwritten by the next block's advance before any device read it; the
    // latch is what makes the gap reach one (LfoState::forceZero).
    if (!gap)
        return;

    switch (modifier.kind) {
        case ModKind::Lfo:
            state->lfo.forceZero = true;
            break;
        case ModKind::Adsr:
            state->adsr.forceZero = true;
            break;
        case ModKind::Random:
        case ModKind::Follower:
            // Neither has a gate, so neither has a gap to stand in for: a walk
            // that blanked for a block on every hit would be a hole the fork
            // does not have.
            break;
    }
}

void ModRuntime::setGated(int index, const ParamTable& table, bool gated) {
    auto* state = mutableState(index);
    if (state == nullptr || index >= static_cast<int>(table.modifiers.size()))
        return;

    applyGate(*state, table.modifiers[static_cast<std::size_t>(index)], gated);
}

}  // namespace magda::engine
