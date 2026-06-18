#pragma once

#include <juce_core/juce_core.h>

#include <limits>
#include <utility>
#include <vector>

#include "FaustMetadataParser.hpp"

// Forward-declare FAUSTFLOAT without dragging the libfaust headers into
// every translation unit that includes this file. libfaust always
// typedefs `FAUSTFLOAT = float` for our (interpreter) backend.
using FAUSTFLOAT = float;

namespace magda::daw::audio {

/**
 * @brief One slot in the FaustPlugin's fixed parameter pool.
 *
 * The pool keeps 64 of these for the plugin's lifetime so macro / mod /
 * MIDI Learn / automation links survive a DSP recompile. When a new DSP
 * is loaded, the harvested controls are routed into slots and `active`
 * + the rest of the descriptive fields are filled in; unused slots stay
 * around with `active == false` and zeroed metadata.
 *
 * `zone` is owned by the live Faust DSP and is only valid for the
 * lifetime of the matching `FaustState`. Audio-thread reads of the zone
 * MUST go through the immutable `FaustState::ActiveBinding` snapshot
 * (atomic-loaded at the top of `applyToBuffer`), not through a
 * `FaustParamSlot` directly — slot fields are mutated on the message
 * thread when DSP swaps happen.
 */
struct FaustParamSlot {
    enum class Kind {
        Continuous,  // hslider / vslider / numentry without a [style:menu|radio]
        Discrete,    // slider with [style:menu{…}] or [style:radio{…}]
        Boolean,     // checkbox / button
    };

    int index = -1;       // 0..63 within the pool
    bool active = false;  // false slots are reserved but unmapped
    juce::String label;   // cleaned label (no `[…]` annotations)
    juce::String unit;    // from `[unit:Hz]` / `[unit:dB]` / …
    Kind kind = Kind::Continuous;

    // Real-units range. For Continuous: from the Faust slider min/max/step.
    // For Boolean: 0/1/1. For Discrete: 0..(N-1) over the choice list.
    float minValue = 0.0f;
    float maxValue = 1.0f;
    float stepValue = 0.0f;
    float defaultValue = 0.0f;

    // True iff `[scale:log]` was set; informs ParameterScale::Logarithmic.
    bool logScale = false;

    // For Kind::Discrete: ordered (real-value, display-name) pairs from
    // `[style:menu{'Off':0;'Low':1;'High':2}]` or the equivalent
    // `[style:radio{…}]`. Empty for non-discrete slots.
    std::vector<std::pair<float, juce::String>> choices;

    // Pointer into the live DSP's zone. Only valid while the matching
    // FaustState is alive. The audio thread must NOT read this directly
    // off a slot — see the class comment.
    FAUSTFLOAT* zone = nullptr;

    // MAGDA role tag from `[role:<value>]`. ProjectTempo means the host
    // writes the live BPM into this slot's zone every audio block. See
    // FaustMetadataParser.hpp for the full list.
    FaustControlRole role = FaustControlRole::User;

    // True iff the slot was declared with `[hidden:1]`. Hidden slots
    // still occupy a pool index (and may carry a role like ProjectTempo
    // that the host writes into) but are omitted from the inspector
    // parameter grid via FaustParamInfo.
    bool hidden = false;

    // Gate condition from `[gate:N]` or `[gate:!N]`. When >= 0, the UI
    // disables (greys out) this slot's cell when the referenced slot's
    // current value does not satisfy the condition. -1 = no gate.
    int gateSlotIndex = -1;

    // True iff the gate is negated (`[gate:!N]`): the cell is active when
    // the gate slot's value is < 0.5 (i.e. slot is OFF / near zero).
    bool gateNegated = false;

    // Optional musical anchor from `[scaleAnchor:N]`. NaN ⇒ unset; any
    // finite value is forwarded to ParameterInfo.scaleAnchor so the
    // slider's drag math gets a useful skew (without it, even
    // `[scale:log]` controls drag linearly across their real range).
    float scaleAnchor = std::numeric_limits<float>::quiet_NaN();
};

}  // namespace magda::daw::audio
