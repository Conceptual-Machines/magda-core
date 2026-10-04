#pragma once

#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace magda::devices::faust {

/// What the host does with a control's zone, from `[role:...]`.
enum class FaustControlRole {
    User,
    /// The host writes the project tempo (BPM) into the zone every block; pair with `[hidden:1]`.
    ProjectTempo,
};

/// `[style:menu{...}]` versus `[style:radio{...}]`: one discrete parameter, two presentations.
enum class FaustChoiceStyle {
    None,
    Menu,
    Radio,
};

/// A bargraph's `[style:led]` or `[style:numerical]`, the keys Faust's own MetaDataUI reads.
enum class FaustOutputStyle {
    Bar,
    Numerical,
    Led,
};

/**
 * @brief A control's annotations, e.g. from "Cutoff [unit:Hz] [scale:log] [idx:7]".
 *
 * Effective metadata is the group-scope declares merged with the control's own; the control's win
 * (docs/architecture/faust-param-pool.md).
 */
struct ControlMetadata {
    /// `[idx:N]`; -1 for encounter order. Out-of-range and duplicate values are the pool's to
    /// judge.
    int slotIndex = -1;

    std::string unit;

    /// `[scale:log]`; exp and lin are recognised but have no ParameterScale mapping.
    bool logScale = false;

    /// (value, label) pairs from a menu or radio style.
    std::vector<std::pair<float, std::string>> menuChoices;

    /// None also tells "no choice style" from "an empty choice list".
    FaustChoiceStyle choiceStyle = FaustChoiceStyle::None;

    bool isChoiceStyle() const {
        return choiceStyle != FaustChoiceStyle::None;
    }

    /// Output widgets only.
    FaustOutputStyle outputStyle = FaustOutputStyle::Bar;

    /// Grid cells from `[width:N]`; the layout clamps it to a row.
    int widthCells = 1;

    /// `[tooltip:...]`, presentational only.
    std::string tooltip;

    FaustControlRole role = FaustControlRole::User;

    /// `[hidden:1]`: left out of the grid but still a pool slot the host writes.
    bool hidden = false;

    /// `[gate:N]` enables the control while slot N is >= 0.5; `[gate:!N]` (negated) while < 0.5.
    int gateSlotIndex = -1;
    bool gateNegated = false;

    /// `[scaleAnchor:N]`: the real value at the drag midpoint, without which a log control drags
    /// linearly. NaN is unset.
    float scaleAnchor = std::numeric_limits<float>::quiet_NaN();
};

/// A label with its recognised `[key:value]` annotations stripped and whitespace collapsed;
/// unrecognised ones stay in the label. Later keys overwrite earlier ones.
struct ParsedLabel {
    std::string cleanLabel;
    ControlMetadata metadata;
};

ParsedLabel parseFaustLabel(std::string_view rawLabel);

/// Apply one annotation (@p key lowercased, @p value without brackets); false when unrecognised.
bool applyFaustAnnotation(std::string_view key, std::string_view value, ControlMetadata& metadata);

/// The `'A':0;'B':1` body of a menu or radio style as ordered (value, label) pairs; empty when
/// malformed.
std::vector<std::pair<float, std::string>> parseMenuChoices(std::string_view payload);

/// Merge @p child over @p parent: every field the child sets wins.
void mergeFaustMetadata(ControlMetadata& parent, const ControlMetadata& child);

}  // namespace magda::devices::faust
