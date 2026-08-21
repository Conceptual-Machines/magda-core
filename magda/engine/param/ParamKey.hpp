#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

#include "core/ControlTarget.hpp"
#include "core/TypeIds.hpp"
#include "plan/RenderPlan.hpp"

/**
 * @file ParamKey.hpp
 * @brief Where a parameter lives, as the engine addresses it.
 *
 * The model addresses a parameter with a ControlTarget: a path through the
 * track, rack and chain hierarchy, plus whatever the kind needs. Every consumer
 * in MAGDA speaks it, which is what makes it the right thing to translate from
 * and the wrong thing to carry into a block. It holds a vector of steps, so it
 * allocates, and it says where a parameter is by describing the walk to it.
 *
 * This is the same address flattened, the way OpKey is the flattened form of a
 * model location for the plan. No heap, comparable and hashable, and it names
 * the owner rather than the route: a device by its DeviceKey, a macro or a
 * modifier by the scope that owns it.
 */

namespace magda::engine {

/** Index of a parameter inside a ParamTable. */
using ParamId = int;
constexpr ParamId INVALID_PARAM_ID = -1;

/** @brief What a parameter is, and whose it is. */
struct ParamKey {
    enum class Kind : std::uint8_t {
        DeviceParam,  ///< a device's own parameter, by its declared index
        Macro,        ///< a macro knob belonging to the scope named below
        ModParam,     ///< one parameter of a modifier belonging to that scope

        // The mixer values, which are parameters for the same reason the rest
        // are: a lane plays over them inside a block, and a table resolved by
        // the publisher cannot move faster than a publish. They are carried
        // only when something reaches them, so a project with no automation on
        // its faders keeps the value table's answer and pays nothing (#2118).
        TrackVolume,  ///< the track fader, in dB
        TrackPan,     ///< the track pan, -1 to 1
        SendLevel,    ///< one send slot's level, in dB, by its index
    };

    /// Who owns the macro or modifier. Always Device for a device parameter.
    enum class Scope : std::uint8_t { Track, Rack, Device };

    Kind kind = Kind::DeviceParam;
    Scope scope = Scope::Device;

    /// The track the owner is on. Set whatever the scope is, because a rack and
    /// a device are somewhere as well as being something, and a key that says
    /// where reads as an address rather than as an integer.
    TrackId trackId = INVALID_TRACK_ID;

    RackId rackId = INVALID_RACK_ID;
    DeviceKey device;

    /// The modifier, for Kind::ModParam. A modifier addressed as a source
    /// rather than as a set of parameters leaves @ref index at -1: it names the
    /// modifier itself, which is a thing that produces a value rather than a
    /// value something can write.
    ModId modId = INVALID_MOD_ID;

    /// The device's parameter index, the macro's index, or the modifier
    /// parameter's index.
    int index = 0;

    bool operator==(const ParamKey& o) const;
    bool operator!=(const ParamKey& o) const {
        return !(*this == o);
    }
    bool operator<(const ParamKey& o) const;
};

struct ParamKeyHash {
    std::size_t operator()(const ParamKey& key) const noexcept;
};

/**
 * @brief The parameter a ControlTarget names.
 *
 * The one translation between the model's address and the engine's, so a target
 * that resolves differently in two places is not a thing that can happen.
 *
 * Empty for a target the parameter table does not carry, which is not the same
 * as a target that is wrong: a track volume is a real target that the value
 * table resolves rather than this one, and a tempo is a real target that
 * belongs to the transport. What to say about the difference is the caller's,
 * since only the caller knows whether it is reporting or asking.
 */
std::optional<ParamKey> paramKeyFor(const magda::ControlTarget& target);

/// The modifier a ControlTarget's scope and modId name, as a source: the same
/// key with no parameter index. Empty when the target names no modifier.
std::optional<ParamKey> modifierKeyFor(const magda::ControlTarget& target);

/// Canonical text, e.g. "T1/D7:param3", "T1/R4:macro0", "T1:mod2.param0",
/// and "T1/R5/D90:param0" for a device inside a rack. Injective over keys:
/// two keys that differ render differently, which is what lets a
/// diagnostic name the thing it missed.
std::string toString(const ParamKey& key);

}  // namespace magda::engine
