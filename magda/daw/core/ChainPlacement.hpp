#pragma once

#include "ChainNodePath.hpp"
#include "RackInfo.hpp"  // ChainElement is a variant alias, not a forward-declarable type

namespace magda {

/**
 * @brief Why a structural edit was refused, or `Allowed`.
 *
 * One answer for every operation that puts a subtree somewhere: move, paste,
 * wrap, unwrap and duplication all ask the same question and used to answer it
 * with their own subset of the rules. `wrapDeviceInRack()` checked whether a pad
 * was on a bus but not whether the device drove a multi-out child track, so
 * wrapping a multi-out instrument into a rack stranded the child track that a
 * move of the same device refuses to strand (#2221).
 */
enum class PlacementRefusal {
    Allowed,
    DestinationCannotHostInstrument,  ///< An aux or group track, and the subtree has an instrument
    DestinationInsideSource,          ///< A rack being put inside one of its own chains
    PadOnABus,                        ///< A Drum Grid pad routed to a bus no destination carries
    OwnsMultiOutChildTracks,          ///< A device whose pairs drive child tracks of its own track
};

/**
 * @brief What is being placed, and where.
 *
 * `leavesItsContainer` is the difference between a placement change and a
 * reorder. Reordering inside the container a subtree already lives in changes
 * neither the track nor the ids anything is keyed on, so the rules that exist to
 * protect those do not apply to it.
 */
struct PlacementRequest {
    const ChainElement* subtree = nullptr;  ///< The element being placed
    /// Where it is now. Empty for a source-less reconstruction: a paste, or an
    /// undo restoring a device that was deleted. Such a subtree owns no child
    /// tracks and is leaving no track, so the rules keyed on where it came from
    /// do not apply to it.
    ChainNodePath sourcePath;
    ChainNodePath destination;  ///< The chain it is going into
    bool leavesItsContainer = true;
    /// Whether it lands directly in a track's own list, which is where the
    /// output instance that carries a multi-out bus is made. False for a rack
    /// chain, a pad chain, and for a wrap, whose destination is a rack that does
    /// not exist yet and so cannot be described by a path.
    bool destinationIsTrackTopLevel = false;
};

/// A one-line reason for @p refusal, for logs and diagnostics.
const char* describe(PlacementRefusal refusal);

}  // namespace magda
