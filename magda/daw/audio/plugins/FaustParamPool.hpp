#pragma once

#include <juce_core/juce_core.h>

#include <array>
#include <limits>
#include <vector>

#include "FaustMetadataParser.hpp"
#include "FaustParamSlot.hpp"

namespace magda::daw::audio {

/**
 * @brief A control extracted from a freshly-compiled Faust DSP.
 *
 * The harvester (Faust UI subclass that walks the DSP's control tree)
 * produces one of these per active widget. Labels are already cleaned
 * by the harvester via parseFaustLabel; group-level vs control-level
 * declares are already merged into `metadata`. Pool input only — has
 * no lifetime relationship with anything stable.
 */
struct HarvestedControl {
    FaustParamSlot::Kind kind = FaustParamSlot::Kind::Continuous;
    juce::String label;
    float minValue = 0.0f;
    float maxValue = 1.0f;
    float stepValue = 0.0f;
    float defaultValue = 0.0f;
    FAUSTFLOAT* zone = nullptr;
    ControlMetadata metadata;
    // Top-level author group label, shared by effect and instrument UIs.
    juce::String group;
};

/**
 * @brief A bargraph extracted from a freshly-compiled Faust DSP.
 *
 * The output counterpart to HarvestedControl. Bargraphs carry no `[idx:N]`
 * identity because nothing binds to them, so declaration order is the only
 * ordering the pool needs. `zone` is one instance's pointer; the instrument
 * path merges the per-voice occurrences of one bargraph before rebinding.
 */
struct HarvestedOutput {
    juce::String label;
    float minValue = 0.0f;
    float maxValue = 1.0f;
    bool vertical = false;
    std::vector<FAUSTFLOAT*> zones;
    ControlMetadata metadata;
    juce::String group;
};

/**
 * @brief Fixed pool of 64 parameter slots for FaustPlugin.
 *
 * The pool is constructed once per FaustPlugin instance and persists
 * across DSP swaps. `rebindFromHarvest` is called on the message
 * thread after each successful compile; it routes harvested controls
 * into slots, marks the rest inactive, and returns a binding
 * descriptor list for the audio thread to consume via FaustState.
 *
 * The pool itself owns no parameter values — those belong to FaustPlugin and
 * live for the device's lifetime, which is what gives macro / mod / automation
 * links their stable home. The pool only owns the slot metadata table.
 *
 * Routing rules (full spec in docs/architecture/faust-param-pool.md):
 *   1. First pass — every harvested control with a valid `[idx:N]`
 *      (0..63 and not already taken) claims that slot.
 *   2. Second pass — every remaining control fills the next free slot
 *      in encounter order.
 *   3. Anything that doesn't fit (>64 active, duplicate idx, idx
 *      out-of-range) is dropped and surfaced through `RebindReport`
 *      so the UI can show a one-line diagnostic.
 *
 * Slot metadata not touched by the harvest is left alone (so an
 * AutomatableParameter's stored value survives a DSP swap when the
 * same slot remains active). When a slot moves from active→inactive,
 * its `zone` pointer is cleared but its label / range stay so the
 * historical state is recoverable for diagnostics.
 */
class FaustParamPool {
  public:
    static constexpr int kSize = 64;

    /// Output (bargraph) slots. Smaller than the control pool because a
    /// readout is a summary: a patch wanting dozens of them is describing a
    /// spectrum, which is one banked widget rather than 64 cells.
    static constexpr int kOutputSize = 16;

    /// What FaustState needs at audio time. One of these per active
    /// slot, immutable for the state's lifetime.
    struct ActiveBindingDescriptor {
        int slotIndex = -1;
        FAUSTFLOAT* zone = nullptr;
        FaustParamSlot::Kind kind = FaustParamSlot::Kind::Continuous;
        float minValue = 0.0f;
        float maxValue = 1.0f;
        float stepValue = 0.0f;
        bool logScale = false;
        /// Mirrors slot.scaleAnchor — needed on the audio thread so
        /// `denormalizeForBinding` can invert the same anchor-skew
        /// `ParameterUtils::realToNormalized` applied when the host
        /// wrote the slider value into the AutomatableParameter. Without
        /// it, slider→audio round-trips would squash mid-range values
        /// (e.g. 1000 Hz on a log cutoff with 1k anchor → 632 Hz at the
        /// zone). NaN means "no anchor".
        float scaleAnchor = std::numeric_limits<float>::quiet_NaN();
        /// MAGDA role for this binding. The audio-thread param loop
        /// only writes its `param->getCurrentValue()` for role==User
        /// bindings; non-User roles (e.g. ProjectTempo) are filled in
        /// by the host directly each block.
        FaustControlRole role = FaustControlRole::User;
        /// For Kind::Discrete only: real-unit values indexed by sorted
        /// choice order. The audio thread maps `round(normalized *
        /// (size-1))` to an index here, then writes the result to
        /// `zone`. Empty for Continuous / Boolean / Trigger.
        std::vector<float> discreteValues;
        /// Gate condition mirrored from the slot. -1 = no gate.
        int gateSlotIndex = -1;
        /// True iff the gate condition is negated (`[gate:!N]`).
        bool gateNegated = false;
    };

    struct RebindReport {
        std::vector<ActiveBindingDescriptor> activeBindings;
        /// Human-readable warnings ("3 controls dropped: pool overflow",
        /// "duplicate [idx:7]", …) — UI surfaces these in the FaustUI
        /// error label so silent failures don't slip through.
        std::vector<juce::String> diagnostics;
    };

    FaustParamPool();

    /// Reset every slot to inactive. Used when the live DSP fails to
    /// compile — the audio thread keeps reading the previous, valid
    /// FaustState until a working DSP lands.
    void clearActive();

    /// Apply a fresh harvest to the slot table. Returns the binding
    /// descriptors the caller installs on the new FaustState. Outputs are
    /// rebound in the same pass so a meter can never outlive the DSP that
    /// declared it.
    RebindReport rebindFromHarvest(const std::vector<HarvestedControl>& harvested,
                                   const std::vector<HarvestedOutput>& outputs = {});

    /// Read access for UI / parameter-info bridge.
    const FaustParamSlot& slot(int index) const {
        return slots_[static_cast<size_t>(index)];
    }
    int activeCount() const;

    const FaustOutputSlot& output(int index) const {
        return outputs_[static_cast<size_t>(index)];
    }
    int activeOutputCount() const;

    /// Current reading of an output slot, or its minimum when the slot is
    /// inactive or its DSP has gone. Polyphonic instruments harvest one zone
    /// per voice, so the loudest voice wins, which is what a meter means.
    ///
    /// Message thread only, and never cache what it read from: zones move on
    /// every recompile. A torn read against the audio thread is tolerated for
    /// the same reason MagdaDriveCurveView tolerates one: a frame of a
    /// cosmetic readout is worth less than a lock on the audio path.
    float readOutput(int index) const;

    /// Returns the live-DSP zone of the active slot tagged
    /// `[role:projectTempo]`, or `nullptr` if no such slot exists in
    /// the current binding. The host writes the project BPM here every
    /// audio block. Lifetime is bounded by the matching FaustState —
    /// callers must take a snapshot via FaustState::ActiveBinding
    /// rather than re-querying this from the audio thread.
    FAUSTFLOAT* getProjectTempoZone() const;

  private:
    std::array<FaustParamSlot, kSize> slots_;
    std::array<FaustOutputSlot, kOutputSize> outputs_;
};

}  // namespace magda::daw::audio
