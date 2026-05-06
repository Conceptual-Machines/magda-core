#pragma once

#include <juce_core/juce_core.h>

#include <utility>
#include <vector>

namespace magda::daw::audio {

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
