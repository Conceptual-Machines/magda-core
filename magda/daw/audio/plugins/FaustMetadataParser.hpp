#pragma once

#include <juce_core/juce_core.h>

#include <limits>
#include <utility>
#include <vector>

namespace magda::daw::audio {

/**
 * @brief Faust control roles MAGDA recognises.
 *
 * Roles are MAGDA-specific extensions surfaced through the
 * `[role:<value>]` annotation. They're orthogonal to slot index /
 * unit / scale: a role tells the host *what to do* with the control's
 * zone (e.g. write the project tempo into it every block) rather than
 * how to display it.
 */
enum class FaustControlRole {
    /// Default — user-visible parameter the player drives.
    User,
    /// Host writes the live project tempo (BPM) to this slot's zone
    /// every audio block. The DSP reads it as a regular control. Used
    /// for tempo-synced delay times, grain rates, LFO frequencies,
    /// etc. Pair with `[hidden:1]` so it doesn't clutter the param
    /// grid.
    ProjectTempo,
};

/**
 * @brief Parsed output of a Faust label like
 *        "Cutoff [unit:Hz] [scale:log] [idx:7]".
 *
 * A control's effective metadata is the merge of every group-scope
 * declare() between the surrounding open/closeBox calls plus the
 * declares attached directly to the control. Control-level keys win
 * when the same key appears at multiple scopes.
 */
struct ControlMetadata {
    /// Slot index from `[idx:N]`. -1 means "no idx tag, use encounter
    /// order". Out-of-range or duplicate idx values are left as-is by
    /// the parser; the pool decides what to do (see FAUST_POOL_REFACTOR.md).
    int slotIndex = -1;

    /// Display unit from `[unit:Hz]`, `[unit:dB]`, etc. Empty if absent.
    juce::String unit;

    /// True iff `[scale:log]`. `[scale:exp]` is reserved for future use
    /// but not currently surfaced — Faust only ships log/exp/lin and
    /// MAGDA's ParameterScale doesn't have a clean Exp mapping yet.
    bool logScale = false;

    /// Choices from `[style:menu{'A':0;'B':1}]` or
    /// `[style:radio{'A':0;'B':1}]`. Empty unless the style annotation
    /// was a menu or radio.
    std::vector<std::pair<float, juce::String>> menuChoices;

    /// Whether the menu/radio style was set (distinguishes "no menu
    /// declared" from "empty menu" — defensive; Faust shouldn't emit
    /// the latter).
    bool isMenuStyle = false;

    /// MAGDA role tag from `[role:<value>]`. Defaults to User.
    FaustControlRole role = FaustControlRole::User;

    /// True iff `[hidden:1]`. Hidden controls are omitted from the
    /// inspector's parameter grid but still occupy a pool slot — the
    /// host writes to their zones (e.g. ProjectTempo).
    bool hidden = false;

    /// Gate slot from `[gate:N]` or `[gate:!N]`. When >= 0, this param
    /// is only enabled (interactive) in the UI when the referenced slot's
    /// current value satisfies the condition. `[gate:N]` means "enabled
    /// when slot N >= 0.5"; `[gate:!N]` means "enabled when slot N < 0.5"
    /// (i.e. negated). -1 means no gate — param is always enabled.
    int gateSlotIndex = -1;

    /// True iff the gate condition is negated (declared as `[gate:!N]`).
    bool gateNegated = false;

    /// Optional musical anchor from `[scaleAnchor:N]` — the real-units
    /// value that should fall at the slider's drag-midpoint (norm=0.5).
    /// Without it, even `[scale:log]` controls drag linearly because
    /// MAGDA's TextSlider needs a non-symmetric anchor to compute a skew
    /// (geometric mean ⇒ skew=1 ⇒ no help). Typical use: `1000` on a
    /// 20–20k Hz cutoff so the bottom of the range gets pixel
    /// resolution proportional to a log slider. NaN ⇒ unset.
    float scaleAnchor = std::numeric_limits<float>::quiet_NaN();
};

/**
 * @brief Strip the `[…]` annotations from a Faust label and parse them.
 *
 * Returns:
 *   - `cleanLabel` — the label with every well-formed `[key:value]`
 *     occurrence removed and surrounding whitespace collapsed.
 *     Annotations the parser doesn't recognise are kept intact (we
 *     don't want to silently swallow things we don't understand).
 *   - `metadata` — populated from every recognised `[key:value]`
 *     occurrence in source order. Later keys overwrite earlier ones.
 */
struct ParsedLabel {
    juce::String cleanLabel;
    ControlMetadata metadata;
};

ParsedLabel parseFaustLabel(const juce::String& rawLabel);

/**
 * @brief Parse a single `[key:value]` annotation payload (without the
 *        brackets) into the appropriate metadata field.
 *
 * Public mostly for testing. Returns true iff the annotation was
 * recognised and applied to `metadata`.
 */
bool applyFaustAnnotation(const juce::String& key, const juce::String& value,
                          ControlMetadata& metadata);

/**
 * @brief Parse a `[style:menu{…}]` or `[style:radio{…}]` payload's
 *        choice list — i.e. the `'A':0;'B':1` part — into ordered
 *        (value, label) pairs. Empty result on malformed input.
 */
std::vector<std::pair<float, juce::String>> parseMenuChoices(const juce::String& payload);

/**
 * @brief Merge `child` over `parent` in place. Keys present on `child`
 *        win (control-level wins over group-level). Used by the
 *        UIHarvester to compose group-scope declares with the
 *        control's own declares.
 *
 * `slotIndex` follows the rule: if child has -1, keep parent; else use
 * child. (Group-level idx tags don't make sense, but the mechanism is
 * the same.)
 */
void mergeFaustMetadata(ControlMetadata& parent, const ControlMetadata& child);

}  // namespace magda::daw::audio
