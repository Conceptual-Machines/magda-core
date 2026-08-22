#pragma once

#include "TransportLayout.hpp"

namespace magda::daw::ui::transport {

// The AUTO / SNAP captions live here rather than at the point the buttons are
// built, so the width measured for them and the text drawn in them are the
// same string.
inline constexpr const char* kAutoGridCaption = "AUTO";
inline constexpr const char* kSnapCaption = "SNAP";

/** Measures the widest string each text-sized section of the transport has to
 *  hold, in the font that section actually draws with.
 *
 *  This is the half of the layout that needs a graphics context, kept apart
 *  from TransportLayout so the arithmetic there stays a pure function and can
 *  be asserted without one.
 */
TextWidths measureTextWidths();

}  // namespace magda::daw::ui::transport
