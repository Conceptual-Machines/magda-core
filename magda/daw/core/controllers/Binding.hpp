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
 * @brief Identifies which MIDI message on which controller triggers a binding.
 *
 * channel: 1..16, or 0 = any channel.
 */
struct BindingSource {
    ControllerId controllerId;
    BindingMsgType msgType = BindingMsgType::CC;
    int channel = 0;  // 1..16, 0 = any
    int number = 0;   // CC number, note number, or NRPN number

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
