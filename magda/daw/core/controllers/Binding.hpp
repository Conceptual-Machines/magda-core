#pragma once

#include <juce_core/juce_core.h>

#include <optional>

#include "../aliases/Target.hpp"
#include "Controller.hpp"

namespace magda {

// ============================================================================
// Binding enums
// ============================================================================

enum class BindingMsgType {
    CC,
    Note,
    PitchBend,
    NRPN,
};

enum class BindingMode {
    Absolute,
    Relative2sComp,
    RelativeSignMag,
    RelativeBinOff,
    Toggle,
};

enum class BindingCurve {
    Linear,
    Log,
    Exp,
    SCurve,
};

// ============================================================================
// BindingSource
// ============================================================================

/**
 * @brief Which transport a source speaks.
 *
 * One `Binding` type rather than two parallel ones, because everything past
 * the source is shared: the same targets, the same mode / range / curve, the
 * same two persistence scopes, the same learn gesture. Only the question "what
 * event triggers this" differs, and that is exactly what this selects.
 */
enum class BindingSourceKind : std::uint8_t { Midi, Osc };

/**
 * @brief Identifies which incoming event triggers a binding.
 *
 * A MIDI source can be addressed two ways:
 *
 *  - portKey: a live MIDI input identifier or display name (matched via
 *    magda::midi::matches). Used by MIDI Learn — project-scoped bindings
 *    created by user gestures attach to a concrete port and require no
 *    ControllerRegistry entry.
 *
 *  - controllerId: a UUID pointing into ControllerRegistry. Used by
 *    controller scripts / surfaces (MCU, Lua, …) where the binding is
 *    logically owned by a registered control surface rather than a raw port.
 *
 * Exactly one is normally populated, but both may be set for scripted
 * surfaces that also want a port-specific fallback.
 *
 * channel: 1..16, or 0 = any channel.
 *
 * An OSC source is addressed by the literal address the surface sends, plus
 * which of the message's arguments carries the value. There is no port and no
 * controller: OSC arrives on one socket from whoever is pointed at it, so the
 * address is the whole of the identity. It is matched exactly rather than as
 * an OSC address *pattern* — a learn gesture captures one control, and a
 * stored wildcard would silently bind every control that happened to match it.
 */
struct BindingSource {
    BindingSourceKind kind = BindingSourceKind::Midi;

    // ---- MIDI ----
    juce::String portKey;  // live MIDI identifier or display name (Learn path)
    ControllerId controllerId;
    BindingMsgType msgType = BindingMsgType::CC;
    int channel = 0;  // 1..16, 0 = any
    int number = 0;   // CC number, note number, or NRPN number

    // ---- OSC ----
    juce::String oscAddress;  // e.g. "/1/fader3", exactly as the surface sends it
    int oscArgIndex = 0;      // which argument carries the value

    bool isOsc() const {
        return kind == BindingSourceKind::Osc;
    }

    bool operator==(const BindingSource& other) const;
    bool operator!=(const BindingSource& other) const {
        return !(*this == other);
    }
};

// ============================================================================
// BindingRange
// ============================================================================

struct BindingRange {
    float min = 0.0f;
    float max = 1.0f;
    BindingCurve curve = BindingCurve::Linear;

    bool operator==(const BindingRange& other) const {
        return min == other.min && max == other.max && curve == other.curve;
    }
};

// ============================================================================
// Binding
// ============================================================================

/**
 * @brief Maps a MIDI controller source to a plugin parameter target.
 */
struct Binding {
    BindingId id;
    BindingSource source;
    Target target;  // reuses magda::Target from the alias system
    BindingMode mode = BindingMode::Absolute;
    BindingRange range;

    /** Returns true when the binding has a valid source and a plausible target. */
    bool isValid() const;
};

// ============================================================================
// JSON round-trip
// ============================================================================

juce::var encodeBinding(const Binding& b);
std::optional<Binding> decodeBinding(const juce::var& v);

}  // namespace magda
