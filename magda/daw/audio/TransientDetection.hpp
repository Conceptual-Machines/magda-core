#pragma once

#include "../core/ClipTypes.hpp"

/// A clip's transients under whichever engine renders, cached in AudioThumbnailManager (#3017).
namespace magda::transients {

/// Whether @p clipId's transients are cached, starting their detection when not.
bool detect(ClipId clipId);

/// Detect @p clipId's transients again at @p sensitivity, once the value settles.
void setSensitivity(ClipId clipId, float sensitivity);

}  // namespace magda::transients
