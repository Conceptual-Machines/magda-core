// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

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
        case PlacementRefusal::OwnsMultiOutChildTracks:
            return "the device drives multi-out child tracks of its current track";
    }
    return "refused";
}

}  // namespace magda
