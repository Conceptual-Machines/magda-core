#pragma once

#include <optional>

#include "../../core/aliases/TargetResolver.hpp"

namespace magda {

// ============================================================================
// ControllerParamReader (abstract)
// ============================================================================

/**
 * @brief Reads back the normalized value at a resolved parameter target.
 *
 * The mirror of `ControllerParamWriter`, and deliberately its neighbour rather
 * than a private helper somewhere: OSC feedback (#2091) has to answer "where
 * should this fader sit" with the value a write of that position would have
 * produced, and the only way that stays true is for the two to be read side by
 * side and change together.
 *
 * A target that cannot be read answers `nullopt` rather than a plausible zero.
 * A surface told 0 puts its fader at the bottom, which is a lie; a surface told
 * nothing leaves it where it is, which is merely stale.
 *
 * Called on the message thread.
 */
class ControllerParamReader {
  public:
    virtual ~ControllerParamReader() = default;

    /**
     * @param resolved  Fully resolved device + param index.
     * @return The target's value as a normalized float in [0,1], or nullopt
     *         when it has none to give.
     */
    virtual std::optional<float> read(const ResolveResult& resolved) = 0;
};

// ============================================================================
// DefaultControllerParamReader
// ============================================================================

class AudioBridge;

/**
 * @brief The reader that inverts `DefaultControllerParamWriter`, branch for
 *        branch.
 *
 * Each case below is the writer's own case read backwards, and the pairing is
 * the point:
 *
 *  - Track and master volume and pan and send levels come from `TrackInfo`,
 *    because that is what the writer wrote to. Going to the
 *    `te::AutomatableParameter` instead would mean converting through TE's
 *    fader-position curve, which is a different domain from the linear gain
 *    `TrackManager` holds, and the round trip would not close.
 *  - Macros are normalized on both sides, so `MacroInfo::value` is the answer
 *    already.
 *  - A plugin parameter takes whichever of the writer's two paths its own
 *    `ParameterInfo` selects: a display-mapped internal parameter inverts
 *    through `ParameterUtils`, and everything else inverts through the TE
 *    parameter's value range, which is what the writer mapped it into.
 *
 * `ModParam` is the one kind with no reading. Its forward path resolves a
 * modifier's tempo-sync flag and then writes either a rate in Hz or a discrete
 * sync-division ordinal, and the ordinal is a rounding that does not invert. No
 * surface displays a modifier rate, so this answers nullopt rather than growing
 * a special case for it.
 *
 * `Tempo` has no reading either, for the reason the writer has no writing: it is
 * not reachable from a control surface, and the OSC namespace sets it through
 * `ProjectApi`.
 */
class DefaultControllerParamReader : public ControllerParamReader {
  public:
    explicit DefaultControllerParamReader(AudioBridge& bridge) : bridge_(bridge) {}

    std::optional<float> read(const ResolveResult& resolved) override;

  private:
    static std::optional<float> readTrackLevel(const ControlTarget& target);
    static std::optional<float> readSendLevel(const ControlTarget& target);
    static std::optional<float> readMacro(const ControlTarget& target);
    std::optional<float> readPluginParam(const ControlTarget& target);

    AudioBridge& bridge_;
};

}  // namespace magda
