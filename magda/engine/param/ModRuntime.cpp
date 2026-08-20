#include "param/ModRuntime.hpp"

#include <algorithm>
#include <unordered_map>

#include "param/ParamTable.hpp"

namespace magda::engine {

void ModRuntime::reset() {
    states_.clear();
    values_.clear();
    keys_.clear();
    kinds_.clear();
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

        if (state == nullptr)
            state = std::make_shared<ModState>();

        states_.push_back(std::move(state));
        values_.push_back(modifier.value);
        keys_.push_back(modifier.key);
        kinds_.push_back(modifier.kind);
    }
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

    if (modifier.kind != ModKind::Lfo) {
        // No engine yet (#2120). The model's own reading, held, which is what
        // every modifier published before this slice.
        values_[static_cast<std::size_t>(index)] = modifier.value;
        return;
    }

    auto settings = modifier.lfo;
    settings.rate = rate;

    values_[static_cast<std::size_t>(index)] =
        advanceLfo(state->lfo, settings, table.modCurveFor(index), block, timing_);
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

void ModRuntime::noteOn(int index, const ParamTable& table) {
    auto* state = mutableState(index);
    if (state == nullptr || index >= static_cast<int>(table.modifiers.size()))
        return;

    const auto& settings = table.modifiers[static_cast<std::size_t>(index)].lfo;

    // A cross-track sidechain LFO follows its source track and nothing else:
    // not its phase, and not its gate either. Retriggering it from the track
    // it ducks is the bug the fork's flag exists to prevent, and so is gating
    // it from there, which flaps on every note the destination plays.
    //
    // The fork suppresses the two separately, because its flags are set apart
    // and the combination is prevented rather than refused. Here the policy is
    // the guard: a modifier driven from elsewhere does not hear this at all.
    if (settings.skipNativeResync)
        return;

    restartLfo(state->lfo, settings);

    if (settings.gateOnTrigger) {
        ++state->lfo.heldNotes;
        state->lfo.gated = false;
        state->lfo.started = true;
    }
}

void ModRuntime::noteOff(int index, const ParamTable& table) {
    auto* state = mutableState(index);
    if (state == nullptr || index >= static_cast<int>(table.modifiers.size()))
        return;

    const auto& settings = table.modifiers[static_cast<std::size_t>(index)].lfo;
    if (settings.skipNativeResync || !settings.gateOnTrigger)
        return;

    state->lfo.heldNotes = std::max(0, state->lfo.heldNotes - 1);
    if (state->lfo.heldNotes == 0)
        state->lfo.gated = true;
    state->lfo.started = true;
}

void ModRuntime::allNotesOff(int index, const ParamTable& table) {
    auto* state = mutableState(index);
    if (state == nullptr || index >= static_cast<int>(table.modifiers.size()))
        return;

    const auto& settings = table.modifiers[static_cast<std::size_t>(index)].lfo;
    if (settings.skipNativeResync || !settings.gateOnTrigger)
        return;

    state->lfo.heldNotes = 0;
    state->lfo.gated = true;
    state->lfo.started = true;
}

void ModRuntime::trigger(int index, const ParamTable& table, bool forceZero) {
    auto* state = mutableState(index);
    if (state == nullptr || index >= static_cast<int>(table.modifiers.size()))
        return;

    const auto& settings = table.modifiers[static_cast<std::size_t>(index)].lfo;

    state->lfo.gated = false;
    state->lfo.started = true;
    restartLfo(state->lfo, settings);

    // The forced zero stands in for the gap a gated retrigger leaves, so the
    // device sees a transition rather than a continuation. Never for a level
    // curve: there, zero output is full level, and forcing one in the middle
    // of a duck pops the gain up for a block, which is a click on every hit.
    //
    // Latched rather than published. A trigger arrives inside a block whose
    // parameters have already been resolved, so a value written here would be
    // overwritten by the next block's advance before any device read it; the
    // latch is what makes the gap reach one (LfoState::forceZero).
    if (forceZero && !settings.invertOutput)
        state->lfo.forceZero = true;
}

void ModRuntime::setGated(int index, bool gated) {
    auto* state = mutableState(index);
    if (state == nullptr)
        return;

    state->lfo.gated = gated;
    state->lfo.started = true;
}

}  // namespace magda::engine
