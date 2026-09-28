#include "ChainPlacement.hpp"

namespace magda {

const char* describe(PlacementRefusal refusal) {
    switch (refusal) {
        case PlacementRefusal::Allowed:
            return "allowed";
        case PlacementRefusal::DestinationCannotHostInstrument:
            return "the destination track cannot host an instrument";
        case PlacementRefusal::DestinationInsideSource:
            return "the destination is inside the rack being moved";
        case PlacementRefusal::PadOnABus:
            return "a pad is routed to a bus the destination does not carry";
        case PlacementRefusal::OwnsMultiOutChildTracks:
            return "the device drives multi-out child tracks of its current track";
    }
    return "refused";
}

}  // namespace magda
